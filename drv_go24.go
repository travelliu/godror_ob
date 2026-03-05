//go:build cgo && go1.24

// Copyright 2019, 2025 The Godror Authors
//
//
// SPDX-License-Identifier: UPL-1.0 OR Apache-2.0

package godror

// See https://github.com/golang/go/issues/56378
// For pain, see https://github.com/travelliu/godror_ob/issues/365 .

/*
#cgo nocallback ob_dpiConn_breakExecution
#cgo nocallback ob_dpiConn_commit
#cgo nocallback ob_dpiConn_create
#cgo nocallback ob_dpiConn_getCurrentSchema
#cgo nocallback ob_dpiConn_getDbDomain
#cgo nocallback ob_dpiConn_getDbName
#cgo nocallback ob_dpiConn_getEdition
#cgo nocallback ob_dpiConn_getIsHealthy
#cgo nocallback ob_dpiConn_getObjectType
#cgo nocallback ob_dpiConn_getServerVersion
#cgo nocallback ob_dpiConn_getServiceName
#cgo nocallback ob_dpiConn_newMsgProps
#cgo nocallback ob_dpiConn_newQueue
#cgo nocallback ob_dpiConn_newTempLob
#cgo nocallback ob_dpiConn_newVar
#cgo nocallback ob_dpiConn_ping
#cgo nocallback ob_dpiConn_prepareStmt
#cgo nocallback ob_dpiConn_release
#cgo nocallback ob_dpiConn_rollback
#cgo nocallback ob_dpiConn_setAction
#cgo nocallback ob_dpiConn_setCallTimeout
// #cgo nocallback ob_dpiConn_setClientIdentifier
#cgo nocallback ob_dpiConn_setClientInfo
#cgo nocallback ob_dpiConn_setCurrentSchema
#cgo nocallback ob_dpiConn_setDbOp
#cgo nocallback ob_dpiConn_setModule
#cgo nocallback ob_dpiConn_shutdownDatabase
#cgo nocallback ob_dpiConn_startupDatabase
#cgo nocallback ob_dpiContext_createWithParams
#cgo nocallback ob_dpiContext_destroy
#cgo nocallback ob_dpiContext_getClientVersion
#cgo nocallback ob_dpiContext_getError
#cgo nocallback ob_dpiContext_initCommonCreateParams
#cgo nocallback ob_dpiContext_initConnCreateParams
#cgo nocallback ob_dpiContext_initPoolCreateParams
#cgo nocallback ob_dpiContext_initSubscrCreateParams
// #cgo nocallback ob_dpiData_getBool
// #cgo nocallback ob_dpiData_getBytes
// #cgo nocallback ob_dpiData_getDouble
// #cgo nocallback ob_dpiData_getFloat
// #cgo nocallback ob_dpiData_getInt64
// #cgo nocallback ob_dpiData_getIntervalDS
// #cgo nocallback ob_dpiData_getIntervalYM
// #cgo nocallback ob_dpiData_getIsNull
#cgo nocallback ob_dpiData_getJson
#cgo nocallback ob_dpiData_getJsonArray
#cgo nocallback ob_dpiData_getJsonObject
#cgo nocallback ob_dpiData_getLOB
#cgo nocallback ob_dpiData_getObject
// #cgo nocallback ob_dpiData_getRowid
#cgo nocallback ob_dpiData_getStmt
// #cgo nocallback ob_dpiData_getTimestamp
// #cgo nocallback ob_dpiData_getUint64
#cgo nocallback ob_dpiData_setBool
#cgo nocallback ob_dpiData_setBytes
#cgo nocallback ob_dpiData_setDouble
#cgo nocallback ob_dpiData_setFloat
#cgo nocallback ob_dpiData_setInt64
#cgo nocallback ob_dpiData_setIntervalDS
#cgo nocallback ob_dpiData_setIntervalYM
#cgo nocallback ob_dpiData_setLOB
// #cgo nocallback ob_dpiData_setNull
#cgo nocallback ob_dpiData_setObject
#cgo nocallback ob_dpiData_setStmt
#cgo nocallback ob_dpiData_setTimestamp
#cgo nocallback ob_dpiData_setUint64
#cgo nocallback ob_dpiDeqOptions_getCondition
#cgo nocallback ob_dpiDeqOptions_getConsumerName
#cgo nocallback ob_dpiDeqOptions_getCorrelation
#cgo nocallback ob_dpiDeqOptions_getMode
#cgo nocallback ob_dpiDeqOptions_getMsgId
#cgo nocallback ob_dpiDeqOptions_getNavigation
#cgo nocallback ob_dpiDeqOptions_getTransformation
#cgo nocallback ob_dpiDeqOptions_getVisibility
#cgo nocallback ob_dpiDeqOptions_getWait
#cgo nocallback ob_dpiDeqOptions_setCondition
#cgo nocallback ob_dpiDeqOptions_setConsumerName
#cgo nocallback ob_dpiDeqOptions_setCorrelation
#cgo nocallback ob_dpiDeqOptions_setDeliveryMode
#cgo nocallback ob_dpiDeqOptions_setMode
#cgo nocallback ob_dpiDeqOptions_setMsgId
#cgo nocallback ob_dpiDeqOptions_setNavigation
#cgo nocallback ob_dpiDeqOptions_setTransformation
#cgo nocallback ob_dpiDeqOptions_setVisibility
#cgo nocallback ob_dpiDeqOptions_setWait
#cgo nocallback ob_dpiEnqOptions_getTransformation
#cgo nocallback ob_dpiEnqOptions_getVisibility
#cgo nocallback ob_dpiEnqOptions_setDeliveryMode
#cgo nocallback ob_dpiEnqOptions_setTransformation
#cgo nocallback ob_dpiEnqOptions_setVisibility
#cgo nocallback ob_dpiJson_setFromText
#cgo nocallback ob_dpiJson_setValue
#cgo nocallback ob_dpiLob_close
#cgo nocallback ob_dpiLob_closeResource
#cgo nocallback ob_dpiLob_getChunkSize
#cgo nocallback ob_dpiLob_getDirectoryAndFileName
#cgo nocallback ob_dpiLob_getIsResourceOpen
#cgo nocallback ob_dpiLob_getSize
#cgo nocallback ob_dpiLob_getType
#cgo nocallback ob_dpiLob_openResource
#cgo nocallback ob_dpiLob_readBytes
#cgo nocallback ob_dpiLob_release
#cgo nocallback ob_dpiLob_setFromBytes
#cgo nocallback ob_dpiLob_trim
#cgo nocallback ob_dpiLob_writeBytes
#cgo nocallback ob_dpiMsgProps_getCorrelation
#cgo nocallback ob_dpiMsgProps_getDelay
#cgo nocallback ob_dpiMsgProps_getDeliveryMode
#cgo nocallback ob_dpiMsgProps_getEnqTime
#cgo nocallback ob_dpiMsgProps_getExceptionQ
#cgo nocallback ob_dpiMsgProps_getExpiration
#cgo nocallback ob_dpiMsgProps_getMsgId
#cgo nocallback ob_dpiMsgProps_getNumAttempts
#cgo nocallback ob_dpiMsgProps_getOriginalMsgId
#cgo nocallback ob_dpiMsgProps_getPayload
#cgo nocallback ob_dpiMsgProps_getPriority
#cgo nocallback ob_dpiMsgProps_getState
#cgo nocallback ob_dpiMsgProps_release
#cgo nocallback ob_dpiMsgProps_setCorrelation
#cgo nocallback ob_dpiMsgProps_setDelay
#cgo nocallback ob_dpiMsgProps_setExceptionQ
#cgo nocallback ob_dpiMsgProps_setExpiration
#cgo nocallback ob_dpiMsgProps_setOriginalMsgId
#cgo nocallback ob_dpiMsgProps_setPayloadBytes
#cgo nocallback ob_dpiMsgProps_setPayloadObject
#cgo nocallback ob_dpiMsgProps_setPriority
#cgo nocallback ob_dpiObject_addRef
#cgo nocallback ob_dpiObject_appendElement
#cgo nocallback ob_dpiObjectAttr_getInfo
#cgo nocallback ob_dpiObjectAttr_release
#cgo nocallback ob_dpiObject_deleteElementByIndex
#cgo nocallback ob_dpiObject_getAttributeValue
#cgo nocallback ob_dpiObject_getElementExistsByIndex
#cgo nocallback ob_dpiObject_getElementValueByIndex
#cgo nocallback ob_dpiObject_getFirstIndex
#cgo nocallback ob_dpiObject_getLastIndex
#cgo nocallback ob_dpiObject_getNextIndex
#cgo nocallback ob_dpiObject_getSize
#cgo nocallback ob_dpiObject_release
#cgo nocallback ob_dpiObject_setAttributeValue
#cgo nocallback ob_dpiObject_setElementValueByIndex
#cgo nocallback ob_dpiObject_trim
#cgo nocallback ob_dpiObjectType_addRef
#cgo nocallback ob_dpiObjectType_createObject
#cgo nocallback ob_dpiObjectType_getAttributes
#cgo nocallback ob_dpiObjectType_getInfo
#cgo nocallback ob_dpiObjectType_release
#cgo nocallback ob_dpiPool_close
#cgo nocallback ob_dpiPool_create
#cgo nocallback ob_dpiPool_getBusyCount
#cgo nocallback ob_dpiPool_getMaxLifetimeSession
#cgo nocallback ob_dpiPool_getOpenCount
#cgo nocallback ob_dpiPool_getTimeout
#cgo nocallback ob_dpiPool_getWaitTimeout
#cgo nocallback ob_dpiPool_release
#cgo nocallback ob_dpiPool_setStmtCacheSize
#cgo nocallback ob_dpiQueue_deqMany
#cgo nocallback ob_dpiQueue_deqOne
#cgo nocallback ob_dpiQueue_enqMany
#cgo nocallback ob_dpiQueue_enqOne
#cgo nocallback ob_dpiQueue_getDeqOptions
#cgo nocallback ob_dpiQueue_getEnqOptions
#cgo nocallback ob_dpiQueue_release
#cgo nocallback ob_dpiRowid_getStringValue
#cgo nocallback ob_dpiStmt_addRef
#cgo nocallback ob_dpiStmt_bindByName
#cgo nocallback ob_dpiStmt_bindByPos
#cgo nocallback ob_dpiStmt_define
#cgo nocallback ob_dpiStmt_deleteFromCache
#cgo nocallback ob_dpiStmt_execute
#cgo nocallback ob_dpiStmt_executeMany
#cgo nocallback ob_dpiStmt_fetchRows
#cgo nocallback ob_dpiStmt_getBatchErrorCount
#cgo nocallback ob_dpiStmt_getBatchErrors
#cgo nocallback ob_dpiStmt_getBindCount
#cgo nocallback ob_dpiStmt_getBindNames
#cgo nocallback ob_dpiStmt_getImplicitResult
#cgo nocallback ob_dpiStmt_getInfo
#cgo nocallback ob_dpiStmt_getNumQueryColumns
#cgo nocallback ob_dpiStmt_getQueryInfo
#cgo nocallback ob_dpiStmt_getRowCount
#cgo nocallback ob_dpiStmt_getSubscrQueryId
#cgo nocallback ob_dpiStmt_release
#cgo nocallback ob_dpiStmt_setFetchArraySize
#cgo nocallback ob_dpiStmt_setPrefetchRows
#cgo nocallback ob_dpiVar_getNumElementsInArray
#cgo nocallback ob_dpiVar_getReturnedData
#cgo nocallback ob_dpiVar_release
#cgo nocallback ob_dpiVar_setFromBytes
#cgo nocallback ob_dpiVar_setFromJson
#cgo nocallback ob_dpiVar_setFromLob
#cgo nocallback ob_dpiVar_setFromObject
#cgo nocallback ob_dpiVar_setNumElementsInArray
#cgo nocallback ob_godror_allocate_dpiNode
#cgo nocallback ob_godror_dpiasJsonArray
#cgo nocallback ob_godror_dpiasJsonObject
#cgo nocallback ob_godror_dpiJsonArray_initialize
#cgo nocallback ob_godror_dpiJsonfreeMem
#cgo nocallback ob_godror_dpiJsonObject_initialize
#cgo nocallback ob_godror_dpiJsonObject_setKey
#cgo nocallback ob_godror_dpiJson_setBool
#cgo nocallback ob_godror_dpiJson_setBytes
#cgo nocallback ob_godror_dpiJson_setDouble
#cgo nocallback ob_godror_dpiJson_setInt64
#cgo nocallback ob_godror_dpiJson_setIntervalDS
#cgo nocallback ob_godror_dpiJson_setNumber
#cgo nocallback ob_godror_dpiJson_setString
#cgo nocallback ob_godror_dpiJson_setTime
#cgo nocallback ob_godror_dpiJson_setUint64
#cgo nocallback ob_godror_getAnnotation
#cgo nocallback ob_godror_setArrayElements
#cgo nocallback ob_godror_setFromString
#cgo nocallback ob_godror_setObjectFields
*/
import "C"
