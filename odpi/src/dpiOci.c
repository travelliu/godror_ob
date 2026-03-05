//-----------------------------------------------------------------------------
// Copyright (c) 2017, 2025, Oracle and/or its affiliates.
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
// dpiOci.c
//   Link to OCI using dynamic linking. The OCI library (11.2+) is loaded
// dynamically and a function table kept for the functions that are used by
// DPI. This function table is populated as functions are used and permits use
// of all versions of OCI from one driver.
//-----------------------------------------------------------------------------

#include "dpiImpl.h"

// define structure used for loading the OCI library
typedef struct {
    void *handle;
    char *nameBuffer;
    size_t nameBufferLength;
    char *moduleNameBuffer;
    size_t moduleNameBufferLength;
    char *loadError;
    size_t loadErrorLength;
    char *errorBuffer;
    size_t errorBufferLength;
    char *envBuffer;
    size_t envBufferLength;
    char **configDir;
} dpiOciLoadLibParams;


// forward declarations of internal functions only used in this file
static void *ob_dpiOci__allocateMem(void *unused, size_t size);
static void ob_dpiOci__freeMem(void *unused, void *ptr);
static int ob_dpiOci__loadLibValidate(dpiContextCreateParams *params,
        dpiOciLoadLibParams *loadParams, dpiVersionInfo *versionInfo,
        dpiError *error);
static int ob_dpiOci__loadLibWithDir(dpiOciLoadLibParams *loadParams,
        const char *dirName, size_t dirNameLength, int scanAllNames,
        dpiError *error);
static int ob_dpiOci__loadLibWithName(dpiOciLoadLibParams *loadParams,
        const char *libName, dpiError *error);
static int ob_dpiOci__loadSymbol(const char *symbolName, void **symbol,
        dpiError *error);
static void *ob_dpiOci__reallocMem(void *unused, void *ptr, size_t newSize);


// macro to simplify code for loading each symbol
#define DPI_OCI_LOAD_SYMBOL(symbolName, symbol) \
    if (!symbol && ob_dpiOci__loadSymbol(symbolName, (void**) &symbol, \
            error) < 0) \
        return DPI_FAILURE;

// macro to ensure that an error handle is available
#define DPI_OCI_ENSURE_ERROR_HANDLE(error) \
    if (!error->handle && ob_dpiError__initHandle(error) < 0) \
        return DPI_FAILURE;

// macros to simplify code for checking results of OCI calls
#define DPI_OCI_ERROR_OCCURRED(status) \
    (status != DPI_OCI_SUCCESS && status != DPI_OCI_SUCCESS_WITH_INFO)
#define DPI_OCI_CHECK_AND_RETURN(error, status, conn, action) \
    if (status != DPI_OCI_SUCCESS) \
        return ob_dpiError__setFromOCI(error, status, conn, action); \
    return DPI_SUCCESS;

// macro to get the default mode to use when binding
#define DPI_OCI_DEFAULT_BIND_MODE(stmt) \
    (stmt->env->versionInfo->versionNum < 23 \
        || (stmt->env->versionInfo->versionNum == 23 \
        && stmt->env->versionInfo->releaseNum < 6)) ? DPI_OCI_DEFAULT : \
    DPI_OCI_BIND_DEDICATED_REF_CURSOR


// typedefs for all OCI functions used by ODPI-C
typedef int (*ob_dpiOciFnType__aqDeq)(void *svchp, void *errhp,
        const char *queue_name, void *deqopt, void *msgprop, void *payload_tdo,
        void **payload, void **payload_ind, void **msgid, uint32_t flags);
typedef int (*ob_dpiOciFnType__aqDeqArray)(void *svchp, void *errhp,
        const char *queue_name, void *deqopt, uint32_t *iters, void **msgprop,
        void *payload_tdo, void **payload, void **payload_ind, void **msgid,
        void *ctxp, void *deqcbfp, uint32_t flags);
typedef int (*ob_dpiOciFnType__aqEnq)(void *svchp, void *errhp,
        const char *queue_name, void *enqopt, void *msgprop, void *payload_tdo,
        void **payload, void **payload_ind, void **msgid, uint32_t flags);
typedef int (*ob_dpiOciFnType__aqEnqArray)(void *svchp, void *errhp,
        const char *queue_name, void *enqopt, uint32_t *iters, void **msgprop,
        void *payload_tdo, void **payload, void **payload_ind, void **msgid,
        void *ctxp, void *enqcbfp, uint32_t flags);
typedef int (*ob_dpiOciFnType__arrayDescriptorAlloc)(const void *parenth,
        void **descpp, const uint32_t type, uint32_t array_size,
        const size_t xtramem_sz, void **usrmempp);
typedef int (*ob_dpiOciFnType__arrayDescriptorFree)(void **descp,
        const uint32_t type);
typedef int (*ob_dpiOciFnType__attrGet)(const void  *trgthndlp,
        uint32_t trghndltyp, void *attributep, uint32_t *sizep,
        uint32_t attrtype, void *errhp);
typedef int (*ob_dpiOciFnType__attrSet)(void *trgthndlp, uint32_t trghndltyp,
        void *attributep, uint32_t size, uint32_t attrtype, void *errhp);
typedef int (*ob_dpiOciFnType__bindByName)(void *stmtp, void **bindp, void *errhp,
        const char *placeholder, int32_t placeh_len, void *valuep,
        int32_t value_sz, uint16_t dty, void *indp, uint16_t *alenp,
        uint16_t *rcodep, uint32_t maxarr_len, uint32_t *curelep,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__bindByName2)(void *stmtp, void **bindp,
        void *errhp, const char *placeholder, int32_t placeh_len, void *valuep,
        int64_t value_sz, uint16_t dty, void *indp, uint32_t *alenp,
        uint16_t *rcodep, uint32_t maxarr_len, uint32_t *curelep,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__bindByPos)(void *stmtp, void **bindp, void *errhp,
        uint32_t position, void *valuep, int32_t value_sz, uint16_t dty,
        void *indp, uint16_t *alenp, uint16_t *rcodep, uint32_t maxarr_len,
        uint32_t *curelep, uint32_t mode);
typedef int (*ob_dpiOciFnType__bindByPos2)(void *stmtp, void **bindp, void *errhp,
        uint32_t position, void *valuep, int64_t value_sz, uint16_t dty,
        void *indp, uint32_t *alenp, uint16_t *rcodep, uint32_t maxarr_len,
        uint32_t *curelep, uint32_t mode);
typedef int (*ob_dpiOciFnType__bindDynamic)(void *bindp, void *errhp, void *ictxp,
        void *icbfp, void *octxp, void *ocbfp);
typedef int (*ob_dpiOciFnType__bindObject)(void *bindp, void *errhp,
        const void *type, void **pgvpp, uint32_t *pvszsp, void **indpp,
        uint32_t *indszp);
typedef int (*ob_dpiOciFnType__break)(void *hndlp, void *errhp);
typedef void (*ob_dpiOciFnType__clientVersion)(int *major_version,
        int *minor_version, int *update_num, int *patch_num,
        int *port_update_num);
typedef int (*ob_dpiOciFnType__collAppend)(void *env, void *err, const void *elem,
        const void *elemind, void *coll);
typedef int (*ob_dpiOciFnType__collAssignElem)(void *env, void *err,
        int32_t index, const void *elem, const void *elemind, void *coll);
typedef int (*ob_dpiOciFnType__collGetElem)(void *env, void *err,
        const void *coll, int32_t index, int *exists, void **elem,
        void **elemind);
typedef int (*ob_dpiOciFnType__collSize)(void *env, void *err, const void *coll,
        int32_t *size);
typedef int (*ob_dpiOciFnType__collTrim)(void *env, void *err, int32_t trim_num,
        void *coll);
typedef int (*ob_dpiOciFnType__contextGetValue)(void *hdl, void *err,
        const char *key, uint8_t keylen, void **ctx_value);
typedef int (*ob_dpiOciFnType__contextSetValue)(void *hdl, void *err,
        uint16_t duration, const char *key, uint8_t keylen, void *ctx_value);
typedef int (*ob_dpiOciFnType__dateTimeConstruct)(void *hndl, void *err,
        void *datetime, int16_t yr, uint8_t mnth, uint8_t dy, uint8_t hr,
        uint8_t mm, uint8_t ss, uint32_t fsec, const char *tz,
        size_t tzLength);
typedef int (*ob_dpiOciFnType__dateTimeConvert)(void *hndl, void *err,
        void *indate, void *outdate);
typedef int (*ob_dpiOciFnType__dateTimeGetDate)(void *hndl, void *err,
        const void *date, int16_t *yr, uint8_t *mnth, uint8_t *dy);
typedef int (*ob_dpiOciFnType__dateTimeGetTime)(void *hndl, void *err,
        void *datetime, uint8_t *hr, uint8_t *mm, uint8_t *ss, uint32_t *fsec);
typedef int (*ob_dpiOciFnType__dateTimeGetTimeZoneOffset)(void *hndl, void *err,
        const void *datetime, int8_t *hr, int8_t *mm);
typedef int (*ob_dpiOciFnType__dateTimeIntervalAdd)(void *hndl, void *err,
        void *datetime, void *inter, void *outdatetime);
typedef int (*ob_dpiOciFnType__dateTimeSubtract)(void *hndl, void *err,
        void *indate1, void *indate2, void *inter);
typedef int (*ob_dpiOciFnType__dbShutdown)(void *svchp, void *errhp, void *admhp,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__dbStartup)(void *svchp, void *errhp, void *admhp,
        uint32_t mode, uint32_t flags);
typedef int (*ob_dpiOciFnType__defineByPos)(void *stmtp, void **defnp,
        void *errhp, uint32_t position, void *valuep, int32_t value_sz,
        uint16_t dty, void *indp, uint16_t *rlenp, uint16_t *rcodep,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__defineByPos2)(void *stmtp, void **defnp,
        void *errhp, uint32_t position, void *valuep, uint64_t value_sz,
        uint16_t dty, void *indp, uint32_t *rlenp, uint16_t *rcodep,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__defineDynamic)(void *defnp, void *errhp,
        void *octxp, void *ocbfp);
typedef int (*ob_dpiOciFnType__defineObject)(void *defnp, void *errhp,
        const void *type, void **pgvpp, uint32_t *pvszsp, void **indpp,
        uint32_t *indszp);
typedef int (*ob_dpiOciFnType__describeAny)(void *svchp, void *errhp,
        void *objptr, uint32_t objnm_len, uint8_t objptr_typ,
        uint8_t info_level, uint8_t objtyp, void *dschp);
typedef int (*ob_dpiOciFnType__descriptorAlloc)(const void *parenth,
        void **descpp, const uint32_t type, const size_t xtramem_sz,
        void **usrmempp);
typedef int (*ob_dpiOciFnType__descriptorFree)(void *descp, const uint32_t type);
typedef int (*ob_dpiOciFnType__envNlsCreate)(void **envp, uint32_t mode,
        void *ctxp, void *malocfp, void *ralocfp, void *mfreefp,
        size_t xtramem_sz, void **usrmempp, uint16_t charset,
        uint16_t ncharset);
typedef int (*ob_dpiOciFnType__errorGet)(void *hndlp, uint32_t recordno,
        char *sqlstate, int32_t *errcodep, char *bufp, uint32_t bufsiz,
        uint32_t type);
typedef int (*ob_dpiOciFnType__handleAlloc)(const void *parenth, void **hndlpp,
        const uint32_t type, const size_t xtramem_sz, void **usrmempp);
typedef int (*ob_dpiOciFnType__handleFree)(void *hndlp, const uint32_t type);
typedef int (*ob_dpiOciFnType__intervalGetDaySecond)(void *hndl, void *err,
        int32_t *dy, int32_t *hr, int32_t *mm, int32_t *ss, int32_t *fsec,
        const void *result);
typedef int (*ob_dpiOciFnType__intervalGetYearMonth)(void *hndl, void *err,
        int32_t *yr, int32_t *mnth, const void *result);
typedef int (*ob_dpiOciFnType__intervalSetDaySecond)(void *hndl, void *err,
        int32_t dy, int32_t hr, int32_t mm, int32_t ss, int32_t fsec,
        void *result);
typedef int (*ob_dpiOciFnType__intervalSetYearMonth)(void *hndl, void *err,
        int32_t yr, int32_t mnth, void *result);
typedef int (*ob_dpiOciFnType__jsonDomDocGet)(void *svchp, void *jsond,
        dpiJznDomDoc **jDomDoc, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__jsonTextBufferParse)(void *hndlp, void *jsond,
        void *bufp, uint64_t buf_sz, uint32_t validation, uint16_t encoding,
        void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__lobClose)(void *svchp, void *errhp, void *locp);
typedef int (*ob_dpiOciFnType__lobCreateTemporary)(void *svchp, void *errhp,
        void *locp, uint16_t csid, uint8_t csfrm, uint8_t lobtype, int cache,
        uint16_t duration);
typedef int (*ob_dpiOciFnType__lobFileExists)(void *svchp, void *errhp,
        void *filep, int *flag);
typedef int (*ob_dpiOciFnType__lobFileGetName)(void *envhp, void *errhp,
        const void *filep, char *dir_alias, uint16_t *d_length, char *filename,
        uint16_t *f_length);
typedef int (*ob_dpiOciFnType__lobFileSetName)(void *envhp, void *errhp,
        void **filepp, const char *dir_alias, uint16_t d_length,
        const char *filename, uint16_t f_length);
typedef int (*ob_dpiOciFnType__lobFreeTemporary)(void *svchp, void *errhp,
        void *locp);
typedef int (*ob_dpiOciFnType__lobGetChunkSize)(void *svchp, void *errhp,
        void *locp, uint32_t *chunksizep);
typedef int (*ob_dpiOciFnType__lobGetLength2)(void *svchp, void *errhp,
        void *locp, uint64_t *lenp);
typedef int (*ob_dpiOciFnType__lobIsOpen)(void *svchp, void *errhp, void *locp,
        int *flag);
typedef int (*ob_dpiOciFnType__lobIsTemporary)(void *envp, void *errhp,
        void *locp, int *is_temporary);
typedef int (*ob_dpiOciFnType__lobLocatorAssign)(void *svchp, void *errhp,
        const void *src_locp, void **dst_locpp);
typedef int (*ob_dpiOciFnType__lobOpen)(void *svchp, void *errhp, void *locp,
        uint8_t mode);
typedef int (*ob_dpiOciFnType__lobRead2)(void *svchp, void *errhp, void *locp,
        uint64_t *byte_amtp, uint64_t *char_amtp, uint64_t offset, void *bufp,
        uint64_t bufl, uint8_t piece, void *ctxp, void *cbfp, uint16_t csid,
        uint8_t csfrm);
typedef int (*ob_dpiOciFnType__lobTrim2)(void *svchp, void *errhp, void *locp,
        uint64_t newlen);
typedef int (*ob_dpiOciFnType__lobWrite2)(void *svchp, void *errhp, void *locp,
        uint64_t *byte_amtp, uint64_t *char_amtp, uint64_t offset, void *bufp,
        uint64_t buflen, uint8_t piece, void *ctxp, void *cbfp, uint16_t csid,
        uint8_t csfrm);
typedef int (*ob_dpiOciFnType__memoryAlloc)(void *hdl, void *err, void **mem,
        uint16_t dur, uint32_t size, uint32_t flags);
typedef int (*ob_dpiOciFnType__memoryFree)(void *hdl, void *err, void *mem);
typedef int (*ob_dpiOciFnType__nlsCharSetConvert)(void *envhp, void *errhp,
        uint16_t dstid, void  *dstp, size_t dstlen, uint16_t srcid,
        const void *srcp, size_t srclen, size_t *rsize);
typedef int (*ob_dpiOciFnType__nlsCharSetIdToName)(void *envhp, char *buf,
        size_t buflen, uint16_t id);
typedef uint16_t (*ob_dpiOciFnType__nlsCharSetNameToId)(void *envhp,
        const char *name);
typedef int (*ob_dpiOciFnType__nlsEnvironmentVariableGet)(void *val, size_t size,
        uint16_t item, uint16_t charset, size_t *rsize);
typedef int (*ob_dpiOciFnType__nlsNameMap)(void *envhp, char *buf, size_t buflen,
        const char *srcbuf, uint32_t flag);
typedef int (*ob_dpiOciFnType__nlsNumericInfoGet)(void *envhp, void *errhp,
        int32_t *val, uint16_t item);
typedef int (*ob_dpiOciFnType__numberFromInt)(void *err, const void *inum,
        unsigned int inum_length, unsigned int inum_s_flag, void *number);
typedef int (*ob_dpiOciFnType__numberFromReal)(void *err, const void *number,
        unsigned int rsl_length, void *rsl);
typedef int (*ob_dpiOciFnType__numberToInt)(void *err, const void *number,
        unsigned int rsl_length, unsigned int rsl_flag, void *rsl);
typedef int (*ob_dpiOciFnType__numberToReal)(void *err, const void *number,
        unsigned int rsl_length, void *rsl);
typedef int (*ob_dpiOciFnType__objectCopy)(void *env, void *err, const void *svc,
        void *source, void *null_source, void *target, void *null_target,
        void *tdo, uint16_t duration, uint8_t option);
typedef int (*ob_dpiOciFnType__objectFree)(void *env, void *err, void *instance,
        uint16_t flags);
typedef int (*ob_dpiOciFnType__objectGetAttr)(void *env, void *err,
        void *instance, void *null_struct, void *tdo, const char **names,
        const uint32_t *lengths, const uint32_t name_count,
        const uint32_t *indexes, const uint32_t index_count,
        int16_t *attr_null_status, void **attr_null_struct, void **attr_value,
        void **attr_tdo);
typedef int (*ob_dpiOciFnType__objectGetInd)(void *env, void *err, void *instance,
        void **null_struct);
typedef int (*ob_dpiOciFnType__objectNew)(void *env, void *err, const void *svc,
        uint16_t typecode, void *tdo, void *table, uint16_t duration,
        int value, void **instance);
typedef int (*ob_dpiOciFnType__objectPin)(void *env, void *err, void *object_ref,
        void *corhdl, int pin_option, uint16_t pin_duration, int lock_option,
        void **object);
typedef int (*ob_dpiOciFnType__objectSetAttr)(void *env, void *err,
        void *instance, void *null_struct, void *tdo, const char **names,
        const uint32_t *lengths, const uint32_t name_count,
        const uint32_t *indexes, const uint32_t index_count,
        const int16_t null_status, const void *attr_null_struct,
        const void *attr_value);
typedef int (*ob_dpiOciFnType__paramGet)(const void *hndlp, uint32_t htype,
        void *errhp, void **parmdpp, uint32_t pos);
typedef int (*ob_dpiOciFnType__passwordChange)(void *svchp, void *errhp,
        const char *user_name, uint32_t usernm_len, const char *opasswd,
        uint32_t opasswd_len, const char *npasswd, uint32_t npasswd_len,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__ping)(void *svchp, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__rawAssignBytes)(void *env, void *err,
        const char *rhs, uint32_t rhs_len, void **lhs);
typedef void *(*ob_dpiOciFnType__rawPtr)(void *env, const void *raw);
typedef int (*ob_dpiOciFnType__rawResize)(void *env, void *err, uint32_t new_size,
        void **raw);
typedef uint32_t (*ob_dpiOciFnType__rawSize)(void * env, const void *raw);
typedef int (*ob_dpiOciFnType__rowidToChar)(void *rowidDesc, char *outbfp,
        uint16_t *outbflp, void *errhp);
typedef int (*ob_dpiOciFnType__serverAttach)(void *srvhp, void *errhp,
        const char *dblink, int32_t dblink_len, uint32_t mode);
typedef int (*ob_dpiOciFnType__serverDetach)(void *srvhp, void *errhp,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__serverRelease)(void *hndlp, void *errhp,
        char *bufp, uint32_t bufsz, uint8_t hndltype, uint32_t *version);
typedef int (*ob_dpiOciFnType__serverRelease2)(void *hndlp, void *errhp,
        char *bufp, uint32_t bufsz, uint8_t hndltype, uint32_t *version,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__sessionBegin)(void *svchp, void *errhp,
        void *usrhp, uint32_t credt, uint32_t mode);
