//-----------------------------------------------------------------------------
// Copyright (c) 2018, 2022, Oracle and/or its affiliates.
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
// dpiSodaCollCursor.c
//   Implementation of SODA collection cursors.
//-----------------------------------------------------------------------------

#include "dpiImpl.h"

//-----------------------------------------------------------------------------
// ob_dpiSodaCollCursor__allocate() [INTERNAL]
//   Allocate and initialize a SODA collection cursor structure.
//-----------------------------------------------------------------------------
int ob_dpiSodaCollCursor__allocate(dpiSodaDb *db, void *handle,
        dpiSodaCollCursor **cursor, dpiError *error)
{
    dpiSodaCollCursor *tempCursor;

    if (ob_dpiGen__allocate(DPI_HTYPE_SODA_COLL_CURSOR, db->env,
            (void**) &tempCursor, error) < 0)
        return DPI_FAILURE;
    ob_dpiGen__setRefCount(db, error, 1);
    tempCursor->db = db;
    tempCursor->handle = handle;
    *cursor = tempCursor;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiSodaCollCursor__check() [INTERNAL]
//   Determine if the SODA collection cursor is available to use.
//-----------------------------------------------------------------------------
static int ob_dpiSodaCollCursor__check(dpiSodaCollCursor *cursor,
        const char *fnName, dpiError *error)
{
    if (ob_dpiGen__startPublicFn(cursor, DPI_HTYPE_SODA_COLL_CURSOR, fnName,
            error) < 0)
        return DPI_FAILURE;
    if (!cursor->handle)
        return ob_dpiError__set(error, "check closed",
                DPI_ERR_SODA_CURSOR_CLOSED);
    if (!cursor->db->conn->handle || cursor->db->conn->closing)
        return ob_dpiError__set(error, "check connection", DPI_ERR_NOT_CONNECTED);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiSodaCollCursor__free() [INTERNAL]
//   Free the memory for a SODA collection cursor. Note that the reference to
//   the database must remain until after the handle is freed; otherwise, a
//   segfault can take place.
//-----------------------------------------------------------------------------
void ob_dpiSodaCollCursor__free(dpiSodaCollCursor *cursor, dpiError *error)
{
    if (cursor->handle) {
        ob_dpiOci__handleFree(cursor->handle, DPI_OCI_HTYPE_SODA_COLL_CURSOR);
        cursor->handle = NULL;
    }
    if (cursor->db) {
        ob_dpiGen__setRefCount(cursor->db, error, -1);
        cursor->db = NULL;
    }
    ob_dpiUtils__freeMemory(cursor);
}


//-----------------------------------------------------------------------------
// ob_dpiSodaCollCursor_addRef() [PUBLIC]
//   Add a reference to the SODA collection cursor.
//-----------------------------------------------------------------------------
int ob_dpiSodaCollCursor_addRef(dpiSodaCollCursor *cursor)
{
    return ob_dpiGen__addRef(cursor, DPI_HTYPE_SODA_COLL_CURSOR, __func__);
}


//-----------------------------------------------------------------------------
// ob_dpiSodaCollCursor_close() [PUBLIC]
//   Close the cursor.
//-----------------------------------------------------------------------------
int ob_dpiSodaCollCursor_close(dpiSodaCollCursor *cursor)
{
    dpiError error;

    if (ob_dpiSodaCollCursor__check(cursor, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(cursor, DPI_FAILURE, &error);
    if (cursor->handle) {
        ob_dpiOci__handleFree(cursor->handle, DPI_OCI_HTYPE_SODA_COLL_CURSOR);
        cursor->handle = NULL;
    }
    return ob_dpiGen__endPublicFn(cursor, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiSodaCollCursor_getNext() [PUBLIC]
//   Return the next collection available from the cursor.
//-----------------------------------------------------------------------------
int ob_dpiSodaCollCursor_getNext(dpiSodaCollCursor *cursor, UNUSED uint32_t flags,
        dpiSodaColl **coll)
{
    dpiError error;
    void *handle;

    if (ob_dpiSodaCollCursor__check(cursor, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(cursor, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(cursor, coll)
    if (ob_dpiOci__sodaCollGetNext(cursor->db->conn, cursor->handle, &handle,
            &error) < 0)
        return ob_dpiGen__endPublicFn(cursor, DPI_FAILURE, &error);
    *coll = NULL;
    if (handle) {
        if (ob_dpiSodaColl__allocate(cursor->db, handle, coll, &error) < 0) {
            ob_dpiOci__handleFree(handle, DPI_OCI_HTYPE_SODA_COLLECTION);
            return ob_dpiGen__endPublicFn(cursor, DPI_FAILURE, &error);
        }
    }
    return ob_dpiGen__endPublicFn(cursor, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiSodaCollCursor_release() [PUBLIC]
//   Release a reference to the SODA collection cursor.
//-----------------------------------------------------------------------------
int ob_dpiSodaCollCursor_release(dpiSodaCollCursor *cursor)
{
    return ob_dpiGen__release(cursor, DPI_HTYPE_SODA_COLL_CURSOR, __func__);
}
