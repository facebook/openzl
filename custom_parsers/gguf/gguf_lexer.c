// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/gguf/gguf_lexer.h"

#include <stdlib.h>
#include <string.h>

#include "openzl/shared/mem.h"
#include "openzl/shared/utils.h"

#define GG_DEFAULT_ALIGNMENT 32
#define GG_MAX_ARRAY_DEPTH 8
// name size, number of dimensions, type and offset
#define GG_MIN_TENSOR_INFO_SIZE 24
// Errors print names up to this size: GGUF does not bound them.
#define GG_MAX_PRINTED_NAME 64

// Types of metadata values
enum {
    GG_Value_u8     = 0,
    GG_Value_i8     = 1,
    GG_Value_u16    = 2,
    GG_Value_i16    = 3,
    GG_Value_u32    = 4,
    GG_Value_i32    = 5,
    GG_Value_f32    = 6,
    GG_Value_bool   = 7,
    GG_Value_string = 8,
    GG_Value_array  = 9,
    GG_Value_u64    = 10,
    GG_Value_i64    = 11,
    GG_Value_f64    = 12,
};

typedef struct {
    const uint8_t* ptr;
    const uint8_t* end;
} GG_Cursor;

typedef struct {
    uint32_t type;
    uint16_t blockElts;
    uint16_t blockBytes;
} GG_TypeLayout;

// Block sizes of the block structs of ggml/src/ggml-common.h, checked against
// llama.cpp b11046 (upstream 60081bb2b5b3294165a4d67c5cbeebe74c868014).
static const GG_TypeLayout kTypes[] = {
    { ZL_GGUFType_f32, 1, 4 },        { ZL_GGUFType_f16, 1, 2 },
    { ZL_GGUFType_q4_0, 32, 18 },     { ZL_GGUFType_q4_1, 32, 20 },
    { ZL_GGUFType_q5_0, 32, 22 },     { ZL_GGUFType_q5_1, 32, 24 },
    { ZL_GGUFType_q8_0, 32, 34 },     { ZL_GGUFType_q8_1, 32, 36 },
    { ZL_GGUFType_q2_k, 256, 84 },    { ZL_GGUFType_q3_k, 256, 110 },
    { ZL_GGUFType_q4_k, 256, 144 },   { ZL_GGUFType_q5_k, 256, 176 },
    { ZL_GGUFType_q6_k, 256, 210 },   { ZL_GGUFType_q8_k, 256, 292 },
    { ZL_GGUFType_iq2_xxs, 256, 66 }, { ZL_GGUFType_iq2_xs, 256, 74 },
    { ZL_GGUFType_iq3_xxs, 256, 98 }, { ZL_GGUFType_iq1_s, 256, 50 },
    { ZL_GGUFType_iq4_nl, 32, 18 },   { ZL_GGUFType_iq3_s, 256, 110 },
    { ZL_GGUFType_iq2_s, 256, 82 },   { ZL_GGUFType_iq4_xs, 256, 136 },
    { ZL_GGUFType_i8, 1, 1 },         { ZL_GGUFType_i16, 1, 2 },
    { ZL_GGUFType_i32, 1, 4 },        { ZL_GGUFType_i64, 1, 8 },
    { ZL_GGUFType_f64, 1, 8 },        { ZL_GGUFType_iq1_m, 256, 56 },
    { ZL_GGUFType_bf16, 1, 2 },       { ZL_GGUFType_tq1_0, 256, 54 },
    { ZL_GGUFType_tq2_0, 256, 66 },   { ZL_GGUFType_mxfp4, 32, 17 },
    { ZL_GGUFType_nvfp4, 64, 36 },    { ZL_GGUFType_q1_0, 128, 18 },
    { ZL_GGUFType_q2_0, 64, 18 },
};
#define GG_NB_TYPES (sizeof(kTypes) / sizeof(kTypes[0]))