typedef int (*ob_dpiOciFnType__sessionEnd)(void *svchp, void *errhp, void *usrhp,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__sessionGet)(void *envhp, void *errhp, void **svchp,
        void *authhp, const char *poolName, uint32_t poolName_len,
        const char *tagInfo, uint32_t tagInfo_len, const char **retTagInfo,
        uint32_t *retTagInfo_len, int *found, uint32_t mode);
typedef int (*ob_dpiOciFnType__sessionPoolCreate)(void *envhp, void *errhp,
        void *spoolhp, char **poolName, uint32_t *poolNameLen,
        const char *connStr, uint32_t connStrLen, uint32_t sessMin,
        uint32_t sessMax, uint32_t sessIncr, const char *userid,
        uint32_t useridLen, const char *password, uint32_t passwordLen,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__sessionPoolDestroy)(void *spoolhp, void *errhp,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__sessionRelease)(void *svchp, void *errhp,
        const char *tag, uint32_t tag_len, uint32_t mode);
typedef int (*ob_dpiOciFnType__shardingKeyColumnAdd)(void *shardingKey,
        void *errhp, void *col, uint32_t colLen, uint16_t colType,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaBulkInsert)(void *svchp,
        void *collection, void **documentarray, uint32_t arraylen,
        void *opoptns, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaBulkInsertAndGet)(void *svchp,
        void *collection, void **documentarray, uint32_t arraylen,
        void *opoptns, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaBulkInsertAndGetWithOpts)(void *svchp,
        void *collection, void **documentarray, uint32_t arraylen,
        void *oproptns, void *opoptns, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaCollCreateWithMetadata)(void *svchp,
        const char *collname, uint32_t collnamelen, const char *metadata,
        uint32_t metadatalen, void **collection, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaCollDrop)(void *svchp, void *coll,
        int *isDropped, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaCollGetNext)(void *svchp, const void *cur,
        void **coll, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaCollList)(void *svchp, const char *startname,
        uint32_t stnamelen, void **cur, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaCollOpen)(void *svchp, const char *collname,
        uint32_t collnamelen, void **coll, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaCollTruncate)(void *svchp, void *collection,
        void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaDataGuideGet)(void *svchp,
        const void *collection, uint32_t docFlags, void **doc, void *errhp,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaDocCount)(void *svchp, const void *coll,
        const void *optns, uint64_t *numdocs, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaDocGetNext)(void *svchp, const void *cur,
        void **doc, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaFind)(void *svchp, const void *coll,
        const void *findOptions, uint32_t docFlags, void **cursor,
        void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaFindOne)(void *svchp, const void *coll,
        const void *findOptions, uint32_t docFlags, void **doc, void *errhp,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaIndexCreate)(void *svchp, const void *coll,
        const char *indexspec, uint32_t speclen, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaIndexDrop)(void *svchp, const char *indexname,
        uint32_t indexnamelen, int *isDropped, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaIndexList)(void *svchp, const void *collection,
        uint32_t flags, void **indexList, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaInsert)(void *svchp, void *collection,
        void *document, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaInsertAndGet)(void *svchp, void *collection,
        void **document, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaInsertAndGetWithOpts)(void *svchp,
        void *collection, void **document, void *oproptns, void *errhp,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaOperKeysSet)(const void *operhp,
        const char **keysArray, uint32_t *lengthsArray, uint32_t count,
        void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaRemove)(void *svchp, const void *coll,
        const void *optns, uint64_t *removeCount, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaReplOne)(void *svchp, const void *coll,
        const void *optns, void *document, int *isReplaced, void *errhp,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaReplOneAndGet)(void *svchp, const void *coll,
        const void *optns, void **document, int *isReplaced, void *errhp,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaSave)(void *svchp, void *collection,
        void *document, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaSaveAndGet)(void *svchp, void *collection,
        void **document, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__sodaSaveAndGetWithOpts)(void *svchp,
        void *collection, void **document, void *oproptns, void *errhp,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__stmtExecute)(void *svchp, void *stmtp, void *errhp,
        uint32_t iters, uint32_t rowoff, const void *snap_in, void *snap_out,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__stmtFetch2)(void *stmtp, void *errhp,
        uint32_t nrows, uint16_t orientation, int32_t scrollOffset,
        uint32_t mode);
typedef int (*ob_dpiOciFnType__stmtGetBindInfo)(void *stmtp, void *errhp,
        uint32_t size, uint32_t startloc, int32_t *found, char *bvnp[],
        uint8_t bvnl[], char *invp[], uint8_t inpl[], uint8_t dupl[],
        void **hndl);
typedef int (*ob_dpiOciFnType__stmtGetNextResult)(void *stmthp, void *errhp,
        void **result, uint32_t *rtype, uint32_t mode);
typedef int (*ob_dpiOciFnType__stmtPrepare2)(void *svchp, void **stmtp,
        void *errhp, const char *stmt, uint32_t stmt_len, const char *key,
        uint32_t key_len, uint32_t language, uint32_t mode);
typedef int (*ob_dpiOciFnType__stmtRelease)(void *stmtp, void *errhp,
        const char *key, uint32_t key_len, uint32_t mode);
typedef int (*ob_dpiOciFnType__stringAssignText)(void *env, void *err,
        const char *rhs, uint32_t rhs_len, void **lhs);
typedef char *(*ob_dpiOciFnType__stringPtr)(void *env, const void *vs);
typedef int (*ob_dpiOciFnType__stringResize)(void *env, void *err,
        uint32_t new_size, void **str);
typedef uint32_t (*ob_dpiOciFnType__stringSize)(void *env, const void *vs);
typedef int (*ob_dpiOciFnType__subscriptionRegister)(void *svchp,
        void **subscrhpp, uint16_t count, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__subscriptionUnRegister)(void *svchp,
        void *subscrhp, void *errhp, uint32_t mode);
typedef int (*ob_dpiOciFnType__tableDelete)(void *env, void *err, int32_t index,
        void *tbl);
typedef int (*ob_dpiOciFnType__tableExists)(void *env, void *err, const void *tbl,
        int32_t index, int *exists);
typedef int (*ob_dpiOciFnType__tableFirst)(void *env, void *err, const void *tbl,
        int32_t *index);
typedef int (*ob_dpiOciFnType__tableLast)(void *env, void *err, const void *tbl,
        int32_t *index);
typedef int (*ob_dpiOciFnType__tableNext)(void *env, void *err, int32_t index,
        const void *tbl, int32_t *next_index, int *exists);
typedef int (*ob_dpiOciFnType__tablePrev)(void *env, void *err, int32_t index,
        const void *tbl, int32_t *prev_index, int *exists);
typedef int (*ob_dpiOciFnType__tableSize)(void *env, void *err, const void *tbl,
        int32_t *size);
typedef int (*ob_dpiOciFnType__threadKeyDestroy)(void *hndl, void *err,
        void **key);
typedef int (*ob_dpiOciFnType__threadKeyGet)(void *hndl, void *err, void *key,
        void **pValue);
typedef int (*ob_dpiOciFnType__threadKeyInit)(void *hndl, void *err, void **key,
        void *destFn);
typedef int (*ob_dpiOciFnType__threadKeySet)(void *hndl, void *err, void *key,
        void *value);
typedef void (*ob_dpiOciFnType__threadProcessInit)(void);
typedef int (*ob_dpiOciFnType__transCommit)(void *svchp, void *errhp,
        uint32_t flags);
typedef int (*ob_dpiOciFnType__transDetach)(void *svchp, void *errhp,
        uint32_t flags);
typedef int (*ob_dpiOciFnType__transForget)(void *svchp, void *errhp,
        uint32_t flags);
typedef int (*ob_dpiOciFnType__transPrepare)(void *svchp, void *errhp,
        uint32_t flags);
typedef int (*ob_dpiOciFnType__transRollback)(void *svchp, void *errhp,
        uint32_t flags);
typedef int (*ob_dpiOciFnType__transStart)(void *svchp, void *errhp,
        unsigned int timeout, uint32_t flags);
typedef int (*ob_dpiOciFnType__typeByFullName)(void *env, void *err,
        const void *svc, const char *full_type_name,
        uint32_t full_type_name_length, const char *version_name,
        uint32_t version_name_length, uint16_t pin_duration, int get_option,
        void **tdo);
typedef int (*ob_dpiOciFnType__typeByName)(void *env, void *err, const void *svc,
        const char *schema_name, uint32_t s_length, const char *type_name,
        uint32_t t_length, const char *version_name, uint32_t v_length,
        uint16_t pin_duration, int get_option, void **tdo);
typedef int (*ob_dpiOciFnType__vectorFromArray)(void *vectord, void *errhp,
        uint8_t vformat, uint32_t vdim, void *vecarray, uint32_t mode);
typedef int (*ob_dpiOciFnType__vectorFromSparseArray)(void *vectord, void *errhp,
        uint8_t vformat, uint32_t vdim, uint32_t indices, void *indarray,
        void *vecarray, uint32_t mode);
typedef int (*ob_dpiOciFnType__vectorToArray)(void *vectord, void *errhp,
        uint8_t vformat, uint32_t *vdim, void *vecarray, uint32_t mode);
typedef int (*ob_dpiOciFnType__vectorToSparseArray)(void *vectord, void *errhp,
        uint8_t vformat, uint32_t *vdim, uint32_t *indices, void *indarray,
        void *vecarray, uint32_t mode);


// library handle for dynamically loaded OCI library
static void *dpiOciLibHandle = NULL;

// library names to search
static const char *dpiOciLibNames[] = {
#if defined _WIN32 || defined __CYGWIN__
    "libobci_obclnt.dll",
    "libobci.dll",
#elif __APPLE__
    "libclntsh.dylib",
    "libclntsh.dylib.19.1",
    "libclntsh.dylib.18.1",
    "libclntsh.dylib.12.1",
    "libclntsh.dylib.11.1",
    "libclntsh.dylib.20.1",
    "libclntsh.dylib.21.1",
#else
    "libobci_obclnt.so",
    "libobci.so",
//    "libclntsh.so",
//    "libclntsh.so.19.1",
//    "libclntsh.so.18.1",
//    "libclntsh.so.12.1",
//    "libclntsh.so.11.1",
//    "libclntsh.so.20.1",
//    "libclntsh.so.21.1",
#endif
    NULL
};

// subdirectory for configuration directory
static const char *dpiOciConfigSubDir = "network/admin";

// all OCI symbols used by ODPI-C
static struct {
    ob_dpiOciFnType__aqDeq fnAqDeq;
    ob_dpiOciFnType__aqDeqArray fnAqDeqArray;
    ob_dpiOciFnType__aqEnq fnAqEnq;
    ob_dpiOciFnType__aqEnqArray fnAqEnqArray;
    ob_dpiOciFnType__arrayDescriptorAlloc fnArrayDescriptorAlloc;
    ob_dpiOciFnType__arrayDescriptorFree fnArrayDescriptorFree;
    ob_dpiOciFnType__attrGet fnAttrGet;
    ob_dpiOciFnType__attrSet fnAttrSet;
    ob_dpiOciFnType__bindByName fnBindByName;
    ob_dpiOciFnType__bindByName2 fnBindByName2;
    ob_dpiOciFnType__bindByPos fnBindByPos;
    ob_dpiOciFnType__bindByPos2 fnBindByPos2;
    ob_dpiOciFnType__bindDynamic fnBindDynamic;
    ob_dpiOciFnType__bindObject fnBindObject;
    ob_dpiOciFnType__break fnBreak;
    ob_dpiOciFnType__clientVersion fnClientVersion;
    ob_dpiOciFnType__collAppend fnCollAppend;
    ob_dpiOciFnType__collAssignElem fnCollAssignElem;
    ob_dpiOciFnType__collGetElem fnCollGetElem;
    ob_dpiOciFnType__collSize fnCollSize;
    ob_dpiOciFnType__collTrim fnCollTrim;
    ob_dpiOciFnType__contextGetValue fnContextGetValue;
    ob_dpiOciFnType__contextSetValue fnContextSetValue;
    ob_dpiOciFnType__dateTimeConstruct fnDateTimeConstruct;
    ob_dpiOciFnType__dateTimeConvert fnDateTimeConvert;
    ob_dpiOciFnType__dateTimeGetDate fnDateTimeGetDate;
    ob_dpiOciFnType__dateTimeGetTime fnDateTimeGetTime;
    ob_dpiOciFnType__dateTimeGetTimeZoneOffset fnDateTimeGetTimeZoneOffset;
    ob_dpiOciFnType__dateTimeIntervalAdd fnDateTimeIntervalAdd;
    ob_dpiOciFnType__dateTimeSubtract fnDateTimeSubtract;
    ob_dpiOciFnType__dbShutdown fnDbShutdown;
    ob_dpiOciFnType__dbStartup fnDbStartup;
    ob_dpiOciFnType__defineByPos fnDefineByPos;
    ob_dpiOciFnType__defineByPos2 fnDefineByPos2;
    ob_dpiOciFnType__defineDynamic fnDefineDynamic;
    ob_dpiOciFnType__defineObject fnDefineObject;
    ob_dpiOciFnType__describeAny fnDescribeAny;
    ob_dpiOciFnType__descriptorAlloc fnDescriptorAlloc;
    ob_dpiOciFnType__descriptorFree fnDescriptorFree;
    ob_dpiOciFnType__envNlsCreate fnEnvNlsCreate;
    ob_dpiOciFnType__errorGet fnErrorGet;
    ob_dpiOciFnType__handleAlloc fnHandleAlloc;
    ob_dpiOciFnType__handleFree fnHandleFree;
    ob_dpiOciFnType__intervalGetDaySecond fnIntervalGetDaySecond;
    ob_dpiOciFnType__intervalGetYearMonth fnIntervalGetYearMonth;
    ob_dpiOciFnType__intervalSetDaySecond fnIntervalSetDaySecond;
    ob_dpiOciFnType__intervalSetYearMonth fnIntervalSetYearMonth;
    ob_dpiOciFnType__jsonDomDocGet fnJsonDomDocGet;
    ob_dpiOciFnType__jsonTextBufferParse fnJsonTextBufferParse;
    ob_dpiOciFnType__lobClose fnLobClose;
    ob_dpiOciFnType__lobCreateTemporary fnLobCreateTemporary;
    ob_dpiOciFnType__lobFileExists fnLobFileExists;
    ob_dpiOciFnType__lobFileGetName fnLobFileGetName;
    ob_dpiOciFnType__lobFileSetName fnLobFileSetName;
    ob_dpiOciFnType__lobFreeTemporary fnLobFreeTemporary;
    ob_dpiOciFnType__lobGetChunkSize fnLobGetChunkSize;
    ob_dpiOciFnType__lobGetLength2 fnLobGetLength2;
    ob_dpiOciFnType__lobIsOpen fnLobIsOpen;
    ob_dpiOciFnType__lobIsTemporary fnLobIsTemporary;
    ob_dpiOciFnType__lobLocatorAssign fnLobLocatorAssign;
    ob_dpiOciFnType__lobOpen fnLobOpen;
    ob_dpiOciFnType__lobRead2 fnLobRead2;
    ob_dpiOciFnType__lobTrim2 fnLobTrim2;
    ob_dpiOciFnType__lobWrite2 fnLobWrite2;
    ob_dpiOciFnType__memoryAlloc fnMemoryAlloc;
    ob_dpiOciFnType__memoryFree fnMemoryFree;
    ob_dpiOciFnType__nlsCharSetConvert fnNlsCharSetConvert;
    ob_dpiOciFnType__nlsCharSetIdToName fnNlsCharSetIdToName;
    ob_dpiOciFnType__nlsCharSetNameToId fnNlsCharSetNameToId;
    ob_dpiOciFnType__nlsEnvironmentVariableGet fnNlsEnvironmentVariableGet;
    ob_dpiOciFnType__nlsNameMap fnNlsNameMap;
    ob_dpiOciFnType__nlsNumericInfoGet fnNlsNumericInfoGet;
    ob_dpiOciFnType__numberFromInt fnNumberFromInt;
    ob_dpiOciFnType__numberFromReal fnNumberFromReal;
    ob_dpiOciFnType__numberToInt fnNumberToInt;
    ob_dpiOciFnType__numberToReal fnNumberToReal;
    ob_dpiOciFnType__objectCopy fnObjectCopy;
    ob_dpiOciFnType__objectFree fnObjectFree;
    ob_dpiOciFnType__objectGetAttr fnObjectGetAttr;
    ob_dpiOciFnType__objectGetInd fnObjectGetInd;
    ob_dpiOciFnType__objectNew fnObjectNew;
    ob_dpiOciFnType__objectPin fnObjectPin;
    ob_dpiOciFnType__objectSetAttr fnObjectSetAttr;
    ob_dpiOciFnType__paramGet fnParamGet;
    ob_dpiOciFnType__passwordChange fnPasswordChange;
    ob_dpiOciFnType__ping fnPing;
    ob_dpiOciFnType__rawAssignBytes fnRawAssignBytes;
    ob_dpiOciFnType__rawPtr fnRawPtr;
    ob_dpiOciFnType__rawResize fnRawResize;
    ob_dpiOciFnType__rawSize fnRawSize;
    ob_dpiOciFnType__rowidToChar fnRowidToChar;
    ob_dpiOciFnType__serverAttach fnServerAttach;
    ob_dpiOciFnType__serverDetach fnServerDetach;
    ob_dpiOciFnType__serverRelease fnServerRelease;
    ob_dpiOciFnType__serverRelease2 fnServerRelease2;
    ob_dpiOciFnType__sessionBegin fnSessionBegin;
    ob_dpiOciFnType__sessionEnd fnSessionEnd;
    ob_dpiOciFnType__sessionGet fnSessionGet;
    ob_dpiOciFnType__sessionPoolCreate fnSessionPoolCreate;
    ob_dpiOciFnType__sessionPoolDestroy fnSessionPoolDestroy;
    ob_dpiOciFnType__sessionRelease fnSessionRelease;
    ob_dpiOciFnType__shardingKeyColumnAdd fnShardingKeyColumnAdd;
    ob_dpiOciFnType__stmtExecute fnStmtExecute;
    ob_dpiOciFnType__sodaBulkInsert fnSodaBulkInsert;
    ob_dpiOciFnType__sodaBulkInsertAndGet fnSodaBulkInsertAndGet;
    ob_dpiOciFnType__sodaBulkInsertAndGetWithOpts fnSodaBulkInsertAndGetWithOpts;
    ob_dpiOciFnType__sodaCollCreateWithMetadata fnSodaCollCreateWithMetadata;
    ob_dpiOciFnType__sodaCollDrop fnSodaCollDrop;
    ob_dpiOciFnType__sodaCollGetNext fnSodaCollGetNext;
    ob_dpiOciFnType__sodaCollList fnSodaCollList;
    ob_dpiOciFnType__sodaCollOpen fnSodaCollOpen;
    ob_dpiOciFnType__sodaCollTruncate fnSodaCollTruncate;
    ob_dpiOciFnType__sodaDataGuideGet fnSodaDataGuideGet;
    ob_dpiOciFnType__sodaDocCount fnSodaDocCount;
    ob_dpiOciFnType__sodaDocGetNext fnSodaDocGetNext;
    ob_dpiOciFnType__sodaFind fnSodaFind;
    ob_dpiOciFnType__sodaFindOne fnSodaFindOne;
    ob_dpiOciFnType__sodaIndexCreate fnSodaIndexCreate;
    ob_dpiOciFnType__sodaIndexDrop fnSodaIndexDrop;
    ob_dpiOciFnType__sodaIndexList fnSodaIndexList;
    ob_dpiOciFnType__sodaInsert fnSodaInsert;
    ob_dpiOciFnType__sodaInsertAndGet fnSodaInsertAndGet;
    ob_dpiOciFnType__sodaInsertAndGetWithOpts fnSodaInsertAndGetWithOpts;
    ob_dpiOciFnType__sodaOperKeysSet fnSodaOperKeysSet;
    ob_dpiOciFnType__sodaRemove fnSodaRemove;
    ob_dpiOciFnType__sodaReplOne fnSodaReplOne;
    ob_dpiOciFnType__sodaReplOneAndGet fnSodaReplOneAndGet;
    ob_dpiOciFnType__sodaSave fnSodaSave;
    ob_dpiOciFnType__sodaSaveAndGet fnSodaSaveAndGet;
    ob_dpiOciFnType__sodaSaveAndGetWithOpts fnSodaSaveAndGetWithOpts;
    ob_dpiOciFnType__stmtFetch2 fnStmtFetch2;
    ob_dpiOciFnType__stmtGetBindInfo fnStmtGetBindInfo;
    ob_dpiOciFnType__stmtGetNextResult fnStmtGetNextResult;
    ob_dpiOciFnType__stmtPrepare2 fnStmtPrepare2;
    ob_dpiOciFnType__stmtRelease fnStmtRelease;
    ob_dpiOciFnType__stringAssignText fnStringAssignText;
    ob_dpiOciFnType__stringPtr fnStringPtr;
    ob_dpiOciFnType__stringResize fnStringResize;
    ob_dpiOciFnType__stringSize fnStringSize;
    ob_dpiOciFnType__subscriptionRegister fnSubscriptionRegister;
    ob_dpiOciFnType__subscriptionUnRegister fnSubscriptionUnRegister;
    ob_dpiOciFnType__tableDelete fnTableDelete;
    ob_dpiOciFnType__tableExists fnTableExists;
    ob_dpiOciFnType__tableFirst fnTableFirst;
    ob_dpiOciFnType__tableLast fnTableLast;
    ob_dpiOciFnType__tableNext fnTableNext;
    ob_dpiOciFnType__tablePrev fnTablePrev;
    ob_dpiOciFnType__tableSize fnTableSize;
    ob_dpiOciFnType__threadKeyDestroy fnThreadKeyDestroy;
    ob_dpiOciFnType__threadKeyGet fnThreadKeyGet;
    ob_dpiOciFnType__threadKeyInit fnThreadKeyInit;
    ob_dpiOciFnType__threadKeySet fnThreadKeySet;
    ob_dpiOciFnType__threadProcessInit fnThreadProcessInit;
    ob_dpiOciFnType__transCommit fnTransCommit;
    ob_dpiOciFnType__transDetach fnTransDetach;
    ob_dpiOciFnType__transForget fnTransForget;
    ob_dpiOciFnType__transPrepare fnTransPrepare;
    ob_dpiOciFnType__transRollback fnTransRollback;
    ob_dpiOciFnType__transStart fnTransStart;
    ob_dpiOciFnType__typeByFullName fnTypeByFullName;
    ob_dpiOciFnType__typeByName fnTypeByName;
    ob_dpiOciFnType__vectorFromArray fnVectorFromArray;
    ob_dpiOciFnType__vectorFromSparseArray fnVectorFromSparseArray;
    ob_dpiOciFnType__vectorToArray fnVectorToArray;
    ob_dpiOciFnType__vectorToSparseArray fnVectorToSparseArray;
} dpiOciSymbols;


