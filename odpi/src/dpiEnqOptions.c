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
// dpiEnqOptions.c
//   Implementation of AQ enqueue options.
//-----------------------------------------------------------------------------

#include "dpiImpl.h"

//-----------------------------------------------------------------------------
// ob_dpiEnqOptions__create() [INTERNAL]
//   Create a new subscription structure and return it. In case of error NULL
// is returned.
//-----------------------------------------------------------------------------
int ob_dpiEnqOptions__create(dpiEnqOptions *options, dpiConn *conn,
        dpiError *error)
{
    ob_dpiGen__setRefCount(conn, error, 1);
    options->conn = conn;
    return ob_dpiOci__descriptorAlloc(conn->env->handle, &options->handle,
            DPI_OCI_DTYPE_AQENQ_OPTIONS, "allocate descriptor", error);
}


//-----------------------------------------------------------------------------
// ob_dpiEnqOptions__free() [INTERNAL]
//   Free the memory for a enqueue options structure.
//-----------------------------------------------------------------------------
void ob_dpiEnqOptions__free(dpiEnqOptions *options, dpiError *error)
{
    if (options->handle) {
        ob_dpiOci__descriptorFree(options->handle, DPI_OCI_DTYPE_AQENQ_OPTIONS);
        options->handle = NULL;
    }
    if (options->conn) {
        ob_dpiGen__setRefCount(options->conn, error, -1);
        options->conn = NULL;
    }
    ob_dpiUtils__freeMemory(options);
}


//-----------------------------------------------------------------------------
// ob_dpiEnqOptions__getAttrValue() [INTERNAL]
//   Get the attribute value in OCI.
//-----------------------------------------------------------------------------
static int ob_dpiEnqOptions__getAttrValue(dpiEnqOptions *options,
        uint32_t attribute, const char *fnName, void *value,
        uint32_t *valueLength)
{
    dpiError error;
    int status;

    if (ob_dpiGen__startPublicFn(options, DPI_HTYPE_ENQ_OPTIONS, fnName,
            &error) < 0)
        return ob_dpiGen__endPublicFn(options, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(options, value)
    DPI_CHECK_PTR_NOT_NULL(options, valueLength)
    status = ob_dpiOci__attrGet(options->handle, DPI_OCI_DTYPE_AQENQ_OPTIONS,
            value, valueLength, attribute, "get attribute value", &error);
    return ob_dpiGen__endPublicFn(options, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiEnqOptions__setAttrValue() [INTERNAL]
//   Set the attribute value in OCI.
//-----------------------------------------------------------------------------
static int ob_dpiEnqOptions__setAttrValue(dpiEnqOptions *options,
        uint32_t attribute, const char *fnName, const void *value,
        uint32_t valueLength)
{
    dpiError error;
    int status;

    if (ob_dpiGen__startPublicFn(options, DPI_HTYPE_ENQ_OPTIONS, fnName,
            &error) < 0)
        return ob_dpiGen__endPublicFn(options, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(options, value)
    status = ob_dpiOci__attrSet(options->handle, DPI_OCI_DTYPE_AQENQ_OPTIONS,
            (void*) value, valueLength, attribute, "set attribute value",
            &error);
    return ob_dpiGen__endPublicFn(options, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiEnqOptions_addRef() [PUBLIC]
//   Add a reference to the enqueue options.
//-----------------------------------------------------------------------------
int ob_dpiEnqOptions_addRef(dpiEnqOptions *options)
{
    return ob_dpiGen__addRef(options, DPI_HTYPE_ENQ_OPTIONS, __func__);
}


//-----------------------------------------------------------------------------
// ob_dpiEnqOptions_getTransformation() [PUBLIC]
//   Return transformation associated with enqueue options.
//-----------------------------------------------------------------------------
int ob_dpiEnqOptions_getTransformation(dpiEnqOptions *options, const char **value,
        uint32_t *valueLength)
{
    return ob_dpiEnqOptions__getAttrValue(options, DPI_OCI_ATTR_TRANSFORMATION,
            __func__, (void*) value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiEnqOptions_getVisibility() [PUBLIC]
//   Return visibility associated with enqueue options.
//-----------------------------------------------------------------------------
int ob_dpiEnqOptions_getVisibility(dpiEnqOptions *options, dpiVisibility *value)
{
    uint32_t valueLength = sizeof(uint32_t);

    return ob_dpiEnqOptions__getAttrValue(options, DPI_OCI_ATTR_VISIBILITY,
            __func__, value, &valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiEnqOptions_release() [PUBLIC]
//   Release a reference to the enqueue options.
//-----------------------------------------------------------------------------
int ob_dpiEnqOptions_release(dpiEnqOptions *options)
{
    return ob_dpiGen__release(options, DPI_HTYPE_ENQ_OPTIONS, __func__);
}


//-----------------------------------------------------------------------------
// ob_dpiEnqOptions_setDeliveryMode() [PUBLIC]
//   Set the delivery mode associated with enqueue options.
//-----------------------------------------------------------------------------
int ob_dpiEnqOptions_setDeliveryMode(dpiEnqOptions *options,
        dpiMessageDeliveryMode value)
{
    return ob_dpiEnqOptions__setAttrValue(options, DPI_OCI_ATTR_MSG_DELIVERY_MODE,
            __func__, &value, 0);
}


//-----------------------------------------------------------------------------
// ob_dpiEnqOptions_setTransformation() [PUBLIC]
//   Set transformation associated with enqueue options.
//-----------------------------------------------------------------------------
int ob_dpiEnqOptions_setTransformation(dpiEnqOptions *options, const char *value,
        uint32_t valueLength)
{
    return ob_dpiEnqOptions__setAttrValue(options, DPI_OCI_ATTR_TRANSFORMATION,
            __func__,  value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiEnqOptions_setVisibility() [PUBLIC]
//   Set visibility associated with enqueue options.
//-----------------------------------------------------------------------------
int ob_dpiEnqOptions_setVisibility(dpiEnqOptions *options, dpiVisibility value)
{
    return ob_dpiEnqOptions__setAttrValue(options, DPI_OCI_ATTR_VISIBILITY,
            __func__, &value, 0);
}
