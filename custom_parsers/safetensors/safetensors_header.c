// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/safetensors/safetensors_header.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "openzl/shared/mem.h"

#define ST_MAX_JSON_DEPTH 32

typedef struct {
    const char* ptr;
    const char* end;
} ST_Cursor;

typedef struct {
    const char* str;
    ZL_SafetensorsDtype dtype;
    size_t size;
} ST_DtypeInfo;

static const ST_DtypeInfo kDtypes[] = {
    { "BOOL", ZL_SafetensorsDtype_bool, 1 },
    { "U8", ZL_SafetensorsDtype_u8, 1 },
    { "I8", ZL_SafetensorsDtype_i8, 1 },
    { "F8_E4M3", ZL_SafetensorsDtype_f8_e4m3, 1 },
    { "F8_E5M2", ZL_SafetensorsDtype_f8_e5m2, 1 },
    { "I16", ZL_SafetensorsDtype_i16, 2 },
    { "U16", ZL_SafetensorsDtype_u16, 2 },
    { "F16", ZL_SafetensorsDtype_f16, 2 },
    { "BF16", ZL_SafetensorsDtype_bf16, 2 },
    { "I32", ZL_SafetensorsDtype_i32, 4 },
    { "U32", ZL_SafetensorsDtype_u32, 4 },
    { "F32", ZL_SafetensorsDtype_f32, 4 },
    { "I64", ZL_SafetensorsDtype_i64, 8 },
    { "U64", ZL_SafetensorsDtype_u64, 8 },
    { "F64", ZL_SafetensorsDtype_f64, 8 },
};
#define ST_NB_DTYPES (sizeof(kDtypes) / sizeof(kDtypes[0]))