//-----------------------------------------------------------------------------
// ob_dpiOci__allocateMem() [INTERNAL]
//   Wrapper for OCI allocation of memory, only used when debugging memory
// allocation.
//-----------------------------------------------------------------------------
static void *ob_dpiOci__allocateMem(UNUSED void *unused, size_t size)
{
    void *ptr;

    ptr = malloc(size);
    ob_dpiDebug__print("OCI allocated %u bytes at %p\n", size, ptr);
    return ptr;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__aqDeq() [INTERNAL]
//   Wrapper for OCIAQDeq().
//-----------------------------------------------------------------------------
int ob_dpiOci__aqDeq(dpiConn *conn, const char *queueName, void *options,
        void *msgProps, void *payloadType, void **payload, void **payloadInd,
        void **msgId, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIAQDeq", dpiOciSymbols.fnAqDeq)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnAqDeq)(conn->handle, error->handle, queueName,
            options, msgProps, payloadType, payload, payloadInd, msgId,
            DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "dequeue message");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__aqDeqArray() [INTERNAL]
//   Wrapper for OCIAQDeqArray().
//-----------------------------------------------------------------------------
int ob_dpiOci__aqDeqArray(dpiConn *conn, const char *queueName, void *options,
        uint32_t *numIters, void **msgProps, void *payloadType, void **payload,
        void **payloadInd, void **msgId, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIAQDeqArray", dpiOciSymbols.fnAqDeqArray)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnAqDeqArray)(conn->handle, error->handle,
            queueName, options, numIters, msgProps, payloadType, payload,
            payloadInd, msgId, NULL, NULL, DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "dequeue messages");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__aqEnq() [INTERNAL]
//   Wrapper for OCIAQEnq().
//-----------------------------------------------------------------------------
int ob_dpiOci__aqEnq(dpiConn *conn, const char *queueName, void *options,
        void *msgProps, void *payloadType, void **payload, void **payloadInd,
        void **msgId, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIAQEnq", dpiOciSymbols.fnAqEnq)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnAqEnq)(conn->handle, error->handle, queueName,
            options, msgProps, payloadType, payload, payloadInd, msgId,
            DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "enqueue message");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__aqEnqArray() [INTERNAL]
//   Wrapper for OCIAQEnqArray().
//-----------------------------------------------------------------------------
int ob_dpiOci__aqEnqArray(dpiConn *conn, const char *queueName, void *options,
        uint32_t *numIters, void **msgProps, void *payloadType, void **payload,
        void **payloadInd, void **msgId, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIAQEnqArray", dpiOciSymbols.fnAqEnqArray)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnAqEnqArray)(conn->handle, error->handle,
            queueName, options, numIters, msgProps, payloadType, payload,
            payloadInd, msgId, NULL, NULL, DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "enqueue messages");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__arrayDescriptorAlloc() [INTERNAL]
//   Wrapper for OCIArrayDescriptorAlloc().
//-----------------------------------------------------------------------------
int ob_dpiOci__arrayDescriptorAlloc(void *envHandle, void **handle,
        uint32_t handleType, uint32_t arraySize, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIArrayDescriptorAlloc",
            dpiOciSymbols.fnArrayDescriptorAlloc)
    status = (*dpiOciSymbols.fnArrayDescriptorAlloc)(envHandle, handle,
            handleType, arraySize, 0, NULL);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "allocate descriptors");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__arrayDescriptorFree() [INTERNAL]
//   Wrapper for OCIArrayDescriptorFree().
//-----------------------------------------------------------------------------
int ob_dpiOci__arrayDescriptorFree(void **handle, uint32_t handleType)
{
    dpiError *error = NULL;
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIArrayDescriptorFree",
            dpiOciSymbols.fnArrayDescriptorFree)
    status = (*dpiOciSymbols.fnArrayDescriptorFree)(handle, handleType);
    if (status != DPI_OCI_SUCCESS &&
            ob_dpiDebugLevel & DPI_DEBUG_LEVEL_UNREPORTED_ERRORS)
        ob_dpiDebug__print("free array descriptors %p, handleType %d failed\n",
                handle, handleType);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__attrGet() [INTERNAL]
//   Wrapper for OCIAttrGet().
//-----------------------------------------------------------------------------
int ob_dpiOci__attrGet(const void *handle, uint32_t handleType, void *ptr,
        uint32_t *size, uint32_t attribute, const char *action,
        dpiError *error)
{
    int status;

    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnAttrGet)(handle, handleType, ptr, size,
            attribute, error->handle);
    if (status == DPI_OCI_NO_DATA && size) {
        *size = 0;
        return DPI_SUCCESS;
    } else if (!action) {
        return DPI_SUCCESS;
    }
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, action);
}


//-----------------------------------------------------------------------------
// ob_dpiOci__attrSet() [INTERNAL]
//   Wrapper for OCIAttrSet().
//-----------------------------------------------------------------------------
int ob_dpiOci__attrSet(void *handle, uint32_t handleType, void *ptr,
        uint32_t size, uint32_t attribute, const char *action, dpiError *error)
{
    int status;

    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnAttrSet)(handle, handleType, ptr, size,
            attribute, error->handle);
    if (!action)
        return DPI_SUCCESS;
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, action);
}


//-----------------------------------------------------------------------------
// ob_dpiOci__bindByName() [INTERNAL]
//   Wrapper for OCIBindByName().
//-----------------------------------------------------------------------------
int ob_dpiOci__bindByName(dpiStmt *stmt, void **bindHandle, const char *name,
        int32_t nameLength, int dynamicBind, dpiVar *var, dpiError *error)
{
    uint32_t mode = DPI_OCI_DEFAULT;
    int status;

    if (dynamicBind)
        mode |= DPI_OCI_DATA_AT_EXEC;
    DPI_OCI_LOAD_SYMBOL("OCIBindByName", dpiOciSymbols.fnBindByName)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnBindByName)(stmt->handle, bindHandle,
            error->handle, name, nameLength,
            (dynamicBind) ? NULL : var->buffer.data.asRaw,
            (var->isDynamic) ? INT_MAX : (int32_t) var->sizeInBytes,
            var->type->oracleType, (dynamicBind) ? NULL :
                    var->buffer.indicator,
            (dynamicBind || var->type->sizeInBytes) ? NULL :
                    var->buffer.actualLength16,
            (dynamicBind) ? NULL : var->buffer.returnCode,
            (var->isArray) ? var->buffer.maxArraySize : 0,
            (var->isArray) ? &var->buffer.actualArraySize : NULL, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, stmt->conn, "bind by name");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__bindByName2() [INTERNAL]
//   Wrapper for OCIBindByName2().
//-----------------------------------------------------------------------------
int ob_dpiOci__bindByName2(dpiStmt *stmt, void **bindHandle, const char *name,
        int32_t nameLength, int dynamicBind, dpiVar *var, dpiError *error)
{
    uint32_t mode = DPI_OCI_DEFAULT_BIND_MODE(stmt);
    int status;

    if (dynamicBind)
        mode |= DPI_OCI_DATA_AT_EXEC;
    DPI_OCI_LOAD_SYMBOL("OCIBindByName2", dpiOciSymbols.fnBindByName2)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnBindByName2)(stmt->handle, bindHandle,
            error->handle, name, nameLength,
            (dynamicBind) ? NULL : var->buffer.data.asRaw,
            (var->isDynamic) ? INT_MAX : var->sizeInBytes,
            var->type->oracleType, (dynamicBind) ? NULL :
                    var->buffer.indicator,
            (dynamicBind || var->type->sizeInBytes) ? NULL :
                    var->buffer.actualLength32,
            (dynamicBind) ? NULL : var->buffer.returnCode,
            (var->isArray) ? var->buffer.maxArraySize : 0,
            (var->isArray) ? &var->buffer.actualArraySize : NULL, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, stmt->conn, "bind by name");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__bindByPos() [INTERNAL]
//   Wrapper for OCIBindByPos().
//-----------------------------------------------------------------------------
int ob_dpiOci__bindByPos(dpiStmt *stmt, void **bindHandle, uint32_t pos,
        int dynamicBind, dpiVar *var, dpiError *error)
{
    uint32_t mode = DPI_OCI_DEFAULT;
    int status;

    if (dynamicBind)
        mode |= DPI_OCI_DATA_AT_EXEC;
    DPI_OCI_LOAD_SYMBOL("OCIBindByPos", dpiOciSymbols.fnBindByPos)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnBindByPos)(stmt->handle, bindHandle,
            error->handle, pos, (dynamicBind) ? NULL : var->buffer.data.asRaw,
            (var->isDynamic) ? INT_MAX : (int32_t) var->sizeInBytes,
            var->type->oracleType, (dynamicBind) ? NULL :
                    var->buffer.indicator,
            (dynamicBind || var->type->sizeInBytes) ? NULL :
                    var->buffer.actualLength16,
            (dynamicBind) ? NULL : var->buffer.returnCode,
            (var->isArray) ? var->buffer.maxArraySize : 0,
            (var->isArray) ? &var->buffer.actualArraySize : NULL, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, stmt->conn, "bind by position");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__bindByPos2() [INTERNAL]
//   Wrapper for OCIBindByPos2().
//-----------------------------------------------------------------------------
int ob_dpiOci__bindByPos2(dpiStmt *stmt, void **bindHandle, uint32_t pos,
        int dynamicBind, dpiVar *var, dpiError *error)
{
    uint32_t mode = DPI_OCI_DEFAULT_BIND_MODE(stmt);
    int status;

    if (dynamicBind)
        mode |= DPI_OCI_DATA_AT_EXEC;
    DPI_OCI_LOAD_SYMBOL("OCIBindByPos2", dpiOciSymbols.fnBindByPos2)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnBindByPos2)(stmt->handle, bindHandle,
            error->handle, pos, (dynamicBind) ? NULL : var->buffer.data.asRaw,
            (var->isDynamic) ? INT_MAX : var->sizeInBytes,
            var->type->oracleType, (dynamicBind) ? NULL :
                    var->buffer.indicator,
            (dynamicBind || var->type->sizeInBytes) ? NULL :
                    var->buffer.actualLength32,
            (dynamicBind) ? NULL : var->buffer.returnCode,
            (var->isArray) ? var->buffer.maxArraySize : 0,
            (var->isArray) ? &var->buffer.actualArraySize : NULL, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, stmt->conn, "bind by position");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__bindDynamic() [INTERNAL]
//   Wrapper for OCIBindDynamic().
//-----------------------------------------------------------------------------
int ob_dpiOci__bindDynamic(dpiVar *var, void *bindHandle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIBindDynamic", dpiOciSymbols.fnBindDynamic)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnBindDynamic)(bindHandle, error->handle, var,
            (void*) ob_dpiVar__inBindCallback, var,
            (void*) ob_dpiVar__outBindCallback);
    DPI_OCI_CHECK_AND_RETURN(error, status, var->conn, "bind dynamic");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__bindObject() [INTERNAL]
//   Wrapper for OCIBindObject().
//-----------------------------------------------------------------------------
int ob_dpiOci__bindObject(dpiVar *var, void *bindHandle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIBindObject", dpiOciSymbols.fnBindObject)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnBindObject)(bindHandle, error->handle,
            var->objectType->tdo, (void**) var->buffer.data.asRaw, 0,
            var->buffer.objectIndicator, 0);
    DPI_OCI_CHECK_AND_RETURN(error, status, var->conn, "bind object");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__break() [INTERNAL]
//   Wrapper for OCIBreak().
//-----------------------------------------------------------------------------
int ob_dpiOci__break(dpiConn *conn, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIBreak", dpiOciSymbols.fnBreak)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnBreak)(conn->handle, error->handle);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "break execution");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__collAppend() [INTERNAL]
//   Wrapper for OCICollAppend().
//-----------------------------------------------------------------------------
int ob_dpiOci__collAppend(dpiConn *conn, const void *elem, const void *elemInd,
        void *coll, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCICollAppend", dpiOciSymbols.fnCollAppend)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnCollAppend)(conn->env->handle, error->handle,
            elem, elemInd, coll);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "append element");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__collAssignElem() [INTERNAL]
//   Wrapper for OCICollAssignElem().
//-----------------------------------------------------------------------------
int ob_dpiOci__collAssignElem(dpiConn *conn, int32_t index, const void *elem,
        const void *elemInd, void *coll, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCICollAssignElem", dpiOciSymbols.fnCollAssignElem)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnCollAssignElem)(conn->env->handle,
            error->handle, index, elem, elemInd, coll);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "assign element");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__collGetElem() [INTERNAL]
//   Wrapper for OCICollGetElem().
//-----------------------------------------------------------------------------
int ob_dpiOci__collGetElem(dpiConn *conn, void *coll, int32_t index, int *exists,
        void **elem, void **elemInd, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCICollGetElem", dpiOciSymbols.fnCollGetElem)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnCollGetElem)(conn->env->handle, error->handle,
            coll, index, exists, elem, elemInd);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "get element");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__collSize() [INTERNAL]
//   Wrapper for OCICollSize().
//-----------------------------------------------------------------------------
int ob_dpiOci__collSize(dpiConn *conn, void *coll, int32_t *size, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCICollSize", dpiOciSymbols.fnCollSize)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnCollSize)(conn->env->handle, error->handle,
            coll, size);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "get size");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__collTrim() [INTERNAL]
//   Wrapper for OCICollTrim().
//-----------------------------------------------------------------------------
int ob_dpiOci__collTrim(dpiConn *conn, uint32_t numToTrim, void *coll,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCICollTrim", dpiOciSymbols.fnCollTrim)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnCollTrim)(conn->env->handle, error->handle,
            (int32_t) numToTrim, coll);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "trim");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__contextGetValue() [INTERNAL]
//   Wrapper for OCIContextGetValue().
//-----------------------------------------------------------------------------
int ob_dpiOci__contextGetValue(dpiConn *conn, const char *key, uint32_t keyLength,
        void **value, int checkError, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIContextGetValue", dpiOciSymbols.fnContextGetValue)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnContextGetValue)(conn->sessionHandle,
            error->handle, key, (uint8_t) keyLength, value);
    if (!checkError)
        return DPI_SUCCESS;
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "get context value");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__contextSetValue() [INTERNAL]
//   Wrapper for OCIContextSetValue().
//-----------------------------------------------------------------------------
int ob_dpiOci__contextSetValue(dpiConn *conn, const char *key, uint32_t keyLength,
        void *value, int checkError, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIContextSetValue", dpiOciSymbols.fnContextSetValue)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnContextSetValue)(conn->sessionHandle,
            error->handle, DPI_OCI_DURATION_SESSION, key, (uint8_t) keyLength,
            value);
    if (!checkError)
        return DPI_SUCCESS;
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "set context value");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__dateTimeConstruct() [INTERNAL]
//   Wrapper for OCIDateTimeConstruct().
//-----------------------------------------------------------------------------
int ob_dpiOci__dateTimeConstruct(void *envHandle, void *handle, int16_t year,
        uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
        uint8_t second, uint32_t fsecond, const char *tz, size_t tzLength,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDateTimeConstruct",
            dpiOciSymbols.fnDateTimeConstruct)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDateTimeConstruct)(envHandle, error->handle,
            handle, year, month, day, hour, minute, second, fsecond, tz,
            tzLength);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "construct date");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__dateTimeConvert() [INTERNAL]
//   Wrapper for OCIDateTimeConvert().
//-----------------------------------------------------------------------------
int ob_dpiOci__dateTimeConvert(void *envHandle, void *inDate, void *outDate,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDateTimeConvert", dpiOciSymbols.fnDateTimeConvert)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDateTimeConvert)(envHandle, error->handle,
            inDate, outDate);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "convert date");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__dateTimeGetDate() [INTERNAL]
//   Wrapper for OCIDateTimeGetDate().
//-----------------------------------------------------------------------------
int ob_dpiOci__dateTimeGetDate(void *envHandle, void *handle, int16_t *year,
        uint8_t *month, uint8_t *day, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDateTimeGetDate", dpiOciSymbols.fnDateTimeGetDate)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDateTimeGetDate)(envHandle, error->handle,
            handle, year, month, day);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "get date portion");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__dateTimeGetTime() [INTERNAL]
//   Wrapper for OCIDateTimeGetTime().
//-----------------------------------------------------------------------------
int ob_dpiOci__dateTimeGetTime(void *envHandle, void *handle, uint8_t *hour,
        uint8_t *minute, uint8_t *second, uint32_t *fsecond, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDateTimeGetTime", dpiOciSymbols.fnDateTimeGetTime)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDateTimeGetTime)(envHandle, error->handle,
            handle, hour, minute, second, fsecond);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "get time portion");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__dateTimeGetTimeZoneOffset() [INTERNAL]
//   Wrapper for OCIDateTimeGetTimeZoneOffset().
//-----------------------------------------------------------------------------
int ob_dpiOci__dateTimeGetTimeZoneOffset(void *envHandle, void *handle,
        int8_t *tzHourOffset, int8_t *tzMinuteOffset, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDateTimeGetTimeZoneOffset",
            dpiOciSymbols.fnDateTimeGetTimeZoneOffset)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDateTimeGetTimeZoneOffset)(envHandle,
            error->handle, handle, tzHourOffset, tzMinuteOffset);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "get time zone portion");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__dateTimeIntervalAdd() [INTERNAL]
//   Wrapper for OCIDateTimeIntervalAdd().
//-----------------------------------------------------------------------------
int ob_dpiOci__dateTimeIntervalAdd(void *envHandle, void *handle, void *interval,
        void *outHandle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDateTimeIntervalAdd",
            dpiOciSymbols.fnDateTimeIntervalAdd)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDateTimeIntervalAdd)(envHandle, error->handle,
            handle, interval, outHandle);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "add interval to date");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__dateTimeSubtract() [INTERNAL]
