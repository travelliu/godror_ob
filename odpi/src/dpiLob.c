//-----------------------------------------------------------------------------
// Copyright (c) 2016, 2024, Oracle and/or its affiliates.
//
// This software is dual-licensed to you under the Universal Permissive License
// (UPL) 1.0 as shown at https://oss.oracle.com/licenses/upl and Apache License
// 2.0 as shown at http://www.apache.org/licenses/LICENSE-2.0. You may choose
// either license.
//
// If you elect to accept the software under the Apache License, Version 2.0,
// the following applies:
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// dpiLob.c
//   Implementation of LOB data.
//-----------------------------------------------------------------------------

#include "dpiImpl.h"

//-----------------------------------------------------------------------------
// ob_dpiLob__allocate() [INTERNAL]
//   Allocate and initialize LOB object.
//-----------------------------------------------------------------------------
int ob_dpiLob__allocate(dpiConn *conn, const dpiOracleType *type, dpiLob **lob,
        dpiError *error)
{
    dpiLob *tempLob;

    if (ob_dpiGen__allocate(DPI_HTYPE_LOB, conn->env, (void**) &tempLob,
            error) < 0)
        return DPI_FAILURE;
    ob_dpiGen__setRefCount(conn, error, 1);
    tempLob->conn = conn;
    tempLob->type = type;
    if (ob_dpiOci__descriptorAlloc(conn->env->handle, &tempLob->locator,
            DPI_OCI_DTYPE_LOB, "allocate descriptor", error) < 0) {
        ob_dpiLob__free(tempLob, error);
        return DPI_FAILURE;
    }
    if (ob_dpiHandleList__addHandle(conn->openLobs, tempLob,
            &tempLob->openSlotNum, error) < 0) {
        ob_dpiLob__free(tempLob, error);
        return DPI_FAILURE;
    }

    *lob = tempLob;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiLob__check() [INTERNAL]
//   Check that the LOB is valid and get an error handle for subsequent calls.
//-----------------------------------------------------------------------------
static int ob_dpiLob__check(dpiLob *lob, const char *fnName, dpiError *error)
{
    if (ob_dpiGen__startPublicFn(lob, DPI_HTYPE_LOB, fnName, error) < 0)
        return DPI_FAILURE;
    if (!lob->conn || !lob->conn->handle)
        return ob_dpiError__set(error, "conn closed?", DPI_ERR_NOT_CONNECTED);
    if (!lob->locator)
        return ob_dpiError__set(error, "LOB closed?", DPI_ERR_LOB_CLOSED);
    return ob_dpiConn__checkConnected(lob->conn, error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob__close() [INTERNAL]
//   Internal method used for closing the LOB.
//-----------------------------------------------------------------------------
int ob_dpiLob__close(dpiLob *lob, int propagateErrors, dpiError *error)
{
    int isTemporary, closing, status = DPI_SUCCESS;

    // determine whether LOB is already being closed and if not, mark LOB as
    // being closed; this MUST be done while holding the lock (if in threaded
    // mode) to avoid race conditions!
    if (lob->env->threaded)
        ob_dpiMutex__acquire(lob->env->mutex);
    closing = lob->closing;
    lob->closing = 1;
    if (lob->env->threaded)
        ob_dpiMutex__release(lob->env->mutex);

    // if LOB is already being closed, nothing needs to be done
    if (closing)
        return DPI_SUCCESS;

    // perform actual work of closing LOB
    if (lob->locator) {
        if (!lob->conn->deadSession && lob->conn->handle) {
            status = ob_dpiOci__lobIsTemporary(lob, &isTemporary, propagateErrors,
                    error);
            if (isTemporary && status == DPI_SUCCESS)
                status = ob_dpiOci__lobFreeTemporary(lob->conn,
                        lob->locator, propagateErrors, error);
        }
        ob_dpiOci__descriptorFree(lob->locator, DPI_OCI_DTYPE_LOB);
        lob->locator = NULL;
    }
    if (lob->buffer) {
        ob_dpiUtils__freeMemory(lob->buffer);
        lob->buffer = NULL;
    }

    // if actual close fails, reset closing flag; again, this must be done
    // while holding the lock (if in threaded mode) in order to avoid race
    // conditions!
    if (status < 0) {
        if (lob->env->threaded)
            ob_dpiMutex__acquire(lob->env->mutex);
        lob->closing = 0;
        if (lob->env->threaded)
            ob_dpiMutex__release(lob->env->mutex);
    }

    return status;
}


//-----------------------------------------------------------------------------
// ob_dpiLob__free() [INTERNAL]
//   Free the memory for a LOB.
//-----------------------------------------------------------------------------
void ob_dpiLob__free(dpiLob *lob, dpiError *error)
{
    ob_dpiLob__close(lob, 0, error);
    if (lob->conn) {
        ob_dpiHandleList__removeHandle(lob->conn->openLobs, lob->openSlotNum);
        ob_dpiGen__setRefCount(lob->conn, error, -1);
        lob->conn = NULL;
    }
    ob_dpiUtils__freeMemory(lob);
}


//-----------------------------------------------------------------------------
// ob_dpiLob__readBytes() [INTERNAL]
//   Return a portion (or all) of the data in the LOB.
//-----------------------------------------------------------------------------
int ob_dpiLob__readBytes(dpiLob *lob, uint64_t offset, uint64_t amount,
        char *value, uint64_t *valueLength, dpiError *error)
{
    uint64_t lengthInBytes = 0, lengthInChars = 0;
    int isOpen = 0;

    // amount is in characters for character LOBs and bytes for binary LOBs
    if (lob->type->isCharacterData)
        lengthInChars = amount;
    else lengthInBytes = amount;

    // for files, open the file if needed
    if (lob->type->oracleTypeNum == DPI_ORACLE_TYPE_BFILE) {
        if (ob_dpiOci__lobIsOpen(lob, &isOpen, error) < 0)
            return DPI_FAILURE;
        if (!isOpen) {
            if (ob_dpiOci__lobOpen(lob, error) < 0)
                return DPI_FAILURE;
        }
    }

    // read the bytes from the LOB
    if (ob_dpiOci__lobRead2(lob, offset, &lengthInBytes, &lengthInChars,
            value, *valueLength, error) < 0)
        return DPI_FAILURE;
    *valueLength = lengthInBytes;

    // if file was opened in this routine, close it again
    if (lob->type->oracleTypeNum == DPI_ORACLE_TYPE_BFILE && !isOpen) {
        if (ob_dpiOci__lobClose(lob, error) < 0)
            return DPI_FAILURE;
    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiLob__setFromBytes() [INTERNAL]
//   Clear the LOB completely and then write the specified bytes to it.
//-----------------------------------------------------------------------------
int ob_dpiLob__setFromBytes(dpiLob *lob, const char *value, uint64_t valueLength,
        dpiError *error)
{
    if (ob_dpiOci__lobTrim2(lob, 0, error) < 0)
        return DPI_FAILURE;
    if (valueLength == 0)
        return DPI_SUCCESS;
    return ob_dpiOci__lobWrite2(lob, 1, value, valueLength, error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_addRef() [PUBLIC]
//   Add a reference to the LOB.
//-----------------------------------------------------------------------------
int ob_dpiLob_addRef(dpiLob *lob)
{
    return ob_dpiGen__addRef(lob, DPI_HTYPE_LOB, __func__);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_close() [PUBLIC]
//   Close the LOB and make it unusable for further operations.
//-----------------------------------------------------------------------------
int ob_dpiLob_close(dpiLob *lob)
{
    dpiError error;
    int status;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    status = ob_dpiLob__close(lob, 1, &error);
    return ob_dpiGen__endPublicFn(lob, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_closeResource() [PUBLIC]
//   Close the LOB's resources.
//-----------------------------------------------------------------------------
int ob_dpiLob_closeResource(dpiLob *lob)
{
    dpiError error;
    int status;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    status = ob_dpiOci__lobClose(lob, &error);
    return ob_dpiGen__endPublicFn(lob, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_copy() [PUBLIC]
//   Create a copy of the LOB and return it.
//-----------------------------------------------------------------------------
int ob_dpiLob_copy(dpiLob *lob, dpiLob **copiedLob)
{
    dpiLob *tempLob;
    dpiError error;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(lob, copiedLob)
    if (ob_dpiLob__allocate(lob->conn, lob->type, &tempLob, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    if (ob_dpiOci__lobLocatorAssign(lob, &tempLob->locator, &error) < 0) {
        ob_dpiLob__free(tempLob, &error);
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    }
    *copiedLob = tempLob;
    return ob_dpiGen__endPublicFn(lob, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_getBufferSize() [PUBLIC]
//   Get the required size of a buffer given the number of characters. If the
// LOB does not refer to a character LOB the value is returned unchanged.
//-----------------------------------------------------------------------------
int ob_dpiLob_getBufferSize(dpiLob *lob, uint64_t sizeInChars,
        uint64_t *sizeInBytes)
{
    dpiError error;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(lob, sizeInBytes)
    if (lob->type->oracleTypeNum == DPI_ORACLE_TYPE_CLOB)
        *sizeInBytes = sizeInChars * lob->env->maxBytesPerCharacter;
    else if (lob->type->oracleTypeNum == DPI_ORACLE_TYPE_NCLOB)
        *sizeInBytes = sizeInChars * lob->env->nmaxBytesPerCharacter;
    else *sizeInBytes = sizeInChars;
    return ob_dpiGen__endPublicFn(lob, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_getChunkSize() [PUBLIC]
//   Return the chunk size associated with the LOB.
//-----------------------------------------------------------------------------
int ob_dpiLob_getChunkSize(dpiLob *lob, uint32_t *size)
{
    dpiError error;
    int status;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(lob, size)
    status = ob_dpiOci__lobGetChunkSize(lob, size, &error);
    return ob_dpiGen__endPublicFn(lob, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_getDirectoryAndFileName() [PUBLIC]
//   Return the directory alias and file name for the BFILE lob.
//-----------------------------------------------------------------------------
int ob_dpiLob_getDirectoryAndFileName(dpiLob *lob, const char **directoryAlias,
        uint32_t *directoryAliasLength, const char **fileName,
        uint32_t *fileNameLength)
{
    uint16_t ociDirectoryAliasLength, ociFileNameLength;
    dpiError error;

    // validate parameters
    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(lob, directoryAlias)
    DPI_CHECK_PTR_NOT_NULL(lob, directoryAliasLength)
    DPI_CHECK_PTR_NOT_NULL(lob, fileName)
    DPI_CHECK_PTR_NOT_NULL(lob, fileNameLength)

    // get directory and file name
    ociDirectoryAliasLength = 30;
    ociFileNameLength = 255;
    if (!lob->buffer) {
        if (ob_dpiUtils__allocateMemory(1,
                ociDirectoryAliasLength + ociFileNameLength, 0,
                "allocate name buffer", (void**) &lob->buffer, &error) < 0)
            return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    }
    *directoryAlias = lob->buffer;
    *fileName = lob->buffer + ociDirectoryAliasLength;
    if (ob_dpiOci__lobFileGetName(lob, (char*) *directoryAlias,
            &ociDirectoryAliasLength, (char*) *fileName, &ociFileNameLength,
            &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    *directoryAliasLength = ociDirectoryAliasLength;
    *fileNameLength = ociFileNameLength;
    return ob_dpiGen__endPublicFn(lob, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_getFileExists() [PUBLIC]
//   Return whether or not the file pointed to by the locator exists.
//-----------------------------------------------------------------------------
int ob_dpiLob_getFileExists(dpiLob *lob, int *exists)
{
    dpiError error;
    int status;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(lob, exists)
    status = ob_dpiOci__lobFileExists(lob, exists, &error);
    return ob_dpiGen__endPublicFn(lob, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_getIsResourceOpen() [PUBLIC]
//   Return whether or not the LOB' resources are open.
//-----------------------------------------------------------------------------
int ob_dpiLob_getIsResourceOpen(dpiLob *lob, int *isOpen)
{
    dpiError error;
    int status;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(lob, isOpen)
    status = ob_dpiOci__lobIsOpen(lob, isOpen, &error);
    return ob_dpiGen__endPublicFn(lob, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_getSize() [PUBLIC]
//   Returns the size of the LOB.
//-----------------------------------------------------------------------------
int ob_dpiLob_getSize(dpiLob *lob, uint64_t *size)
{
    dpiError error;
    int status;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(lob, size)
    status = ob_dpiOci__lobGetLength2(lob, size, &error);
    return ob_dpiGen__endPublicFn(lob, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_getType() [PUBLIC]
//   Returns the type of the LOB.
//-----------------------------------------------------------------------------
int ob_dpiLob_getType(dpiLob *lob, dpiOracleTypeNum *type)
{
    dpiError error;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(lob, type)
    *type = lob->type->oracleTypeNum;
    return ob_dpiGen__endPublicFn(lob, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_openResource() [PUBLIC]
//   Open the LOB's resources to speed further accesses.
//-----------------------------------------------------------------------------
int ob_dpiLob_openResource(dpiLob *lob)
{
    dpiError error;
    int status;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    status = ob_dpiOci__lobOpen(lob, &error);
    return ob_dpiGen__endPublicFn(lob, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_readBytes() [PUBLIC]
//   Return a portion (or all) of the data in the LOB.
//-----------------------------------------------------------------------------
int ob_dpiLob_readBytes(dpiLob *lob, uint64_t offset, uint64_t amount,
        char *value, uint64_t *valueLength)
{
    dpiError error;
    int status;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(lob, value)
    DPI_CHECK_PTR_NOT_NULL(lob, valueLength)
    status = ob_dpiLob__readBytes(lob, offset, amount, value, valueLength,
            &error);
    return ob_dpiGen__endPublicFn(lob, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_release() [PUBLIC]
//   Release a reference to the LOB.
//-----------------------------------------------------------------------------
int ob_dpiLob_release(dpiLob *lob)
{
    return ob_dpiGen__release(lob, DPI_HTYPE_LOB, __func__);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_setDirectoryAndFileName() [PUBLIC]
//   Set the directory alias and file name for the BFILE LOB.
//-----------------------------------------------------------------------------
int ob_dpiLob_setDirectoryAndFileName(dpiLob *lob, const char *directoryAlias,
        uint32_t directoryAliasLength, const char *fileName,
        uint32_t fileNameLength)
{
    dpiError error;
    int status;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(lob, directoryAlias)
    DPI_CHECK_PTR_NOT_NULL(lob, fileName)
    status = ob_dpiOci__lobFileSetName(lob, directoryAlias,
            (uint16_t) directoryAliasLength, fileName,
            (uint16_t) fileNameLength, &error);
    return ob_dpiGen__endPublicFn(lob, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_setFromBytes() [PUBLIC]
//   Clear the LOB completely and then write the specified bytes to it.
//-----------------------------------------------------------------------------
int ob_dpiLob_setFromBytes(dpiLob *lob, const char *value, uint64_t valueLength)
{
    dpiError error;
    int status;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    DPI_CHECK_PTR_AND_LENGTH(lob, value)
    status = ob_dpiLob__setFromBytes(lob, value, valueLength, &error);
    return ob_dpiGen__endPublicFn(lob, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_trim() [PUBLIC]
//   Trim the LOB to the specified length.
//-----------------------------------------------------------------------------
int ob_dpiLob_trim(dpiLob *lob, uint64_t newSize)
{
    dpiError error;
    int status;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    status = ob_dpiOci__lobTrim2(lob, newSize, &error);
    return ob_dpiGen__endPublicFn(lob, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiLob_writeBytes() [PUBLIC]
//   Write the data to the LOB at the offset specified.
//-----------------------------------------------------------------------------
int ob_dpiLob_writeBytes(dpiLob *lob, uint64_t offset, const char *value,
        uint64_t valueLength)
{
    dpiError error;
    int status;

    if (ob_dpiLob__check(lob, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(lob, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(lob, value)
    status = ob_dpiOci__lobWrite2(lob, offset, value, valueLength, &error);
    return ob_dpiGen__endPublicFn(lob, status, &error);
}
