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
// dpiDeqOptions.c
//   Implementation of AQ dequeue options.
//-----------------------------------------------------------------------------

#include "dpiImpl.h"

//-----------------------------------------------------------------------------
// ob_dpiDeqOptions__create() [INTERNAL]
//   Create a new subscription structure and return it. In case of error NULL
// is returned.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions__create(dpiDeqOptions *options, dpiConn *conn,
        dpiError *error)
{
    ob_dpiGen__setRefCount(conn, error, 1);
    options->conn = conn;
    return ob_dpiOci__descriptorAlloc(conn->env->handle, &options->handle,
            DPI_OCI_DTYPE_AQDEQ_OPTIONS, "allocate descriptor", error);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions__free() [INTERNAL]
//   Free the memory for a dequeue options structure.
//-----------------------------------------------------------------------------
void ob_dpiDeqOptions__free(dpiDeqOptions *options, dpiError *error)
{
    if (options->msgIdRaw) {
        ob_dpiOci__rawResize(options->env->handle, &options->msgIdRaw, 0, error);
        options->msgIdRaw = NULL;
    }
    if (options->handle) {
        ob_dpiOci__descriptorFree(options->handle, DPI_OCI_DTYPE_AQDEQ_OPTIONS);
        options->handle = NULL;
    }
    if (options->conn) {
        ob_dpiGen__setRefCount(options->conn, error, -1);
        options->conn = NULL;
    }
    ob_dpiUtils__freeMemory(options);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions__getAttrValue() [INTERNAL]
//   Get the attribute value in OCI.
//-----------------------------------------------------------------------------
static int ob_dpiDeqOptions__getAttrValue(dpiDeqOptions *options,
        uint32_t attribute, const char *fnName, void *value,
        uint32_t *valueLength)
{
    dpiError error;
    int status;

    if (ob_dpiGen__startPublicFn(options, DPI_HTYPE_DEQ_OPTIONS, fnName,
            &error) < 0)
        return ob_dpiGen__endPublicFn(options, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(options, value)
    DPI_CHECK_PTR_NOT_NULL(options, valueLength)
    status = ob_dpiOci__attrGet(options->handle, DPI_OCI_DTYPE_AQDEQ_OPTIONS,
            value, valueLength, attribute, "get attribute value", &error);
    return ob_dpiGen__endPublicFn(options, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions__setAttrValue() [INTERNAL]
//   Set the attribute value in OCI.
//-----------------------------------------------------------------------------
static int ob_dpiDeqOptions__setAttrValue(dpiDeqOptions *options,
        uint32_t attribute, const char *fnName, const void *value,
        uint32_t valueLength)
{
    dpiError error;
    int status;

    if (ob_dpiGen__startPublicFn(options, DPI_HTYPE_DEQ_OPTIONS, fnName,
            &error) < 0)
        return ob_dpiGen__endPublicFn(options, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(options, value)
    status = ob_dpiOci__attrSet(options->handle, DPI_OCI_DTYPE_AQDEQ_OPTIONS,
            (void*) value, valueLength, attribute, "set attribute value",
            &error);
    return ob_dpiGen__endPublicFn(options, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_addRef() [PUBLIC]
//   Add a reference to the dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_addRef(dpiDeqOptions *options)
{
    return ob_dpiGen__addRef(options, DPI_HTYPE_DEQ_OPTIONS, __func__);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_getCondition() [PUBLIC]
//   Return condition associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_getCondition(dpiDeqOptions *options, const char **value,
        uint32_t *valueLength)
{
    return ob_dpiDeqOptions__getAttrValue(options, DPI_OCI_ATTR_DEQCOND, __func__,
            (void*) value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_getConsumerName() [PUBLIC]
//   Return consumer name associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_getConsumerName(dpiDeqOptions *options, const char **value,
        uint32_t *valueLength)
{
    return ob_dpiDeqOptions__getAttrValue(options, DPI_OCI_ATTR_CONSUMER_NAME,
            __func__, (void*) value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_getCorrelation() [PUBLIC]
//   Return correlation associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_getCorrelation(dpiDeqOptions *options, const char **value,
        uint32_t *valueLength)
{
    return ob_dpiDeqOptions__getAttrValue(options, DPI_OCI_ATTR_CORRELATION,
            __func__, (void*) value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_getMode() [PUBLIC]
//   Return mode associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_getMode(dpiDeqOptions *options, dpiDeqMode *value)
{
    uint32_t valueLength = sizeof(uint32_t);

    return ob_dpiDeqOptions__getAttrValue(options, DPI_OCI_ATTR_DEQ_MODE,
            __func__, value, &valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_getMsgId() [PUBLIC]
//   Return message id associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_getMsgId(dpiDeqOptions *options, const char **value,
        uint32_t *valueLength)
{
    dpiError error;
    void *rawValue;

    if (ob_dpiGen__startPublicFn(options, DPI_HTYPE_DEQ_OPTIONS, __func__,
            &error) < 0)
        return ob_dpiGen__endPublicFn(options, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(options, value)
    DPI_CHECK_PTR_NOT_NULL(options, valueLength)
    if (ob_dpiOci__attrGet(options->handle, DPI_OCI_DTYPE_AQDEQ_OPTIONS,
            &rawValue, NULL, DPI_OCI_ATTR_DEQ_MSGID, "get attribute value",
            &error) < 0)
        return ob_dpiGen__endPublicFn(options, DPI_FAILURE, &error);
    ob_dpiOci__rawPtr(options->env->handle, rawValue, (void**) value);
    ob_dpiOci__rawSize(options->env->handle, rawValue, valueLength);
    return ob_dpiGen__endPublicFn(options, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_getNavigation() [PUBLIC]
//   Return navigation associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_getNavigation(dpiDeqOptions *options,
        dpiDeqNavigation *value)
{
    uint32_t valueLength = sizeof(uint32_t);

    return ob_dpiDeqOptions__getAttrValue(options, DPI_OCI_ATTR_NAVIGATION,
            __func__, value, &valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_getTransformation() [PUBLIC]
//   Return transformation associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_getTransformation(dpiDeqOptions *options, const char **value,
        uint32_t *valueLength)
{
    return ob_dpiDeqOptions__getAttrValue(options, DPI_OCI_ATTR_TRANSFORMATION,
            __func__, (void*) value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_getVisibility() [PUBLIC]
//   Return visibility associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_getVisibility(dpiDeqOptions *options, dpiVisibility *value)
{
    uint32_t valueLength = sizeof(uint32_t);

    return ob_dpiDeqOptions__getAttrValue(options, DPI_OCI_ATTR_VISIBILITY,
            __func__, value, &valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_getWait() [PUBLIC]
//   Return the number of seconds to wait for a message when dequeuing.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_getWait(dpiDeqOptions *options, uint32_t *value)
{
    uint32_t valueLength = sizeof(uint32_t);

    return ob_dpiDeqOptions__getAttrValue(options, DPI_OCI_ATTR_WAIT, __func__,
            value, &valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_release() [PUBLIC]
//   Release a reference to the dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_release(dpiDeqOptions *options)
{
    return ob_dpiGen__release(options, DPI_HTYPE_DEQ_OPTIONS, __func__);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_setCondition() [PUBLIC]
//   Set condition associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_setCondition(dpiDeqOptions *options, const char *value,
        uint32_t valueLength)
{
    return ob_dpiDeqOptions__setAttrValue(options, DPI_OCI_ATTR_DEQCOND, __func__,
            value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_setConsumerName() [PUBLIC]
//   Set consumer name associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_setConsumerName(dpiDeqOptions *options, const char *value,
        uint32_t valueLength)
{
    return ob_dpiDeqOptions__setAttrValue(options, DPI_OCI_ATTR_CONSUMER_NAME,
            __func__, value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_setCorrelation() [PUBLIC]
//   Set correlation associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_setCorrelation(dpiDeqOptions *options, const char *value,
        uint32_t valueLength)
{
    return ob_dpiDeqOptions__setAttrValue(options, DPI_OCI_ATTR_CORRELATION,
            __func__, value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_setDeliveryMode() [PUBLIC]
//   Set the delivery mode associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_setDeliveryMode(dpiDeqOptions *options,
        dpiMessageDeliveryMode value)
{
    return ob_dpiDeqOptions__setAttrValue(options, DPI_OCI_ATTR_MSG_DELIVERY_MODE,
            __func__, &value, 0);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_setMode() [PUBLIC]
//   Set the mode associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_setMode(dpiDeqOptions *options, dpiDeqMode value)
{
    return ob_dpiDeqOptions__setAttrValue(options, DPI_OCI_ATTR_DEQ_MODE,
            __func__, &value, 0);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_setMsgId() [PUBLIC]
//   Set the message id associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_setMsgId(dpiDeqOptions *options, const char *value,
        uint32_t valueLength)
{
    dpiError error;
    int status;

    if (ob_dpiGen__startPublicFn(options, DPI_HTYPE_DEQ_OPTIONS, __func__,
            &error) < 0)
        return ob_dpiGen__endPublicFn(options, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(options, value)
    if (ob_dpiOci__rawAssignBytes(options->env->handle, value, valueLength,
            &options->msgIdRaw, &error) < 0)
        return ob_dpiGen__endPublicFn(options, DPI_FAILURE, &error);
    status = ob_dpiOci__attrSet(options->handle, DPI_OCI_DTYPE_AQDEQ_OPTIONS,
            (void*) &options->msgIdRaw, valueLength, DPI_OCI_ATTR_DEQ_MSGID,
            "set value", &error);
    return ob_dpiGen__endPublicFn(options, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_setNavigation() [PUBLIC]
//   Set navigation associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_setNavigation(dpiDeqOptions *options, dpiDeqNavigation value)
{
    return ob_dpiDeqOptions__setAttrValue(options, DPI_OCI_ATTR_NAVIGATION,
            __func__, &value, 0);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_setTransformation() [PUBLIC]
//   Set transformation associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_setTransformation(dpiDeqOptions *options, const char *value,
        uint32_t valueLength)
{
    return ob_dpiDeqOptions__setAttrValue(options, DPI_OCI_ATTR_TRANSFORMATION,
            __func__, value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_setVisibility() [PUBLIC]
//   Set visibility associated with dequeue options.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_setVisibility(dpiDeqOptions *options, dpiVisibility value)
{
    return ob_dpiDeqOptions__setAttrValue(options, DPI_OCI_ATTR_VISIBILITY,
            __func__, &value, 0);
}


//-----------------------------------------------------------------------------
// ob_dpiDeqOptions_setWait() [PUBLIC]
//   Set the number of seconds to wait for a message when dequeuing.
//-----------------------------------------------------------------------------
int ob_dpiDeqOptions_setWait(dpiDeqOptions *options, uint32_t value)
{
    return ob_dpiDeqOptions__setAttrValue(options, DPI_OCI_ATTR_WAIT, __func__,
            &value, 0);
}