//   Wrapper for OCIDateTimeSubtract().
//-----------------------------------------------------------------------------
int ob_dpiOci__dateTimeSubtract(void *envHandle, void *handle1, void *handle2,
        void *interval, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDateTimeSubtract",
            dpiOciSymbols.fnDateTimeSubtract)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDateTimeSubtract)(envHandle, error->handle,
            handle1, handle2, interval);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "subtract date");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__dbShutdown() [INTERNAL]
//   Wrapper for OCIDBShutdown().
//-----------------------------------------------------------------------------
int ob_dpiOci__dbShutdown(dpiConn *conn, uint32_t mode, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDBShutdown", dpiOciSymbols.fnDbShutdown)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDbShutdown)(conn->handle, error->handle, NULL,
            mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "shutdown database");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__dbStartup() [INTERNAL]
//   Wrapper for OCIDBStartup().
//-----------------------------------------------------------------------------
int ob_dpiOci__dbStartup(dpiConn *conn, void *adminHandle, uint32_t mode,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDBStartup", dpiOciSymbols.fnDbStartup)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDbStartup)(conn->handle, error->handle,
            adminHandle, DPI_OCI_DEFAULT, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "startup database");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__defineByPos() [INTERNAL]
//   Wrapper for OCIDefineByPos().
//-----------------------------------------------------------------------------
int ob_dpiOci__defineByPos(dpiStmt *stmt, void **defineHandle, uint32_t pos,
        dpiVar *var, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDefineByPos", dpiOciSymbols.fnDefineByPos)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDefineByPos)(stmt->handle, defineHandle,
            error->handle, pos, (var->isDynamic) ? NULL :
                    var->buffer.data.asRaw,
            (var->isDynamic) ? INT_MAX : (int32_t) var->sizeInBytes,
            var->type->oracleType, (var->isDynamic) ? NULL :
                    var->buffer.indicator,
            (var->isDynamic) ? NULL : var->buffer.actualLength16,
            (var->isDynamic) ? NULL : var->buffer.returnCode,
            (var->isDynamic) ? DPI_OCI_DYNAMIC_FETCH : DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, stmt->conn, "define");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__defineByPos2() [INTERNAL]
//   Wrapper for OCIDefineByPos2().
//-----------------------------------------------------------------------------
int ob_dpiOci__defineByPos2(dpiStmt *stmt, void **defineHandle, uint32_t pos,
        dpiVar *var, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDefineByPos2", dpiOciSymbols.fnDefineByPos2)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDefineByPos2)(stmt->handle, defineHandle,
            error->handle, pos, (var->isDynamic) ? NULL :
                    var->buffer.data.asRaw,
            (var->isDynamic) ? INT_MAX : var->sizeInBytes,
            var->type->oracleType, (var->isDynamic) ? NULL :
                    var->buffer.indicator,
            (var->isDynamic) ? NULL : var->buffer.actualLength32,
            (var->isDynamic) ? NULL : var->buffer.returnCode,
            (var->isDynamic) ? DPI_OCI_DYNAMIC_FETCH : DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, stmt->conn, "define");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__defineDynamic() [INTERNAL]
//   Wrapper for OCIDefineDynamic().
//-----------------------------------------------------------------------------
int ob_dpiOci__defineDynamic(dpiVar *var, void *defineHandle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDefineDynamic", dpiOciSymbols.fnDefineDynamic)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDefineDynamic)(defineHandle, error->handle, var,
            (void*) ob_dpiVar__defineCallback);
    DPI_OCI_CHECK_AND_RETURN(error, status, var->conn, "define dynamic");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__defineObject() [INTERNAL]
//   Wrapper for OCIDefineObject().
//-----------------------------------------------------------------------------
int ob_dpiOci__defineObject(dpiVar *var, void *defineHandle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDefineObject", dpiOciSymbols.fnDefineObject)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDefineObject)(defineHandle, error->handle,
            var->objectType->tdo, (void**) var->buffer.data.asRaw, 0,
            var->buffer.objectIndicator, 0);
    DPI_OCI_CHECK_AND_RETURN(error, status, var->conn, "define object");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__describeAny() [INTERNAL]
//   Wrapper for OCIDescribeAny().
//-----------------------------------------------------------------------------
int ob_dpiOci__describeAny(dpiConn *conn, void *obj, uint32_t objLength,
        uint8_t objType, void *describeHandle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDescribeAny", dpiOciSymbols.fnDescribeAny)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnDescribeAny)(conn->handle, error->handle, obj,
            objLength, objType, 0, DPI_OCI_PTYPE_TYPE, describeHandle);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "describe type");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__descriptorAlloc() [INTERNAL]
//   Wrapper for OCIDescriptorAlloc().
//-----------------------------------------------------------------------------
int ob_dpiOci__descriptorAlloc(void *envHandle, void **handle,
        const uint32_t handleType, const char *action, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDescriptorAlloc", dpiOciSymbols.fnDescriptorAlloc)
    status = (*dpiOciSymbols.fnDescriptorAlloc)(envHandle, handle, handleType,
            0, NULL);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, action);
}


//-----------------------------------------------------------------------------
// ob_dpiOci__descriptorFree() [INTERNAL]
//   Wrapper for OCIDescriptorFree().
//-----------------------------------------------------------------------------
int ob_dpiOci__descriptorFree(void *handle, uint32_t handleType)
{
    dpiError *error = NULL;
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIDescriptorFree", dpiOciSymbols.fnDescriptorFree)
    status = (*dpiOciSymbols.fnDescriptorFree)(handle, handleType);
    if (status != DPI_OCI_SUCCESS &&
            ob_dpiDebugLevel & DPI_DEBUG_LEVEL_UNREPORTED_ERRORS)
        ob_dpiDebug__print("free descriptor %p, type %d failed\n", handle,
                handleType);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__envNlsCreate() [INTERNAL]
//   Wrapper for OCIEnvNlsCreate().
//-----------------------------------------------------------------------------
int ob_dpiOci__envNlsCreate(void **envHandle, uint32_t mode, uint16_t charsetId,
        uint16_t ncharsetId, dpiError *error)
{
    void *mallocFn = NULL, *reallocFn = NULL, *freeFn = NULL;
    int status;

    *envHandle = NULL;
    DPI_OCI_LOAD_SYMBOL("OCIEnvNlsCreate", dpiOciSymbols.fnEnvNlsCreate)
    if (ob_dpiDebugLevel & DPI_DEBUG_LEVEL_MEM) {
        mallocFn = (void*) ob_dpiOci__allocateMem;
        reallocFn = (void*) ob_dpiOci__reallocMem;
        freeFn = (void*) ob_dpiOci__freeMem;
    }
    status = (*dpiOciSymbols.fnEnvNlsCreate)(envHandle, mode, NULL, mallocFn,
            reallocFn, freeFn, 0, NULL, charsetId, ncharsetId);
    if (*envHandle) {
        if (status == DPI_OCI_SUCCESS || status == DPI_OCI_SUCCESS_WITH_INFO)
            return DPI_SUCCESS;
        if (ob_dpiOci__errorGet(*envHandle, DPI_OCI_HTYPE_ENV, charsetId,
                "create env", error) == 0)
            return DPI_FAILURE;
    }
    return ob_dpiError__set(error, "create env", DPI_ERR_CREATE_ENV);
}


//-----------------------------------------------------------------------------
// ob_dpiOci__errorGet() [INTERNAL]
//   Wrapper for OCIErrorGet().
//-----------------------------------------------------------------------------
int ob_dpiOci__errorGet(void *handle, uint32_t handleType, uint16_t charsetId,
        const char *action, dpiError *error)
{
    uint32_t i, numChars, bufferChars;
    uint16_t *utf16chars;
    int status;
    char *ptr;

    DPI_OCI_LOAD_SYMBOL("OCIErrorGet", dpiOciSymbols.fnErrorGet)
    status = (*dpiOciSymbols.fnErrorGet)(handle, 1, NULL, &error->buffer->code,
            error->buffer->message, sizeof(error->buffer->message),
            handleType);
    if (status != DPI_OCI_SUCCESS)
        return ob_dpiError__set(error, action, DPI_ERR_GET_FAILED);
    error->buffer->action = action;

    // determine length of message since OCI does not provide this information;
    // all encodings except UTF-16 can use normal string processing; cannot use
    // type whar_t for processing UTF-16, though, as its size may be 4 on some
    // platforms, not 2; also strip trailing whitespace from error messages
    if (charsetId == DPI_CHARSET_ID_UTF16) {
        numChars = 0;
        utf16chars = (uint16_t*) error->buffer->message;
        bufferChars = sizeof(error->buffer->message) / 2;
        for (i = 0; i < bufferChars; i++) {
            if (utf16chars[i] == 0)
                break;
            if (utf16chars[i] > 127 || !isspace(utf16chars[i]))
                numChars = i + 1;
        }
        error->buffer->messageLength = numChars * 2;
    } else {
        error->buffer->messageLength =
                (uint32_t) strlen(error->buffer->message);
        ptr = error->buffer->message + error->buffer->messageLength - 1;
        while (ptr > error->buffer->message && isspace((uint8_t) *ptr--))
            error->buffer->messageLength--;
    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__freeMem() [INTERNAL]
//   Wrapper for OCI allocation of memory, only used when debugging memory
// allocation.
//-----------------------------------------------------------------------------
static void ob_dpiOci__freeMem(UNUSED void *unused, void *ptr)
{
    char message[40];

    (void) sprintf(message, "OCI freed ptr at %p", ptr);
    free(ptr);
    ob_dpiDebug__print("%s\n", message);
}


//-----------------------------------------------------------------------------
// ob_dpiOci__handleAlloc() [INTERNAL]
//   Wrapper for OCIHandleAlloc().
//-----------------------------------------------------------------------------
int ob_dpiOci__handleAlloc(void *envHandle, void **handle, uint32_t handleType,
        const char *action, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIHandleAlloc", dpiOciSymbols.fnHandleAlloc)
    status = (*dpiOciSymbols.fnHandleAlloc)(envHandle, handle, handleType, 0,
            NULL);
    if (handleType == DPI_OCI_HTYPE_ERROR && status != DPI_OCI_SUCCESS)
        return ob_dpiError__set(error, action, DPI_ERR_NO_MEMORY);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, action);
}


//-----------------------------------------------------------------------------
// ob_dpiOci__handleFree() [INTERNAL]
//   Wrapper for OCIHandleFree().
//-----------------------------------------------------------------------------
int ob_dpiOci__handleFree(void *handle, uint32_t handleType)
{
    dpiError *error = NULL;
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIHandleFree", dpiOciSymbols.fnHandleFree)
    status = (*dpiOciSymbols.fnHandleFree)(handle, handleType);
    if (status != DPI_OCI_SUCCESS &&
            ob_dpiDebugLevel & DPI_DEBUG_LEVEL_UNREPORTED_ERRORS)
        ob_dpiDebug__print("free handle %p, handleType %d failed\n", handle,
                handleType);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__intervalGetDaySecond() [INTERNAL]
//   Wrapper for OCIIntervalGetDaySecond().
//-----------------------------------------------------------------------------
int ob_dpiOci__intervalGetDaySecond(void *envHandle, int32_t *day, int32_t *hour,
        int32_t *minute, int32_t *second, int32_t *fsecond,
        const void *interval, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIIntervalGetDaySecond",
            dpiOciSymbols.fnIntervalGetDaySecond)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnIntervalGetDaySecond)(envHandle,
            error->handle, day, hour, minute, second, fsecond, interval);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "get interval components");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__intervalGetYearMonth() [INTERNAL]
//   Wrapper for OCIIntervalGetYearMonth().
//-----------------------------------------------------------------------------
int ob_dpiOci__intervalGetYearMonth(void *envHandle, int32_t *year,
        int32_t *month, const void *interval, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIIntervalGetYearMonth",
            dpiOciSymbols.fnIntervalGetYearMonth)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnIntervalGetYearMonth)(envHandle, error->handle,
            year, month, interval);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "get interval components");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__intervalSetDaySecond() [INTERNAL]
//   Wrapper for OCIIntervalSetDaySecond().
//-----------------------------------------------------------------------------
int ob_dpiOci__intervalSetDaySecond(void *envHandle, int32_t day, int32_t hour,
        int32_t minute, int32_t second, int32_t fsecond, void *interval,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIIntervalSetDaySecond",
            dpiOciSymbols.fnIntervalSetDaySecond)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnIntervalSetDaySecond)(envHandle, error->handle,
            day, hour, minute, second, fsecond, interval);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "set interval components");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__intervalSetYearMonth() [INTERNAL]
//   Wrapper for OCIIntervalSetYearMonth().
//-----------------------------------------------------------------------------
int ob_dpiOci__intervalSetYearMonth(void *envHandle, int32_t year, int32_t month,
        void *interval, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIIntervalSetYearMonth",
            dpiOciSymbols.fnIntervalSetYearMonth)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnIntervalSetYearMonth)(envHandle, error->handle,
            year, month, interval);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "set interval components");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__jsonDomDocGet() [INTERNAL]
//   Wrapper for OCIJsonDomDocGet().
//-----------------------------------------------------------------------------
int ob_dpiOci__jsonDomDocGet(dpiJson *json, dpiJznDomDoc **domDoc,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIJsonDomDocGet", dpiOciSymbols.fnJsonDomDocGet)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnJsonDomDocGet)(json->conn->handle, json->handle,
            domDoc, error->handle, DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, json->conn, "get JSON DOM doc");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__jsonTextBufferParse() [INTERNAL]
//   Wrapper for OCIJsonTextBufferParse().
//-----------------------------------------------------------------------------
int ob_dpiOci__jsonTextBufferParse(dpiJson *json, const char *value,
        uint64_t valueLength, uint32_t flags, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIJsonTextBufferParse",
            dpiOciSymbols.fnJsonTextBufferParse)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnJsonTextBufferParse)(json->conn->handle,
            json->handle, (void*) value, valueLength,
            (DPI_JZN_ALLOW_SCALAR_DOCUMENTS | flags), DPI_JZN_INPUT_UTF8,
            error->handle, DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, json->conn, "parse JSON text");
}


#ifdef _WIN32

//-----------------------------------------------------------------------------
// ob_dpiOci__checkDllArchitecture() [INTERNAL]
//   Check the architecture of the specified DLL name and check if it
// matches the expected architecture. If it does not, the load error is
// modified and DPI_SUCCESS is returned; otherwise, DPI_FAILURE is returned.
//-----------------------------------------------------------------------------
static int ob_dpiOci__checkDllArchitecture(dpiOciLoadLibParams *loadParams,
        const char *name, dpiError *error)
{
    const char *errorFormat = "%s is not the correct architecture";
    IMAGE_DOS_HEADER dosHeader;
    IMAGE_NT_HEADERS ntHeaders;
    FILE *fp;

    // check DLL architecture
    fp = fopen(name, "rb");
    if (!fp)
        return DPI_FAILURE;
    fread(&dosHeader, sizeof(dosHeader), 1, fp);
    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        fclose(fp);
        return DPI_FAILURE;
    }
    fseek(fp, dosHeader.e_lfanew, SEEK_SET);
    fread(&ntHeaders, sizeof(ntHeaders), 1, fp);
    fclose(fp);
    if (ntHeaders.Signature != IMAGE_NT_SIGNATURE)
        return DPI_FAILURE;

#if defined _M_AMD64
    if (ntHeaders.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64)
        return DPI_FAILURE;
