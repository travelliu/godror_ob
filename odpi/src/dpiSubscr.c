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
// dpiSubscr.c
//   Implementation of subscriptions (CQN).
//-----------------------------------------------------------------------------

#include "dpiImpl.h"

// forward declarations of internal functions only used in this file
static void ob_dpiSubscr__freeMessage(dpiSubscrMessage *message);
static int ob_dpiSubscr__populateMessage(dpiSubscr *subscr,
        dpiSubscrMessage *message, void *descriptor, dpiError *error);
static int ob_dpiSubscr__populateMessageTable(dpiSubscr *subscr,
        dpiSubscrMessageTable *table, void *descriptor, dpiError *error);
static int ob_dpiSubscr__populateQueryChangeMessage(dpiSubscr *subscr,
        dpiSubscrMessage *message, void *descriptor, dpiError *error);


//-----------------------------------------------------------------------------
// ob_dpiSubscr__callback() [INTERNAL]
//   Callback that is used to execute the callback registered when the
// subscription was created.
//-----------------------------------------------------------------------------
static void ob_dpiSubscr__callback(dpiSubscr *subscr, UNUSED void *handle,
        UNUSED void *payload, UNUSED uint32_t payloadLength, void *descriptor,
        UNUSED uint32_t mode)
{
    dpiSubscrMessage message;
    dpiErrorInfo errorInfo;
    dpiError error;

    // ensure that the subscription handle is still valid
    if (ob_dpiGen__startPublicFn(subscr, DPI_HTYPE_SUBSCR, __func__,
            &error) < 0) {
        ob_dpiGen__endPublicFn(subscr, DPI_FAILURE, &error);
        return;
    }

    // if the subscription is no longer registered, nothing further to do
    ob_dpiMutex__acquire(subscr->mutex);
    if (!subscr->registered) {
        ob_dpiMutex__release(subscr->mutex);
        ob_dpiGen__endPublicFn(subscr, DPI_SUCCESS, &error);
        return;
    }

    // populate message
    memset(&message, 0, sizeof(message));
    if (ob_dpiSubscr__populateMessage(subscr, &message, descriptor, &error) < 0) {
        ob_dpiError__getInfo(&error, &errorInfo);
        message.errorInfo = &errorInfo;
    }
    message.registered = subscr->registered;

    // invoke user callback; temporarily increase reference count to ensure
    // that the subscription is not freed during the callback
    ob_dpiGen__setRefCount(subscr, &error, 1);
    (*subscr->callback)(subscr->callbackContext, &message);
    ob_dpiSubscr__freeMessage(&message);
    ob_dpiMutex__release(subscr->mutex);
    ob_dpiGen__setRefCount(subscr, &error, -1);
    ob_dpiGen__endPublicFn(subscr, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr__check() [INTERNAL]
//   Determine if the subscription is open and available for use.
//-----------------------------------------------------------------------------
static int ob_dpiSubscr__check(dpiSubscr *subscr, const char *fnName,
        dpiError *error)
{
    if (ob_dpiGen__startPublicFn(subscr, DPI_HTYPE_SUBSCR, fnName, error) < 0)
        return DPI_FAILURE;
    if (!subscr->handle)
        return ob_dpiError__set(error, "check closed", DPI_ERR_SUBSCR_CLOSED);
    return ob_dpiConn__checkConnected(subscr->conn, error);
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr__create() [INTERNAL]
//   Create a new subscription structure and return it. In case of error NULL
// is returned.
//-----------------------------------------------------------------------------
int ob_dpiSubscr__create(dpiSubscr *subscr, dpiConn *conn,
        dpiSubscrCreateParams *params, dpiError *error)
{
    uint32_t qosFlags, mode;
    int32_t int32Val;
    int rowids;

    // retain a reference to the connection
    ob_dpiGen__setRefCount(conn, error, 1);
    subscr->conn = conn;
    subscr->callback = params->callback;
    subscr->callbackContext = params->callbackContext;
    subscr->subscrNamespace = params->subscrNamespace;
    subscr->qos = params->qos;
    subscr->clientInitiated = params->clientInitiated;
    ob_dpiMutex__initialize(subscr->mutex);

    // create the subscription handle
    if (ob_dpiOci__handleAlloc(conn->env->handle, &subscr->handle,
            DPI_OCI_HTYPE_SUBSCRIPTION, "create subscr handle", error) < 0)
        return DPI_FAILURE;

    // set the namespace
    if (ob_dpiOci__attrSet(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION,
            (void*) &params->subscrNamespace, sizeof(uint32_t),
            DPI_OCI_ATTR_SUBSCR_NAMESPACE, "set namespace", error) < 0)
        return DPI_FAILURE;

    // set the protocol
    if (ob_dpiOci__attrSet(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION,
            (void*) &params->protocol, sizeof(uint32_t),
            DPI_OCI_ATTR_SUBSCR_RECPTPROTO, "set protocol", error) < 0)
        return DPI_FAILURE;

    // set the timeout
    if (ob_dpiOci__attrSet(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION,
            (void*) &params->timeout, sizeof(uint32_t),
            DPI_OCI_ATTR_SUBSCR_TIMEOUT, "set timeout", error) < 0)
        return DPI_FAILURE;

    // set the IP address used on the client to listen for events
    if (params->ipAddress && params->ipAddressLength > 0 &&
            ob_dpiOci__attrSet(subscr->env->handle, DPI_OCI_HTYPE_ENV,
                    (void*) params->ipAddress, params->ipAddressLength,
                    DPI_OCI_ATTR_SUBSCR_IPADDR, "set IP address", error) < 0)
        return DPI_FAILURE;

    // set the port number used on the client to listen for events
    if (params->portNumber > 0 && ob_dpiOci__attrSet(subscr->env->handle,
            DPI_OCI_HTYPE_ENV, (void*) &params->portNumber, 0,
            DPI_OCI_ATTR_SUBSCR_PORTNO, "set port number", error) < 0)
        return DPI_FAILURE;

    // set the context for the callback
    if (ob_dpiOci__attrSet(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION,
            (void*) subscr, 0, DPI_OCI_ATTR_SUBSCR_CTX, "set callback context",
            error) < 0)
        return DPI_FAILURE;

    // set the callback, if applicable
    if (params->callback && ob_dpiOci__attrSet(subscr->handle,
            DPI_OCI_HTYPE_SUBSCRIPTION, (void*) ob_dpiSubscr__callback, 0,
            DPI_OCI_ATTR_SUBSCR_CALLBACK, "set callback", error) < 0)
        return DPI_FAILURE;

    // set the subscription name, if applicable
    if (params->name && params->nameLength > 0 &&
            ob_dpiOci__attrSet(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION,
                    (void*) params->name, params->nameLength,
                    DPI_OCI_ATTR_SUBSCR_NAME, "set name", error) < 0)
        return DPI_FAILURE;

    // set QOS flags
    qosFlags = 0;
    if (params->qos & DPI_SUBSCR_QOS_RELIABLE)
        qosFlags |= DPI_OCI_SUBSCR_QOS_RELIABLE;
    if (params->qos & DPI_SUBSCR_QOS_DEREG_NFY)
        qosFlags |= DPI_OCI_SUBSCR_QOS_PURGE_ON_NTFN;
    if (qosFlags && ob_dpiOci__attrSet(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION,
            (void*) &qosFlags, sizeof(uint32_t), DPI_OCI_ATTR_SUBSCR_QOSFLAGS,
            "set QOS", error) < 0)
        return DPI_FAILURE;

    // set CQ specific QOS flags
    qosFlags = 0;
    if (params->qos & DPI_SUBSCR_QOS_QUERY)
        qosFlags |= DPI_OCI_SUBSCR_CQ_QOS_QUERY;
    if (params->qos & DPI_SUBSCR_QOS_BEST_EFFORT)
        qosFlags |= DPI_OCI_SUBSCR_CQ_QOS_BEST_EFFORT;
    if (qosFlags && ob_dpiOci__attrSet(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION,
            (void*) &qosFlags, sizeof(uint32_t),
            DPI_OCI_ATTR_SUBSCR_CQ_QOSFLAGS, "set CQ QOS", error) < 0)
        return DPI_FAILURE;

    // set rowids flag, if applicable
    if (params->qos & DPI_SUBSCR_QOS_ROWIDS) {
        rowids = 1;
        if (ob_dpiOci__attrSet(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION,
                (void*) &rowids, 0, DPI_OCI_ATTR_CHNF_ROWIDS,
                "set rowids flag", error) < 0)
            return DPI_FAILURE;
    }

    // set which operations are desired, if applicable
    if (params->operations && ob_dpiOci__attrSet(subscr->handle,
            DPI_OCI_HTYPE_SUBSCRIPTION, (void*) &params->operations, 0,
            DPI_OCI_ATTR_CHNF_OPERATIONS, "set operations", error) < 0)
        return DPI_FAILURE;

    // set grouping information, if applicable
    if (params->groupingClass) {

        // set grouping class
        if (ob_dpiOci__attrSet(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION,
                (void*) &params->groupingClass, 0,
                DPI_OCI_ATTR_SUBSCR_NTFN_GROUPING_CLASS, "set grouping class",
                error) < 0)
            return DPI_FAILURE;

        // set grouping value
        if (ob_dpiOci__attrSet(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION,
                (void*) &params->groupingValue, 0,
                DPI_OCI_ATTR_SUBSCR_NTFN_GROUPING_VALUE, "set grouping value",
                error) < 0)
            return DPI_FAILURE;

        // set grouping type
        if (ob_dpiOci__attrSet(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION,
                (void*) &params->groupingType, 0,
                DPI_OCI_ATTR_SUBSCR_NTFN_GROUPING_TYPE, "set grouping type",
                error) < 0)
            return DPI_FAILURE;

        // set grouping repeat count
        int32Val = DPI_SUBSCR_GROUPING_FOREVER;
        if (ob_dpiOci__attrSet(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION,
                (void*) &int32Val, 0,
                DPI_OCI_ATTR_SUBSCR_NTFN_GROUPING_REPEAT_COUNT,
                "set grouping repeat count", error) < 0)
            return DPI_FAILURE;

    }

    // register the subscription; client initiated subscriptions are only valid
    // with 19.4 client and database
    mode = DPI_OCI_DEFAULT;
    if (params->clientInitiated) {
        if (ob_dpiUtils__checkClientVersion(conn->env->versionInfo, 19, 4,
                error) < 0)
            return DPI_FAILURE;
        if (ob_dpiUtils__checkDatabaseVersion(conn, 19, 4, error) < 0)
            return DPI_FAILURE;
        mode = DPI_OCI_SECURE_NOTIFICATION;
    }
    if (ob_dpiOci__subscriptionRegister(conn, &subscr->handle, mode, error) < 0)
        return DPI_FAILURE;
    subscr->registered = 1;

    // acquire the registration id, if applicable; for AQ, the value is always
    // set to zero
    if (params->subscrNamespace == DPI_SUBSCR_NAMESPACE_AQ) {
        params->outRegId = 0;
    } else {
        if (ob_dpiOci__attrGet(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION,
                &params->outRegId, NULL, DPI_OCI_ATTR_SUBSCR_CQ_REGID,
                "get registration id", error) < 0)
            return DPI_FAILURE;
    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr__free() [INTERNAL]
//   Free the memory and any resources associated with the subscription.
//-----------------------------------------------------------------------------
void ob_dpiSubscr__free(dpiSubscr *subscr, dpiError *error)
{
    ob_dpiMutex__acquire(subscr->mutex);
    if (subscr->handle) {
        if (subscr->registered)
            ob_dpiOci__subscriptionUnRegister(subscr->conn, subscr, error);
        ob_dpiOci__handleFree(subscr->handle, DPI_OCI_HTYPE_SUBSCRIPTION);
        subscr->handle = NULL;
    }
    if (subscr->conn) {
        ob_dpiGen__setRefCount(subscr->conn, error, -1);
        subscr->conn = NULL;
    }
    ob_dpiMutex__release(subscr->mutex);
    ob_dpiMutex__destroy(subscr->mutex);
    ob_dpiUtils__freeMemory(subscr);
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr__freeMessage() [INTERNAL]
//   Free memory associated with the message.
//-----------------------------------------------------------------------------
static void ob_dpiSubscr__freeMessage(dpiSubscrMessage *message)
{
    dpiSubscrMessageQuery *query;
    uint32_t i, j;

    // free the tables for the message
    if (message->numTables > 0) {
        for (i = 0; i < message->numTables; i++) {
            if (message->tables[i].numRows > 0)
                ob_dpiUtils__freeMemory(message->tables[i].rows);
        }
        ob_dpiUtils__freeMemory(message->tables);
    }

    // free the queries for the message
    if (message->numQueries > 0) {
        for (i = 0; i < message->numQueries; i++) {
            query = &message->queries[i];
            if (query->numTables > 0) {
                for (j = 0; j < query->numTables; j++) {
                    if (query->tables[j].numRows > 0)
                        ob_dpiUtils__freeMemory(query->tables[j].rows);
                }
                ob_dpiUtils__freeMemory(query->tables);
            }
        }
        ob_dpiUtils__freeMemory(message->queries);
    }
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr__populateAQMessage() [INTERNAL]
//   Populate message with details.
//-----------------------------------------------------------------------------
static int ob_dpiSubscr__populateAQMessage(dpiSubscr *subscr,
        dpiSubscrMessage *message, void *descriptor, dpiError *error)
{
    uint32_t flags = 0;
    void *rawValue;

    // determine if message is a deregistration message
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_AQNFY_DESCRIPTOR, &flags,
            NULL, DPI_OCI_ATTR_NFY_FLAGS, "get flags", error) < 0)
        return DPI_FAILURE;
    message->eventType = (flags == 1) ? DPI_EVENT_DEREG : DPI_EVENT_AQ;
    if (message->eventType == DPI_EVENT_DEREG) {
        subscr->registered = 0;
        return DPI_SUCCESS;
    }

    // determine the name of the queue which spawned the event
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_AQNFY_DESCRIPTOR,
            (void*) &message->queueName, &message->queueNameLength,
            DPI_OCI_ATTR_QUEUE_NAME, "get queue name", error) < 0)
        return DPI_FAILURE;

    // determine the consumer name for the queue that spawned the event
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_AQNFY_DESCRIPTOR,
            (void*) &message->consumerName, &message->consumerNameLength,
            DPI_OCI_ATTR_CONSUMER_NAME, "get consumer name", error) < 0)
        return DPI_FAILURE;

    // determine the msgid of the message of queue that spawned the event
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_AQNFY_DESCRIPTOR, &rawValue,
            NULL, DPI_OCI_ATTR_NFY_MSGID, "get message id", error) < 0)
        return DPI_FAILURE;
    ob_dpiOci__rawPtr(subscr->env->handle, rawValue, (void**) &message->aqMsgId);
    ob_dpiOci__rawSize(subscr->env->handle, rawValue, &message->aqMsgIdLength);

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr__populateObjectChangeMessage() [INTERNAL]
//   Populate object change message with details.
//-----------------------------------------------------------------------------
static int ob_dpiSubscr__populateObjectChangeMessage(dpiSubscr *subscr,
        dpiSubscrMessage *message, void *descriptor, dpiError *error)
{
    void **tableDescriptor, *indicator;
    int32_t numTables;
    void *tables;
    uint32_t i;
    int exists;

    // determine table collection
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_CHDES, &tables, 0,
            DPI_OCI_ATTR_CHDES_TABLE_CHANGES, "get tables", error) < 0)
        return DPI_FAILURE;
    if (!tables)
        return DPI_SUCCESS;

    // determine number of tables
    if (ob_dpiOci__collSize(subscr->conn, tables, &numTables, error) < 0)
        return DPI_FAILURE;

    // allocate memory for table entries
    if (ob_dpiUtils__allocateMemory((size_t) numTables,
            sizeof(dpiSubscrMessageTable), 1, "allocate msg tables",
            (void**) &message->tables, error) < 0)
        return DPI_FAILURE;
    message->numTables = (uint32_t) numTables;

    // populate message table entries
    for (i = 0; i < message->numTables; i++) {
        if (ob_dpiOci__collGetElem(subscr->conn, tables, (int32_t) i, &exists,
                (void**) &tableDescriptor, &indicator, error) < 0)
            return DPI_FAILURE;
        if (ob_dpiSubscr__populateMessageTable(subscr, &message->tables[i],
                *tableDescriptor, error) < 0)
            return DPI_FAILURE;
    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr__populateMessage() [INTERNAL]
//   Populate message with details.
//-----------------------------------------------------------------------------
static int ob_dpiSubscr__populateMessage(dpiSubscr *subscr,
        dpiSubscrMessage *message, void *descriptor, dpiError *error)
{
    void *rawValue;

    // if quality of service flag indicates that deregistration should take
    // place when the first notification is received, mark the subscription
    // as no longer registered
    if (subscr->qos & DPI_SUBSCR_QOS_DEREG_NFY)
        subscr->registered = 0;

    // handle AQ messages, if applicable
    if (subscr->subscrNamespace == DPI_SUBSCR_NAMESPACE_AQ)
        return ob_dpiSubscr__populateAQMessage(subscr, message, descriptor,
                error);

    // determine the type of event that was spawned
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_CHDES, &message->eventType,
            NULL, DPI_OCI_ATTR_CHDES_NFYTYPE, "get event type", error) < 0)
        return DPI_FAILURE;

    // determine the name of the database which spawned the event
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_CHDES,
            (void*) &message->dbName, &message->dbNameLength,
            DPI_OCI_ATTR_CHDES_DBNAME, "get DB name", error) < 0)
        return DPI_FAILURE;

    // determine the id of the transaction which spawned the event
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_CHDES, &rawValue, NULL,
            DPI_OCI_ATTR_CHDES_XID, "get transaction id", error) < 0)
        return DPI_FAILURE;
    ob_dpiOci__rawPtr(subscr->env->handle, rawValue, (void**) &message->txId);
    ob_dpiOci__rawSize(subscr->env->handle, rawValue, &message->txIdLength);

    // populate event specific attributes
    switch (message->eventType) {
        case DPI_EVENT_OBJCHANGE:
            return ob_dpiSubscr__populateObjectChangeMessage(subscr, message,
                    descriptor, error);
        case DPI_EVENT_QUERYCHANGE:
            return ob_dpiSubscr__populateQueryChangeMessage(subscr, message,
                    descriptor, error);
        case DPI_EVENT_DEREG:
            subscr->registered = 0;
            break;
        case DPI_EVENT_STARTUP:
        case DPI_EVENT_SHUTDOWN:
        case DPI_EVENT_SHUTDOWN_ANY:
            break;
        default:
            return ob_dpiError__set(error, "event type", DPI_ERR_NOT_SUPPORTED);
    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr__populateMessageQuery() [INTERNAL]
//   Populate a message query structure from the OCI descriptor.
//-----------------------------------------------------------------------------
static int ob_dpiSubscr__populateMessageQuery(dpiSubscr *subscr,
        dpiSubscrMessageQuery *query, void *descriptor, dpiError *error)
{
    void **tableDescriptor, *indicator, *tables;
    int32_t numTables;
    uint32_t i;
    int exists;

    // determine query id
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_CQDES, &query->id, 0,
            DPI_OCI_ATTR_CQDES_QUERYID, "get id", error) < 0)
        return DPI_FAILURE;

    // determine operation
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_CQDES, &query->operation, 0,
            DPI_OCI_ATTR_CQDES_OPERATION, "get operation", error) < 0)
        return DPI_FAILURE;

    // determine table collection
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_CQDES, &tables, 0,
            DPI_OCI_ATTR_CQDES_TABLE_CHANGES, "get table descriptor",
            error) < 0)
        return DPI_FAILURE;
    if (!tables)
        return DPI_SUCCESS;

    // determine number of tables
    if (ob_dpiOci__collSize(subscr->conn, tables, &numTables, error) < 0)
        return DPI_FAILURE;

    // allocate memory for table entries
    if (ob_dpiUtils__allocateMemory((size_t) numTables,
            sizeof(dpiSubscrMessageTable), 1, "allocate query tables",
            (void**) &query->tables, error) < 0)
        return DPI_FAILURE;
    query->numTables = (uint32_t) numTables;

    // populate message table entries
    for (i = 0; i < query->numTables; i++) {
        if (ob_dpiOci__collGetElem(subscr->conn, tables, (int32_t) i, &exists,
                (void**) &tableDescriptor, &indicator, error) < 0)
            return DPI_FAILURE;
        if (ob_dpiSubscr__populateMessageTable(subscr, &query->tables[i],
                *tableDescriptor, error) < 0)
            return DPI_FAILURE;
    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr__populateMessageRow() [INTERNAL]
//   Populate a message row structure from the OCI descriptor.
//-----------------------------------------------------------------------------
static int ob_dpiSubscr__populateMessageRow(dpiSubscrMessageRow *row,
        void *descriptor, dpiError *error)
{
    // determine operation
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_ROW_CHDES, &row->operation,
            0, DPI_OCI_ATTR_CHDES_ROW_OPFLAGS, "get operation", error) < 0)
        return DPI_FAILURE;

    // determine rowid
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_ROW_CHDES,
            (void*) &row->rowid, &row->rowidLength,
            DPI_OCI_ATTR_CHDES_ROW_ROWID, "get rowid", error) < 0)
        return DPI_FAILURE;

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr__populateMessageTable() [INTERNAL]
//   Populate a message table structure from the OCI descriptor.
//-----------------------------------------------------------------------------
static int ob_dpiSubscr__populateMessageTable(dpiSubscr *subscr,
        dpiSubscrMessageTable *table, void *descriptor, dpiError *error)
{
    void **rowDescriptor, *indicator, *rows;
    int32_t numRows;
    int exists;
    uint32_t i;

    // determine operation
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_TABLE_CHDES,
            &table->operation, 0, DPI_OCI_ATTR_CHDES_TABLE_OPFLAGS,
            "get operation", error) < 0)
        return DPI_FAILURE;

    // determine table name
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_TABLE_CHDES,
            (void*) &table->name, &table->nameLength,
            DPI_OCI_ATTR_CHDES_TABLE_NAME, "get table name", error) < 0)
        return DPI_FAILURE;

    // if change invalidated all rows, nothing to do
    if (table->operation & DPI_OPCODE_ALL_ROWS)
        return DPI_SUCCESS;

    // determine rows collection
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_TABLE_CHDES, &rows, 0,
            DPI_OCI_ATTR_CHDES_TABLE_ROW_CHANGES, "get rows descriptor",
            error) < 0)
        return DPI_FAILURE;

    // determine number of rows in collection
    if (ob_dpiOci__collSize(subscr->conn, rows, &numRows, error) < 0)
        return DPI_FAILURE;

    // allocate memory for row entries
    if (ob_dpiUtils__allocateMemory((size_t) numRows, sizeof(dpiSubscrMessageRow),
            1, "allocate rows", (void**) &table->rows, error) < 0)
        return DPI_FAILURE;
    table->numRows = (uint32_t) numRows;

    // populate the rows attribute
    for (i = 0; i < table->numRows; i++) {
        if (ob_dpiOci__collGetElem(subscr->conn, rows, (int32_t) i, &exists,
                (void**) &rowDescriptor, &indicator, error) < 0)
            return DPI_FAILURE;
        if (ob_dpiSubscr__populateMessageRow(&table->rows[i], *rowDescriptor,
                error) < 0)
            return DPI_FAILURE;
    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr__populateQueryChangeMessage() [INTERNAL]
//   Populate query change message with details.
//-----------------------------------------------------------------------------
static int ob_dpiSubscr__populateQueryChangeMessage(dpiSubscr *subscr,
        dpiSubscrMessage *message, void *descriptor, dpiError *error)
{
    void **queryDescriptor, *indicator, *queries;
    int32_t numQueries;
    int exists;
    uint32_t i;

    // determine query collection
    if (ob_dpiOci__attrGet(descriptor, DPI_OCI_DTYPE_CHDES, &queries, 0,
            DPI_OCI_ATTR_CHDES_QUERIES, "get queries", error) < 0)
        return DPI_FAILURE;
    if (!queries)
        return DPI_SUCCESS;

    // determine number of queries
    if (ob_dpiOci__collSize(subscr->conn, queries, &numQueries, error) < 0)
        return DPI_FAILURE;

    // allocate memory for query entries
    if (ob_dpiUtils__allocateMemory((size_t) numQueries,
            sizeof(dpiSubscrMessageQuery), 1, "allocate queries",
            (void**) &message->queries, error) < 0)
        return DPI_FAILURE;
    message->numQueries = (uint32_t) numQueries;

    // populate each entry with a message query instance
    for (i = 0; i < message->numQueries; i++) {
        if (ob_dpiOci__collGetElem(subscr->conn, queries, (int32_t) i, &exists,
                (void**) &queryDescriptor, &indicator, error) < 0)
            return DPI_FAILURE;
        if (ob_dpiSubscr__populateMessageQuery(subscr, &message->queries[i],
                *queryDescriptor, error) < 0)
            return DPI_FAILURE;
    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr__prepareStmt() [INTERNAL]
//   Internal method for preparing statement against a subscription. This
// allows for normal error processing without having to worry about freeing the
// statement for every error that might take place.
//-----------------------------------------------------------------------------
static int ob_dpiSubscr__prepareStmt(dpiSubscr *subscr, dpiStmt *stmt,
        const char *sql, uint32_t sqlLength, dpiError *error)
{
    // prepare statement for execution; only SELECT statements are supported
    if (ob_dpiStmt__prepare(stmt, sql, sqlLength, NULL, 0, error) < 0)
        return DPI_FAILURE;
    if (stmt->statementType != DPI_STMT_TYPE_SELECT)
        return ob_dpiError__set(error, "subscr prepare statement",
                DPI_ERR_NOT_SUPPORTED);

    // fetch array size is set to 1 in order to avoid over allocation since
    // the query is not really going to be used for fetching rows, just for
    // registration
    stmt->fetchArraySize = 1;

    // set subscription handle
    return ob_dpiOci__attrSet(stmt->handle, DPI_OCI_HTYPE_STMT, subscr->handle, 0,
            DPI_OCI_ATTR_CHNF_REGHANDLE, "set subscription handle", error);
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr_addRef() [PUBLIC]
//   Add a reference to the subscription.
//-----------------------------------------------------------------------------
int ob_dpiSubscr_addRef(dpiSubscr *subscr)
{
    return ob_dpiGen__addRef(subscr, DPI_HTYPE_SUBSCR, __func__);
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr_prepareStmt() [PUBLIC]
//   Prepare statement for registration with subscription.
//-----------------------------------------------------------------------------
int ob_dpiSubscr_prepareStmt(dpiSubscr *subscr, const char *sql,
        uint32_t sqlLength, dpiStmt **stmt)
{
    dpiStmt *tempStmt;
    dpiError error;

    if (ob_dpiSubscr__check(subscr, __func__, &error) < 0)
        return ob_dpiGen__endPublicFn(subscr, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(subscr, sql)
    DPI_CHECK_PTR_NOT_NULL(subscr, stmt)
    if (ob_dpiStmt__allocate(subscr->conn, 0, &tempStmt, &error) < 0)
        return ob_dpiGen__endPublicFn(subscr, DPI_FAILURE, &error);
    if (ob_dpiSubscr__prepareStmt(subscr, tempStmt, sql, sqlLength,
            &error) < 0) {
        ob_dpiStmt__free(tempStmt, &error);
        return ob_dpiGen__endPublicFn(subscr, DPI_FAILURE, &error);
    }

    *stmt = tempStmt;
    return ob_dpiGen__endPublicFn(subscr, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// ob_dpiSubscr_release() [PUBLIC]
//   Release a reference to the subscription.
//-----------------------------------------------------------------------------
int ob_dpiSubscr_release(dpiSubscr *subscr)
{
    return ob_dpiGen__release(subscr, DPI_HTYPE_SUBSCR, __func__);
}
