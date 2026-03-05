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
// dpiObjectAttr.c
//   Implementation of object attributes.
//-----------------------------------------------------------------------------

#include "dpiImpl.h"

//-----------------------------------------------------------------------------
// ob_dpiObjectAttr__allocate() [INTERNAL]
//   Allocate and initialize an object attribute structure.
//-----------------------------------------------------------------------------
int ob_dpiObjectAttr__allocate(dpiObjectType *objType, void *param,
        dpiObjectAttr **attr, dpiError *error)
{
    dpiObjectAttr *tempAttr;

    // allocate and assign main reference to the type this attribute belongs to
    *attr = NULL;
    if (ob_dpiGen__allocate(DPI_HTYPE_OBJECT_ATTR, objType->env,
            (void**) &tempAttr, error) < 0)
        return DPI_FAILURE;
    ob_dpiGen__setRefCount(objType, error, 1);
    tempAttr->belongsToType = objType;

    // determine the name of the attribute
    if (ob_dpiUtils__getAttrStringWithDup("get name", param, DPI_OCI_DTYPE_PARAM,
            DPI_OCI_ATTR_NAME, &tempAttr->name, &tempAttr->nameLength,
            error) < 0) {
        ob_dpiObjectAttr__free(tempAttr, error);
        return DPI_FAILURE;
    }

    // determine type information of the attribute
    if (ob_dpiOracleType__populateTypeInfo(objType->conn, param,
            DPI_OCI_DTYPE_PARAM, &tempAttr->typeInfo, error) < 0) {
        ob_dpiObjectAttr__free(tempAttr, error);
        return DPI_FAILURE;
    }

    *attr = tempAttr;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiObjectAttr__free() [INTERNAL]
//   Free the memory for an object attribute.
//-----------------------------------------------------------------------------
void ob_dpiObjectAttr__free(dpiObjectAttr *attr, dpiError *error)
{
    if (attr->belongsToType) {
        ob_dpiGen__setRefCount(attr->belongsToType, error, -1);
        attr->belongsToType = NULL;
    }
    if (attr->typeInfo.objectType) {
        ob_dpiGen__setRefCount(attr->typeInfo.objectType, error, -1);
        attr->typeInfo.objectType = NULL;
    }
    if (attr->name) {
        ob_dpiUtils__freeMemory((void*) attr->name);
        attr->name = NULL;
    }
    ob_dpiUtils__freeMemory(attr);
}


//-----------------------------------------------------------------------------
// ob_dpiObjectAttr_addRef() [PUBLIC]
//   Add a reference to the object attribute.
//-----------------------------------------------------------------------------
int ob_dpiObjectAttr_addRef(dpiObjectAttr *attr)
{
    return ob_dpiGen__addRef(attr, DPI_HTYPE_OBJECT_ATTR, __func__);
}


//-----------------------------------------------------------------------------
// ob_dpiObjectAttr_getInfo() [PUBLIC]
//   Return information about the attribute to the caller.
//-----------------------------------------------------------------------------
int ob_dpiObjectAttr_getInfo(dpiObjectAttr *attr, dpiObjectAttrInfo *info)
{
    dpiError error;

    if (ob_dpiGen__startPublicFn(attr, DPI_HTYPE_OBJECT_ATTR, __func__,
            &error) < 0)
        return ob_dpiGen__endPublicFn(attr, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(attr, info)
    info->name = attr->name;
    info->nameLength = attr->nameLength;

    // the size of the dpiDataTypeInfo structure changed in version 5.1 and
    // again in 5.2; this check and memcpy() for older versions can be removed
    // once 6.0 is released
    if (attr->env->context->dpiMinorVersion > 1) {
        info->typeInfo = attr->typeInfo;
    } else if (attr->env->context->dpiMinorVersion == 1) {
        memcpy(&info->typeInfo, &attr->typeInfo, sizeof(ob_dpiDataTypeInfo__v51));
    } else {
        memcpy(&info->typeInfo, &attr->typeInfo, sizeof(ob_dpiDataTypeInfo__v50));
    }

    return ob_dpiGen__endPublicFn(attr, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiObjectAttr_release() [PUBLIC]
//   Release a reference to the object attribute.
//-----------------------------------------------------------------------------
int ob_dpiObjectAttr_release(dpiObjectAttr *attr)
{
    return ob_dpiGen__release(attr, DPI_HTYPE_OBJECT_ATTR, __func__);
}