#elif defined _M_IX86
    if (ntHeaders.FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
        return DPI_FAILURE;
#else
    return DPI_FAILURE;
#endif

    // store a modified error in the error buffer
    if (ob_dpiUtils__ensureBuffer(strlen(errorFormat) + strlen(name) + 1,
            "allocate wrong architecture load error buffer",
            (void**) &loadParams->errorBuffer,
            &loadParams->errorBufferLength, error) < 0)
        return DPI_FAILURE;
    sprintf(loadParams->errorBuffer, errorFormat, name);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__getEnv() [INTERNAL]
//   Gets the value of the environment variable with the given name. If the
// environment variable is not found, NULL is returned. On Windows, a buffer is
// required.
//-----------------------------------------------------------------------------
static char *ob_dpiOci__getEnv(dpiOciLoadLibParams *loadParams, const char *name)
{
    DWORD numBytes, actualNumBytes;

    // call the first time to get the length; if the environment variable is
    // not found, NULL is returned
    numBytes = GetEnvironmentVariable(name, NULL, 0);
    if (numBytes == 0)
        return NULL;

    // ensure the buffer is large enough to receive the contents
    if (ob_dpiUtils__ensureBuffer(numBytes + 1, "allocate environment variable",
            (void**) &loadParams->envBuffer, &loadParams->envBufferLength,
            NULL) < 0)
        return NULL;

    // call a second time to get the value
    actualNumBytes = GetEnvironmentVariable(name, loadParams->envBuffer,
            (DWORD) loadParams->envBufferLength);
    if (actualNumBytes + 1 != numBytes)
        return NULL;

    return loadParams->envBuffer;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__getModuleDir() [INTERNAL]
//   Attempts to get the directory of the module from the given function
// pointer. This is platform specific.
//-----------------------------------------------------------------------------
static int ob_dpiOci__getModuleDir(void *fn, const char *moduleType,
        char **nameBuffer, size_t *nameBufferLength, dpiError *error)
{
    HMODULE module = NULL;
    DWORD result = 0;
    char *temp;

    // attempt to get the module handle from a known function pointer
    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            (LPCSTR) fn, &module) == 0)
        return DPI_FAILURE;

    // attempt to get the module name from the module; the size of the buffer
    // is increased as needed as there is no other known way to acquire the
    // full name (MAX_PATH is no longer the maximum path length)
    if (ob_dpiUtils__ensureBuffer(MAX_PATH, "allocate module name",
            (void**) nameBuffer, nameBufferLength, error) < 0) {
        FreeLibrary(module);
        return DPI_FAILURE;
    }
    while (1) {
        result = GetModuleFileName(module, *nameBuffer,
                (DWORD) *nameBufferLength);
        if (result < (DWORD) *nameBufferLength)
            break;
        if (ob_dpiUtils__ensureBuffer(*nameBufferLength * 2,
                "allocate module name", (void**) nameBuffer, nameBufferLength,
                error) < 0) {
            FreeLibrary(module);
            return DPI_FAILURE;
        }
    }
    FreeLibrary(module);
    if (result == 0)
        return DPI_FAILURE;
    if (ob_dpiDebugLevel & DPI_DEBUG_LEVEL_LOAD_LIB)
        ob_dpiDebug__print("%s module name is %s\n", moduleType, *nameBuffer);

    // strip off the module name and only return the directory name
    temp = strrchr(*nameBuffer, '\\');
    if (temp) {
        *temp = '\0';
        return DPI_SUCCESS;
    }

    return DPI_FAILURE;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__findAndCheckDllArchitecture() [INTERNAL]
//   Attempt to find the specified DLL name using the standard search path and
// if the DLL can be found but is of the wrong architecture, include the full
// name of the DLL in the load error. Return DPI_SUCCESS if such a DLL was
// found and was of the wrong architecture (in which case the load error has
// been set); otherwise, return DPI_FAILURE so that the normal load error can
// be determined.
//-----------------------------------------------------------------------------
static int ob_dpiOci__findAndCheckDllArchitecture(dpiOciLoadLibParams *loadParams,
        const char *name, dpiError *error)
{
    DWORD bufferLength;
    char *temp, *path;
    size_t length;
    int status;

    // if the name of the DLL is an absolute path, check it directly
    temp = strchr(name, '\\');
    if (temp)
        return ob_dpiOci__checkDllArchitecture(loadParams, name, error);

    // check current directory
    bufferLength = GetCurrentDirectory(0, NULL);
    if (bufferLength == 0)
        return DPI_FAILURE;
    if (ob_dpiUtils__ensureBuffer(strlen(name) + 1 + bufferLength,
            "allocate load params name buffer (current dir)",
            (void**) &loadParams->nameBuffer,
            &loadParams->nameBufferLength, error) < 0)
        return DPI_FAILURE;
    if (GetCurrentDirectory(bufferLength, loadParams->nameBuffer) == 0)
        return DPI_FAILURE;
    temp = loadParams->nameBuffer + strlen(loadParams->nameBuffer);
    *temp++ = '\\';
    strcpy(temp, name);
    status = ob_dpiOci__checkDllArchitecture(loadParams, loadParams->nameBuffer,
            error);

    // search PATH
    path = ob_dpiOci__getEnv(loadParams, "PATH");
    if (path) {
        while (status < 0) {
            temp = strchr(path, ';');
            if (temp) {
                length = temp - path;
            } else {
                length = strlen(path);
            }
            if (ob_dpiUtils__ensureBuffer(strlen(name) + length + 2,
                    "allocate load params name buffer (PATH)",
                    (void**) &loadParams->nameBuffer,
                    &loadParams->nameBufferLength, error) < 0)
                return DPI_FAILURE;
            (void) sprintf(loadParams->nameBuffer, "%.*s\\%s", (int) length,
                    path, name);
            status = ob_dpiOci__checkDllArchitecture(loadParams,
                    loadParams->nameBuffer, error);
            if (!temp)
                break;
            path = temp + 1;
        }
    }

    return status;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__loadLibWithName() [INTERNAL]
//   Platform specific method of loading the library with a specific name.
// Load errors are stored in the temporary load error buffer and do not cause
// the function to fail; other errors (such as memory allocation errors) will
// result in failure.
//-----------------------------------------------------------------------------
static int ob_dpiOci__loadLibWithName(dpiOciLoadLibParams *loadParams,
        const char *name, dpiError *error)
{
    DWORD errorNum;

    // attempt to load the library
    loadParams->handle = LoadLibrary(name);
    if (loadParams->handle)
        return DPI_SUCCESS;

    // if DLL is of the wrong architecture, attempt to locate the DLL that was
    // loaded and use that information if it can be found
    errorNum = GetLastError();
    if (errorNum == ERROR_BAD_EXE_FORMAT &&
            ob_dpiOci__findAndCheckDllArchitecture(loadParams, name, error) == 0)
        return DPI_SUCCESS;

    // otherwise, attempt to get the error message
    return ob_dpiUtils__getWindowsError(errorNum, &loadParams->errorBuffer,
            &loadParams->errorBufferLength, error);
}


// for platforms other than Windows
#else

//-----------------------------------------------------------------------------
// ob_dpiOci__getEnv() [INTERNAL]
//   Gets the value of the environment variable with the given name. If the
// environment variable is not found, NULL is returned.
//-----------------------------------------------------------------------------
static char *ob_dpiOci__getEnv(UNUSED dpiOciLoadLibParams *loadParams,
        const char *name)
{
    return getenv(name);
}


//-----------------------------------------------------------------------------
// ob_dpiOci__getModuleDir() [INTERNAL]
//   Attempts to get the directory of the module from the given function
// pointer. This is platform specific.
//-----------------------------------------------------------------------------
static int ob_dpiOci__getModuleDir(void *fn, const char *moduleType,
        char **nameBuffer, size_t *nameBufferLength, dpiError *error)
{
#ifndef _AIX
    Dl_info info;
    char *temp;

    if (dladdr(fn, &info) != 0) {
        if (ob_dpiDebugLevel & DPI_DEBUG_LEVEL_LOAD_LIB)
            ob_dpiDebug__print("%s module name is %s\n", moduleType,
                    info.dli_fname);
        if (ob_dpiUtils__ensureBuffer(strlen(info.dli_fname) + 1,
                "allocate module name", (void**) nameBuffer,
                nameBufferLength, error) < 0)
            return DPI_FAILURE;
        strcpy(*nameBuffer, info.dli_fname);
        temp = strrchr(*nameBuffer, '/');
        if (temp) {
            *temp = '\0';
            return DPI_SUCCESS;
        }
    }
#endif

    return DPI_FAILURE;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__loadLibWithName() [INTERNAL]
//   Platform specific method of loading the library with a specific name.
// Load errors are stored in the temporary load error buffer and do not cause
// the function to fail; other errors (such as memory allocation errors) will
// result in failure.
//-----------------------------------------------------------------------------
static int ob_dpiOci__loadLibWithName(dpiOciLoadLibParams *loadParams,
        const char *libName, dpiError *error)
{
    char *osError;

    loadParams->handle = dlopen(libName, RTLD_LAZY);
    if (!loadParams->handle) {
        osError = dlerror();
        if (ob_dpiUtils__ensureBuffer(strlen(osError) + 1,
                "allocate load error buffer",
                (void**) &loadParams->errorBuffer,
                &loadParams->errorBufferLength, error) < 0)
            return DPI_FAILURE;
        strcpy(loadParams->errorBuffer, osError);
    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__loadLibWithOracleHome() [INTERNAL]
//   Attempts to load the library from the lib subdirectory of an Oracle home
// pointed to by the environemnt variable ORACLE_HOME.
//-----------------------------------------------------------------------------
static int ob_dpiOci__loadLibWithOracleHome(dpiOciLoadLibParams *loadParams,
        dpiError *error)
{
    char *oracleHome, *oracleHomeLibDir;
    size_t oracleHomeLength;
    int status;

    // check environment variable; if not set, attempt cannot proceed
    oracleHome = ob_dpiOci__getEnv(loadParams, "OCEANBASE_HOME");
    if (!oracleHome)
        return DPI_FAILURE;

    // a zero-length directory is ignored
    oracleHomeLength = strlen(oracleHome);
    if (oracleHomeLength == 0)
        return DPI_FAILURE;

    // craft directory to search
    if (ob_dpiUtils__allocateMemory(1, oracleHomeLength + 5, 0,
            "allocate ORACLE_HOME dir name", (void**) &oracleHomeLibDir,
            error) < 0)
        return DPI_FAILURE;
    (void) sprintf(oracleHomeLibDir, "%s/lib", oracleHome);

    // perform search
    status = ob_dpiOci__loadLibWithDir(loadParams, oracleHomeLibDir,
           strlen(oracleHomeLibDir), 0, error);
    ob_dpiUtils__freeMemory(oracleHomeLibDir);
    return status;
}

#endif

//-----------------------------------------------------------------------------
// ob_dpiOci__calculateConfigDir() [INTERNAL]
//   Attempt to calculate the default configuration directory to use when
// locating configuration files. If the value cannot be calculated, no errors
// are raised.
//-----------------------------------------------------------------------------
static void ob_dpiOci__calculateConfigDir(dpiOciLoadLibParams *loadParams)
{
    size_t nameBufferLength = 0;
    char *nameBuffer = NULL;
    char *baseDir;
    int status;

    // first check to see if the environment variable TNS_ADMIN is set
    baseDir = ob_dpiOci__getEnv(loadParams, "TNS_ADMIN");
    if (baseDir) {
        status = ob_dpiUtils__allocateMemory(1, strlen(baseDir) + 1, 0,
                "allocate config dir", (void**) loadParams->configDir, NULL);
        if (status == DPI_SUCCESS)
            strcpy(*loadParams->configDir, baseDir);
        return;
    }

    // otherwise, check the environment variable ORACLE_HOME is set and if not,
    // look for the directory in which the Oracle Client library which has been
    // loaded
    baseDir = ob_dpiOci__getEnv(loadParams, "ORACLE_HOME");
    if (!baseDir) {
        status = ob_dpiOci__getModuleDir(dpiOciSymbols.fnThreadProcessInit,
                "OCI", &nameBuffer, &nameBufferLength, NULL);
        if (status == DPI_SUCCESS)
            baseDir = nameBuffer;
    }
    if (baseDir) {
        status = ob_dpiUtils__allocateMemory(1,
                strlen(baseDir) + strlen(dpiOciConfigSubDir) + 2, 0,
                "allocate config dir", (void**) loadParams->configDir, NULL);
        if (status == DPI_SUCCESS)
            sprintf(*loadParams->configDir, "%s/%s", baseDir,
                    dpiOciConfigSubDir);
    }
    if (nameBuffer)
        ob_dpiUtils__freeMemory(nameBuffer);
}


//-----------------------------------------------------------------------------
// ob_dpiOci__loadLibWithDir() [INTERNAL]
//   Helper function for loading the OCI library. If a directory is specified,
// that directory is searched; otherwise, an unqualfied search is performed
// using the normal OS library loading rules.
//-----------------------------------------------------------------------------
static int ob_dpiOci__loadLibWithDir(dpiOciLoadLibParams *loadParams,
        const char *dirName, size_t dirNameLength, int scanAllNames,
        dpiError *error)
{
    const char *searchName;
    size_t nameLength;
    int i;

    // report attempt with directory, if applicable
    if (dirName && ob_dpiDebugLevel & DPI_DEBUG_LEVEL_LOAD_LIB)
        ob_dpiDebug__print("load in dir %.*s\n", (int) dirNameLength, dirName);

    // iterate over all possible options
    for (i = 0; dpiOciLibNames[i]; i++) {

        // determine name to search
        if (!dirName) {
            searchName = dpiOciLibNames[i];
        } else {
            nameLength = strlen(dpiOciLibNames[i]) + dirNameLength + 2;
            if (ob_dpiUtils__ensureBuffer(nameLength, "allocate name buffer",
                    (void**) &loadParams->nameBuffer,
                    &loadParams->nameBufferLength, error) < 0)
                return DPI_FAILURE;
            (void) sprintf(loadParams->nameBuffer, "%.*s/%s",
                    (int) dirNameLength, dirName, dpiOciLibNames[i]);
            searchName = loadParams->nameBuffer;
        }

        // attempt to load the library using the calculated name; failure here
        // implies something other than a load failure and this error is
        // reported immediately
        if (ob_dpiDebugLevel & DPI_DEBUG_LEVEL_LOAD_LIB)
            ob_dpiDebug__print("load with name %s\n", searchName);
        if (ob_dpiOci__loadLibWithName(loadParams, searchName, error) < 0)
            return DPI_FAILURE;

        // success is also reported immediately
        if (loadParams->handle) {
            if (ob_dpiDebugLevel & DPI_DEBUG_LEVEL_LOAD_LIB)
                ob_dpiDebug__print("load by OS successful\n");
            return DPI_SUCCESS;
        }

        // load failed; store the first failure that occurs which will be
        // reported if no successful loads were made and no other errors took
        // place
        if (ob_dpiDebugLevel & DPI_DEBUG_LEVEL_LOAD_LIB)
            ob_dpiDebug__print("load by OS failure: %s\n",
                    loadParams->errorBuffer);
        if (i == 0) {
            if (ob_dpiUtils__ensureBuffer(loadParams->errorBufferLength,
                    "allocate load error buffer",
                    (void**) &loadParams->loadError,
                    &loadParams->loadErrorLength, error) < 0)
                return DPI_FAILURE;
            strcpy(loadParams->loadError, loadParams->errorBuffer);
            if (!scanAllNames)
                break;
        }

    }

    // no attempts were successful
    return DPI_FAILURE;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__loadLib() [INTERNAL]
//   Load the OCI library.
//-----------------------------------------------------------------------------
int ob_dpiOci__loadLib(dpiContextCreateParams *params,
        dpiVersionInfo *clientVersionInfo, char **configDir, dpiError *error)
{
    static const char *envNamesToCheck[] = {
        "ORACLE_HOME",
        "ORA_TZFILE",
        "TNS_ADMIN",
#ifdef _WIN32
        "PATH",
#else
        "LD_LIBRARY_PATH",
        "DYLD_LIBRARY_PATH",
        "LIBPATH",
        "SHLIB_PATH",
#endif
        NULL
    };
    dpiOciLoadLibParams loadLibParams;
    const char *temp;
    int status, i;

    // initialize loading parameters; these are used to provide space for
    // loading errors and the names that are being searched; memory is
    // allocated dynamically in order to avoid potential issues with long paths
    // on some platforms
    memset(&loadLibParams, 0, sizeof(loadLibParams));
    loadLibParams.configDir = configDir;

    // log the directory parameter values and any environment variables that
    // have an impact on loading the library
    if (ob_dpiDebugLevel & DPI_DEBUG_LEVEL_LOAD_LIB) {

        // first log directory parameter values
        ob_dpiDebug__print("Context Parameters:\n");
        if (params->oracleClientLibDir)
            ob_dpiDebug__print("    Oracle Client Lib Dir: %s\n",
                    params->oracleClientLibDir);
        if (params->oracleClientConfigDir)
            ob_dpiDebug__print("    Oracle Client Config Dir: %s\n",
                    params->oracleClientConfigDir);

        // now log environment variable values
        ob_dpiDebug__print("Environment Variables:\n");
        for (i = 0; envNamesToCheck[i]; i++) {
            temp = ob_dpiOci__getEnv(&loadLibParams, envNamesToCheck[i]);
            if (temp)
                ob_dpiDebug__print("    %s => \"%s\"\n", envNamesToCheck[i],
                        temp);
        }

    }

    // if a config directory was specified in the create params, set the
    // TNS_ADMIN environment variable
    if (params->oracleClientConfigDir) {
#ifdef _WIN32
        if (!SetEnvironmentVariable("TNS_ADMIN",
                    params->oracleClientConfigDir)) {
#else
        if (setenv("TNS_ADMIN", params->oracleClientConfigDir, 1) != 0) {
#endif
            return ob_dpiError__setFromOS(error,
                    "set TNS_ADMIN environment variable");
        }
    }

    // if a lib directory was specified in the create params, look for the OCI
    // library in that location only
    if (params->oracleClientLibDir) {
        if (ob_dpiDebugLevel & DPI_DEBUG_LEVEL_LOAD_LIB)
            ob_dpiDebug__print("load in parameter directory\n");
        status = ob_dpiOci__loadLibWithDir(&loadLibParams,
                params->oracleClientLibDir, strlen(params->oracleClientLibDir),
                1, error);

    // otherwise, use the normal loading mechanism
    } else {

        // first try the directory in which the ODPI-C library itself is found
        if (ob_dpiDebugLevel & DPI_DEBUG_LEVEL_LOAD_LIB)
            ob_dpiDebug__print("check ODPI-C module directory\n");
        status = ob_dpiOci__getModuleDir(ob_dpiContext_createWithParams,
                "ODPI-C", &loadLibParams.moduleNameBuffer,
                &loadLibParams.moduleNameBufferLength, error);
        if (status == DPI_SUCCESS)
            status = ob_dpiOci__loadLibWithDir(&loadLibParams,
                    loadLibParams.moduleNameBuffer,
                    strlen(loadLibParams.moduleNameBuffer), 0, error);

        // if that fails, try the default OS library loading mechanism
        if (status < 0) {
            if (ob_dpiDebugLevel & DPI_DEBUG_LEVEL_LOAD_LIB)
                ob_dpiDebug__print("load with OS search heuristics\n");
            status = ob_dpiOci__loadLibWithDir(&loadLibParams, NULL, 0, 1, error);
        }

#ifndef _WIN32
        // if that fails, on platforms other than Windows, attempt to load
        // from $ORACLE_HOME/lib
        if (status < 0) {
            if (ob_dpiDebugLevel & DPI_DEBUG_LEVEL_LOAD_LIB)
                ob_dpiDebug__print("check ORACLE_HOME\n");
            status = ob_dpiOci__loadLibWithOracleHome(&loadLibParams, error);
        }
#endif

    }

    // if no attempts succeeded and no other error was reported, craft the
    // error message that will be returned
    if (status < 0 && (int) error->buffer->errorNum == 0) {
        const char *bits = (sizeof(void*) == 8) ? "64" : "32";
        ob_dpiError__set(error, "load library", DPI_ERR_LOAD_LIBRARY,
                bits, loadLibParams.loadError, params->loadErrorUrl);
    }

    // validate library, if a library was loaded
    if (status == DPI_SUCCESS) {
        dpiOciLibHandle = loadLibParams.handle;
        status = ob_dpiOci__loadLibValidate(params, &loadLibParams,
                clientVersionInfo, error);
    }

    // free any memory that was allocated
    if (loadLibParams.nameBuffer)
        ob_dpiUtils__freeMemory(loadLibParams.nameBuffer);
    if (loadLibParams.moduleNameBuffer)
        ob_dpiUtils__freeMemory(loadLibParams.moduleNameBuffer);
    if (loadLibParams.loadError)
        ob_dpiUtils__freeMemory(loadLibParams.loadError);
    if (loadLibParams.errorBuffer)
        ob_dpiUtils__freeMemory(loadLibParams.errorBuffer);
    if (loadLibParams.envBuffer)
        ob_dpiUtils__freeMemory(loadLibParams.envBuffer);

    // free the library, if a library was loaded and any error occurred
    if (status < 0) {
        if (dpiOciLibHandle != NULL) {
#ifdef _WIN32
            FreeLibrary(dpiOciLibHandle);
#else
            dlclose(dpiOciLibHandle);
#endif
            dpiOciLibHandle = NULL;
        }
        memset(&dpiOciSymbols, 0, sizeof(dpiOciSymbols));
        return DPI_FAILURE;
    }

    // if no Oracle Client configuration directory was specified, set the
    // value to contain the calculated value instead
    if (!params->oracleClientConfigDir)
        params->oracleClientConfigDir = *configDir;

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__loadLibValidate() [INTERNAL]
//   Validate the OCI library after loading.
//-----------------------------------------------------------------------------
static int ob_dpiOci__loadLibValidate(dpiContextCreateParams *params,
        dpiOciLoadLibParams *loadParams, dpiVersionInfo *clientVersionInfo,
        dpiError *error)
{
    if (ob_dpiDebugLevel & DPI_DEBUG_LEVEL_LOAD_LIB)
        ob_dpiDebug__print("validating loaded library\n");

    // determine the OCI client version information
    if (ob_dpiOci__loadSymbol("OCIClientVersion",
            (void**) &dpiOciSymbols.fnClientVersion, NULL) < 0)
        return ob_dpiError__set(error, "load symbol OCIClientVersion",
                DPI_ERR_ORACLE_CLIENT_UNSUPPORTED);
    memset(clientVersionInfo, 0, sizeof(*clientVersionInfo));
    (*dpiOciSymbols.fnClientVersion)(&clientVersionInfo->versionNum,
            &clientVersionInfo->releaseNum,
            &clientVersionInfo->updateNum,
            &clientVersionInfo->portReleaseNum,
            &clientVersionInfo->portUpdateNum);
    if (clientVersionInfo->versionNum == 0)
        return ob_dpiError__set(error, "get OCI client version",
                DPI_ERR_ORACLE_CLIENT_UNSUPPORTED);
    clientVersionInfo->fullVersionNum = (uint32_t)
            DPI_ORACLE_VERSION_TO_NUMBER(clientVersionInfo->versionNum,
                    clientVersionInfo->releaseNum,
                    clientVersionInfo->updateNum,
                    clientVersionInfo->portReleaseNum,
                    clientVersionInfo->portUpdateNum);

    // OCI version must be a minimum of 11.2
    if (ob_dpiUtils__checkClientVersion(clientVersionInfo, 10, 2, error) < 0)
        return DPI_FAILURE;

    // initialize threading capability in the OCI library
    // this must be run prior to any other OCI threading calls
    DPI_OCI_LOAD_SYMBOL("OCIThreadProcessInit",
            dpiOciSymbols.fnThreadProcessInit)
    (*dpiOciSymbols.fnThreadProcessInit)();

    // load symbols for key functions which are called many times
    // this list should be kept as small as possible in order to avoid
    // overhead in looking up symbols at startup
    DPI_OCI_LOAD_SYMBOL("OCIAttrGet", dpiOciSymbols.fnAttrGet)
    DPI_OCI_LOAD_SYMBOL("OCIAttrSet", dpiOciSymbols.fnAttrSet)
    DPI_OCI_LOAD_SYMBOL("OCIThreadKeyGet", dpiOciSymbols.fnThreadKeyGet)

    // if a configuration directory is not supplied, calculate one, if possible
    if (!params->oracleClientConfigDir)
        ob_dpiOci__calculateConfigDir(loadParams);

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__loadSymbol() [INTERNAL]
//   Return the symbol for the function that is to be called. The symbol table
// is first consulted. If the symbol is not found there, it is looked up and
// then stored there so the next invocation does not have to perform the
// lookup.
//-----------------------------------------------------------------------------
static int ob_dpiOci__loadSymbol(const char *symbolName, void **symbol,
        dpiError *error)
{
#ifdef _WIN32
    *symbol = GetProcAddress(dpiOciLibHandle, symbolName);
#else
    *symbol = dlsym(dpiOciLibHandle, symbolName);
#endif
    if (!*symbol)
        return ob_dpiError__set(error, "get symbol", DPI_ERR_LOAD_SYMBOL,
                symbolName);

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobClose() [INTERNAL]
//   Wrapper for OCILobClose().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobClose(dpiLob *lob, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobClose", dpiOciSymbols.fnLobClose)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnLobClose)(lob->conn->handle, error->handle,
            lob->locator);
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "close LOB");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobCreateTemporary() [INTERNAL]
//   Wrapper for OCILobCreateTemporary().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobCreateTemporary(dpiLob *lob, dpiError *error)
{
    uint8_t lobType;
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobCreateTemporary",
            dpiOciSymbols.fnLobCreateTemporary)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    if (lob->type->oracleTypeNum == DPI_ORACLE_TYPE_BLOB)
        lobType = DPI_OCI_TEMP_BLOB;
    else lobType = DPI_OCI_TEMP_CLOB;
    status = (*dpiOciSymbols.fnLobCreateTemporary)(lob->conn->handle,
            error->handle, lob->locator, DPI_OCI_DEFAULT,
            lob->type->charsetForm, lobType, 1, DPI_OCI_DURATION_SESSION);
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "create temporary LOB");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobFileExists() [INTERNAL]
//   Wrapper for OCILobFileExists().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobFileExists(dpiLob *lob, int *exists, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobFileExists", dpiOciSymbols.fnLobFileExists)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnLobFileExists)(lob->conn->handle, error->handle,
            lob->locator, exists);
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "get file exists");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobFileGetName() [INTERNAL]
//   Wrapper for OCILobFileGetName().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobFileGetName(dpiLob *lob, char *dirAlias,
        uint16_t *dirAliasLength, char *name, uint16_t *nameLength,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobFileGetName", dpiOciSymbols.fnLobFileGetName)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnLobFileGetName)(lob->env->handle, error->handle,
            lob->locator, dirAlias, dirAliasLength, name, nameLength);
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "get LOB file name");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobFileSetName() [INTERNAL]
//   Wrapper for OCILobFileSetName().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobFileSetName(dpiLob *lob, const char *dirAlias,
        uint16_t dirAliasLength, const char *name, uint16_t nameLength,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobFileSetName", dpiOciSymbols.fnLobFileSetName)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnLobFileSetName)(lob->env->handle, error->handle,
            &lob->locator, dirAlias, dirAliasLength, name, nameLength);
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "set LOB file name");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobFreeTemporary() [INTERNAL]
//   Wrapper for OCILobFreeTemporary().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobFreeTemporary(dpiConn *conn, void *lobLocator, int checkError,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobFreeTemporary",
            dpiOciSymbols.fnLobFreeTemporary)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnLobFreeTemporary)(conn->handle,
            error->handle, lobLocator);
    if (!checkError)
        return DPI_SUCCESS;
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "free temporary LOB");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobGetChunkSize() [INTERNAL]
//   Wrapper for OCILobGetChunkSize().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobGetChunkSize(dpiLob *lob, uint32_t *size, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobGetChunkSize", dpiOciSymbols.fnLobGetChunkSize)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnLobGetChunkSize)(lob->conn->handle,
            error->handle, lob->locator, size);
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "get chunk size");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobGetLength2() [INTERNAL]
//   Wrapper for OCILobGetLength2().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobGetLength2(dpiLob *lob, uint64_t *size, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobGetLength2", dpiOciSymbols.fnLobGetLength2)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnLobGetLength2)(lob->conn->handle, error->handle,
            lob->locator, size);
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "get length");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobIsOpen() [INTERNAL]
//   Wrapper for OCILobIsOpen().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobIsOpen(dpiLob *lob, int *isOpen, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobIsOpen", dpiOciSymbols.fnLobIsOpen)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnLobIsOpen)(lob->conn->handle, error->handle,
            lob->locator, isOpen);
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "check is open");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobIsTemporary() [INTERNAL]
//   Wrapper for OCILobIsTemporary().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobIsTemporary(dpiLob *lob, int *isTemporary, int checkError,
        dpiError *error)
{
    int status;

    *isTemporary = 0;
    DPI_OCI_LOAD_SYMBOL("OCILobIsTemporary", dpiOciSymbols.fnLobIsTemporary)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnLobIsTemporary)(lob->env->handle, error->handle,
            lob->locator, isTemporary);
    if (!checkError)
        return DPI_SUCCESS;
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "check is temporary");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobLocatorAssign() [INTERNAL]
//   Wrapper for OCILobLocatorAssign().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobLocatorAssign(dpiLob *lob, void **copiedHandle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobLocatorAssign",
            dpiOciSymbols.fnLobLocatorAssign)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnLobLocatorAssign)(lob->conn->handle,
            error->handle, lob->locator, copiedHandle);
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "assign locator");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobOpen() [INTERNAL]
//   Wrapper for OCILobOpen().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobOpen(dpiLob *lob, dpiError *error)
{
    uint8_t mode;
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobOpen", dpiOciSymbols.fnLobOpen)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    mode = (lob->type->oracleTypeNum == DPI_ORACLE_TYPE_BFILE) ?
            DPI_OCI_LOB_READONLY : DPI_OCI_LOB_READWRITE;
    status = (*dpiOciSymbols.fnLobOpen)(lob->conn->handle, error->handle,
            lob->locator, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "close LOB");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobRead2() [INTERNAL]
//   Wrapper for OCILobRead2().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobRead2(dpiLob *lob, uint64_t offset, uint64_t *amountInBytes,
        uint64_t *amountInChars, char *buffer, uint64_t bufferLength,
        dpiError *error)
{
    uint16_t charsetId;
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobRead2", dpiOciSymbols.fnLobRead2)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    charsetId = (lob->type->charsetForm == DPI_SQLCS_NCHAR) ?
            lob->env->ncharsetId : lob->env->charsetId;
    status = (*dpiOciSymbols.fnLobRead2)(lob->conn->handle, error->handle,
            lob->locator, amountInBytes, amountInChars, offset, buffer,
            bufferLength, DPI_OCI_ONE_PIECE, NULL, NULL, charsetId,
            lob->type->charsetForm);
    if (status == DPI_OCI_NEED_DATA) {
        *amountInChars = 0;
        *amountInBytes = 0;
        return DPI_SUCCESS;
    }
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "read from LOB");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobTrim2() [INTERNAL]
//   Wrapper for OCILobTrim2().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobTrim2(dpiLob *lob, uint64_t newLength, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobTrim2", dpiOciSymbols.fnLobTrim2)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnLobTrim2)(lob->conn->handle, error->handle,
            lob->locator, newLength);
    if (status == DPI_OCI_INVALID_HANDLE)
        return ob_dpiOci__lobCreateTemporary(lob, error);
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "trim LOB");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__lobWrite2() [INTERNAL]
//   Wrapper for OCILobWrite2().
//-----------------------------------------------------------------------------
int ob_dpiOci__lobWrite2(dpiLob *lob, uint64_t offset, const char *value,
        uint64_t valueLength, dpiError *error)
{
    uint64_t lengthInBytes = valueLength, lengthInChars = 0;
    uint16_t charsetId;
    int status;

    DPI_OCI_LOAD_SYMBOL("OCILobWrite2", dpiOciSymbols.fnLobWrite2)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    charsetId = (lob->type->charsetForm == DPI_SQLCS_NCHAR) ?
            lob->env->ncharsetId : lob->env->charsetId;
    status = (*dpiOciSymbols.fnLobWrite2)(lob->conn->handle, error->handle,
            lob->locator, &lengthInBytes, &lengthInChars, offset, (void*) value,
            valueLength, DPI_OCI_ONE_PIECE, NULL, NULL, charsetId,
            lob->type->charsetForm);
    DPI_OCI_CHECK_AND_RETURN(error, status, lob->conn, "write to LOB");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__memoryAlloc() [INTERNAL]
//   Wrapper for OCIMemoryAlloc().
//-----------------------------------------------------------------------------
int ob_dpiOci__memoryAlloc(dpiConn *conn, void **ptr, uint32_t size,
        int checkError, dpiError *error)
{
    int status;

    *ptr = NULL;
    DPI_OCI_LOAD_SYMBOL("OCIMemoryAlloc", dpiOciSymbols.fnMemoryAlloc)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnMemoryAlloc)(conn->sessionHandle, error->handle,
            ptr, DPI_OCI_DURATION_SESSION, size, DPI_OCI_MEMORY_CLEARED);
    if (!checkError)
        return DPI_SUCCESS;
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "allocate memory");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__memoryFree() [INTERNAL]
//   Wrapper for OCIMemoryFree().
//-----------------------------------------------------------------------------
int ob_dpiOci__memoryFree(dpiConn *conn, void *ptr, dpiError *error)
{
    DPI_OCI_LOAD_SYMBOL("OCIMemoryFree", dpiOciSymbols.fnMemoryFree)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    (*dpiOciSymbols.fnMemoryFree)(conn->sessionHandle, error->handle, ptr);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__nlsCharSetConvert() [INTERNAL]
//   Wrapper for OCINlsCharSetConvert().
//-----------------------------------------------------------------------------
int ob_dpiOci__nlsCharSetConvert(void *envHandle, uint16_t destCharsetId,
        char *dest, size_t destLength, uint16_t sourceCharsetId,
        const char *source, size_t sourceLength, size_t *resultSize,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCINlsCharSetConvert",
            dpiOciSymbols.fnNlsCharSetConvert)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnNlsCharSetConvert)(envHandle, error->handle,
            destCharsetId, dest, destLength, sourceCharsetId, source,
            sourceLength, resultSize);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "convert text");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__nlsCharSetIdToName() [INTERNAL]
