//-----------------------------------------------------------------------------
// Copyright (c) 2016, 2025, Oracle and/or its affiliates.
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
// dpiData.c
//   Implementation of transformation routines.
//-----------------------------------------------------------------------------

#include "dpiImpl.h"

// constants used for converting timestamps to/from an interval
#define DPI_MS_DAY        86400000  // 24 * 60 * 60 * 1000
#define DPI_MS_HOUR       3600000   // 60 * 60 * 1000
#define DPI_MS_MINUTE     60000     // 60 * 1000
#define DPI_MS_SECOND     1000      // ms per sec
#define DPI_MS_FSECOND    1000000   // 1000 * 1000


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__fromOracleDate() [INTERNAL]
//   Populate the data from an dpiOciDate structure.
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__fromOracleDate(dpiDataBuffer *data,
        dpiOciDate *oracleValue)
{
    dpiTimestamp *timestamp = &data->asTimestamp;

    timestamp->year = oracleValue->year;
    timestamp->month = oracleValue->month;
    timestamp->day = oracleValue->day;
    timestamp->hour = oracleValue->hour;
    timestamp->minute = oracleValue->minute;
    timestamp->second = oracleValue->second;
    timestamp->fsecond = 0;
    timestamp->tzHourOffset = 0;
    timestamp->tzMinuteOffset = 0;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__fromOracleDateAsDouble() [INTERNAL]
//   Populate the data from an dpiOciDate structure as a double value (number
// of milliseconds since January 1, 1970).
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__fromOracleDateAsDouble(dpiDataBuffer *data,
        dpiEnv *env, dpiError *error, dpiOciDate *oracleValue)
{
    void *timestamp;
    int status;

    // allocate and populate a timestamp with the value of the date
    if (ob_dpiOci__descriptorAlloc(env->handle, &timestamp,
            DPI_OCI_DTYPE_TIMESTAMP, "alloc timestamp", error) < 0)
        return DPI_FAILURE;
    if (ob_dpiOci__dateTimeConstruct(env->handle, timestamp, oracleValue->year,
            oracleValue->month, oracleValue->day, oracleValue->hour,
            oracleValue->minute, oracleValue->second, 0, NULL, 0, error) < 0) {
        ob_dpiOci__descriptorFree(timestamp, DPI_OCI_DTYPE_TIMESTAMP);
        return DPI_FAILURE;
    }

    // now calculate the number of milliseconds since January 1, 1970
    status = ob_dpiDataBuffer__fromOracleTimestampAsDouble(data,
            DPI_ORACLE_TYPE_TIMESTAMP, env, error, timestamp);
    ob_dpiOci__descriptorFree(timestamp, DPI_OCI_DTYPE_TIMESTAMP);
    return status;
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__fromOracleIntervalDS() [INTERNAL]
//   Populate the data from an OCIInterval structure (days/seconds).
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__fromOracleIntervalDS(dpiDataBuffer *data, dpiEnv *env,
        dpiError *error, void *oracleValue)
{
    dpiIntervalDS *interval = &data->asIntervalDS;

    return ob_dpiOci__intervalGetDaySecond(env->handle, &interval->days,
            &interval->hours, &interval->minutes, &interval->seconds,
            &interval->fseconds, oracleValue, error);
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__fromOracleIntervalYM() [INTERNAL]
//   Populate the data from an OCIInterval structure (years/months).
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__fromOracleIntervalYM(dpiDataBuffer *data, dpiEnv *env,
        dpiError *error, void *oracleValue)
{
    dpiIntervalYM *interval = &data->asIntervalYM;

    return ob_dpiOci__intervalGetYearMonth(env->handle, &interval->years,
            &interval->months, oracleValue, error);
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__fromOracleNumberAsDouble() [INTERNAL]
//   Populate the data from an OCINumber structure as a double.
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__fromOracleNumberAsDouble(dpiDataBuffer *data,
        dpiError *error, void *oracleValue)
{
    return ob_dpiOci__numberToReal(&data->asDouble, oracleValue, error);
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__fromOracleNumberAsInteger() [INTERNAL]
//   Populate the data from an OCINumber structure as an integer.
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__fromOracleNumberAsInteger(dpiDataBuffer *data,
        dpiError *error, void *oracleValue)
{
    return ob_dpiOci__numberToInt(oracleValue, &data->asInt64, sizeof(int64_t),
            DPI_OCI_NUMBER_SIGNED, error);
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__fromOracleNumberAsUnsignedInteger() [INTERNAL]
//   Populate the data from an OCINumber structure as an unsigned integer.
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__fromOracleNumberAsUnsignedInteger(dpiDataBuffer *data,
        dpiError *error, void *oracleValue)
{
    return ob_dpiOci__numberToInt(oracleValue, &data->asUint64, sizeof(uint64_t),
            DPI_OCI_NUMBER_UNSIGNED, error);
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__fromOracleNumberAsText() [INTERNAL]
//   Populate the data from an OCINumber structure as text.
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__fromOracleNumberAsText(dpiDataBuffer *data, dpiEnv *env,
        dpiError *error, void *oracleValue)
{
    uint8_t *target, numDigits, digits[DPI_NUMBER_MAX_DIGITS];
    int16_t decimalPointIndex, i;
    uint16_t *targetUtf16;
    uint32_t numBytes;
    dpiBytes *bytes;
    int isNegative;

    // parse the OCINumber structure
    if (ob_dpiUtils__parseOracleNumber(oracleValue, &isNegative,
            &decimalPointIndex, &numDigits, digits, error) < 0)
        return DPI_FAILURE;

    // calculate the number of bytes that will be required for the string
    numBytes = numDigits;
    if (isNegative)
        numBytes++;
    if (decimalPointIndex <= 0)
        numBytes += -decimalPointIndex + 2;
    else if (decimalPointIndex < numDigits)
        numBytes++;
    else if (decimalPointIndex > numDigits)
        numBytes += decimalPointIndex - numDigits;
    if (env->charsetId == DPI_CHARSET_ID_UTF16)
        numBytes *= 2;

    // verify that the provided buffer is large enough
    bytes = &data->asBytes;
    if (numBytes > bytes->length)
        return ob_dpiError__set(error, "check number to text size",
                DPI_ERR_BUFFER_SIZE_TOO_SMALL, bytes->length);
    bytes->length = numBytes;

    // UTF-16 must be handled differently; the platform endianness is used in
    // order to be compatible with OCI which has this restriction
    if (env->charsetId == DPI_CHARSET_ID_UTF16) {
        targetUtf16 = (uint16_t*) bytes->ptr;

        // if negative, include the sign
        if (isNegative)
            *targetUtf16++ = '-';

        // if the decimal point index is 0 or less, add the decimal point and
        // any leading zeroes that are needed
        if (decimalPointIndex <= 0) {
            *targetUtf16++ = '0';
            *targetUtf16++ = '.';
            for (; decimalPointIndex < 0; decimalPointIndex++)
                *targetUtf16++ = '0';
        }

        // add each of the digits
        for (i = 0; i < numDigits; i++) {
            if (i > 0 && i == decimalPointIndex)
                *targetUtf16++ = '.';
            *targetUtf16++ = '0' + digits[i];
        }

        // if the decimal point index exceeds the number of digits, add any
        // trailing zeroes that are needed
        if (decimalPointIndex > numDigits) {
            for (i = numDigits; i < decimalPointIndex; i++)
                *targetUtf16++ = '0';
        }

        // add NULL terminator for ease of use by C conversion functions like
        // strtoll(), strtod(), etc.
        *targetUtf16++ = 0;

    // the following should be the same logic as the section above for UTF-16,
    // simply with single byte encodings instead
    } else {
        target = (uint8_t*) bytes->ptr;

        // if negative, include the sign
        if (isNegative)
            *target++ = '-';

        // if the decimal point index is 0 or less, add the decimal point and
        // any leading zeroes that are needed
        if (decimalPointIndex <= 0) {
            *target++ = '0';
            *target++ = '.';
            for (; decimalPointIndex < 0; decimalPointIndex++)
                *target++ = '0';
        }

        // add each of the digits
        for (i = 0; i < numDigits; i++) {
            if (i > 0 && i == decimalPointIndex)
                *target++ = '.';
            *target++ = '0' + digits[i];
        }

        // if the decimal point index exceeds the number of digits, add any
        // trailing zeroes that are needed
        if (decimalPointIndex > numDigits) {
            for (i = numDigits; i < decimalPointIndex; i++)
                *target++ = '0';
        }

        // add NULL terminator for ease of use by C conversion functions like
        // strtoll(), strtod(), etc.
        *target++ = 0;

    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__fromOracleTimestamp() [INTERNAL]
//   Populate the data from an OCIDateTime structure.
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__fromOracleTimestamp(dpiDataBuffer *data, dpiEnv *env,
        dpiError *error, void *oracleValue, int withTZ)
{
    dpiTimestamp *timestamp = &data->asTimestamp;

    if (ob_dpiOci__dateTimeGetDate(env->handle, oracleValue, &timestamp->year,
            &timestamp->month, &timestamp->day, error) < 0)
        return DPI_FAILURE;
    if (ob_dpiOci__dateTimeGetTime(env->handle, oracleValue, &timestamp->hour,
            &timestamp->minute, &timestamp->second, &timestamp->fsecond,
            error) < 0)
        return DPI_FAILURE;
    if (withTZ) {
        if (ob_dpiOci__dateTimeGetTimeZoneOffset(env->handle, oracleValue,
                &timestamp->tzHourOffset, &timestamp->tzMinuteOffset,
                error) < 0)
            return DPI_FAILURE;
    } else {
        timestamp->tzHourOffset = 0;
        timestamp->tzMinuteOffset = 0;
    }
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__fromOracleTimestampAsDouble() [INTERNAL]
//   Populate the data from an OCIDateTime structure as a double value (number
// of milliseconds since January 1, 1970).
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__fromOracleTimestampAsDouble(dpiDataBuffer *data,
        uint32_t dataType, dpiEnv *env, dpiError *error, void *oracleValue)
{
    int32_t day, hour, minute, second, fsecond;
    void *interval, *baseDate;
    int status;

    // determine the base date to use for the given data type
    if (ob_dpiEnv__getBaseDate(env, dataType, &baseDate, error) < 0)
        return DPI_FAILURE;

    // allocate interval to use in calculation
    if (ob_dpiOci__descriptorAlloc(env->handle, &interval,
            DPI_OCI_DTYPE_INTERVAL_DS, "alloc interval", error) < 0)
        return DPI_FAILURE;

    // subtract dates to determine interval between date and base date
    if (ob_dpiOci__dateTimeSubtract(env->handle, oracleValue, baseDate, interval,
            error) < 0) {
        ob_dpiOci__descriptorFree(interval, DPI_OCI_DTYPE_INTERVAL_DS);
        return DPI_FAILURE;
    }

    // get the days, hours, minutes and seconds from the interval
    status = ob_dpiOci__intervalGetDaySecond(env->handle, &day, &hour, &minute,
            &second, &fsecond, interval, error);
    ob_dpiOci__descriptorFree(interval, DPI_OCI_DTYPE_INTERVAL_DS);
    if (status < 0)
        return DPI_FAILURE;

    // calculate milliseconds since January 1, 1970
    data->asDouble = ((double) day) * DPI_MS_DAY + hour * DPI_MS_HOUR +
            minute * DPI_MS_MINUTE + second * DPI_MS_SECOND +
            fsecond / DPI_MS_FSECOND;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__toOracleDate() [INTERNAL]
//   Populate the data in an dpiOciDate structure.
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__toOracleDate(dpiDataBuffer *data, dpiOciDate *oracleValue)
{
    dpiTimestamp *timestamp = &data->asTimestamp;

    oracleValue->year = timestamp->year;
    oracleValue->month = timestamp->month;
    oracleValue->day = timestamp->day;
    oracleValue->hour = timestamp->hour;
    oracleValue->minute = timestamp->minute;
    oracleValue->second = timestamp->second;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__toOracleDateFromDouble() [INTERNAL]
//   Populate the data in an dpiOciDate structure given a double (number of
// milliseconds since January 1, 1970).
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__toOracleDateFromDouble(dpiDataBuffer *data, dpiEnv *env,
        dpiError *error, dpiOciDate *oracleValue)
{
    uint32_t fsecond;
    void *timestamp;

    // allocate a descriptor to acquire a timestamp
    if (ob_dpiOci__descriptorAlloc(env->handle, &timestamp,
            DPI_OCI_DTYPE_TIMESTAMP, "alloc timestamp", error) < 0)
        return DPI_FAILURE;
    if (ob_dpiDataBuffer__toOracleTimestampFromDouble(data,
            DPI_ORACLE_TYPE_TIMESTAMP, env, error, timestamp) < 0) {
        ob_dpiOci__descriptorFree(timestamp, DPI_OCI_DTYPE_TIMESTAMP);
        return DPI_FAILURE;
    }

    // populate date structure
    if (ob_dpiOci__dateTimeGetDate(env->handle, timestamp, &oracleValue->year,
            &oracleValue->month, &oracleValue->day, error) < 0) {
        ob_dpiOci__descriptorFree(timestamp, DPI_OCI_DTYPE_TIMESTAMP);
        return DPI_FAILURE;
    }
    if (ob_dpiOci__dateTimeGetTime(env->handle, timestamp, &oracleValue->hour,
            &oracleValue->minute, &oracleValue->second, &fsecond, error) < 0) {
        ob_dpiOci__descriptorFree(timestamp, DPI_OCI_DTYPE_TIMESTAMP);
        return DPI_FAILURE;
    }

    ob_dpiOci__descriptorFree(timestamp, DPI_OCI_DTYPE_TIMESTAMP);
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__toOracleIntervalDS() [INTERNAL]
//   Populate the data in an OCIInterval structure (days/seconds).
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__toOracleIntervalDS(dpiDataBuffer *data, dpiEnv *env,
        dpiError *error, void *oracleValue)
{
    dpiIntervalDS *interval = &data->asIntervalDS;

    return ob_dpiOci__intervalSetDaySecond(env->handle, interval->days,
            interval->hours, interval->minutes, interval->seconds,
            interval->fseconds, oracleValue, error);
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__toOracleIntervalYM() [INTERNAL]
//   Populate the data in an OCIInterval structure (years/months).
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__toOracleIntervalYM(dpiDataBuffer *data, dpiEnv *env,
        dpiError *error, void *oracleValue)
{
    dpiIntervalYM *interval = &data->asIntervalYM;

    return ob_dpiOci__intervalSetYearMonth(env->handle, interval->years,
            interval->months, oracleValue, error);
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__toOracleNumberFromDouble() [INTERNAL]
//   Populate the data in an OCINumber structure from a double.
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__toOracleNumberFromDouble(dpiDataBuffer *data,
        dpiError *error, void *oracleValue)
{
    if (isnan(data->asDouble))
        return ob_dpiError__set(error, "convert double to Oracle number",
                DPI_ERR_NAN);
    return ob_dpiOci__numberFromReal(data->asDouble, oracleValue, error);
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__toOracleNumberFromInteger() [INTERNAL]
//   Populate the data in an OCINumber structure from an integer.
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__toOracleNumberFromInteger(dpiDataBuffer *data,
        dpiError *error, void *oracleValue)
{
    return ob_dpiOci__numberFromInt(&data->asInt64, sizeof(int64_t),
            DPI_OCI_NUMBER_SIGNED, oracleValue, error);
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__toOracleNumberFromText() [INTERNAL]
//   Populate the data in an OCINumber structure from text.
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__toOracleNumberFromText(dpiDataBuffer *data, dpiEnv *env,
        dpiError *error, void *oracleValue)
{
    uint8_t numDigits, digits[DPI_NUMBER_AS_TEXT_CHARS], *source, *target, i;
    int isNegative, prependZero, appendSentinel;
    dpiBytes *value = &data->asBytes;
    int16_t decimalPointIndex;
    uint8_t byte, numPairs;
    int8_t ociExponent;

    // parse the string into its constituent components
    if (ob_dpiUtils__parseNumberString(value->ptr, value->length, env->charsetId,
            &isNegative, &decimalPointIndex, &numDigits, digits, error) < 0)
        return DPI_FAILURE;

    // if the exponent is odd, prepend a zero
    prependZero = (decimalPointIndex > 0 && decimalPointIndex % 2 == 1) ||
            (decimalPointIndex < 0 && decimalPointIndex % 2 == -1);
    if (prependZero && numDigits != 0) {
        digits[numDigits++] = 0;
        decimalPointIndex++;
    }

    // determine the number of digit pairs; if the number of digits is odd,
    // append a zero to make the number of digits even
    if (numDigits % 2 == 1)
        digits[numDigits++] = 0;
    numPairs = numDigits / 2;

    // append a sentinel 102 byte for negative numbers if there is room
    appendSentinel = (isNegative && numDigits > 0 &&
            numDigits < DPI_NUMBER_MAX_DIGITS);

    // initialize the OCINumber value
    // the length is the number of pairs, plus one for the exponent
    // include an extra byte for the sentinel if applicable
    target = (uint8_t*) oracleValue;
    *target++ = (uint8_t) (numPairs + 1 + appendSentinel);

    // if the number of digits is zero, the value is itself zero since all
    // leading and trailing zeroes are removed from the digits string; the OCI
    // value for zero is a special case
    if (numDigits == 0) {
        *target = 128;
        return DPI_SUCCESS;
    }

    // calculate the exponent
    ociExponent = (int8_t) ((decimalPointIndex - 2) / 2 + 193);
    if (isNegative)
        ociExponent = ~ociExponent;
    *target++ = (uint8_t) ociExponent;

    // calculate the mantissa bytes
    source = digits;
    for (i = 0; i < numPairs; i++) {
        if (i == 0 && prependZero)
            byte = *source++;
        else {
            byte = *source++ * 10;
            byte += *source++;
        }
        if (isNegative)
            byte = 101 - byte;
        else byte++;
        *target++ = byte;
    }

    // append 102 byte for negative numbers if the number of digits is less
    // than the maximum allowable
    if (appendSentinel)
        *target = 102;

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__toOracleNumberFromUnsignedInteger() [INTERNAL]
//   Populate the data in an OCINumber structure from an integer.
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__toOracleNumberFromUnsignedInteger(dpiDataBuffer *data,
        dpiError *error, void *oracleValue)
{
    return ob_dpiOci__numberFromInt(&data->asUint64, sizeof(uint64_t),
            DPI_OCI_NUMBER_UNSIGNED, oracleValue, error);
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__toOracleTimestamp() [INTERNAL]
//   Populate the data in an OCIDateTime structure.
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__toOracleTimestamp(dpiDataBuffer *data, dpiEnv *env,
        dpiError *error, void *oracleValue, int withTZ)
{
    dpiTimestamp *timestamp = &data->asTimestamp;
    char tzOffsetBuffer[10], *tzOffset = NULL;
    size_t tzOffsetLength = 0;
    char sign;

    if (withTZ) {
        sign = (timestamp->tzHourOffset < 0 || timestamp->tzMinuteOffset < 0) ?
                '-' : '+';
        tzOffsetLength = (size_t) sprintf(tzOffsetBuffer, "%c%.2d:%.2d", sign,
                abs(timestamp->tzHourOffset), abs(timestamp->tzMinuteOffset));
        tzOffset = tzOffsetBuffer;
    }
    return ob_dpiOci__dateTimeConstruct(env->handle, oracleValue, timestamp->year,
            timestamp->month, timestamp->day, timestamp->hour,
            timestamp->minute, timestamp->second, timestamp->fsecond, tzOffset,
            tzOffsetLength, error);
}


//-----------------------------------------------------------------------------
// ob_dpiDataBuffer__toOracleTimestampFromDouble() [INTERNAL]
//   Populate the data in an OCIDateTime structure, given the number of
// milliseconds since January 1, 1970.
//-----------------------------------------------------------------------------
int ob_dpiDataBuffer__toOracleTimestampFromDouble(dpiDataBuffer *data,
        uint32_t dataType, dpiEnv *env, dpiError *error, void *oracleValue)
{
    int32_t day, hour, minute, second, fsecond;
    void *interval, *baseDate;
    int status;
    double ms;

    // determine the base date to use for the given data type
    if (ob_dpiEnv__getBaseDate(env, dataType, &baseDate, error) < 0)
        return DPI_FAILURE;

    // allocate interval to use in calculation
    if (ob_dpiOci__descriptorAlloc(env->handle, &interval,
            DPI_OCI_DTYPE_INTERVAL_DS, "alloc interval", error) < 0)
        return DPI_FAILURE;

    // determine the interval
    ms = data->asDouble;
    day = (int32_t) (ms / DPI_MS_DAY);
    ms = ms - ((double) day) * DPI_MS_DAY;
    hour = (int32_t) (ms / DPI_MS_HOUR);
    ms = ms - (hour * DPI_MS_HOUR);
    minute = (int32_t) (ms / DPI_MS_MINUTE);
    ms = ms - (minute * DPI_MS_MINUTE);
    second = (int32_t) (ms / DPI_MS_SECOND);
    ms = ms - (second * DPI_MS_SECOND);
    fsecond = (int32_t)(ms * DPI_MS_FSECOND);
    if (ob_dpiOci__intervalSetDaySecond(env->handle, day, hour, minute, second,
            fsecond, interval, error) < 0) {
        ob_dpiOci__descriptorFree(interval, DPI_OCI_DTYPE_INTERVAL_DS);
        return DPI_FAILURE;
    }

    // add the interval to the base date
    status = ob_dpiOci__dateTimeIntervalAdd(env->handle, baseDate, interval,
            oracleValue, error);
    ob_dpiOci__descriptorFree(interval, DPI_OCI_DTYPE_INTERVAL_DS);
    return status;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getBool() [PUBLIC]
//   Return the boolean portion of the data.
//-----------------------------------------------------------------------------
int ob_dpiData_getBool(dpiData *data)
{
    return data->value.asBoolean;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getBytes() [PUBLIC]
//   Return the bytes portion of the data.
//-----------------------------------------------------------------------------
dpiBytes *ob_dpiData_getBytes(dpiData *data)
{
    return &data->value.asBytes;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getDouble() [PUBLIC]
//   Return the double portion of the data.
//-----------------------------------------------------------------------------
double ob_dpiData_getDouble(dpiData *data)
{
    return data->value.asDouble;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getFloat() [PUBLIC]
//   Return the float portion of the data.
//-----------------------------------------------------------------------------
float ob_dpiData_getFloat(dpiData *data)
{
    return data->value.asFloat;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getInt64() [PUBLIC]
//   Return the integer portion of the data.
//-----------------------------------------------------------------------------
int64_t ob_dpiData_getInt64(dpiData *data)
{
    return data->value.asInt64;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getIntervalDS() [PUBLIC]
//   Return the interval (days/seconds) portion of the data.
//-----------------------------------------------------------------------------
dpiIntervalDS *ob_dpiData_getIntervalDS(dpiData *data)
{
    return &data->value.asIntervalDS;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getIntervalYM() [PUBLIC]
//   Return the interval (years/months) portion of the data.
//-----------------------------------------------------------------------------
dpiIntervalYM *ob_dpiData_getIntervalYM(dpiData *data)
{
    return &data->value.asIntervalYM;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getIsNull() [PUBLIC]
//   Return a boolean indicating if the value is null or not.
//-----------------------------------------------------------------------------
int ob_dpiData_getIsNull(dpiData *data)
{
    return data->isNull;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getLOB() [PUBLIC]
//   Return the LOB portion of the data.
//-----------------------------------------------------------------------------
dpiLob *ob_dpiData_getLOB(dpiData *data)
{
    return data->value.asLOB;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getJson() [PUBLIC]
//   Return the JSON portion of the data.
//-----------------------------------------------------------------------------
dpiJson *ob_dpiData_getJson(dpiData *data)
{
    return data->value.asJson;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getJsonArray() [PUBLIC]
//   Return the JSON Array portion of the data.
//-----------------------------------------------------------------------------
dpiJsonArray *ob_dpiData_getJsonArray(dpiData *data)
{
    return &data->value.asJsonArray;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getJsonObject() [PUBLIC]
//   Return the JSON Object portion of the data.
//-----------------------------------------------------------------------------
dpiJsonObject *ob_dpiData_getJsonObject(dpiData *data)
{
    return &data->value.asJsonObject;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getObject() [PUBLIC]
//   Return the object portion of the data.
//-----------------------------------------------------------------------------
dpiObject *ob_dpiData_getObject(dpiData *data)
{
    return data->value.asObject;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getStmt() [PUBLIC]
//   Return the statement portion of the data.
//-----------------------------------------------------------------------------
dpiStmt *ob_dpiData_getStmt(dpiData *data)
{
    return data->value.asStmt;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getTimestamp() [PUBLIC]
//   Return the timestamp portion of the data.
//-----------------------------------------------------------------------------
dpiTimestamp *ob_dpiData_getTimestamp(dpiData *data)
{
    return &data->value.asTimestamp;
}


//-----------------------------------------------------------------------------
// ob_dpiData_getUint64() [PUBLIC]
//   Return the unsigned integer portion of the data.
//-----------------------------------------------------------------------------
uint64_t ob_dpiData_getUint64(dpiData *data)
{
    return data->value.asUint64;
}

//-----------------------------------------------------------------------------
// ob_dpiData_getVector() [PUBLIC]
//   Return the VECTOR portion of the data.
//-----------------------------------------------------------------------------
dpiVector *ob_dpiData_getVector(dpiData *data)
{
    return data->value.asVector;
}


//-----------------------------------------------------------------------------
// ob_dpiData_setBool() [PUBLIC]
//   Set the boolean portion of the data.
//-----------------------------------------------------------------------------
void ob_dpiData_setBool(dpiData *data, int value)
{
    data->isNull = 0;
    data->value.asBoolean = value;
}


//-----------------------------------------------------------------------------
// ob_dpiData_setBytes() [PUBLIC]
//   Set the bytes portion of the data.
//-----------------------------------------------------------------------------
void ob_dpiData_setBytes(dpiData *data, char *ptr, uint32_t length)
{
    data->isNull = 0;
    data->value.asBytes.ptr = ptr;
    data->value.asBytes.length = length;
}


//-----------------------------------------------------------------------------
// ob_dpiData_setDouble() [PUBLIC]
//   Set the double portion of the data.
//-----------------------------------------------------------------------------
void ob_dpiData_setDouble(dpiData *data, double value)
{
    data->isNull = 0;
    data->value.asDouble = value;
}


//-----------------------------------------------------------------------------
// ob_dpiData_setFloat() [PUBLIC]
//   Set the float portion of the data.
//-----------------------------------------------------------------------------
void ob_dpiData_setFloat(dpiData *data, float value)
{
    data->isNull = 0;
    data->value.asFloat = value;
}


//-----------------------------------------------------------------------------
// ob_dpiData_setInt64() [PUBLIC]
//   Set the integer portion of the data.
//-----------------------------------------------------------------------------
void ob_dpiData_setInt64(dpiData *data, int64_t value)
{
    data->isNull = 0;
    data->value.asInt64 = value;
}


//-----------------------------------------------------------------------------
// ob_dpiData_setIntervalDS() [PUBLIC]
//   Set the interval (days/seconds) portion of the data.
//-----------------------------------------------------------------------------
void ob_dpiData_setIntervalDS(dpiData *data, int32_t days, int32_t hours,
        int32_t minutes, int32_t seconds, int32_t fseconds)
{
    dpiIntervalDS *interval = &data->value.asIntervalDS;

    data->isNull = 0;
    interval->days = days;
    interval->hours = hours;
    interval->minutes = minutes;
    interval->seconds = seconds;
    interval->fseconds = fseconds;
}


//-----------------------------------------------------------------------------
// ob_dpiData_setIntervalYM() [PUBLIC]
//   Set the interval (years/months) portion of the data.
//-----------------------------------------------------------------------------
void ob_dpiData_setIntervalYM(dpiData *data, int32_t years, int32_t months)
{
    dpiIntervalYM *interval = &data->value.asIntervalYM;

    data->isNull = 0;
    interval->years = years;
    interval->months = months;
}


//-----------------------------------------------------------------------------
// ob_dpiData_setLOB() [PUBLIC]
//   Set the LOB portion of the data.
//-----------------------------------------------------------------------------
void ob_dpiData_setLOB(dpiData *data, dpiLob *lob)
{
    data->isNull = 0;
    data->value.asLOB = lob;
}


//-----------------------------------------------------------------------------
// ob_dpiData_setNull() [PUBLIC]
//   Set the data to be treated as a null value.
//-----------------------------------------------------------------------------
void ob_dpiData_setNull(dpiData *data)
{
    data->isNull = 1;
}


//-----------------------------------------------------------------------------
// ob_dpiData_setObject() [PUBLIC]
//   Set the object portion of the data.
//-----------------------------------------------------------------------------
void ob_dpiData_setObject(dpiData *data, dpiObject *obj)
{
    data->isNull = 0;
    data->value.asObject = obj;
}


//-----------------------------------------------------------------------------
// ob_dpiData_setStmt() [PUBLIC]
//   Set the statement portion of the data.
//-----------------------------------------------------------------------------
void ob_dpiData_setStmt(dpiData *data, dpiStmt *obj)
{
    data->isNull = 0;
    data->value.asStmt = obj;
}


//-----------------------------------------------------------------------------
// ob_dpiData_setTimestamp() [PUBLIC]
//   Set the timestamp portion of the data.
//-----------------------------------------------------------------------------
void ob_dpiData_setTimestamp(dpiData *data, int16_t year, uint8_t month,
        uint8_t day, uint8_t hour, uint8_t minute, uint8_t second,
        uint32_t fsecond, int8_t tzHourOffset, int8_t tzMinuteOffset)
{
    dpiTimestamp *timestamp = &data->value.asTimestamp;

    data->isNull = 0;
    timestamp->year = year;
    timestamp->month = month;
    timestamp->day = day;
    timestamp->hour = hour;
    timestamp->minute = minute;
    timestamp->second = second;
    timestamp->fsecond = fsecond;
    timestamp->tzHourOffset = tzHourOffset;
    timestamp->tzMinuteOffset = tzMinuteOffset;
}


//-----------------------------------------------------------------------------
// ob_dpiData_setUint64() [PUBLIC]
//   Set the unsigned integer portion of the data.
//-----------------------------------------------------------------------------
void ob_dpiData_setUint64(dpiData *data, uint64_t value)
{
    data->isNull = 0;
    data->value.asUint64 = value;
}
