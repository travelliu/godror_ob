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
// dpiMsgProps.c
//   Implementation of AQ message properties.
//-----------------------------------------------------------------------------

#include "dpiImpl.h"

//-----------------------------------------------------------------------------
// ob_dpiMsgProps__allocate() [INTERNAL]
//   Create a new message properties structure and return it. In case of error
// NULL is returned.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps__allocate(dpiConn *conn, dpiMsgProps **props, dpiError *error)
{
    dpiMsgProps *tempProps;

    if (ob_dpiGen__allocate(DPI_HTYPE_MSG_PROPS, conn->env, (void**) &tempProps,
            error) < 0)
        return DPI_FAILURE;
    ob_dpiGen__setRefCount(conn, error, 1);
    tempProps->conn = conn;
    if (ob_dpiOci__descriptorAlloc(conn->env->handle, &tempProps->handle,
            DPI_OCI_DTYPE_AQMSG_PROPERTIES, "allocate descriptor",
            error) < 0) {
        ob_dpiMsgProps__free(tempProps, error);
        return DPI_FAILURE;
    }

    *props = tempProps;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps__extractMsgId() [INTERNAL]
//   Extract bytes from the OCIRaw value containing the message id.
//-----------------------------------------------------------------------------
void ob_dpiMsgProps__extractMsgId(dpiMsgProps *props, const char **msgId,
        uint32_t *msgIdLength)
{
    ob_dpiOci__rawPtr(props->env->handle, props->msgIdRaw, (void**) msgId);
    ob_dpiOci__rawSize(props->env->handle, props->msgIdRaw, msgIdLength);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps__free() [INTERNAL]
//   Free the memory for a message properties structure.
//-----------------------------------------------------------------------------
void ob_dpiMsgProps__free(dpiMsgProps *props, dpiError *error)
{
    if (props->handle) {
        ob_dpiOci__descriptorFree(props->handle, DPI_OCI_DTYPE_AQMSG_PROPERTIES);
        props->handle = NULL;
    }
    if (props->payloadObj) {
        ob_dpiGen__setRefCount(props->payloadObj, error, -1);
        props->payloadObj = NULL;
    }
    if (props->payloadJson) {
        ob_dpiGen__setRefCount(props->payloadJson, error, -1);
        props->payloadJson = NULL;
    }
    if (props->payloadRaw) {
        ob_dpiOci__rawResize(props->env->handle, &props->payloadRaw, 0, error);
        props->payloadRaw = NULL;
    }
    if (props->msgIdRaw) {
        ob_dpiOci__rawResize(props->env->handle, &props->msgIdRaw, 0, error);
        props->msgIdRaw = NULL;
    }
    if (props->conn) {
        ob_dpiGen__setRefCount(props->conn, error, -1);
        props->conn = NULL;
    }
    ob_dpiUtils__freeMemory(props);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps__getAttrValue() [INTERNAL]
//   Get the attribute value in OCI.
//-----------------------------------------------------------------------------
static int ob_dpiMsgProps__getAttrValue(dpiMsgProps *props, uint32_t attribute,
        const char *fnName, void *value, uint32_t *valueLength)
{
    dpiError error;
    int status;

    if (ob_dpiGen__startPublicFn(props, DPI_HTYPE_MSG_PROPS, fnName, &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(props, value)
    DPI_CHECK_PTR_NOT_NULL(props, valueLength)
    status = ob_dpiOci__attrGet(props->handle, DPI_OCI_DTYPE_AQMSG_PROPERTIES,
            value, valueLength, attribute, "get attribute value", &error);
    return ob_dpiGen__endPublicFn(props, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps__setAttrValue() [INTERNAL]
//   Set the attribute value in OCI.
//-----------------------------------------------------------------------------
static int ob_dpiMsgProps__setAttrValue(dpiMsgProps *props, uint32_t attribute,
        const char *fnName, const void *value, uint32_t valueLength)
{
    dpiError error;
    int status;

    if (ob_dpiGen__startPublicFn(props, DPI_HTYPE_MSG_PROPS, fnName, &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(props, value)
    status = ob_dpiOci__attrSet(props->handle, DPI_OCI_DTYPE_AQMSG_PROPERTIES,
            (void*) value, valueLength, attribute, "set attribute value",
            &error);
    return ob_dpiGen__endPublicFn(props, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps__setRecipients() [INTERNAL]
//   Set the recipients value in OCI.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps__setRecipients(dpiMsgProps *props,
        dpiMsgRecipient *recipients, uint32_t numRecipients,
        void **aqAgents, dpiError *error)
{
    uint32_t i;

    for (i = 0; i < numRecipients; i++) {
        if (ob_dpiOci__descriptorAlloc(props->env->handle, &aqAgents[i],
                DPI_OCI_DTYPE_AQAGENT, "allocate agent descriptor",
                error) < 0)
            return DPI_FAILURE;
        if (recipients[i].name && recipients[i].nameLength > 0) {
            if (ob_dpiOci__attrSet(aqAgents[i], DPI_OCI_DTYPE_AQAGENT,
                    (void*) recipients[i].name, recipients[i].nameLength,
                    DPI_OCI_ATTR_AGENT_NAME, "set agent name", error) < 0)
                return DPI_FAILURE;
        }
    }
    if (ob_dpiOci__attrSet(props->handle, DPI_OCI_DTYPE_AQMSG_PROPERTIES,
            aqAgents, numRecipients, DPI_OCI_ATTR_RECIPIENT_LIST,
            "set recipient list", error) < 0)
        return DPI_FAILURE;

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_addRef() [PUBLIC]
//   Add a reference to the message properties.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_addRef(dpiMsgProps *props)
{
    return ob_dpiGen__addRef(props, DPI_HTYPE_MSG_PROPS, __func__);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_getCorrelation() [PUBLIC]
//   Return correlation associated with the message.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_getCorrelation(dpiMsgProps *props, const char **value,
        uint32_t *valueLength)
{
    return ob_dpiMsgProps__getAttrValue(props, DPI_OCI_ATTR_CORRELATION, __func__,
            (void*) value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_getDelay() [PUBLIC]
//   Return the number of seconds the message was delayed.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_getDelay(dpiMsgProps *props, int32_t *value)
{
    uint32_t valueLength = sizeof(uint32_t);

    return ob_dpiMsgProps__getAttrValue(props, DPI_OCI_ATTR_DELAY, __func__,
            value, &valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_getDeliveryMode() [PUBLIC]
//   Return the mode used for delivering the message.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_getDeliveryMode(dpiMsgProps *props,
        dpiMessageDeliveryMode *value)
{
    uint32_t valueLength = sizeof(uint16_t);

    return ob_dpiMsgProps__getAttrValue(props, DPI_OCI_ATTR_MSG_DELIVERY_MODE,
            __func__, value, &valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_getEnqTime() [PUBLIC]
//   Return the time the message was enqueued.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_getEnqTime(dpiMsgProps *props, dpiTimestamp *value)
{
    dpiOciDate ociValue;
    dpiError error;

    if (ob_dpiGen__startPublicFn(props, DPI_HTYPE_MSG_PROPS, __func__,
            &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(props, value)
    if (ob_dpiOci__attrGet(props->handle, DPI_OCI_DTYPE_AQMSG_PROPERTIES,
            &ociValue, NULL, DPI_OCI_ATTR_ENQ_TIME, "get attribute value",
            &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    value->year = ociValue.year;
    value->month = ociValue.month;
    value->day = ociValue.day;
    value->hour = ociValue.hour;
    value->minute = ociValue.minute;
    value->second = ociValue.second;
    value->fsecond = 0;
    value->tzHourOffset = 0;
    value->tzMinuteOffset = 0;
    return ob_dpiGen__endPublicFn(props, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_getExceptionQ() [PUBLIC]
//   Return the name of the exception queue associated with the message.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_getExceptionQ(dpiMsgProps *props, const char **value,
        uint32_t *valueLength)
{
    return ob_dpiMsgProps__getAttrValue(props, DPI_OCI_ATTR_EXCEPTION_QUEUE,
            __func__, (void*) value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_getExpiration() [PUBLIC]
//   Return the number of seconds until the message expires.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_getExpiration(dpiMsgProps *props, int32_t *value)
{
    uint32_t valueLength = sizeof(uint32_t);

    return ob_dpiMsgProps__getAttrValue(props, DPI_OCI_ATTR_EXPIRATION, __func__,
            value, &valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_getNumAttempts() [PUBLIC]
//   Return the number of attempts made to deliver the message.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_getNumAttempts(dpiMsgProps *props, int32_t *value)
{
    uint32_t valueLength = sizeof(uint32_t);

    return ob_dpiMsgProps__getAttrValue(props, DPI_OCI_ATTR_ATTEMPTS, __func__,
            value, &valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_getMsgId() [PUBLIC]
//   Return the message id for the message (available after enqueuing or
// dequeuing a message).
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_getMsgId(dpiMsgProps *props, const char **value,
        uint32_t *valueLength)
{
    dpiError error;

    if (ob_dpiGen__startPublicFn(props, DPI_HTYPE_MSG_PROPS, __func__,
            &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(props, value)
    DPI_CHECK_PTR_NOT_NULL(props, valueLength)
    ob_dpiMsgProps__extractMsgId(props, value, valueLength);
    return ob_dpiGen__endPublicFn(props, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_getOriginalMsgId() [PUBLIC]
//   Return the original message id for the message.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_getOriginalMsgId(dpiMsgProps *props, const char **value,
        uint32_t *valueLength)
{
    dpiError error;
    void *rawValue;

    if (ob_dpiGen__startPublicFn(props, DPI_HTYPE_MSG_PROPS, __func__,
            &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(props, value)
    DPI_CHECK_PTR_NOT_NULL(props, valueLength)
    if (ob_dpiOci__attrGet(props->handle, DPI_OCI_DTYPE_AQMSG_PROPERTIES,
            &rawValue, NULL, DPI_OCI_ATTR_ORIGINAL_MSGID,
            "get attribute value", &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    ob_dpiOci__rawPtr(props->env->handle, rawValue, (void**) value);
    ob_dpiOci__rawSize(props->env->handle, rawValue, valueLength);
    return ob_dpiGen__endPublicFn(props, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_getPayload() [PUBLIC]
//   Get the payload for the message (as an object or a series of bytes).
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_getPayload(dpiMsgProps *props, dpiObject **obj,
        const char **value, uint32_t *valueLength)
{
    dpiError error;

    if (ob_dpiGen__startPublicFn(props, DPI_HTYPE_MSG_PROPS, __func__,
            &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    if (obj)
        *obj = props->payloadObj;
    if (value && valueLength) {
        if (props->payloadRaw) {
            ob_dpiOci__rawPtr(props->env->handle, props->payloadRaw,
                    (void**) value);
            ob_dpiOci__rawSize(props->env->handle, props->payloadRaw,
                    valueLength);
        } else {
            *value = NULL;
            *valueLength = 0;
        }
    }

    return ob_dpiGen__endPublicFn(props, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_getPayloadJson() [PUBLIC]
//   Get the JSON payload for the message (as a dpiJson).
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_getPayloadJson(dpiMsgProps *props, dpiJson **json)
{
    dpiError error;

    if (ob_dpiGen__startPublicFn(props, DPI_HTYPE_MSG_PROPS, __func__,
            &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(props, json)
    *json = props->payloadJson;
    return ob_dpiGen__endPublicFn(props, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_getPriority() [PUBLIC]
//   Return the priority of the message.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_getPriority(dpiMsgProps *props, int32_t *value)
{
    uint32_t valueLength = sizeof(uint32_t);

    return ob_dpiMsgProps__getAttrValue(props, DPI_OCI_ATTR_PRIORITY, __func__,
            value, &valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_getState() [PUBLIC]
//   Return the state of the message.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_getState(dpiMsgProps *props, dpiMessageState *value)
{
    uint32_t valueLength = sizeof(uint32_t);


    return ob_dpiMsgProps__getAttrValue(props, DPI_OCI_ATTR_MSG_STATE, __func__,
            value, &valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_release() [PUBLIC]
//   Release a reference to the message properties.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_release(dpiMsgProps *props)
{
    return ob_dpiGen__release(props, DPI_HTYPE_MSG_PROPS, __func__);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_setCorrelation() [PUBLIC]
//   Set correlation associated with the message.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_setCorrelation(dpiMsgProps *props, const char *value,
        uint32_t valueLength)
{
    return ob_dpiMsgProps__setAttrValue(props, DPI_OCI_ATTR_CORRELATION, __func__,
            value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_setDelay() [PUBLIC]
//   Set the number of seconds to delay the message.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_setDelay(dpiMsgProps *props, int32_t value)
{
    return ob_dpiMsgProps__setAttrValue(props, DPI_OCI_ATTR_DELAY, __func__,
            &value, 0);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_setExceptionQ() [PUBLIC]
//   Set the name of the exception queue associated with the message.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_setExceptionQ(dpiMsgProps *props, const char *value,
        uint32_t valueLength)
{
    return ob_dpiMsgProps__setAttrValue(props, DPI_OCI_ATTR_EXCEPTION_QUEUE,
            __func__, value, valueLength);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_setExpiration() [PUBLIC]
//   Set the number of seconds until the message expires.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_setExpiration(dpiMsgProps *props, int32_t value)
{
    return ob_dpiMsgProps__setAttrValue(props, DPI_OCI_ATTR_EXPIRATION, __func__,
            &value, 0);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_setOriginalMsgId() [PUBLIC]
//   Set the original message id for the message.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_setOriginalMsgId(dpiMsgProps *props, const char *value,
        uint32_t valueLength)
{
    void *rawValue = NULL;
    dpiError error;
    int status;

    if (ob_dpiGen__startPublicFn(props, DPI_HTYPE_MSG_PROPS, __func__,
            &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(props, value)
    if (ob_dpiOci__rawAssignBytes(props->env->handle, value, valueLength,
            &rawValue, &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    status = ob_dpiOci__attrSet(props->handle, DPI_OCI_DTYPE_AQMSG_PROPERTIES,
            (void*) rawValue, 0, DPI_OCI_ATTR_ORIGINAL_MSGID, "set value",
            &error);
    ob_dpiOci__rawResize(props->env->handle, &rawValue, 0, &error);
    return ob_dpiGen__endPublicFn(props, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_setPayloadBytes() [PUBLIC]
//   Set the payload for the message (as a series of bytes).
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_setPayloadBytes(dpiMsgProps *props, const char *value,
        uint32_t valueLength)
{
    dpiError error;
    int status;

    if (ob_dpiGen__startPublicFn(props, DPI_HTYPE_MSG_PROPS, __func__,
            &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(props, value)
    if (props->payloadRaw) {
        ob_dpiOci__rawResize(props->env->handle, &props->payloadRaw, 0, &error);
        props->payloadRaw = NULL;
    }
    status = ob_dpiOci__rawAssignBytes(props->env->handle, value, valueLength,
            &props->payloadRaw, &error);
    return ob_dpiGen__endPublicFn(props, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_setPayloadJson() [PUBLIC]
//   Set the payload for the message (as a JSON object).
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_setPayloadJson(dpiMsgProps *props, dpiJson *json)
{
    dpiError error;
    if (ob_dpiGen__startPublicFn(props, DPI_HTYPE_MSG_PROPS, __func__,
            &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    if (ob_dpiGen__checkHandle(json, DPI_HTYPE_JSON, "check json object",
            &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    if (props->payloadJson)
        ob_dpiGen__setRefCount(props->payloadJson, &error, -1);
    ob_dpiGen__setRefCount(json, &error, 1);
    props->payloadJson = json;
    return ob_dpiGen__endPublicFn(props, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_setPayloadObject() [PUBLIC]
//   Set the payload for the message (as an object).
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_setPayloadObject(dpiMsgProps *props, dpiObject *obj)
{
    dpiError error;

    if (ob_dpiGen__startPublicFn(props, DPI_HTYPE_MSG_PROPS, __func__,
            &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    if (ob_dpiGen__checkHandle(obj, DPI_HTYPE_OBJECT, "check object", &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    if (props->payloadObj)
        ob_dpiGen__setRefCount(props->payloadObj, &error, -1);
    ob_dpiGen__setRefCount(obj, &error, 1);
    props->payloadObj = obj;
    return ob_dpiGen__endPublicFn(props, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_setPriority() [PUBLIC]
//   Set the priority of the message.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_setPriority(dpiMsgProps *props, int32_t value)
{
    return ob_dpiMsgProps__setAttrValue(props, DPI_OCI_ATTR_PRIORITY, __func__,
            &value, 0);
}


//-----------------------------------------------------------------------------
// ob_dpiMsgProps_setRecipients() [PUBLIC]
//   Set recipients associated with the message.
//-----------------------------------------------------------------------------
int ob_dpiMsgProps_setRecipients(dpiMsgProps *props,
        dpiMsgRecipient *recipients, uint32_t numRecipients)
{
    void **aqAgents;
    dpiError error;
    uint32_t i;
    int status;

    if (ob_dpiGen__startPublicFn(props, DPI_HTYPE_MSG_PROPS, __func__,
            &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(props, recipients)
    if (ob_dpiUtils__allocateMemory(numRecipients, sizeof(void*), 1,
            "allocate memory for agents", (void**) &aqAgents, &error) < 0)
        return ob_dpiGen__endPublicFn(props, DPI_FAILURE, &error);
    status = ob_dpiMsgProps__setRecipients(props, recipients, numRecipients,
            aqAgents, &error);
    for (i = 0; i < numRecipients; i++) {
        if (aqAgents[i])
            ob_dpiOci__descriptorFree(aqAgents[i], DPI_OCI_DTYPE_AQAGENT);
    }
    ob_dpiUtils__freeMemory(aqAgents);
    return ob_dpiGen__endPublicFn(props, status, &error);
}