//   Wrapper for OCINlsCharSetIdToName().
//-----------------------------------------------------------------------------
int ob_dpiOci__nlsCharSetIdToName(void *envHandle, char *buf, size_t bufLength,
        uint16_t charsetId, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCINlsCharSetIdToName",
            dpiOciSymbols.fnNlsCharSetIdToName)
    status = (*dpiOciSymbols.fnNlsCharSetIdToName)(envHandle, buf, bufLength,
            charsetId);
    return (status == DPI_OCI_SUCCESS) ? DPI_SUCCESS : DPI_FAILURE;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__nlsCharSetNameToId() [INTERNAL]
//   Wrapper for OCINlsCharSetNameToId().
//-----------------------------------------------------------------------------
int ob_dpiOci__nlsCharSetNameToId(void *envHandle, const char *name,
        uint16_t *charsetId, dpiError *error)
{
    DPI_OCI_LOAD_SYMBOL("OCINlsCharSetNameToId",
            dpiOciSymbols.fnNlsCharSetNameToId)
    *charsetId = (*dpiOciSymbols.fnNlsCharSetNameToId)(envHandle, name);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__nlsEnvironmentVariableGet() [INTERNAL]
//   Wrapper for OCIEnvironmentVariableGet().
//-----------------------------------------------------------------------------
int ob_dpiOci__nlsEnvironmentVariableGet(uint16_t item, void *value,
        dpiError *error)
{
    size_t ignored;
    int status;

    DPI_OCI_LOAD_SYMBOL("OCINlsEnvironmentVariableGet",
            dpiOciSymbols.fnNlsEnvironmentVariableGet)
    status = (*dpiOciSymbols.fnNlsEnvironmentVariableGet)(value, 0, item, 0,
            &ignored);
    if (status != DPI_OCI_SUCCESS)
        return ob_dpiError__set(error, "get NLS environment variable",
                DPI_ERR_NLS_ENV_VAR_GET);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__nlsNameMap() [INTERNAL]
//   Wrapper for OCINlsNameMap().
//-----------------------------------------------------------------------------
int ob_dpiOci__nlsNameMap(void *envHandle, char *buf, size_t bufLength,
        const char *source, uint32_t flag, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCINlsNameMap", dpiOciSymbols.fnNlsNameMap)
    status = (*dpiOciSymbols.fnNlsNameMap)(envHandle, buf, bufLength, source,
            flag);
    return (status == DPI_OCI_SUCCESS) ? DPI_SUCCESS : DPI_FAILURE;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__nlsNumericInfoGet() [INTERNAL]
//   Wrapper for OCINlsNumericInfoGet().
//-----------------------------------------------------------------------------
int ob_dpiOci__nlsNumericInfoGet(void *envHandle, int32_t *value, uint16_t item,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCINlsNumericInfoGet",
            dpiOciSymbols.fnNlsNumericInfoGet)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnNlsNumericInfoGet)(envHandle, error->handle,
            value, item);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "get NLS info");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__numberFromInt() [INTERNAL]
//   Wrapper for OCINumberFromInt().
//-----------------------------------------------------------------------------
int ob_dpiOci__numberFromInt(const void *value, unsigned int valueLength,
        unsigned int flags, void *number, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCINumberFromInt", dpiOciSymbols.fnNumberFromInt)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnNumberFromInt)(error->handle, value,
            valueLength, flags, number);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "number from integer");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__numberFromReal() [INTERNAL]
//   Wrapper for OCINumberFromReal().
//-----------------------------------------------------------------------------
int ob_dpiOci__numberFromReal(const double value, void *number, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCINumberFromReal", dpiOciSymbols.fnNumberFromReal)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnNumberFromReal)(error->handle, &value,
            sizeof(double), number);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "number from real");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__numberToInt() [INTERNAL]
//   Wrapper for OCINumberToInt().
//-----------------------------------------------------------------------------
int ob_dpiOci__numberToInt(void *number, void *value, unsigned int valueLength,
        unsigned int flags, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCINumberToInt", dpiOciSymbols.fnNumberToInt)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnNumberToInt)(error->handle, number, valueLength,
            flags, value);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "number to integer");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__numberToReal() [INTERNAL]
//   Wrapper for OCINumberToReal().
//-----------------------------------------------------------------------------
int ob_dpiOci__numberToReal(double *value, void *number, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCINumberToReal", dpiOciSymbols.fnNumberToReal)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnNumberToReal)(error->handle, number,
            sizeof(double), value);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "number to real");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__objectCopy() [INTERNAL]
//   Wrapper for OCIObjectCopy().
//-----------------------------------------------------------------------------
int ob_dpiOci__objectCopy(dpiObject *obj, void *sourceInstance,
        void *sourceIndicator, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIObjectCopy", dpiOciSymbols.fnObjectCopy)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnObjectCopy)(obj->env->handle, error->handle,
            obj->type->conn->handle, sourceInstance, sourceIndicator,
            obj->instance, obj->indicator, obj->type->tdo,
            DPI_OCI_DURATION_SESSION, DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, obj->type->conn, "copy object");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__objectFree() [INTERNAL]
//   Wrapper for OCIObjectFree().
//-----------------------------------------------------------------------------
int ob_dpiOci__objectFree(void *envHandle, void *data, int checkError,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIObjectFree", dpiOciSymbols.fnObjectFree)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnObjectFree)(envHandle, error->handle, data,
            DPI_OCI_DEFAULT);
    if (checkError && DPI_OCI_ERROR_OCCURRED(status)) {
        ob_dpiError__setFromOCI(error, status, NULL, "free instance");

        // during the attempt to free, PL/SQL records fail with error
        // "ORA-21602: operation does not support the specified typecode", but
        // a subsequent attempt will yield error "OCI-21500: internal error
        // code" and crash the process, so pretend like the free was
        // successful!
        if (error->buffer->code == 21602)
            return DPI_SUCCESS;
        return DPI_FAILURE;
    }
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__objectGetAttr() [INTERNAL]
//   Wrapper for OCIObjectGetAttr().
//-----------------------------------------------------------------------------
int ob_dpiOci__objectGetAttr(dpiObject *obj, dpiObjectAttr *attr,
        int16_t *scalarValueIndicator, void **valueIndicator, void **value,
        void **tdo, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIObjectGetAttr", dpiOciSymbols.fnObjectGetAttr)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnObjectGetAttr)(obj->env->handle, error->handle,
            obj->instance, obj->indicator, obj->type->tdo, &attr->name,
            &attr->nameLength, 1, 0, 0, scalarValueIndicator, valueIndicator,
            value, tdo);
    DPI_OCI_CHECK_AND_RETURN(error, status, obj->type->conn, "get attribute");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__objectGetInd() [INTERNAL]
//   Wrapper for OCIObjectGetInd().
//-----------------------------------------------------------------------------
int ob_dpiOci__objectGetInd(dpiObject *obj, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIObjectGetInd", dpiOciSymbols.fnObjectGetInd)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnObjectGetInd)(obj->env->handle, error->handle,
            obj->instance, &obj->indicator);
    DPI_OCI_CHECK_AND_RETURN(error, status, obj->type->conn, "get indicator");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__objectNew() [INTERNAL]
//   Wrapper for OCIObjectNew().
//-----------------------------------------------------------------------------
int ob_dpiOci__objectNew(dpiObject *obj, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIObjectNew", dpiOciSymbols.fnObjectNew)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnObjectNew)(obj->env->handle, error->handle,
            obj->type->conn->handle, obj->type->typeCode, obj->type->tdo, NULL,
            DPI_OCI_DURATION_SESSION, 1, &obj->instance);
    DPI_OCI_CHECK_AND_RETURN(error, status, obj->type->conn, "create object");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__objectPin() [INTERNAL]
//   Wrapper for OCIObjectPin().
//-----------------------------------------------------------------------------
int ob_dpiOci__objectPin(void *envHandle, void *objRef, void **obj,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIObjectPin", dpiOciSymbols.fnObjectPin)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnObjectPin)(envHandle, error->handle, objRef,
            NULL, DPI_OCI_PIN_ANY, DPI_OCI_DURATION_SESSION, DPI_OCI_LOCK_NONE,
            obj);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "pin reference");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__objectSetAttr() [INTERNAL]
//   Wrapper for OCIObjectSetAttr().
//-----------------------------------------------------------------------------
int ob_dpiOci__objectSetAttr(dpiObject *obj, dpiObjectAttr *attr,
        int16_t scalarValueIndicator, void *valueIndicator, const void *value,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIObjectSetAttr", dpiOciSymbols.fnObjectSetAttr)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnObjectSetAttr)(obj->env->handle, error->handle,
            obj->instance, obj->indicator, obj->type->tdo, &attr->name,
            &attr->nameLength, 1, NULL, 0, scalarValueIndicator,
            valueIndicator, value);
    DPI_OCI_CHECK_AND_RETURN(error, status, obj->type->conn, "set attribute");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__passwordChange() [INTERNAL]
//   Wrapper for OCIPasswordChange().
//-----------------------------------------------------------------------------
int ob_dpiOci__passwordChange(dpiConn *conn, const char *userName,
        uint32_t userNameLength, const char *oldPassword,
        uint32_t oldPasswordLength, const char *newPassword,
        uint32_t newPasswordLength, uint32_t mode, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIPasswordChange", dpiOciSymbols.fnPasswordChange)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnPasswordChange)(conn->handle, error->handle,
            userName, userNameLength, oldPassword, oldPasswordLength,
            newPassword, newPasswordLength, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "change password");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__paramGet() [INTERNAL]
//   Wrapper for OCIParamGet().
//-----------------------------------------------------------------------------
int ob_dpiOci__paramGet(const void *handle, uint32_t handleType, void **parameter,
        uint32_t pos, const char *action, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIParamGet", dpiOciSymbols.fnParamGet)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnParamGet)(handle, handleType, error->handle,
            parameter, pos);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, action);
}


//-----------------------------------------------------------------------------
// ob_dpiOci__ping() [INTERNAL]
//   Wrapper for OCIPing().
//-----------------------------------------------------------------------------
int ob_dpiOci__ping(dpiConn *conn, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIPing", dpiOciSymbols.fnPing)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnPing)(conn->handle, error->handle,
            DPI_OCI_DEFAULT);
    if (DPI_OCI_ERROR_OCCURRED(status)) {
        ob_dpiError__setFromOCI(error, status, conn, "ping");

        // attempting to ping a database earlier than 10g will result in error
        // ORA-1010: invalid OCI operation, but that implies a successful ping
        // so ignore that error and treat it as a successful operation
        if (error->buffer->code == 1010)
            return DPI_SUCCESS;
        return DPI_FAILURE;
    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__rawAssignBytes() [INTERNAL]
//   Wrapper for OCIRawAssignBytes().
//-----------------------------------------------------------------------------
int ob_dpiOci__rawAssignBytes(void *envHandle, const char *value,
        uint32_t valueLength, void **handle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIRawAssignBytes", dpiOciSymbols.fnRawAssignBytes)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnRawAssignBytes)(envHandle, error->handle, value,
            valueLength, handle);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "assign bytes to raw");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__rawPtr() [INTERNAL]
