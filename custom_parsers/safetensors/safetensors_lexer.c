// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/safetensors/safetensors_lexer.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "openzl/shared/mem.h"
#include "openzl/shared/utils.h"

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
        ZL_SafetensorsToken* t,
        uint64_t offsets[2],
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
        uint32_t count;
        if (!ST_readUIntArray(c, offsets, 2, &count) || count != 2
            || offsets[0] > offsets[1]) {
            return false;
        }
        seen->hasOffsets = true;
        return true;
    }
    return ST_skipValue(c, 1);
}

/// Reads a tensor entry into @p t, and its data offsets into @p offsets.
static bool
ST_readTensor(ST_Cursor* c, ZL_SafetensorsToken* t, uint64_t offsets[2])
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
                || !ST_readTensorField(c, key, keySize, t, offsets, &seen)) {
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
static void ST_checkByteSize(ZL_SafetensorsToken* t, uint64_t byteSize)
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
    if (bytes != byteSize) {
        t->dtype = ZL_SafetensorsDtype_unknown;
    }
}

/// Reads the members of the header into tensor tokens, in header order, or
/// only counts them when @p tokens is NULL.
/// @returns the number of tensors, or an error.
static ZL_Report ST_parseMembers(
        ST_Cursor* c,
        const char* data,
        uint64_t dataSize,
        ZL_SafetensorsToken* tokens,
        size_t capacity,
        ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    ZL_ERR_IF_NOT(
            ST_consume(c, '{'),
            graph_parser_malformedInput,
            "header is not a JSON object");
    size_t nbTensors = 0;
    if (ST_consume(c, '}')) {
        return ZL_returnValue(nbTensors);
    }
    for (;;) {
        const char* name;
        size_t nameSize;
        ZL_ERR_IF_NOT(
                ST_readString(c, &name, &nameSize) && ST_consume(c, ':'),
                graph_parser_malformedInput,
                "malformed member name after %zu tensors",
                nbTensors);
        if (ST_strEquals(name, nameSize, "__metadata__")) {
            ZL_ERR_IF_NOT(
                    ST_skipValue(c, 1),
                    graph_parser_malformedInput,
                    "malformed __metadata__");
        } else {
            ZL_ERR_IF_GE(
                    nbTensors,
                    ZL_SAFETENSORS_MAX_TENSORS,
                    graph_parser_unhandledInput,
                    "too many tensors");
            ZL_ERR_IF_AND(
                    tokens != NULL,
                    nbTensors >= capacity,
                    parameter_invalid,
                    "too many tensors for the capacity");
            ZL_SafetensorsToken counted;
            ZL_SafetensorsToken* const t =
                    tokens != NULL ? &tokens[nbTensors] : &counted;
            memset(t, 0, sizeof(*t));
            t->type     = ZL_SafetensorsTokenType_tensor;
            t->name     = name;
            t->nameSize = nameSize;
            uint64_t offsets[2];
            ZL_ERR_IF_NOT(
                    ST_readTensor(c, t, offsets),
                    graph_parser_malformedInput,
                    "malformed entry of tensor '%.*s'",
                    (int)nameSize,
                    name);
            ST_checkByteSize(t, offsets[1] - offsets[0]);
            if (tokens != NULL) {
                ZL_ERR_IF_GT(
                        offsets[1],
                        dataSize,
                        graph_parser_malformedInput,
                        "tensor data out of bounds: end of '%.*s' past the "
                        "data section",
                        (int)nameSize,
                        name);
                t->ptr  = data + offsets[0];
                t->size = (size_t)(offsets[1] - offsets[0]);
            }
            ++nbTensors;
        }
        if (ST_consume(c, '}')) {
            return ZL_returnValue(nbTensors);
        }
        ZL_ERR_IF_NOT(
                ST_consume(c, ','),
                graph_parser_malformedInput,
                "malformed header object after %zu tensors",
                nbTensors);
    }
}

static int ST_compareByPosition(const void* a, const void* b)
{
    const ZL_SafetensorsToken* x = (const ZL_SafetensorsToken*)a;
    const ZL_SafetensorsToken* y = (const ZL_SafetensorsToken*)b;
    if (x->ptr != y->ptr) {
        return x->ptr < y->ptr ? -1 : 1;
    }
    return (x->size > y->size) - (x->size < y->size);
}