bool ZL_GGUF_typeLayout(uint32_t type, size_t* blockElts, size_t* blockBytes)
{
    for (size_t i = 0; i < GG_NB_TYPES; ++i) {
        if (kTypes[i].type == type) {
            *blockElts  = kTypes[i].blockElts;
            *blockBytes = kTypes[i].blockBytes;
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------------
 * Reading primitives
 * ------------------------------------------------------------------------ */

static uint64_t GG_remaining(const GG_Cursor* c)
{
    return (uint64_t)(c->end - c->ptr);
}

static bool GG_skip(GG_Cursor* c, uint64_t size)
{
    if (size > GG_remaining(c)) {
        return false;
    }
    c->ptr += size;
    return true;
}

static bool GG_readU32(GG_Cursor* c, uint32_t* value)
{
    if (GG_remaining(c) < 4) {
        return false;
    }
    *value = ZL_readLE32(c->ptr);
    c->ptr += 4;
    return true;
}

static bool GG_readU64(GG_Cursor* c, uint64_t* value)
{
    if (GG_remaining(c) < 8) {
        return false;
    }
    *value = ZL_readLE64(c->ptr);
    c->ptr += 8;
    return true;
}

static bool GG_readString(GG_Cursor* c, const char** str, size_t* size)
{
    uint64_t length;
    if (!GG_readU64(c, &length) || length > GG_remaining(c)) {
        return false;
    }
    *str  = (const char*)c->ptr;
    *size = (size_t)length;
    c->ptr += length;
    return true;
}

static bool GG_strEquals(const char* str, size_t size, const char* literal)
{
    return size == strlen(literal) && memcmp(str, literal, size) == 0;
}

/* ------------------------------------------------------------------------
 * Metadata
 * ------------------------------------------------------------------------ */

/// @returns the size of a scalar value of @p type, or 0 if it is not a scalar.
static size_t GG_scalarSize(uint32_t type)
{
    switch (type) {
        case GG_Value_u8:
        case GG_Value_i8:
        case GG_Value_bool:
            return 1;
        case GG_Value_u16:
        case GG_Value_i16:
            return 2;
        case GG_Value_u32:
        case GG_Value_i32:
        case GG_Value_f32:
            return 4;
        case GG_Value_u64:
        case GG_Value_i64:
        case GG_Value_f64:
            return 8;
        default:
            return 0;
    }
}

/// Each element that is not a scalar consumes at least 8 bytes, so loops end
/// when the input does.
static bool GG_skipValue(GG_Cursor* c, uint32_t type, int depth)
{
    const size_t scalarSize = GG_scalarSize(type);
    if (scalarSize != 0) {
        return GG_skip(c, scalarSize);
    }
    if (type == GG_Value_string) {
        const char* str;
        size_t size;
        return GG_readString(c, &str, &size);
    }
    if (type != GG_Value_array || depth >= GG_MAX_ARRAY_DEPTH) {
        return false;
    }
    uint32_t eltType;
    uint64_t count;
    if (!GG_readU32(c, &eltType) || !GG_readU64(c, &count)) {
        return false;
    }
    const size_t eltSize = GG_scalarSize(eltType);
    if (eltSize != 0) {
        return count <= GG_remaining(c) / eltSize
                && GG_skip(c, count * eltSize);
    }
    for (uint64_t i = 0; i < count; ++i) {
        if (!GG_skipValue(c, eltType, depth + 1)) {
            return false;
        }
    }
    return true;
}

/// @returns the alignment of the data section, or an error.
static ZL_Report
GG_parseMetadata(GG_Cursor* c, uint64_t count, ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    uint32_t alignment = GG_DEFAULT_ALIGNMENT;
    bool hasAlignment  = false;
    for (uint64_t i = 0; i < count; ++i) {
        const char* key;
        size_t keySize;
        uint32_t type;
        ZL_ERR_IF_NOT(
                GG_readString(c, &key, &keySize) && GG_readU32(c, &type),
                graph_parser_malformedInput,
                "malformed metadata key");
        if (GG_strEquals(key, keySize, "general.alignment")) {
            // llama.cpp rejects any duplicate key.
            ZL_ERR_IF(
                    hasAlignment,
                    graph_parser_malformedInput,
                    "duplicate general.alignment");
            hasAlignment = true;
            ZL_ERR_IF_NE(
                    type,
                    GG_Value_u32,
                    graph_parser_malformedInput,
                    "general.alignment is not a u32");
            ZL_ERR_IF_NOT(
                    GG_readU32(c, &alignment),
                    graph_parser_malformedInput,
                    "truncated general.alignment");
            ZL_ERR_IF(
                    alignment == 0 || (alignment & (alignment - 1)) != 0,
                    graph_parser_malformedInput,
                    "general.alignment %u is not a power of 2",
                    alignment);
        } else {
            ZL_ERR_IF_NOT(
                    GG_skipValue(c, type, 0),
                    graph_parser_malformedInput,
                    "malformed value of metadata '%.*s'",
                    (int)ZL_MIN(keySize, GG_MAX_PRINTED_NAME),
                    key);
        }
    }
    return ZL_returnValue(alignment);
}

/* ------------------------------------------------------------------------
 * Tensor infos
 * ------------------------------------------------------------------------ */

/// Reads a tensor info into @p t, and its data offset into @p begin.
static bool GG_readTensorInfo(GG_Cursor* c, ZL_GGUFToken* t, uint64_t* begin)
{
    memset(t, 0, sizeof(*t));
    t->type = ZL_GGUFTokenType_tensor;
    uint32_t ndims;
    if (!GG_readString(c, &t->name, &t->nameSize) || !GG_readU32(c, &ndims)
        || ndims > ZL_GGUF_MAX_DIMS) {
        return false;
    }
    t->ndims = ndims;
    for (uint32_t d = 0; d < t->ndims; ++d) {
        if (!GG_readU64(c, &t->ne[d])) {
            return false;
        }
    }
    return GG_readU32(c, &t->ggmlType) && GG_readU64(c, begin);
}

/// @returns the byte size of @p t starting at @p begin, or 0 when its type is
/// unknown or its rows are not made of whole blocks. ggml only writes rows of
/// whole blocks, so such a tensor is malformed, and its bytes are left to the
/// gaps rather than splitting off a valid prefix.
static uint64_t GG_tensorSize(const ZL_GGUFToken* t, uint64_t begin)
{
    size_t blockElts, blockBytes;
    if (!ZL_GGUF_typeLayout(t->ggmlType, &blockElts, &blockBytes)) {
        return 0;
    }
    uint64_t nbElts = 1;
    for (uint32_t d = 0; d < t->ndims; ++d) {
        if (t->ne[d] != 0 && nbElts > UINT64_MAX / t->ne[d]) {
            return 0;
        }
        nbElts *= t->ne[d];
    }
    // As in ggml, missing dimensions count as 1: a tensor without dimensions
    // holds one element.
    const uint64_t rowElts = t->ndims > 0 ? t->ne[0] : 1;
    if (rowElts % blockElts != 0) {
        return 0;
    }
    const uint64_t nbBlocks = nbElts / blockElts;
    if (nbBlocks > (UINT64_MAX - begin) / blockBytes) {
        return 0;
    }
    return nbBlocks * blockBytes;
}

/// Reads @p count tensor infos into tensor tokens, whose offsets are relative
/// to @p data.
static ZL_Report GG_readTensorTokens(
        GG_Cursor* c,
        size_t count,
        const char* data,
        uint64_t dataSize,
        ZL_GGUFToken* tokens,
        ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    for (size_t i = 0; i < count; ++i) {
        ZL_GGUFToken* const t = &tokens[i];
        uint64_t begin;
        ZL_ERR_IF_NOT(
                GG_readTensorInfo(c, t, &begin),
                graph_parser_malformedInput,
                "malformed tensor info %zu",
                i);
        const uint64_t end = begin + GG_tensorSize(t, begin);
        ZL_ERR_IF_GT(
                end,
                dataSize,
                graph_parser_malformedInput,
                "tensor data out of bounds: end of '%.*s' past the data section",
                (int)ZL_MIN(t->nameSize, GG_MAX_PRINTED_NAME),
                t->name);
        t->ptr  = data + begin;
        t->size = (size_t)(end - begin);
    }
    return ZL_returnSuccess();
}

static int GG_compareByPosition(const void* a, const void* b)
{
    const ZL_GGUFToken* x = (const ZL_GGUFToken*)a;
    const ZL_GGUFToken* y = (const ZL_GGUFToken*)b;
    if (x->ptr != y->ptr) {
        return x->ptr < y->ptr ? -1 : 1;
    }
    return (x->size > y->size) - (x->size < y->size);
}

static ZL_GGUFToken GG_gap(const char* begin, const char* end)
{
    return (ZL_GGUFToken){
        .ptr  = begin,
        .size = (size_t)(end - begin),
        .type = ZL_GGUFTokenType_gap,
    };
}

/// Sorts the @p nbTensors tensor tokens at the start of @p tokens by position,
/// drops those of unknown size, checks that the others don't overlap, then
/// inserts the header and the gaps, filling @p tokens from its end so that each
/// tensor moves only once.
/// @returns the number of tokens, or an error.
static ZL_Report GG_tile(
        const char* src,
        size_t srcSize,
        size_t dataStart,
        ZL_GGUFToken* tokens,
        size_t nbTensors,
        size_t capacity,
        ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    qsort(tokens, nbTensors, sizeof(tokens[0]), GG_compareByPosition);
    const char* const data = src + dataStart;
    const char* end        = data;
    size_t n               = 0;
    size_t nbGaps          = 0;
    for (size_t i = 0; i < nbTensors; ++i) {
        if (tokens[i].size == 0) {
            continue;
        }
        // Offsets in the data section, more telling than pointers in errors.
        const size_t begin   = (size_t)(tokens[i].ptr - data);
        const size_t prevEnd = (size_t)(end - data);
        ZL_ERR_IF_LT(
                begin,
                prevEnd,
                graph_parser_malformedInput,
                "overlapping tensors: '%.*s' starts inside the tensor before",
                (int)ZL_MIN(tokens[i].nameSize, GG_MAX_PRINTED_NAME),
                tokens[i].name);
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
        tokens[--w] = GG_gap(end, srcEnd);
    }
    for (size_t i = n; i-- > 0;) {
        const ZL_GGUFToken t = tokens[i];
        const char* const prevEnd =
                i > 0 ? tokens[i - 1].ptr + tokens[i - 1].size : data;
        tokens[--w] = t;
        if (t.ptr > prevEnd) {
            tokens[--w] = GG_gap(prevEnd, t.ptr);
        }
    }
    tokens[0] = (ZL_GGUFToken){
        .ptr  = src,
        .size = dataStart,
        .type = ZL_GGUFTokenType_header,
    };
    return ZL_returnValue(total);
}

ZL_Report ZL_GGUF_lex(
        const void* src,
        size_t srcSize,
        ZL_GGUFToken* tokens,
        size_t tokensCapacity,
        ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    ZL_ERR_IF_NOT(
            srcSize >= 4 && memcmp(src, "GGUF", 4) == 0,
            graph_parser_malformedInput,
            "not a GGUF file");
    GG_Cursor c = { (const uint8_t*)src + 4, (const uint8_t*)src + srcSize };
    uint32_t version;
    uint64_t tensorCount, metadataCount;
    ZL_ERR_IF_NOT(
            GG_readU32(&c, &version) && GG_readU64(&c, &tensorCount)
                    && GG_readU64(&c, &metadataCount),
            graph_parser_malformedInput,
            "input too small");
    // Version 1 used 32-bit counts. Version 3 only added big-endian files,
    // which read as a huge version here: versions 2 and 3 of little-endian
    // files share the same layout and offsets.
    ZL_ERR_IF(
            version != 2 && version != 3,
            graph_parser_unhandledInput,
            "unsupported GGUF version %u",
            version);
    ZL_TRY_LET_CONST(
            size_t, alignment, GG_parseMetadata(&c, metadataCount, errCtx));
    ZL_ERR_IF_GT(
            tensorCount,
            (uint64_t)ZL_GGUF_MAX_TENSORS,
            graph_parser_unhandledInput,
            "too many tensors");
    ZL_ERR_IF_GT(
            tensorCount,
            GG_remaining(&c) / GG_MIN_TENSOR_INFO_SIZE,
            graph_parser_malformedInput,
            "more tensors than the input can describe");
    const size_t count = (size_t)tensorCount;
    if (tokens == NULL) {
        // header, then each tensor preceded by a gap, then a trailing gap
        return ZL_returnValue(2 * count + 2);
    }
    ZL_ERR_IF_GT(
            count,
            tokensCapacity,
            parameter_invalid,
            "too many tensors for the capacity");

    // The data section starts after the tensor infos, which are read once to
    // find it, then again to place their tokens.
    const GG_Cursor infos = c;
    for (size_t i = 0; i < count; ++i) {
        ZL_GGUFToken t;
        uint64_t begin;
        ZL_ERR_IF_NOT(
                GG_readTensorInfo(&c, &t, &begin),
                graph_parser_malformedInput,
                "malformed tensor info %zu",
                i);
    }
    const uint64_t infoEnd = (uint64_t)(c.ptr - (const uint8_t*)src);
    uint64_t start         = (infoEnd + alignment - 1) / alignment * alignment;
    if (start > srcSize) {
        ZL_ERR_IF_NE(
                count,
                0,
                graph_parser_malformedInput,
                "data section starts past the end of the input");
        start = srcSize;
    }
    c = infos;
    ZL_ERR_IF_ERR(GG_readTensorTokens(
            &c,
            count,
            (const char*)src + start,
            srcSize - start,
            tokens,
            errCtx));
    return GG_tile(
            (const char*)src,
            srcSize,
            (size_t)start,
            tokens,
            count,
            tokensCapacity,
            errCtx);
}