//   Wrapper for OCIRawPtr().
//-----------------------------------------------------------------------------
int ob_dpiOci__rawPtr(void *envHandle, void *handle, void **ptr)
{
    dpiError *error = NULL;

    DPI_OCI_LOAD_SYMBOL("OCIRawPtr", dpiOciSymbols.fnRawPtr)
    *ptr = (*dpiOciSymbols.fnRawPtr)(envHandle, handle);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__rawResize() [INTERNAL]
//   Wrapper for OCIRawResize().
//-----------------------------------------------------------------------------
int ob_dpiOci__rawResize(void *envHandle, void **handle, uint32_t newSize,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIRawResize", dpiOciSymbols.fnRawResize)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnRawResize)(envHandle, error->handle, newSize,
            handle);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "resize raw");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__rawSize() [INTERNAL]
//   Wrapper for OCIRawSize().
//-----------------------------------------------------------------------------
int ob_dpiOci__rawSize(void *envHandle, void *handle, uint32_t *size)
{
    dpiError *error = NULL;

    DPI_OCI_LOAD_SYMBOL("OCIRawSize", dpiOciSymbols.fnRawSize)
    *size = (*dpiOciSymbols.fnRawSize)(envHandle, handle);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__reallocMem() [INTERNAL]
//   Wrapper for OCI allocation of memory, only used when debugging memory
// allocation.
//-----------------------------------------------------------------------------
static void *ob_dpiOci__reallocMem(UNUSED void *unused, void *ptr, size_t newSize)
{
    char message[80];
    void *newPtr;

    (void) sprintf(message, "OCI reallocated ptr at %p", ptr);
    newPtr = realloc(ptr, newSize);
    ob_dpiDebug__print("%s to %u bytes at %p\n", message, newSize, newPtr);
    return newPtr;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__rowidToChar() [INTERNAL]
//   Wrapper for OCIRowidToChar().
//-----------------------------------------------------------------------------
int ob_dpiOci__rowidToChar(dpiRowid *rowid, char *buffer, uint16_t *bufferSize,
        dpiError *error)
{
    uint16_t origSize;
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIRowidToChar", dpiOciSymbols.fnRowidToChar)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    origSize = *bufferSize;
    status = (*dpiOciSymbols.fnRowidToChar)(rowid->handle, buffer, bufferSize,
            error->handle);
    if (origSize == 0)
        return DPI_SUCCESS;
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "get rowid as string");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__serverAttach() [INTERNAL]
//   Wrapper for OCIServerAttach().
//-----------------------------------------------------------------------------
int ob_dpiOci__serverAttach(dpiConn *conn, const char *connectString,
        uint32_t connectStringLength, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIServerAttach", dpiOciSymbols.fnServerAttach)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnServerAttach)(conn->serverHandle, error->handle,
            connectString, (int32_t) connectStringLength, DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "server attach");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__serverDetach() [INTERNAL]
//   Wrapper for OCIServerDetach().
//-----------------------------------------------------------------------------
int ob_dpiOci__serverDetach(dpiConn *conn, int checkError, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIServerDetach", dpiOciSymbols.fnServerDetach)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnServerDetach)(conn->serverHandle, error->handle,
            DPI_OCI_DEFAULT);
    if (!checkError)
        return DPI_SUCCESS;
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "detatch from server");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__serverRelease() [INTERNAL]
//   Wrapper for OCIServerRelease().
//-----------------------------------------------------------------------------
int ob_dpiOci__serverRelease(dpiConn *conn, char *buffer, uint32_t bufferSize,
        uint32_t *version, uint32_t mode, dpiError *error)
{
    int status;

    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    if (conn->env->versionInfo->versionNum < 18) {
        DPI_OCI_LOAD_SYMBOL("OCIServerRelease", dpiOciSymbols.fnServerRelease)
        status = (*dpiOciSymbols.fnServerRelease)(conn->handle, error->handle,
                buffer, bufferSize, DPI_OCI_HTYPE_SVCCTX, version);
    } else {
        DPI_OCI_LOAD_SYMBOL("OCIServerRelease2",
                dpiOciSymbols.fnServerRelease2)
        status = (*dpiOciSymbols.fnServerRelease2)(conn->handle, error->handle,
                buffer, bufferSize, DPI_OCI_HTYPE_SVCCTX, version, mode);
    }
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "get server version");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sessionBegin() [INTERNAL]
//   Wrapper for OCISessionBegin().
//-----------------------------------------------------------------------------
int ob_dpiOci__sessionBegin(dpiConn *conn, uint32_t credentialType,
        uint32_t mode, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISessionBegin", dpiOciSymbols.fnSessionBegin)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSessionBegin)(conn->handle, error->handle,
            conn->sessionHandle, credentialType, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "begin session");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sessionEnd() [INTERNAL]
//   Wrapper for OCISessionEnd().
//-----------------------------------------------------------------------------
int ob_dpiOci__sessionEnd(dpiConn *conn, int checkError, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISessionEnd", dpiOciSymbols.fnSessionEnd)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSessionEnd)(conn->handle, error->handle,
            conn->sessionHandle, DPI_OCI_DEFAULT);
    if (!checkError)
        return DPI_SUCCESS;
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "end session");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sessionGet() [INTERNAL]
//   Wrapper for OCISessionGet().
//-----------------------------------------------------------------------------
int ob_dpiOci__sessionGet(void *envHandle, void **handle, void *authInfo,
        const char *connectString, uint32_t connectStringLength,
        const char *tag, uint32_t tagLength, const char **outTag,
        uint32_t *outTagLength, int *found, uint32_t mode, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISessionGet", dpiOciSymbols.fnSessionGet)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSessionGet)(envHandle, error->handle, handle,
            authInfo, connectString, connectStringLength, tag, tagLength,
            outTag, outTagLength, found, mode);

    // OCI might return a stale handle even though the call to OCISessionGet()
    // failed; clear it to avoid unexpected errors being thrown, masking any
    // true errors
    if (status < 0)
        *handle = NULL;
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "get session");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sessionPoolCreate() [INTERNAL]
//   Wrapper for OCISessionPoolCreate().
//-----------------------------------------------------------------------------
int ob_dpiOci__sessionPoolCreate(dpiPool *pool, const char *connectString,
        uint32_t connectStringLength, uint32_t minSessions,
        uint32_t maxSessions, uint32_t sessionIncrement, const char *userName,
        uint32_t userNameLength, const char *password, uint32_t passwordLength,
        uint32_t mode, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISessionPoolCreate",
            dpiOciSymbols.fnSessionPoolCreate)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSessionPoolCreate)(pool->env->handle,
            error->handle, pool->handle, (char**) &pool->name,
            &pool->nameLength, connectString, connectStringLength, minSessions,
            maxSessions, sessionIncrement, userName, userNameLength, password,
            passwordLength, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "create pool");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sessionPoolDestroy() [INTERNAL]
//   Wrapper for OCISessionPoolDestroy().
//-----------------------------------------------------------------------------
int ob_dpiOci__sessionPoolDestroy(dpiPool *pool, uint32_t mode, int checkError,
        dpiError *error)
{
    void *handle;
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISessionPoolDestroy",
            dpiOciSymbols.fnSessionPoolDestroy)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)

    // clear the pool handle immediately so that no further attempts are made
    // to use the pool while the pool is being closed; if the pool close fails,
    // restore the pool handle afterwards
    handle = pool->handle;
    pool->handle = NULL;
    status = (*dpiOciSymbols.fnSessionPoolDestroy)(handle, error->handle,
            mode);
    if (checkError && DPI_OCI_ERROR_OCCURRED(status)) {
        pool->handle = handle;
        return ob_dpiError__setFromOCI(error, status, NULL, "destroy pool");
    }
    ob_dpiOci__handleFree(handle, DPI_OCI_HTYPE_SPOOL);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sessionRelease() [INTERNAL]
//   Wrapper for OCISessionRelease().
//-----------------------------------------------------------------------------
int ob_dpiOci__sessionRelease(dpiConn *conn, const char *tag, uint32_t tagLength,
        uint32_t mode, int checkError, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISessionRelease", dpiOciSymbols.fnSessionRelease)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSessionRelease)(conn->handle, error->handle,
            tag, tagLength, mode);
    if (!checkError)
        return DPI_SUCCESS;
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "release session");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__shardingKeyColumnAdd() [INTERNAL]
//   Wrapper for OCIshardingKeyColumnAdd().
//-----------------------------------------------------------------------------
int ob_dpiOci__shardingKeyColumnAdd(void *shardingKey, void *col, uint32_t colLen,
        uint16_t colType, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIShardingKeyColumnAdd",
            dpiOciSymbols.fnShardingKeyColumnAdd)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnShardingKeyColumnAdd)(shardingKey,
            error->handle, col, colLen, colType, DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "add sharding column");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaBulkInsert() [INTERNAL]
//   Wrapper for OCISodaBulkInsert().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaBulkInsert(dpiSodaColl *coll, void **documents,
        uint32_t numDocuments, void *outputOptions, uint32_t mode,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaBulkInsert", dpiOciSymbols.fnSodaBulkInsert)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaBulkInsert)(coll->db->conn->handle,
            coll->handle, documents, numDocuments, outputOptions,
            error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "insert multiple documents");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaBulkInsertAndGet() [INTERNAL]
//   Wrapper for OCISodaBulkInsertAndGet().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaBulkInsertAndGet(dpiSodaColl *coll, void **documents,
        uint32_t numDocuments, void *outputOptions, uint32_t mode,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaBulkInsertAndGet",
            dpiOciSymbols.fnSodaBulkInsertAndGet)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaBulkInsertAndGet)(coll->db->conn->handle,
            coll->handle, documents, numDocuments, outputOptions,
            error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "insert (and get) multiple documents");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaBulkInsertAndGetWithOpts() [INTERNAL]
//   Wrapper for OCISodaBulkInsertAndGetWithOpts().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaBulkInsertAndGetWithOpts(dpiSodaColl *coll, void **documents,
        uint32_t numDocuments, void *operOptions, void *outputOptions,
        uint32_t mode, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaBulkInsertAndGetWithOpts",
            dpiOciSymbols.fnSodaBulkInsertAndGetWithOpts)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaBulkInsertAndGetWithOpts)
            (coll->db->conn->handle, coll->handle, documents, numDocuments,
             operOptions, outputOptions, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "insert (and get) multiple documents with options");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaCollCreateWithMetadata() [INTERNAL]
//   Wrapper for OCISodaCollCreateWithMetadata().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaCollCreateWithMetadata(dpiSodaDb *db, const char *name,
        uint32_t nameLength, const char *metadata, uint32_t metadataLength,
        uint32_t mode, void **handle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaCollCreateWithMetadata",
            dpiOciSymbols.fnSodaCollCreateWithMetadata)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaCollCreateWithMetadata)(db->conn->handle,
            name, nameLength, metadata, metadataLength, handle, error->handle,
            mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, db->conn,
            "create SODA collection");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaCollDrop() [INTERNAL]
//   Wrapper for OCISodaCollDrop().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaCollDrop(dpiSodaColl *coll, int *isDropped, uint32_t mode,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaCollDrop", dpiOciSymbols.fnSodaCollDrop)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaCollDrop)(coll->db->conn->handle,
            coll->handle, isDropped, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "drop SODA collection");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaCollGetNext() [INTERNAL]
//   Wrapper for OCISodaCollGetNext().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaCollGetNext(dpiConn *conn, void *cursorHandle,
        void **collectionHandle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaCollGetNext", dpiOciSymbols.fnSodaCollGetNext)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaCollGetNext)(conn->handle, cursorHandle,
            collectionHandle, error->handle, DPI_OCI_DEFAULT);
    if (status == DPI_OCI_NO_DATA) {
        *collectionHandle = NULL;
        return DPI_SUCCESS;
    }
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "get next collection");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaCollList() [INTERNAL]
//   Wrapper for OCISodaCollList().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaCollList(dpiSodaDb *db, const char *startingName,
        uint32_t startingNameLength, void **handle, uint32_t mode,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaCollList", dpiOciSymbols.fnSodaCollList)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaCollList)(db->conn->handle, startingName,
            startingNameLength, handle, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, db->conn,
            "get SODA collection cursor");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaCollOpen() [INTERNAL]
//   Wrapper for OCISodaCollOpen().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaCollOpen(dpiSodaDb *db, const char *name, uint32_t nameLength,
        uint32_t mode, void **handle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaCollOpen", dpiOciSymbols.fnSodaCollOpen)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaCollOpen)(db->conn->handle, name,
            nameLength, handle, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, db->conn, "open SODA collection");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaCollTruncate() [INTERNAL]
//   Wrapper for OCISodaCollTruncate().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaCollTruncate(dpiSodaColl *coll, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaCollTruncate",
            dpiOciSymbols.fnSodaCollTruncate)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaCollTruncate)(coll->db->conn->handle,
            coll->handle, error->handle, DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "truncate SODA collection");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaDataGuideGet() [INTERNAL]
//   Wrapper for OCISodaDataGuideGet().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaDataGuideGet(dpiSodaColl *coll, void **handle, uint32_t mode,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaDataGuideGet",
            dpiOciSymbols.fnSodaDataGuideGet)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaDataGuideGet)(coll->db->conn->handle,
            coll->handle, DPI_OCI_SODA_AS_AL32UTF8, handle, error->handle,
            mode);
    if (DPI_OCI_ERROR_OCCURRED(status)) {
        ob_dpiError__setFromOCI(error, status, coll->db->conn, "get data guide");
        if (error->buffer->code != 24801)
            return DPI_FAILURE;
        *handle = NULL;
    }
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaDocCount() [INTERNAL]
//   Wrapper for OCISodaDocCount().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaDocCount(dpiSodaColl *coll, void *options, uint32_t mode,
        uint64_t *count, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaDocCount", dpiOciSymbols.fnSodaDocCount)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaDocCount)(coll->db->conn->handle,
            coll->handle, options, count, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "get document count");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaDocGetNext() [INTERNAL]
//   Wrapper for OCISodaDocGetNext().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaDocGetNext(dpiSodaDocCursor *cursor, void **handle,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaDocGetNext", dpiOciSymbols.fnSodaDocGetNext)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaDocGetNext)(cursor->coll->db->conn->handle,
            cursor->handle, handle, error->handle, DPI_OCI_DEFAULT);
    if (status == DPI_OCI_NO_DATA) {
        *handle = NULL;
        return DPI_SUCCESS;
    }
    DPI_OCI_CHECK_AND_RETURN(error, status, cursor->coll->db->conn,
            "get next document");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaFind() [INTERNAL]
//   Wrapper for OCISodaFind().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaFind(dpiSodaColl *coll, const void *options, uint32_t flags,
        uint32_t mode, void **handle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaFind", dpiOciSymbols.fnSodaFind)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaFind)(coll->db->conn->handle,
            coll->handle, options, flags, handle, error->handle, mode);
    if (status == DPI_OCI_NO_DATA) {
        *handle = NULL;
        return DPI_SUCCESS;
    }
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "find SODA documents");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaFindOne() [INTERNAL]
//   Wrapper for OCISodaFindOne().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaFindOne(dpiSodaColl *coll, const void *options, uint32_t flags,
        uint32_t mode, void **handle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaFindOne", dpiOciSymbols.fnSodaFindOne)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaFindOne)(coll->db->conn->handle,
            coll->handle, options, flags, handle, error->handle, mode);
    if (status == DPI_OCI_NO_DATA) {
        *handle = NULL;
        return DPI_SUCCESS;
    }
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "get SODA document");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaIndexCreate() [INTERNAL]
//   Wrapper for OCISodaIndexCreate().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaIndexCreate(dpiSodaColl *coll, const char *indexSpec,
        uint32_t indexSpecLength, uint32_t mode, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaIndexCreate", dpiOciSymbols.fnSodaIndexCreate)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaIndexCreate)(coll->db->conn->handle,
            coll->handle, indexSpec, indexSpecLength, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn, "create index");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaIndexDrop() [INTERNAL]
//   Wrapper for OCISodaIndexDrop().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaIndexDrop(dpiSodaColl *coll, const char *name,
        uint32_t nameLength, uint32_t mode, int *isDropped, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaIndexDrop", dpiOciSymbols.fnSodaIndexDrop)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaIndexDrop)(coll->db->conn->handle, name,
            nameLength, isDropped, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn, "drop index");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaIndexList() [INTERNAL]
//   Wrapper for OCISodaIndexList().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaIndexList(dpiSodaColl *coll, uint32_t flags, void **handle,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaIndexList", dpiOciSymbols.fnSodaIndexList)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaIndexList)(coll->db->conn->handle,
            coll->handle, flags, handle, error->handle, DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn, "get index list");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaInsert() [INTERNAL]
//   Wrapper for OCISodaInsert().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaInsert(dpiSodaColl *coll, void *handle, uint32_t mode,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaInsert", dpiOciSymbols.fnSodaInsert)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaInsert)(coll->db->conn->handle,
            coll->handle, handle, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "insert SODA document");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaInsertAndGet() [INTERNAL]
//   Wrapper for OCISodaInsertAndGet().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaInsertAndGet(dpiSodaColl *coll, void **handle, uint32_t mode,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaInsertAndGet",
            dpiOciSymbols.fnSodaInsertAndGet)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaInsertAndGet)(coll->db->conn->handle,
            coll->handle, handle, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "insert and get SODA document");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaInsertAndGetWithOpts() [INTERNAL]
//   Wrapper for OCISodaInsertAndGetWithOpts().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaInsertAndGetWithOpts(dpiSodaColl *coll, void **handle,
        void *operOptions, uint32_t mode, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaInsertAndGetWithOpts",
            dpiOciSymbols.fnSodaInsertAndGetWithOpts)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaInsertAndGetWithOpts)
            (coll->db->conn->handle, coll->handle, handle, operOptions,
             error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "insert and get SODA document with options");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaOperKeysSet() [INTERNAL]
//   Wrapper for OCISodaOperKeysSet().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaOperKeysSet(const dpiSodaOperOptions *options, void *handle,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaOperKeysSet", dpiOciSymbols.fnSodaOperKeysSet)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaOperKeysSet)(handle, options->keys,
            options->keyLengths, options->numKeys, error->handle,
            DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL,
            "set operation options keys");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaRemove() [INTERNAL]
//   Wrapper for OCISodaRemove().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaRemove(dpiSodaColl *coll, void *options, uint32_t mode,
        uint64_t *count, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaRemove", dpiOciSymbols.fnSodaRemove)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaRemove)(coll->db->conn->handle,
            coll->handle, options, count, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "remove documents from SODA collection");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaReplOne() [INTERNAL]
//   Wrapper for OCISodaReplOne().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaReplOne(dpiSodaColl *coll, const void *options, void *handle,
        uint32_t mode, int *isReplaced, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaReplOne", dpiOciSymbols.fnSodaReplOne)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaReplOne)(coll->db->conn->handle,
            coll->handle, options, handle, isReplaced, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "replace SODA document");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaReplOneAndGet() [INTERNAL]
//   Wrapper for OCISodaReplOneAndGet().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaReplOneAndGet(dpiSodaColl *coll, const void *options,
        void **handle, uint32_t mode, int *isReplaced, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaReplOneAndGet",
            dpiOciSymbols.fnSodaReplOneAndGet)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaReplOneAndGet)(coll->db->conn->handle,
            coll->handle, options, handle, isReplaced, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "replace and get SODA document");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaSave() [INTERNAL]
//   Wrapper for OCISodaSave().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaSave(dpiSodaColl *coll, void *handle, uint32_t mode,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaSave", dpiOciSymbols.fnSodaSave)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaSave)(coll->db->conn->handle,
            coll->handle, handle, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "save SODA document");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaSaveAndGet() [INTERNAL]
//   Wrapper for OCISodaSaveAndGet().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaSaveAndGet(dpiSodaColl *coll, void **handle, uint32_t mode,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaSaveAndGet", dpiOciSymbols.fnSodaSaveAndGet)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaSaveAndGet)(coll->db->conn->handle,
            coll->handle, handle, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "save and get SODA document");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__sodaSaveAndGetWithOpts() [INTERNAL]