static ZL_SafetensorsToken ST_gap(const char* begin, const char* end)
{
    return (ZL_SafetensorsToken){
        .ptr  = begin,
        .size = (size_t)(end - begin),
        .type = ZL_SafetensorsTokenType_gap,
    };
}

/// Sorts the @p nbTensors tensor tokens at the start of @p tokens by position,
/// checks that they don't overlap, drops the zero-sized ones, then inserts the
/// header and the gaps, filling @p tokens from its end so that each tensor
/// moves only once.
/// @returns the number of tokens, or an error.
static ZL_Report ST_tile(
        const char* src,
        size_t srcSize,
        size_t dataStart,
        ZL_SafetensorsToken* tokens,
        size_t nbTensors,
        size_t capacity,
        ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    qsort(tokens, nbTensors, sizeof(tokens[0]), ST_compareByPosition);
    const char* const data = src + dataStart;
    const char* end        = data;
    size_t n               = 0;
    size_t nbGaps          = 0;
    for (size_t i = 0; i < nbTensors; ++i) {
        // Offsets in the data section, more telling than pointers in errors.
        const size_t begin   = (size_t)(tokens[i].ptr - data);
        const size_t prevEnd = (size_t)(end - data);
        ZL_ERR_IF_LT(
                begin,
                prevEnd,
                graph_parser_malformedInput,
                "overlapping tensors: '%.*s' starts inside the tensor before",
                (int)tokens[i].nameSize,
                tokens[i].name);
        if (tokens[i].size == 0) {
            continue;
        }
        nbGaps += tokens[i].ptr > end;
        end         = tokens[i].ptr + tokens[i].size;
        tokens[n++] = tokens[i];
    }
    const char* const srcEnd = src + srcSize;
    const size_t total       = 1 + n + nbGaps + (end < srcEnd);
    ZL_ERR_IF_GT(
            total,
            capacity,
            parameter_invalid,
            "too many tokens for the capacity");

    size_t w = total;
    if (end < srcEnd) {
        tokens[--w] = ST_gap(end, srcEnd);
    }
    for (size_t i = n; i-- > 0;) {
        const ZL_SafetensorsToken t = tokens[i];
        const char* const prevEnd =
                i > 0 ? tokens[i - 1].ptr + tokens[i - 1].size : data;
        tokens[--w] = t;
        if (t.ptr > prevEnd) {
            tokens[--w] = ST_gap(prevEnd, t.ptr);
        }
    }
    tokens[0] = (ZL_SafetensorsToken){
        .ptr  = src,
        .size = dataStart,
        .type = ZL_SafetensorsTokenType_header,
    };
    return ZL_returnValue(total);
}

ZL_Report ZL_Safetensors_lex(
        const void* src,
        size_t srcSize,
        ZL_SafetensorsToken* tokens,
        size_t tokensCapacity,
        ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    ZL_ERR_IF_LT(srcSize, 8, graph_parser_malformedInput, "input too small");
    const uint64_t headerSize = ZL_readLE64(src);
    ZL_ERR_IF_GT(
            headerSize,
            ZL_MIN(ZL_SAFETENSORS_MAX_HEADER_SIZE, (uint64_t)srcSize - 8),
            graph_parser_malformedInput,
            "invalid header size");
    const char* const json = (const char*)src + 8;
    const size_t dataStart = (size_t)(8 + headerSize);
    ST_Cursor c            = { json, json + headerSize };
    ZL_TRY_LET_CONST(
            size_t,
            nbTensors,
            ST_parseMembers(
                    &c,
                    (const char*)src + dataStart,
                    srcSize - dataStart,
                    tokens,
                    tokensCapacity,
                    errCtx));
    ST_skipWhitespace(&c);
    ZL_ERR_IF_NE(
            c.ptr,
            c.end,
            graph_parser_malformedInput,
            "trailing bytes after the header object");
    if (tokens == NULL) {
        // header, then each tensor preceded by a gap, then a trailing gap
        return ZL_returnValue(2 * nbTensors + 2);
    }
    return ST_tile(
            (const char*)src,
            srcSize,
            dataStart,
            tokens,
            nbTensors,
            tokensCapacity,
            errCtx);
}
