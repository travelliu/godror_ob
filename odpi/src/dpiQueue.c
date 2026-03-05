//-----------------------------------------------------------------------------
// Copyright (c) 2019, 2024, Oracle and/or its affiliates.
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
// dpiQueue.c
//   Implementation of AQ queues.
//-----------------------------------------------------------------------------

#include "dpiImpl.h"

// forward declarations of internal functions only used in this file
static int ob_dpiQueue__allocateBuffer(dpiQueue *queue, uint32_t numElements,
        dpiError *error);
static int ob_dpiQueue__deq(dpiQueue *queue, uint32_t *numProps,
        dpiMsgProps **props, dpiError *error);
static void ob_dpiQueue__freeBuffer(dpiQueue *queue, dpiError *error);
static int ob_dpiQueue__getPayloadTDO(dpiQueue *queue, void **tdo,
        dpiError *error);


//-----------------------------------------------------------------------------
// ob_dpiQueue__allocate() [INTERNAL]
//   Allocate and initialize a queue.
//-----------------------------------------------------------------------------
int ob_dpiQueue__allocate(dpiConn *conn, const char *name, uint32_t nameLength,
        dpiObjectType *payloadType, dpiQueue **queue, int isJson,
        dpiError *error)
{
    dpiQueue *tempQueue;
    char *buffer;

    // allocate handle; store reference to the connection that created it
    if (ob_dpiGen__allocate(DPI_HTYPE_QUEUE, conn->env, (void**) &tempQueue,
            error) < 0)
        return DPI_FAILURE;
    ob_dpiGen__setRefCount(conn, error, 1);
    tempQueue->conn = conn;
    tempQueue->isJson = isJson;

    // store payload type, which is either an object type or NULL (meaning that
    // RAW or JSON payloads are being enqueued and dequeued)
    if (payloadType) {
        ob_dpiGen__setRefCount(payloadType, error, 1);
        tempQueue->payloadType = payloadType;
    }

    // allocate space for the name of the queue; OCI requires a NULL-terminated
    // string so allocate enough space to store the NULL terminator; UTF-16
    // encoded strings are not currently supported
    if (ob_dpiUtils__allocateMemory(1, nameLength + 1, 0, "queue name",
            (void**) &buffer, error) < 0) {
        ob_dpiQueue__free(tempQueue, error);
        return DPI_FAILURE;
    }
    memcpy(buffer, name, nameLength);
    buffer[nameLength] = '\0';
    tempQueue->name = buffer;

    *queue = tempQueue;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiQueue__allocateBuffer() [INTERNAL]
//   Ensure there is enough space in the buffer for the specified number of
// elements.
//-----------------------------------------------------------------------------
static int ob_dpiQueue__allocateBuffer(dpiQueue *queue, uint32_t numElements,
        dpiError *error)
{
    ob_dpiQueue__freeBuffer(queue, error);
    queue->buffer.numElements = numElements;
    if (ob_dpiUtils__allocateMemory(numElements, sizeof(dpiMsgProps*), 1,
            "allocate msg props array", (void**) &queue->buffer.props,
            error) < 0)
        return DPI_FAILURE;
    if (ob_dpiUtils__allocateMemory(numElements, sizeof(void*), 1,
            "allocate OCI handles array", (void**) &queue->buffer.handles,
            error) < 0)
        return DPI_FAILURE;
    if (ob_dpiUtils__allocateMemory(numElements, sizeof(void*), 1,
            "allocate OCI instances array", (void**) &queue->buffer.instances,
            error) < 0)
        return DPI_FAILURE;
    if (ob_dpiUtils__allocateMemory(numElements, sizeof(void*), 1,
            "allocate OCI indicators array",
            (void**) &queue->buffer.indicators, error) < 0)
        return DPI_FAILURE;
    if (!queue->payloadType) {
        if (ob_dpiUtils__allocateMemory(numElements, sizeof(int16_t), 1,
                "allocate array of OCI scalar indicator buffers",
                (void**) &queue->buffer.scalarIndicators, error) < 0)
            return DPI_FAILURE;
    }
    if (ob_dpiUtils__allocateMemory(numElements, sizeof(void*), 1,
            "allocate message ids array", (void**) &queue->buffer.msgIds,
            error) < 0)
        return DPI_FAILURE;

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiQueue__check() [INTERNAL]
//   Determine if the queue is available to use.
//-----------------------------------------------------------------------------
static int ob_dpiQueue__check(dpiQueue *queue, const char *fnName,
        dpiError *error)
{
    if (ob_dpiGen__startPublicFn(queue, DPI_HTYPE_QUEUE, fnName, error) < 0)
        return DPI_FAILURE;
    if (!queue->conn->handle || queue->conn->closing)
        return ob_dpiError__set(error, "check connection", DPI_ERR_NOT_CONNECTED);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiQueue__createDeqOptions() [INTERNAL]
//   Create the dequeue options object that will be used for performing
// dequeues against the queue.
//-----------------------------------------------------------------------------
static int ob_dpiQueue__createDeqOptions(dpiQueue *queue, dpiError *error)
{
    dpiDeqOptions *tempOptions;

    if (ob_dpiGen__allocate(DPI_HTYPE_DEQ_OPTIONS, queue->env,
            (void**) &tempOptions, error) < 0)
        return DPI_FAILURE;
    if (ob_dpiDeqOptions__create(tempOptions, queue->conn, error) < 0) {
        ob_dpiDeqOptions__free(tempOptions, error);
        return DPI_FAILURE;
    }

    queue->deqOptions = tempOptions;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiQueue__createEnqOptions() [INTERNAL]
//   Create the dequeue options object that will be used for performing
// dequeues against the queue.
//-----------------------------------------------------------------------------
static int ob_dpiQueue__createEnqOptions(dpiQueue *queue, dpiError *error)
{
    dpiEnqOptions *tempOptions;

    if (ob_dpiGen__allocate(DPI_HTYPE_ENQ_OPTIONS, queue->env,
            (void**) &tempOptions, error) < 0)
        return DPI_FAILURE;
    if (ob_dpiEnqOptions__create(tempOptions, queue->conn, error) < 0) {
        ob_dpiEnqOptions__free(tempOptions, error);
        return DPI_FAILURE;
    }

    queue->enqOptions = tempOptions;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiQueue__deq() [INTERNAL]
//   Perform a dequeue of up to the specified number of properties.
//-----------------------------------------------------------------------------
static int ob_dpiQueue__deq(dpiQueue *queue, uint32_t *numProps,
        dpiMsgProps **props, dpiError *error)
{
    dpiMsgProps *prop;
    void *payloadTDO;
    uint32_t i;
    int status;

    // create dequeue options, if necessary
    if (!queue->deqOptions && ob_dpiQueue__createDeqOptions(queue, error) < 0)
        return DPI_FAILURE;

    // allocate buffer, if necessary
    if (queue->buffer.numElements < *numProps &&
            ob_dpiQueue__allocateBuffer(queue, *numProps, error) < 0)
        return DPI_FAILURE;

    // populate buffer
    for (i = 0; i < *numProps; i++) {
        prop = queue->buffer.props[i];

        // create new message properties, if applicable
        if (!prop) {
            if (ob_dpiMsgProps__allocate(queue->conn, &prop, error) < 0)
                return DPI_FAILURE;
            queue->buffer.props[i] = prop;
        }

        // create payload object, if applicable
        if (queue->payloadType && !prop->payloadObj &&
                ob_dpiObject__allocate(queue->payloadType, NULL, NULL, NULL,
                &prop->payloadObj, error) < 0)
            return DPI_FAILURE;

        // create JSON payload object, if applicable
        if (queue->isJson) {
            if (ob_dpiJson__allocate(queue->conn, NULL, &prop->payloadJson,
                    error) < 0)
                return DPI_FAILURE;
        }

        // set OCI arrays
        queue->buffer.handles[i] = prop->handle;
        if (queue->payloadType) {
            queue->buffer.instances[i] = prop->payloadObj->instance;
            queue->buffer.indicators[i] = prop->payloadObj->indicator;
        } else if (queue->isJson) {
            queue->buffer.instances[i] = prop->payloadJson->handle;
            queue->buffer.indicators[i] = &queue->buffer.scalarIndicators[i];
        } else {
            queue->buffer.instances[i] = prop->payloadRaw;
            queue->buffer.indicators[i] = &queue->buffer.scalarIndicators[i];
        }
        queue->buffer.msgIds[i] = prop->msgIdRaw;

    }

    // perform dequeue
    if (ob_dpiQueue__getPayloadTDO(queue, &payloadTDO, error) < 0)
        return DPI_FAILURE;
    if (*numProps == 1) {
        status = ob_dpiOci__aqDeq(queue->conn, queue->name,
                queue->deqOptions->handle, queue->buffer.handles[0],
                payloadTDO, queue->buffer.instances, queue->buffer.indicators,
                queue->buffer.msgIds, error);
        if (status < 0)
            *numProps = 0;
    } else if (queue->isJson) {
        status = DPI_SUCCESS;
        for (i = 0; i < *numProps; i++) {
            status = ob_dpiOci__aqDeq(queue->conn, queue->name,
                    queue->deqOptions->handle, queue->buffer.handles[i],
                    payloadTDO, &queue->buffer.instances[i],
                    &queue->buffer.indicators[i],
                    &queue->buffer.msgIds[i], error);
            if (status < 0) {
                *numProps = i;
                break;
            }
        }
    } else {
        status = ob_dpiOci__aqDeqArray(queue->conn, queue->name,
                queue->deqOptions->handle, numProps, queue->buffer.handles,
                payloadTDO, queue->buffer.instances, queue->buffer.indicators,
                queue->buffer.msgIds, error);
    }
    if (status < 0 && error->buffer->code != 25228) {
        error->buffer->offset = *numProps;
        return DPI_FAILURE;
    }

    // transfer message properties to destination array
    for (i = 0; i < *numProps; i++) {
        props[i] = queue->buffer.props[i];
        queue->buffer.props[i] = NULL;
        if (queue->isJson) {
            props[i]->payloadJson->handle = queue->buffer.instances[i];
        } else if (!queue->payloadType) {
            props[i]->payloadRaw = queue->buffer.instances[i];
        }
        props[i]->msgIdRaw = queue->buffer.msgIds[i];
    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiQueue__enq() [INTERNAL]
//   Perform an enqueue of the specified properties.
//-----------------------------------------------------------------------------
static int ob_dpiQueue__enq(dpiQueue *queue, uint32_t numProps,
        dpiMsgProps **props, dpiError *error)
{
    void *payloadTDO;
    uint32_t i;

    // if no messages are being enqueued, nothing to do!
    if (numProps == 0)
        return DPI_SUCCESS;

    // create enqueue options, if necessary
    if (!queue->enqOptions && ob_dpiQueue__createEnqOptions(queue, error) < 0)
        return DPI_FAILURE;

    // allocate buffer, if necessary
    if (queue->buffer.numElements < numProps &&
            ob_dpiQueue__allocateBuffer(queue, numProps, error) < 0)
        return DPI_FAILURE;

    // populate buffer
    for (i = 0; i < numProps; i++) {

        // perform checks
        if (!props[i]->payloadObj && !props[i]->payloadRaw &&
                !props[i]->payloadJson)
            return ob_dpiError__set(error, "check payload",
                    DPI_ERR_QUEUE_NO_PAYLOAD);
        if ((queue->isJson && !props[i]->payloadJson) ||
                (queue->payloadType && !props[i]->payloadObj) ||
                (!queue->isJson && !queue->payloadType &&
                        !props[i]->payloadRaw))
            return ob_dpiError__set(error, "check payload",
                    DPI_ERR_QUEUE_WRONG_PAYLOAD_TYPE);
        if (queue->payloadType && props[i]->payloadObj &&
                queue->payloadType->tdo != props[i]->payloadObj->type->tdo)
            return ob_dpiError__set(error, "check payload",
                    DPI_ERR_WRONG_TYPE,
                    props[i]->payloadObj->type->schemaLength,
                    props[i]->payloadObj->type->schema,
                    props[i]->payloadObj->type->nameLength,
                    props[i]->payloadObj->type->name,
                    queue->payloadType->schemaLength,
                    queue->payloadType->schema,
                    queue->payloadType->nameLength,
                    queue->payloadType->name);

        // set OCI arrays
        queue->buffer.handles[i] = props[i]->handle;
        if (queue->payloadType) {
            queue->buffer.instances[i] = props[i]->payloadObj->instance;
            queue->buffer.indicators[i] = props[i]->payloadObj->indicator;
        } else if (props[i]->payloadJson) {
            queue->buffer.instances[i] = props[i]->payloadJson->handle;
            queue->buffer.indicators[i] = &queue->buffer.scalarIndicators[i];
        } else {
            queue->buffer.instances[i] = props[i]->payloadRaw;
            queue->buffer.indicators[i] = &queue->buffer.scalarIndicators[i];
        }
        queue->buffer.msgIds[i] = props[i]->msgIdRaw;

    }

    // perform enqueue
    if (ob_dpiQueue__getPayloadTDO(queue, &payloadTDO, error) < 0)
        return DPI_FAILURE;
    if (numProps == 1) {
        if (ob_dpiOci__aqEnq(queue->conn, queue->name, queue->enqOptions->handle,
                queue->buffer.handles[0], payloadTDO, queue->buffer.instances,
                queue->buffer.indicators, queue->buffer.msgIds, error) < 0)
            return DPI_FAILURE;
    } else {
        if (ob_dpiOci__aqEnqArray(queue->conn, queue->name,
                queue->enqOptions->handle, &numProps, queue->buffer.handles,
                payloadTDO, queue->buffer.instances, queue->buffer.indicators,
                queue->buffer.msgIds, error) < 0) {
            error->buffer->offset = numProps;
            return DPI_FAILURE;
        }
    }

    // transfer message ids back to message properties
    for (i = 0; i < numProps; i++)
        props[i]->msgIdRaw = queue->buffer.msgIds[i];

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiQueue__free() [INTERNAL]
//   Free the memory for a queue.
//-----------------------------------------------------------------------------
void ob_dpiQueue__free(dpiQueue *queue, dpiError *error)
{
    if (queue->conn) {
        ob_dpiGen__setRefCount(queue->conn, error, -1);
        queue->conn = NULL;
    }
    if (queue->payloadType) {
        ob_dpiGen__setRefCount(queue->payloadType, error, -1);
        queue->payloadType = NULL;
    }
    if (queue->name) {
        ob_dpiUtils__freeMemory((void*) queue->name);
        queue->name = NULL;
    }
    if (queue->deqOptions) {
        ob_dpiGen__setRefCount(queue->deqOptions, error, -1);
        queue->deqOptions = NULL;
    }
    if (queue->enqOptions) {
        ob_dpiGen__setRefCount(queue->enqOptions, error, -1);
        queue->enqOptions = NULL;
    }
    ob_dpiQueue__freeBuffer(queue, error);
    ob_dpiUtils__freeMemory(queue);
}


//-----------------------------------------------------------------------------
// ob_dpiQueue__freeBuffer() [INTERNAL]
//   Free the memory areas in the queue buffer.
//-----------------------------------------------------------------------------
static void ob_dpiQueue__freeBuffer(dpiQueue *queue, dpiError *error)
{
    dpiQueueBuffer *buffer = &queue->buffer;
    uint32_t i;

    if (buffer->props) {
        for (i = 0; i < buffer->numElements; i++) {
            if (buffer->props[i]) {
                ob_dpiGen__setRefCount(buffer->props[i], error, -1);
                buffer->props[i] = NULL;
            }
        }
        ob_dpiUtils__freeMemory(buffer->props);
        buffer->props = NULL;
    }
    if (buffer->handles) {
        ob_dpiUtils__freeMemory(buffer->handles);
        buffer->handles = NULL;
    }
    if (buffer->instances) {
        ob_dpiUtils__freeMemory(buffer->instances);
        buffer->instances = NULL;
    }
    if (buffer->indicators) {
        ob_dpiUtils__freeMemory(buffer->indicators);
        buffer->indicators = NULL;
    }
    if (buffer->scalarIndicators) {
        ob_dpiUtils__freeMemory(buffer->scalarIndicators);
        buffer->indicators = NULL;
    }
    if (buffer->msgIds) {
        ob_dpiUtils__freeMemory(buffer->msgIds);
        buffer->msgIds = NULL;
    }
}


//-----------------------------------------------------------------------------
// ob_dpiQueue__getPayloadTDO() [INTERNAL]
//   Acquire the TDO to use for the payload. This will either be the TDO of the
// object type (if one was specified when the queue was created), the RAW TDO
// cached on the connection (for RAW queues) or the JSON TDO cached on the
// connection (for JSON queues).
//-----------------------------------------------------------------------------
static int ob_dpiQueue__getPayloadTDO(dpiQueue *queue, void **tdo,
        dpiError *error)
{
    if (queue->payloadType) {
        *tdo = queue->payloadType->tdo;
    } else if (queue->isJson) {
        if (ob_dpiConn__getJsonTDO(queue->conn, error) < 0)
            return DPI_FAILURE;
        *tdo = queue->conn->jsonTDO;
    } else {
        if (ob_dpiConn__getRawTDO(queue->conn, error) < 0)
            return DPI_FAILURE;
        *tdo = queue->conn->rawTDO;
    }
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiQueue_addRef() [PUBLIC]
//   Add a reference to the queue.
//-----------------------------------------------------------------------------
int ob_dpiQueue_addRef(dpiQueue *queue)
{
    return ob_dpiGen__addRef(queue, DPI_HTYPE_QUEUE, __func__);
}


//-----------------------------------------------------------------------------
// ob_dpiQueue_deqMany() [PUBLIC]
//   Dequeue multiple messages from the queue.
//-----------------------------------------------------------------------------
int ob_dpiQueue_deqMany(dpiQueue *queue, uint32_t *numProps, dpiMsgProps **props)
{
    dpiError error;
    int status;

    if (ob_dpiQueue__check(queue, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(queue, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(queue, numProps)
    DPI_CHECK_PTR_NOT_NULL(queue, props)
    status = ob_dpiQueue__deq(queue, numProps, props, &error);
    return ob_dpiGen__endPublicFn(queue, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiQueue_deqOne() [PUBLIC]
//   Dequeue a single message from the queue.
//-----------------------------------------------------------------------------
int ob_dpiQueue_deqOne(dpiQueue *queue, dpiMsgProps **props)
{
    uint32_t numProps = 1;
    dpiError error;

    if (ob_dpiQueue__check(queue, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(queue, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(queue, props)
    if (ob_dpiQueue__deq(queue, &numProps, props, &error) < 0)
        return ob_dpiGen__endPublicFn(queue, DPI_FAILURE, &error);
    if (numProps == 0)
        *props = NULL;
    return ob_dpiGen__endPublicFn(queue, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiQueue_enqMany() [PUBLIC]
//   Enqueue multiple message to the queue.
//-----------------------------------------------------------------------------
int ob_dpiQueue_enqMany(dpiQueue *queue, uint32_t numProps, dpiMsgProps **props)
{
    dpiError error;
    uint32_t i;
    int status;

    // validate parameters
    if (ob_dpiQueue__check(queue, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(queue, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(queue, props)
    for (i = 0; i < numProps; i++) {
        if (ob_dpiGen__checkHandle(props[i], DPI_HTYPE_MSG_PROPS,
                "check message properties", &error) < 0)
            return ob_dpiGen__endPublicFn(queue, DPI_FAILURE, &error);
    }
    status = ob_dpiQueue__enq(queue, numProps, props, &error);
    return ob_dpiGen__endPublicFn(queue, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiQueue_enqOne() [PUBLIC]
//   Enqueue a single message to the queue.
//-----------------------------------------------------------------------------
int ob_dpiQueue_enqOne(dpiQueue *queue, dpiMsgProps *props)
{
    dpiError error;
    int status;

    if (ob_dpiQueue__check(queue, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(queue, DPI_FAILURE, &error);
    if (ob_dpiGen__checkHandle(props, DPI_HTYPE_MSG_PROPS,
            "check message properties", &error) < 0)
        return ob_dpiGen__endPublicFn(queue, DPI_FAILURE, &error);
    status = ob_dpiQueue__enq(queue, 1, &props, &error);
    return ob_dpiGen__endPublicFn(queue, status, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiQueue_getDeqOptions() [PUBLIC]
//   Return the dequeue options associated with the queue. If no dequeue
// options are currently associated with the queue, create them first.
//-----------------------------------------------------------------------------
int ob_dpiQueue_getDeqOptions(dpiQueue *queue, dpiDeqOptions **options)
{
    dpiError error;

    if (ob_dpiGen__startPublicFn(queue, DPI_HTYPE_QUEUE, __func__, &error) < 0)
        return DPI_FAILURE;
    DPI_CHECK_PTR_NOT_NULL(queue, options)
    if (!queue->deqOptions && ob_dpiQueue__createDeqOptions(queue, &error) < 0)
        return ob_dpiGen__endPublicFn(queue, DPI_FAILURE, &error);
    *options = queue->deqOptions;
    return ob_dpiGen__endPublicFn(queue, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiQueue_getEnqOptions() [PUBLIC]
//   Return the enqueue options associated with the queue. If no enqueue
// options are currently associated with the queue, create them first.
//-----------------------------------------------------------------------------
int ob_dpiQueue_getEnqOptions(dpiQueue *queue, dpiEnqOptions **options)
{
    dpiError error;

    if (ob_dpiGen__startPublicFn(queue, DPI_HTYPE_QUEUE, __func__, &error) < 0)
        return DPI_FAILURE;
    DPI_CHECK_PTR_NOT_NULL(queue, options)
    if (!queue->enqOptions && ob_dpiQueue__createEnqOptions(queue, &error) < 0)
        return ob_dpiGen__endPublicFn(queue, DPI_FAILURE, &error);
    *options = queue->enqOptions;
    return ob_dpiGen__endPublicFn(queue, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiQueue_release() [PUBLIC]
//   Release a reference to the queue.
//-----------------------------------------------------------------------------
int ob_dpiQueue_release(dpiQueue *queue)
{
    return ob_dpiGen__release(queue, DPI_HTYPE_QUEUE, __func__);
}