//   Wrapper for OCISodaSaveAndGetWithOpts().
//-----------------------------------------------------------------------------
int ob_dpiOci__sodaSaveAndGetWithOpts(dpiSodaColl *coll, void **handle,
        void *operOptions, uint32_t mode, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISodaSaveAndGetWithOpts",
            dpiOciSymbols.fnSodaSaveAndGetWithOpts)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSodaSaveAndGetWithOpts)(coll->db->conn->handle,
            coll->handle, handle, operOptions, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, coll->db->conn,
            "save and get SODA document");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__stmtExecute() [INTERNAL]
//   Wrapper for OCIStmtExecute().
//-----------------------------------------------------------------------------
int ob_dpiOci__stmtExecute(dpiStmt *stmt, uint32_t numIters, uint32_t mode,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIStmtExecute", dpiOciSymbols.fnStmtExecute)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnStmtExecute)(stmt->conn->handle, stmt->handle,
            error->handle, numIters, 0, 0, 0, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, stmt->conn, "execute");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__stmtFetch2() [INTERNAL]
//   Wrapper for OCIStmtFetch2().
//-----------------------------------------------------------------------------
int ob_dpiOci__stmtFetch2(dpiStmt *stmt, uint32_t numRows, uint16_t fetchMode,
        int32_t offset, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIStmtFetch2", dpiOciSymbols.fnStmtFetch2)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnStmtFetch2)(stmt->handle, error->handle,
            numRows, fetchMode, offset, DPI_OCI_DEFAULT);
    if (status == DPI_OCI_NO_DATA || fetchMode == DPI_MODE_FETCH_LAST) {
        stmt->hasRowsToFetch = 0;
    } else if (DPI_OCI_ERROR_OCCURRED(status)) {
        return ob_dpiError__setFromOCI(error, status, stmt->conn, "fetch");
    } else {
        stmt->hasRowsToFetch = 1;
    }
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__stmtGetBindInfo() [INTERNAL]
//   Wrapper for OCIStmtGetBindInfo().
//-----------------------------------------------------------------------------
int ob_dpiOci__stmtGetBindInfo(dpiStmt *stmt, uint32_t size, uint32_t startLoc,
        int32_t *numFound, char *names[], uint8_t nameLengths[],
        char *indNames[], uint8_t indNameLengths[], uint8_t isDuplicate[],
        void *bindHandles[], dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIStmtGetBindInfo", dpiOciSymbols.fnStmtGetBindInfo)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnStmtGetBindInfo)(stmt->handle, error->handle,
            size, startLoc, numFound, names, nameLengths, indNames,
            indNameLengths, isDuplicate, bindHandles);
    if (status == DPI_OCI_NO_DATA) {
        *numFound = 0;
        return DPI_SUCCESS;
    }
    DPI_OCI_CHECK_AND_RETURN(error, status, stmt->conn, "get bind info");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__stmtGetNextResult() [INTERNAL]
//   Wrapper for OCIStmtGetNextResult().
//-----------------------------------------------------------------------------
int ob_dpiOci__stmtGetNextResult(dpiStmt *stmt, void **handle, dpiError *error)
{
    uint32_t returnType;
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIStmtGetNextResult",
            dpiOciSymbols.fnStmtGetNextResult)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnStmtGetNextResult)(stmt->handle, error->handle,
            handle, &returnType, DPI_OCI_DEFAULT);
    if (status == DPI_OCI_NO_DATA) {
        *handle = NULL;
        return DPI_SUCCESS;
    }
    DPI_OCI_CHECK_AND_RETURN(error, status, stmt->conn, "get next result");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__stmtPrepare2() [INTERNAL]
//   Wrapper for OCIStmtPrepare2().
//-----------------------------------------------------------------------------
int ob_dpiOci__stmtPrepare2(dpiStmt *stmt, const char *sql, uint32_t sqlLength,
        const char *tag, uint32_t tagLength, dpiError *error)
{
    uint32_t mode = DPI_OCI_DEFAULT;
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIStmtPrepare2", dpiOciSymbols.fnStmtPrepare2)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    if (ob_dpiUtils__checkClientVersion(stmt->env->versionInfo, 12, 2,
            NULL) == DPI_SUCCESS)
        mode |= DPI_OCI_PREP2_GET_SQL_ID;
    status = (*dpiOciSymbols.fnStmtPrepare2)(stmt->conn->handle, &stmt->handle,
            error->handle, sql, sqlLength, tag, tagLength, DPI_OCI_NTV_SYNTAX,
            mode);
    if (DPI_OCI_ERROR_OCCURRED(status)) {
        stmt->handle = NULL;
        return ob_dpiError__setFromOCI(error, status, stmt->conn, "prepare SQL");
    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__stmtRelease() [INTERNAL]
//   Wrapper for OCIStmtRelease().
//-----------------------------------------------------------------------------
int ob_dpiOci__stmtRelease(dpiStmt *stmt, const char *tag, uint32_t tagLength,
        int checkError, dpiError *error)
{
    uint32_t mode = DPI_OCI_DEFAULT;
    uint32_t cacheSize = 0;
    int status;

    // if the statement should be deleted from the cache, first check to see
    // that there actually is a cache currently being used; otherwise, the
    // error "ORA-24300: bad value for mode" will be raised
    if (stmt->deleteFromCache) {
        ob_dpiOci__attrGet(stmt->conn->handle, DPI_OCI_HTYPE_SVCCTX,
                &cacheSize, NULL, DPI_OCI_ATTR_STMTCACHESIZE, NULL, error);
        if (cacheSize > 0)
            mode |= DPI_OCI_STRLS_CACHE_DELETE;
    }

    DPI_OCI_LOAD_SYMBOL("OCIStmtRelease", dpiOciSymbols.fnStmtRelease)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnStmtRelease)(stmt->handle, error->handle, tag,
            tagLength, mode);
    if (!checkError)
        return DPI_SUCCESS;
    DPI_OCI_CHECK_AND_RETURN(error, status, stmt->conn, "release statement");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__stringAssignText() [INTERNAL]
//   Wrapper for OCIStringAssignText().
//-----------------------------------------------------------------------------
int ob_dpiOci__stringAssignText(void *envHandle, const char *value,
        uint32_t valueLength, void **handle, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIStringAssignText",
            dpiOciSymbols.fnStringAssignText)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnStringAssignText)(envHandle, error->handle,
            value, valueLength, handle);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "assign to string");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__stringPtr() [INTERNAL]
//   Wrapper for OCIStringPtr().
//-----------------------------------------------------------------------------
int ob_dpiOci__stringPtr(void *envHandle, void *handle, char **ptr)
{
    dpiError *error = NULL;

    DPI_OCI_LOAD_SYMBOL("OCIStringPtr", dpiOciSymbols.fnStringPtr)
    *ptr = (*dpiOciSymbols.fnStringPtr)(envHandle, handle);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__stringResize() [INTERNAL]
//   Wrapper for OCIStringResize().
//-----------------------------------------------------------------------------
int ob_dpiOci__stringResize(void *envHandle, void **handle, uint32_t newSize,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIStringResize", dpiOciSymbols.fnStringResize)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnStringResize)(envHandle, error->handle, newSize,
            handle);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "resize string");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__stringSize() [INTERNAL]
//   Wrapper for OCIStringSize().
//-----------------------------------------------------------------------------
int ob_dpiOci__stringSize(void *envHandle, void *handle, uint32_t *size)
{
    dpiError *error = NULL;

    DPI_OCI_LOAD_SYMBOL("OCIStringSize", dpiOciSymbols.fnStringSize)
    *size = (*dpiOciSymbols.fnStringSize)(envHandle, handle);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__subscriptionRegister() [INTERNAL]
//   Wrapper for OCISubscriptionRegister().
//-----------------------------------------------------------------------------
int ob_dpiOci__subscriptionRegister(dpiConn *conn, void **handle, uint32_t mode,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISubscriptionRegister",
            dpiOciSymbols.fnSubscriptionRegister)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnSubscriptionRegister)(conn->handle, handle, 1,
            error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "register");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__subscriptionUnRegister() [INTERNAL]
//   Wrapper for OCISubscriptionUnRegister().
//-----------------------------------------------------------------------------
int ob_dpiOci__subscriptionUnRegister(dpiConn *conn, dpiSubscr *subscr,
        dpiError *error)
{
    uint32_t mode;
    int status;

    DPI_OCI_LOAD_SYMBOL("OCISubscriptionUnRegister",
            dpiOciSymbols.fnSubscriptionUnRegister)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    mode = (subscr->clientInitiated) ? DPI_OCI_SECURE_NOTIFICATION :
            DPI_OCI_DEFAULT;
    status = (*dpiOciSymbols.fnSubscriptionUnRegister)(conn->handle,
            subscr->handle, error->handle, mode);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "unregister");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__tableDelete() [INTERNAL]
//   Wrapper for OCITableDelete().
//-----------------------------------------------------------------------------
int ob_dpiOci__tableDelete(dpiObject *obj, int32_t index, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITableDelete", dpiOciSymbols.fnTableDelete)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTableDelete)(obj->env->handle, error->handle,
            index, obj->instance);
    DPI_OCI_CHECK_AND_RETURN(error, status, obj->type->conn, "delete element");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__tableExists() [INTERNAL]
//   Wrapper for OCITableExists().
//-----------------------------------------------------------------------------
int ob_dpiOci__tableExists(dpiObject *obj, int32_t index, int *exists,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITableExists", dpiOciSymbols.fnTableExists)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTableExists)(obj->env->handle, error->handle,
            obj->instance, index, exists);
    DPI_OCI_CHECK_AND_RETURN(error, status, obj->type->conn,
            "get index exists");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__tableFirst() [INTERNAL]
//   Wrapper for OCITableFirst().
//-----------------------------------------------------------------------------
int ob_dpiOci__tableFirst(dpiObject *obj, int32_t *index, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITableFirst", dpiOciSymbols.fnTableFirst)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTableFirst)(obj->env->handle, error->handle,
            obj->instance, index);
    DPI_OCI_CHECK_AND_RETURN(error, status, obj->type->conn,
            "get first index");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__tableLast() [INTERNAL]
//   Wrapper for OCITableLast().
//-----------------------------------------------------------------------------
int ob_dpiOci__tableLast(dpiObject *obj, int32_t *index, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITableLast", dpiOciSymbols.fnTableLast)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTableLast)(obj->env->handle, error->handle,
            obj->instance, index);
    DPI_OCI_CHECK_AND_RETURN(error, status, obj->type->conn, "get last index");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__tableNext() [INTERNAL]
//   Wrapper for OCITableNext().
//-----------------------------------------------------------------------------
int ob_dpiOci__tableNext(dpiObject *obj, int32_t index, int32_t *nextIndex,
        int *exists, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITableNext", dpiOciSymbols.fnTableNext)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTableNext)(obj->env->handle, error->handle,
            index, obj->instance, nextIndex, exists);
    DPI_OCI_CHECK_AND_RETURN(error, status, obj->type->conn, "get next index");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__tablePrev() [INTERNAL]
//   Wrapper for OCITablePrev().
//-----------------------------------------------------------------------------
int ob_dpiOci__tablePrev(dpiObject *obj, int32_t index, int32_t *prevIndex,
        int *exists, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITablePrev", dpiOciSymbols.fnTablePrev)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTablePrev)(obj->env->handle, error->handle,
            index, obj->instance, prevIndex, exists);
    DPI_OCI_CHECK_AND_RETURN(error, status, obj->type->conn, "get prev index");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__tableSize() [INTERNAL]
//   Wrapper for OCITableSize().
//-----------------------------------------------------------------------------
int ob_dpiOci__tableSize(dpiObject *obj, int32_t *size, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITableSize", dpiOciSymbols.fnTableSize)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTableSize)(obj->env->handle, error->handle,
            obj->instance, size);
    DPI_OCI_CHECK_AND_RETURN(error, status, obj->type->conn, "get size");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__threadKeyDestroy() [INTERNAL]
//   Wrapper for OCIThreadKeyDestroy().
//-----------------------------------------------------------------------------
int ob_dpiOci__threadKeyDestroy(void *envHandle, void *errorHandle, void **key,
        dpiError *error)
{
    DPI_OCI_LOAD_SYMBOL("OCIThreadKeyDestroy",
            dpiOciSymbols.fnThreadKeyDestroy)
    (*dpiOciSymbols.fnThreadKeyDestroy)(envHandle, errorHandle, key);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__threadKeyGet() [INTERNAL]
//   Wrapper for OCIThreadKeyGet().
//-----------------------------------------------------------------------------
int ob_dpiOci__threadKeyGet(void *envHandle, void *errorHandle, void *key,
        void **value, dpiError *error)
{
    int status;

    status = (*dpiOciSymbols.fnThreadKeyGet)(envHandle, errorHandle, key,
            value);
    if (status != DPI_OCI_SUCCESS)
        return ob_dpiError__set(error, "get TLS error", DPI_ERR_TLS_ERROR);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__threadKeyInit() [INTERNAL]
//   Wrapper for OCIThreadKeyInit().
//-----------------------------------------------------------------------------
int ob_dpiOci__threadKeyInit(void *envHandle, void *errorHandle, void **key,
        void *destroyFunc, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIThreadKeyInit", dpiOciSymbols.fnThreadKeyInit)
    status = (*dpiOciSymbols.fnThreadKeyInit)(envHandle, errorHandle, key,
            destroyFunc);
    DPI_OCI_CHECK_AND_RETURN(error, status, NULL, "initialize thread key");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__threadKeySet() [INTERNAL]
//   Wrapper for OCIThreadKeySet().
//-----------------------------------------------------------------------------
int ob_dpiOci__threadKeySet(void *envHandle, void *errorHandle, void *key,
        void *value, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIThreadKeySet", dpiOciSymbols.fnThreadKeySet)
    status = (*dpiOciSymbols.fnThreadKeySet)(envHandle, errorHandle, key,
            value);
    if (status != DPI_OCI_SUCCESS)
        return ob_dpiError__set(error, "set TLS error", DPI_ERR_TLS_ERROR);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiOci__transCommit() [INTERNAL]
//   Wrapper for OCITransCommit().
//-----------------------------------------------------------------------------
int ob_dpiOci__transCommit(dpiConn *conn, uint32_t flags, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITransCommit", dpiOciSymbols.fnTransCommit)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTransCommit)(conn->handle, error->handle,
            flags);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "commit");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__transDetach() [INTERNAL]
//   Wrapper for OCITransDetach().
//-----------------------------------------------------------------------------
int ob_dpiOci__transDetach(dpiConn *conn, uint32_t flags, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITransDetach", dpiOciSymbols.fnTransDetach)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTransDetach)(conn->handle, error->handle,
            flags);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "detach TPC transaction");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__transForget() [INTERNAL]
//   Wrapper for OCITransForget().
//-----------------------------------------------------------------------------
int ob_dpiOci__transForget(dpiConn *conn, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITransForget", dpiOciSymbols.fnTransForget)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTransForget)(conn->handle, error->handle,
            DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "forget TPC transaction");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__transPrepare() [INTERNAL]
//   Wrapper for OCITransPrepare().
//-----------------------------------------------------------------------------
int ob_dpiOci__transPrepare(dpiConn *conn, int *commitNeeded, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITransPrepare", dpiOciSymbols.fnTransPrepare)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTransPrepare)(conn->handle, error->handle,
            DPI_OCI_DEFAULT);
    *commitNeeded = (status == DPI_OCI_SUCCESS);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "prepare transaction");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__transRollback() [INTERNAL]
//   Wrapper for OCITransRollback().
//-----------------------------------------------------------------------------
int ob_dpiOci__transRollback(dpiConn *conn, int checkError, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITransRollback", dpiOciSymbols.fnTransRollback)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTransRollback)(conn->handle, error->handle,
            DPI_OCI_DEFAULT);
    if (!checkError)
        return DPI_SUCCESS;
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "rollback");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__transStart() [INTERNAL]
//   Wrapper for OCITransStart().
//-----------------------------------------------------------------------------
int ob_dpiOci__transStart(dpiConn *conn, uint32_t transactionTimeout,
        uint32_t flags, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITransStart", dpiOciSymbols.fnTransStart)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTransStart)(conn->handle, error->handle,
            transactionTimeout, flags);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "start transaction");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__typeByName() [INTERNAL]
//   Wrapper for OCITypeByName().
//-----------------------------------------------------------------------------
int ob_dpiOci__typeByName(dpiConn *conn, const char *schema,
        uint32_t schemaLength, const char *name, uint32_t nameLength,
        void **tdo, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITypeByName", dpiOciSymbols.fnTypeByName)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTypeByName)(conn->env->handle, error->handle,
            conn->handle, schema, schemaLength, name, nameLength, NULL, 0,
            DPI_OCI_DURATION_SESSION, DPI_OCI_TYPEGET_ALL, tdo);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "get type by name");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__typeByFullName() [INTERNAL]
//   Wrapper for OCITypeByFullName().
//-----------------------------------------------------------------------------
int ob_dpiOci__typeByFullName(dpiConn *conn, const char *name,
        uint32_t nameLength, void **tdo, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCITypeByFullName", dpiOciSymbols.fnTypeByFullName)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnTypeByFullName)(conn->env->handle,
            error->handle, conn->handle, name, nameLength, NULL, 0,
            DPI_OCI_DURATION_SESSION, DPI_OCI_TYPEGET_ALL, tdo);
    DPI_OCI_CHECK_AND_RETURN(error, status, conn, "get type by full name");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__vectorFromArray() [INTERNAL]
//   Wrapper for OCIVectorFromArray().
//-----------------------------------------------------------------------------
int ob_dpiOci__vectorFromArray(dpiVector *vector, dpiVectorInfo *info,
        dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIVectorFromArray", dpiOciSymbols.fnVectorFromArray)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnVectorFromArray)(vector->handle, error->handle,
            info->format, info->numDimensions, info->dimensions.asPtr,
            DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, vector->conn, "vector from array");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__vectorFromSparseArray() [INTERNAL]
//   Wrapper for OCIVectorFromSparseArray().
//-----------------------------------------------------------------------------
int ob_dpiOci__vectorFromSparseArray(dpiVector *vector, dpiVectorInfo *info,
        dpiError *error)
{
    int status;

    if (ob_dpiUtils__checkClientVersion(vector->env->versionInfo, 23, 7,
            error) < 0)
        return DPI_FAILURE;
    DPI_OCI_LOAD_SYMBOL("OCIVectorFromSparseArray",
            dpiOciSymbols.fnVectorFromSparseArray)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnVectorFromSparseArray)(vector->handle,
            error->handle, info->format, info->numDimensions,
            info->numSparseValues, info->sparseIndices, info->dimensions.asPtr,
            DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, vector->conn, "vector from array");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__vectorToArray() [INTERNAL]
//   Wrapper for OCIVectorToArray().
//-----------------------------------------------------------------------------
int ob_dpiOci__vectorToArray(dpiVector *vector, dpiError *error)
{
    int status;

    DPI_OCI_LOAD_SYMBOL("OCIVectorToArray", dpiOciSymbols.fnVectorToArray)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnVectorToArray)(vector->handle, error->handle,
            vector->format, &vector->numDimensions, vector->dimensions,
            DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, vector->conn, "vector to array");
}


//-----------------------------------------------------------------------------
// ob_dpiOci__vectorToSparseArray() [INTERNAL]
//   Wrapper for OCIVectorToSparseArray().
//-----------------------------------------------------------------------------
int ob_dpiOci__vectorToSparseArray(dpiVector *vector, dpiError *error)
{
    uint32_t numDimensions = vector->numDimensions;
    int status;

    if (ob_dpiUtils__checkClientVersion(vector->env->versionInfo, 23, 7,
            error) < 0)
        return DPI_FAILURE;
    DPI_OCI_LOAD_SYMBOL("OCIVectorToSparseArray",
            dpiOciSymbols.fnVectorToSparseArray)
    DPI_OCI_ENSURE_ERROR_HANDLE(error)
    status = (*dpiOciSymbols.fnVectorToSparseArray)(vector->handle,
            error->handle, vector->format, &numDimensions,
            &vector->numSparseValues, vector->sparseIndices,
            vector->dimensions, DPI_OCI_DEFAULT);
    DPI_OCI_CHECK_AND_RETURN(error, status, vector->conn,
            "vector to sparse array");
}