size_t ZL_Safetensors_dtypeSize(ZL_SafetensorsDtype dtype)
{
    for (size_t i = 0; i < ST_NB_DTYPES; ++i) {
        if (kDtypes[i].dtype == dtype) {
            return kDtypes[i].size;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------------
 * JSON scanning primitives. Strings are not unescaped: keys and dtypes are
 * compared on their raw bytes, which is what real writers produce.
 * ------------------------------------------------------------------------ */

static void ST_skipWhitespace(ST_Cursor* c)
{
    while (c->ptr < c->end
           && (*c->ptr == ' ' || *c->ptr == '\t' || *c->ptr == '\n'
               || *c->ptr == '\r')) {
        ++c->ptr;
    }
}

static bool ST_consume(ST_Cursor* c, char expected)
{
    ST_skipWhitespace(c);
    if (c->ptr < c->end && *c->ptr == expected) {
        ++c->ptr;
        return true;
    }
    return false;
}

/// Reads a string token; on success, (*str, *size) is its raw content.
static bool ST_readString(ST_Cursor* c, const char** str, size_t* size)
{
    if (!ST_consume(c, '"')) {
        return false;
    }
    const char* const begin = c->ptr;
    while (c->ptr < c->end) {
        const unsigned char ch = (unsigned char)*c->ptr;
        if (ch == '"') {
            *str  = begin;
            *size = (size_t)(c->ptr - begin);
            ++c->ptr;
            return true;
        }
        if (ch < 0x20) {
            return false;
        }
        if (ch == '\\') {
            if (c->end - c->ptr < 2) {
                return false;
            }
            ++c->ptr;
        }
        ++c->ptr;
    }
    return false;
}

static bool ST_strEquals(const char* str, size_t size, const char* literal)
{
    return size == strlen(literal) && memcmp(str, literal, size) == 0;
}

/// Reads a non-negative integer that fits in 63 bits.
static bool ST_readUInt(ST_Cursor* c, uint64_t* value)
{
    ST_skipWhitespace(c);
    if (c->ptr >= c->end || *c->ptr < '0' || *c->ptr > '9') {
        return false;
    }
    uint64_t v = 0;
    while (c->ptr < c->end && *c->ptr >= '0' && *c->ptr <= '9') {
        const uint64_t digit = (uint64_t)(*c->ptr - '0');
        if (v > (UINT64_C(0x7FFFFFFFFFFFFFFF) - digit) / 10) {
            return false;
        }
        v = v * 10 + digit;
        ++c->ptr;
    }
    *value = v;
    return true;
}

static bool ST_skipValue(ST_Cursor* c, int depth);

static bool ST_skipContainer(ST_Cursor* c, char close, bool isObject, int depth)
{
    if (ST_consume(c, close)) {
        return true;
    }
    for (;;) {
        if (isObject) {
            const char* key;
            size_t keySize;
            if (!ST_readString(c, &key, &keySize) || !ST_consume(c, ':')) {
                return false;
            }
        }
        if (!ST_skipValue(c, depth + 1)) {
            return false;
        }
        if (ST_consume(c, close)) {
            return true;
        }
        if (!ST_consume(c, ',')) {
            return false;
        }
    }
}

/// Lenient: any literal-like token is accepted, as skipped values are never
/// interpreted.
static bool ST_skipLiteral(ST_Cursor* c)
{
    const char* const begin = c->ptr;
    while (c->ptr < c->end
           && ((*c->ptr >= '0' && *c->ptr <= '9')
               || (*c->ptr >= 'a' && *c->ptr <= 'z') || *c->ptr == '-'
               || *c->ptr == '+' || *c->ptr == '.' || *c->ptr == 'E')) {
        ++c->ptr;
    }
    return c->ptr > begin;
}

static bool ST_skipValue(ST_Cursor* c, int depth)
{
    if (depth > ST_MAX_JSON_DEPTH) {
        return false;
    }
    ST_skipWhitespace(c);
    if (c->ptr >= c->end) {
        return false;
    }
    switch (*c->ptr) {
        case '"': {
            const char* str;
            size_t size;
            return ST_readString(c, &str, &size);
        }
        case '{':
            ++c->ptr;
            return ST_skipContainer(c, '}', true, depth);
        case '[':
            ++c->ptr;
            return ST_skipContainer(c, ']', false, depth);
        default:
            return ST_skipLiteral(c);
    }
}

/* ------------------------------------------------------------------------
 * Tensor entries
 * ------------------------------------------------------------------------ */

static ZL_SafetensorsDtype ST_parseDtype(const char* str, size_t size)
{
    for (size_t i = 0; i < ST_NB_DTYPES; ++i) {
        if (ST_strEquals(str, size, kDtypes[i].str)) {
            return kDtypes[i].dtype;
        }
    }
    return ZL_SafetensorsDtype_unknown;
}

/// Reads [a, b, ...] into @p values; at most @p capacity values.
static bool ST_readUIntArray(
        ST_Cursor* c,
        uint64_t* values,
        uint32_t capacity,
        uint32_t* count)
{
    *count = 0;
    if (!ST_consume(c, '[')) {
        return false;
    }
    if (ST_consume(c, ']')) {
        return true;
    }
    for (;;) {
        if (*count >= capacity || !ST_readUInt(c, &values[*count])) {
            return false;
        }
        ++*count;
        if (ST_consume(c, ']')) {
            return true;
        }
        if (!ST_consume(c, ',')) {
            return false;
        }
    }
}

typedef struct {
    bool hasDtype;
    bool hasShape;
    bool hasOffsets;
} ST_SeenFields;

static bool ST_readTensorField(
        ST_Cursor* c,
        const char* key,
        size_t keySize,
        ZL_SafetensorsTensor* t,
        ST_SeenFields* seen)
{
    if (ST_strEquals(key, keySize, "dtype")) {
        const char* str;
        size_t size;
        if (!ST_readString(c, &str, &size)) {
            return false;
        }
        t->dtype       = ST_parseDtype(str, size);
        seen->hasDtype = true;
        return true;
    }
    if (ST_strEquals(key, keySize, "shape")) {
        seen->hasShape = true;
        return ST_readUIntArray(
                c, t->shape, ZL_SAFETENSORS_MAX_DIMS, &t->ndims);
    }
    if (ST_strEquals(key, keySize, "data_offsets")) {
        uint64_t offsets[2];
        uint32_t count;
        if (!ST_readUIntArray(c, offsets, 2, &count) || count != 2
            || offsets[0] > offsets[1]) {
            return false;
        }
        t->begin         = offsets[0];
        t->end           = offsets[1];
        seen->hasOffsets = true;
        return true;
    }
    return ST_skipValue(c, 1);
}

static bool ST_readTensor(ST_Cursor* c, ZL_SafetensorsTensor* t)
{
    ST_SeenFields seen = { false, false, false };
    if (!ST_consume(c, '{')) {
        return false;
    }
    if (!ST_consume(c, '}')) {
        for (;;) {
            const char* key;
            size_t keySize;
            if (!ST_readString(c, &key, &keySize) || !ST_consume(c, ':')
                || !ST_readTensorField(c, key, keySize, t, &seen)) {
                return false;
            }
            if (ST_consume(c, '}')) {
                break;
            }
            if (!ST_consume(c, ',')) {
                return false;
            }
        }
    }
    return seen.hasDtype && seen.hasShape && seen.hasOffsets;
}

/// Downgrades the dtype to unknown when the byte size disagrees with the shape.
static void ST_checkByteSize(ZL_SafetensorsTensor* t)
{
    if (t->dtype == ZL_SafetensorsDtype_unknown) {
        return;
    }
    uint64_t bytes = ZL_Safetensors_dtypeSize(t->dtype);
    for (uint32_t d = 0; d < t->ndims; ++d) {
        if (t->shape[d] != 0 && bytes > UINT64_MAX / t->shape[d]) {
            t->dtype = ZL_SafetensorsDtype_unknown;
            return;
        }
        bytes *= t->shape[d];
    }
    if (bytes != t->end - t->begin) {
        t->dtype = ZL_SafetensorsDtype_unknown;
    }
}

static int ST_compareByOffset(const void* a, const void* b)
{
    const ZL_SafetensorsTensor* x = (const ZL_SafetensorsTensor*)a;
    const ZL_SafetensorsTensor* y = (const ZL_SafetensorsTensor*)b;
    if (x->begin != y->begin) {
        return x->begin < y->begin ? -1 : 1;
    }
    return (x->end > y->end) - (x->end < y->end);
}

static const char* ST_parseMembers(
        ST_Cursor* c,
        ZL_SafetensorsTensor* tensors,
        size_t capacity,
        size_t* nbTensors)
{
    *nbTensors = 0;
    if (!ST_consume(c, '{')) {
        return "header is not a JSON object";
    }
    if (ST_consume(c, '}')) {
        return NULL;
    }
    for (;;) {
        const char* name;
        size_t nameSize;
        if (!ST_readString(c, &name, &nameSize) || !ST_consume(c, ':')) {
            return "malformed member name";
        }
        if (ST_strEquals(name, nameSize, "__metadata__")) {
            if (!ST_skipValue(c, 1)) {
                return "malformed __metadata__";
            }
        } else {
            ZL_SafetensorsTensor counted;
            if (*nbTensors >= ZL_SAFETENSORS_MAX_TENSORS
                || (tensors != NULL && *nbTensors >= capacity)) {
                return "too many tensors";
            }
            ZL_SafetensorsTensor* const t =
                    tensors != NULL ? &tensors[*nbTensors] : &counted;
            memset(t, 0, sizeof(*t));
            t->name     = name;
            t->nameSize = nameSize;
            if (!ST_readTensor(c, t)) {
                return "malformed tensor entry";
            }
            ST_checkByteSize(t);
            ++*nbTensors;
        }
        if (ST_consume(c, '}')) {
            return NULL;
        }
        if (!ST_consume(c, ',')) {
            return "malformed header object";
        }
    }
}

static const char* ST_checkLayout(
        ZL_SafetensorsTensor* tensors,
        size_t nbTensors,
        uint64_t dataSize)
{
    qsort(tensors, nbTensors, sizeof(tensors[0]), ST_compareByOffset);
    uint64_t prevEnd = 0;
    for (size_t i = 0; i < nbTensors; ++i) {
        if (tensors[i].end > dataSize) {
            return "tensor data out of bounds";
        }
        if (tensors[i].begin < prevEnd) {
            return "overlapping tensors";
        }
        prevEnd = tensors[i].end;
    }
    return NULL;
}

const char* ZL_Safetensors_parseHeader(
        const void* src,
        size_t srcSize,
        ZL_SafetensorsTensor* tensors,
        size_t tensorsCapacity,
        size_t* nbTensors,
        size_t* dataStart)
{
    *nbTensors = 0;
    *dataStart = 0;
    if (srcSize < 8) {
        return "input too small";
    }
    const uint64_t headerSize = ZL_readLE64(src);
    if (headerSize > ZL_SAFETENSORS_MAX_HEADER_SIZE
        || headerSize > srcSize - 8) {
        return "invalid header size";
    }
    const char* const json = (const char*)src + 8;
    ST_Cursor c            = { json, json + headerSize };
    const char* error =
            ST_parseMembers(&c, tensors, tensorsCapacity, nbTensors);
    if (error != NULL) {
        return error;
    }
    ST_skipWhitespace(&c);
    if (c.ptr != c.end) {
        return "trailing bytes after header object";
    }
    *dataStart = (size_t)(8 + headerSize);
    if (tensors == NULL) {
        return NULL;
    }
    return ST_checkLayout(tensors, *nbTensors, srcSize - *dataStart);
}
