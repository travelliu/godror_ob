//-----------------------------------------------------------------------------
// Copyright (c) 2016, 2022, Oracle and/or its affiliates.
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
// dpiEnv.c
//   Implementation of environment.
//-----------------------------------------------------------------------------

#include "dpiImpl.h"

//-----------------------------------------------------------------------------
// ob_dpiEnv__getBaseDate() [INTERNAL]
//   Return the base date (January 1, 1970 UTC) for the specified handle type.
// OCI doesn't permit mixing and matching types so a separate bae date is
// required for each of the three timestamp types. If the type has not been
// populated already it is created.
//-----------------------------------------------------------------------------
int ob_dpiEnv__getBaseDate(dpiEnv *env, uint32_t dataType, void **baseDate,
        dpiError *error)
{
    uint32_t descriptorType;
    char timezoneBuffer[20];
    size_t timezoneLength;
    void **storedBaseDate;

    // determine type of descriptor and location of stored base date
    switch (dataType) {
        case DPI_ORACLE_TYPE_TIMESTAMP:
            storedBaseDate = &env->baseDate;
            descriptorType = DPI_OCI_DTYPE_TIMESTAMP;
            break;
        case DPI_ORACLE_TYPE_TIMESTAMP_TZ:
            storedBaseDate = &env->baseDateTZ;
            descriptorType = DPI_OCI_DTYPE_TIMESTAMP_TZ;
            break;
        case DPI_ORACLE_TYPE_TIMESTAMP_LTZ:
            storedBaseDate = &env->baseDateLTZ;
            descriptorType = DPI_OCI_DTYPE_TIMESTAMP_LTZ;
            break;
        default:
            return ob_dpiError__set(error, "get base date",
                    DPI_ERR_UNHANDLED_DATA_TYPE, dataType);
    }

    // if a base date has not been stored already, create it
    if (!*storedBaseDate) {
        if (ob_dpiOci__descriptorAlloc(env->handle, storedBaseDate,
                descriptorType, "alloc base date descriptor", error) < 0)
            return DPI_FAILURE;
        if (ob_dpiOci__nlsCharSetConvert(env->handle, env->charsetId,
                timezoneBuffer, sizeof(timezoneBuffer), DPI_CHARSET_ID_ASCII,
                "+00:00", 6, &timezoneLength, error) < 0)
            return DPI_FAILURE;
        if (ob_dpiOci__dateTimeConstruct(env->handle, *storedBaseDate, 1970, 1, 1,
                0, 0, 0, 0, timezoneBuffer, timezoneLength, error) < 0)
            return DPI_FAILURE;
    }

    *baseDate = *storedBaseDate;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiEnv__free() [INTERNAL]
//   Free the memory associated with the environment.
//-----------------------------------------------------------------------------
void ob_dpiEnv__free(dpiEnv *env, dpiError *error)
{
    if (env->threaded)
        ob_dpiMutex__destroy(env->mutex);
    if (env->handle && !env->externalHandle) {
        ob_dpiOci__handleFree(env->handle, DPI_OCI_HTYPE_ENV);
        env->handle = NULL;
    }
    if (env->errorHandles) {
        ob_dpiHandlePool__free(env->errorHandles);
        env->errorHandles = NULL;
        error->handle = NULL;
    }
    ob_dpiUtils__freeMemory(env);
}


//-----------------------------------------------------------------------------
// ob_dpiEnv__getCharacterSetIdAndName() [INTERNAL]
//   Retrieve and store the IANA character set name for the attribute.
//-----------------------------------------------------------------------------
static int ob_dpiEnv__getCharacterSetIdAndName(dpiEnv *env, uint16_t attribute,
        uint16_t *charsetId, char *encoding, dpiError *error)
{
    *charsetId = 0;
    ob_dpiOci__attrGet(env->handle, DPI_OCI_HTYPE_ENV, charsetId, NULL, attribute,
            "get environment", error);
    return ob_dpiGlobal__lookupEncoding(*charsetId, encoding, error);
}


//-----------------------------------------------------------------------------
// ob_dpiEnv__getEncodingInfo() [INTERNAL]
//   Populate the structure with the encoding info.
//-----------------------------------------------------------------------------
int ob_dpiEnv__getEncodingInfo(dpiEnv *env, dpiEncodingInfo *info)
{
    info->encoding = env->encoding;
    info->maxBytesPerCharacter = env->maxBytesPerCharacter;
    info->nencoding = env->nencoding;
    info->nmaxBytesPerCharacter = env->nmaxBytesPerCharacter;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiEnv__init() [INTERNAL]
//   Initialize the environment structure. If an external handle is provided it
// is used directly; otherwise, a new OCI environment handle is created. In
// either case, information about the environment is stored for later use.
//-----------------------------------------------------------------------------
int ob_dpiEnv__init(dpiEnv *env, const dpiContext *context,
        const dpiCommonCreateParams *params, void *externalHandle,
        dpiCreateMode createMode, dpiError *error)
{
    int temp;

    // store context and version information
    env->context = context;
    env->versionInfo = context->versionInfo;

    // an external handle is available, use it directly
    if (externalHandle) {
        env->handle = externalHandle;
        env->externalHandle = 1;

    // otherwise, lookup encodings
    } else {

        // lookup encoding
        if (params->encoding && ob_dpiGlobal__lookupCharSet(params->encoding,
                &env->charsetId, error) < 0)
            return DPI_FAILURE;

        // check for identical encoding before performing lookup of national
        // character set encoding
        if (params->nencoding && params->encoding &&
                strcmp(params->nencoding, params->encoding) == 0)
            env->ncharsetId = env->charsetId;
        else if (params->nencoding &&
                ob_dpiGlobal__lookupCharSet(params->nencoding,
                        &env->ncharsetId, error) < 0)
            return DPI_FAILURE;

        // both charsetId and ncharsetId must be zero or both must be non-zero
        // use NLS routine to look up missing value, if needed
        if (env->charsetId && !env->ncharsetId) {
            if (ob_dpiOci__nlsEnvironmentVariableGet(DPI_OCI_NLS_NCHARSET_ID,
                    &env->ncharsetId, error) < 0)
                return DPI_FAILURE;
        } else if (!env->charsetId && env->ncharsetId) {
            if (ob_dpiOci__nlsEnvironmentVariableGet(DPI_OCI_NLS_CHARSET_ID,
                    &env->charsetId, error) < 0)
                return DPI_FAILURE;
        }

        // create new environment handle
        if (ob_dpiOci__envNlsCreate(&env->handle, createMode | DPI_OCI_OBJECT,
                env->charsetId, env->ncharsetId, error) < 0)
            return DPI_FAILURE;

    }

    // create the error handle pool
    if (ob_dpiHandlePool__create(&env->errorHandles, error) < 0)
        return DPI_FAILURE;
    error->env = env;

    // if threaded, create mutex for reference counts
    if (createMode & DPI_OCI_THREADED)
        ob_dpiMutex__initialize(env->mutex);

    // determine encodings in use
    if (ob_dpiEnv__getCharacterSetIdAndName(env, DPI_OCI_ATTR_CHARSET_ID,
            &env->charsetId, env->encoding, error) < 0)
        return DPI_FAILURE;
    if (ob_dpiEnv__getCharacterSetIdAndName(env, DPI_OCI_ATTR_NCHARSET_ID,
            &env->ncharsetId, env->nencoding, error) < 0)
        return DPI_FAILURE;

    // acquire max bytes per character
    if (ob_dpiOci__nlsNumericInfoGet(env->handle, &env->maxBytesPerCharacter,
            DPI_OCI_NLS_CHARSET_MAXBYTESZ, error) < 0)
        return DPI_FAILURE;

    // for NCHAR we have no idea of how many so we simply take the worst case
    // unless the charsets are identical
    if (env->ncharsetId == env->charsetId)
        env->nmaxBytesPerCharacter = env->maxBytesPerCharacter;
    else env->nmaxBytesPerCharacter = 4;

    // set whether or not we are threaded
    if (createMode & DPI_MODE_CREATE_THREADED)
        env->threaded = 1;

    // set whether or not events mode has been set
    if (createMode & DPI_MODE_CREATE_EVENTS)
        env->events = 1;

    // enable SODA metadata cache, if applicable
    if (params->sodaMetadataCache) {
        if (ob_dpiUtils__checkClientVersionMulti(env->versionInfo, 19, 11, 21, 3,
                error) < 0)
            return DPI_FAILURE;
        temp = 1;
        if (ob_dpiOci__attrSet(env->handle, DPI_OCI_HTYPE_ENV, &temp, 0,
                DPI_OCI_ATTR_SODA_METADATA_CACHE, "set SODA metadata cache",
                error) < 0)
            return DPI_FAILURE;
    }

    return DPI_SUCCESS;
}
