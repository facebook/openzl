// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/onnx/onnx_lexer.h"

#include <stdbool.h>

#include "openzl/common/assertion.h"
#include "openzl/shared/utils.h"

// Protobuf wire types. Groups (3 and 4) are deprecated and unused by ONNX.
enum {
    OX_Wire_varint  = 0,
    OX_Wire_fixed64 = 1,
    OX_Wire_len     = 2,
    OX_Wire_fixed32 = 5,
};

#define OX_MAX_FIELD_NUMBER ((1u << 29) - 1)

// Field numbers of onnx/onnx.proto
#define OX_MODEL_IR_VERSION 1
#define OX_MODEL_GRAPH 7
#define OX_GRAPH_NODE 1
#define OX_GRAPH_INITIALIZER 5
#define OX_GRAPH_SPARSE_INITIALIZER 15
#define OX_NODE_ATTRIBUTE 5
#define OX_ATTRIBUTE_T 5
#define OX_ATTRIBUTE_G 6
#define OX_ATTRIBUTE_TENSORS 10
#define OX_ATTRIBUTE_GRAPHS 11
#define OX_ATTRIBUTE_SPARSE_TENSOR 22
#define OX_ATTRIBUTE_SPARSE_TENSORS 23
#define OX_SPARSE_VALUES 1
#define OX_SPARSE_INDICES 2
#define OX_TENSOR_DIMS 1
#define OX_TENSOR_DATA_TYPE 2
#define OX_TENSOR_NAME 8
#define OX_TENSOR_RAW_DATA 9

size_t ZL_ONNX_dataTypeBits(uint32_t dataType)
{
    switch (dataType) {
        case ZL_ONNXDataType_uint2:
        case ZL_ONNXDataType_int2:
            return 2;
        case ZL_ONNXDataType_uint4:
        case ZL_ONNXDataType_int4:
        case ZL_ONNXDataType_float4e2m1:
            return 4;
        case ZL_ONNXDataType_uint8:
        case ZL_ONNXDataType_int8:
        case ZL_ONNXDataType_bool:
        case ZL_ONNXDataType_float8e4m3fn:
        case ZL_ONNXDataType_float8e4m3fnuz:
        case ZL_ONNXDataType_float8e5m2:
        case ZL_ONNXDataType_float8e5m2fnuz:
        case ZL_ONNXDataType_float8e8m0:
            return 8;
        case ZL_ONNXDataType_uint16:
        case ZL_ONNXDataType_int16:
        case ZL_ONNXDataType_float16:
        case ZL_ONNXDataType_bfloat16:
            return 16;
        case ZL_ONNXDataType_float:
        case ZL_ONNXDataType_int32:
        case ZL_ONNXDataType_uint32:
            return 32;
        case ZL_ONNXDataType_int64:
        case ZL_ONNXDataType_uint64:
        case ZL_ONNXDataType_double:
        case ZL_ONNXDataType_complex64:
            return 64;
        case ZL_ONNXDataType_complex128:
            return 128;
        default:
            return 0;
    }
}

/* ------------------------------------------------------------------------
 * Protobuf reading
 * ------------------------------------------------------------------------ */

typedef struct {
    const uint8_t* ptr;
    const uint8_t* end;
} OX_Cursor;

typedef struct {
    uint32_t number;
    uint32_t wireType;
    uint64_t value; // varint fields
    OX_Cursor data; // length-delimited fields
} OX_Field;

/// Varints take at most 10 bytes.
static bool OX_readVarint(OX_Cursor* c, uint64_t* value)
{
    uint64_t v = 0;
    for (unsigned shift = 0; shift < 64; shift += 7) {
        if (c->ptr == c->end) {
            return false;
        }
        const uint8_t byte = *c->ptr++;
        v |= (uint64_t)(byte & 0x7f) << shift;
        if (byte < 0x80) {
            *value = v;
            return true;
        }
    }
    return false;
}

static bool OX_skip(OX_Cursor* c, uint64_t size)
{
    if (size > (uint64_t)(c->end - c->ptr)) {
        return false;
    }
    c->ptr += size;
    return true;
}

static bool OX_readField(OX_Cursor* c, OX_Field* f)
{
    uint64_t key;
    if (!OX_readVarint(c, &key) || key >> 3 == 0
        || key >> 3 > OX_MAX_FIELD_NUMBER) {
        return false;
    }
    *f = (OX_Field){ .number   = (uint32_t)(key >> 3),
                     .wireType = (uint32_t)(key & 7) };
    switch (f->wireType) {
        case OX_Wire_varint:
            return OX_readVarint(c, &f->value);
        case OX_Wire_fixed64:
            return OX_skip(c, 8);
        case OX_Wire_fixed32:
            return OX_skip(c, 4);
        case OX_Wire_len: {
            uint64_t size;
            if (!OX_readVarint(c, &size)) {
                return false;
            }
            const uint8_t* const begin = c->ptr;
            if (!OX_skip(c, size)) {
                return false;
            }
            f->data = (OX_Cursor){ begin, c->ptr };
            return true;
        }
        default:
            return false;
    }
}

/* ------------------------------------------------------------------------
 * Tokens
 * ------------------------------------------------------------------------ */

typedef struct {
    ZL_ONNXToken* tokens; // NULL when only counting
    size_t capacity;
    size_t nbTokens;
    size_t nbTensors;
    const char* end; // end of the last token
    ZL_ErrorContext* errCtx;
} OX_Lexer;

static ZL_Report OX_emit(OX_Lexer* lx, ZL_ONNXToken token)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(lx->errCtx);
    ZL_ERR_IF_GE(
            lx->nbTokens,
            lx->capacity,
            parameter_invalid,
            "too many tokens for the capacity");
    lx->tokens[lx->nbTokens++] = token;
    lx->end                    = token.ptr + token.size;
    return ZL_returnSuccess();
}

/// Emits the bytes since the last token, up to @p end, as the header or a gap.
static ZL_Report OX_emitSpan(OX_Lexer* lx, const char* end)
{
    // The walk reads each message front to back and only descends into the
    // field it is reading, and tensors contain no tensors: their raw data come
    // in file order, without overlap, whatever the input.
    ZL_ASSERT(end >= lx->end);
    if (end == lx->end) {
        return ZL_returnSuccess();
    }
    const ZL_ONNXToken span = {
        .ptr  = lx->end,
        .size = (size_t)(end - lx->end),
        .type = lx->nbTokens == 0 ? ZL_ONNXTokenType_header
                                  : ZL_ONNXTokenType_gap,
    };
    return OX_emit(lx, span);
}

static ZL_Report OX_addTensor(OX_Lexer* lx, const ZL_ONNXToken* t)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(lx->errCtx);
    ZL_ERR_IF_GE(
            lx->nbTensors,
            ZL_ONNX_MAX_TENSORS,
            graph_parser_unhandledInput,
            "too many tensors");
    ++lx->nbTensors;
    if (lx->tokens == NULL) {
        return ZL_returnSuccess();
    }
    ZL_ERR_IF_ERR(OX_emitSpan(lx, t->ptr));
    return OX_emit(lx, *t);
}

/* ------------------------------------------------------------------------
 * Messages
 * ------------------------------------------------------------------------ */

/// Dimensions past ZL_ONNX_MAX_DIMS are counted but not kept.
static void OX_addDim(ZL_ONNXToken* t, uint64_t* nbDims, uint64_t dim)
{
    if (*nbDims < ZL_ONNX_MAX_DIMS) {
        t->dims[*nbDims] = dim;
    }
    ++*nbDims;
}

/// @returns whether the raw data of @p t holds exactly its elements.
static bool OX_sizeMatches(const ZL_ONNXToken* t)
{
    const size_t bits = ZL_ONNX_dataTypeBits(t->dataType);
    if (bits == 0) {
        return false;
    }
    uint64_t nbElts = 1;
    for (uint32_t d = 0; d < t->ndims; ++d) {
        if (t->dims[d] != 0 && nbElts > UINT64_MAX / t->dims[d]) {
            return false;
        }
        nbElts *= t->dims[d];
    }
    if (nbElts > UINT64_MAX / bits) {
        return false;
    }
    // Packed elements may leave the last byte partly used.
    const uint64_t nbBits = nbElts * bits;
    return nbBits / 8 + (nbBits % 8 != 0) == t->size;
}

static ZL_Report OX_walkTensor(OX_Lexer* lx, OX_Cursor c)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(lx->errCtx);
    ZL_ONNXToken t  = { .type = ZL_ONNXTokenType_tensor };
    uint64_t nbDims = 0;
    OX_Cursor raw   = { NULL, NULL };
    while (c.ptr < c.end) {
        OX_Field f;
        ZL_ERR_IF_NOT(
                OX_readField(&c, &f),
                graph_parser_malformedInput,
                "malformed tensor");
        if (f.number == OX_TENSOR_DIMS && f.wireType == OX_Wire_varint) {
            OX_addDim(&t, &nbDims, f.value);
        } else if (f.number == OX_TENSOR_DIMS && f.wireType == OX_Wire_len) {
            // packed dims
            while (f.data.ptr < f.data.end) {
                uint64_t dim;
                ZL_ERR_IF_NOT(
                        OX_readVarint(&f.data, &dim),
                        graph_parser_malformedInput,
                        "malformed tensor dims");
                OX_addDim(&t, &nbDims, dim);
            }
        } else if (
                f.number == OX_TENSOR_DATA_TYPE
                && f.wireType == OX_Wire_varint) {
            t.dataType = f.value <= UINT32_MAX ? (uint32_t)f.value
                                               : ZL_ONNXDataType_undefined;
        } else if (f.number == OX_TENSOR_NAME && f.wireType == OX_Wire_len) {
            t.name     = (const char*)f.data.ptr;
            t.nameSize = (size_t)(f.data.end - f.data.ptr);
        } else if (
                f.number == OX_TENSOR_RAW_DATA && f.wireType == OX_Wire_len) {
            // As in protobuf, the last value of the field wins.
            raw = f.data;
        }
    }
    if (raw.ptr == raw.end) {
        return ZL_returnSuccess();
    }
    t.ptr   = (const char*)raw.ptr;
    t.size  = (size_t)(raw.end - raw.ptr);
    t.ndims = (uint32_t)ZL_MIN(nbDims, (uint64_t)ZL_ONNX_MAX_DIMS);
    if (nbDims > ZL_ONNX_MAX_DIMS || !OX_sizeMatches(&t)) {
        t.dataType = ZL_ONNXDataType_undefined;
    }
    return OX_addTensor(lx, &t);
}

static ZL_Report OX_walkSparseTensor(OX_Lexer* lx, OX_Cursor c)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(lx->errCtx);
    while (c.ptr < c.end) {
        OX_Field f;
        ZL_ERR_IF_NOT(
                OX_readField(&c, &f),
                graph_parser_malformedInput,
                "malformed sparse tensor");
        if (f.wireType == OX_Wire_len
            && (f.number == OX_SPARSE_VALUES
                || f.number == OX_SPARSE_INDICES)) {
            ZL_ERR_IF_ERR(OX_walkTensor(lx, f.data));
        }
    }
    return ZL_returnSuccess();
}

static ZL_Report OX_walkGraph(OX_Lexer* lx, OX_Cursor c, int depth);

static ZL_Report OX_walkAttribute(OX_Lexer* lx, OX_Cursor c, int depth)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(lx->errCtx);
    while (c.ptr < c.end) {
        OX_Field f;
        ZL_ERR_IF_NOT(
                OX_readField(&c, &f),
                graph_parser_malformedInput,
                "malformed attribute");
        if (f.wireType != OX_Wire_len) {
            continue;
        }
        switch (f.number) {
            case OX_ATTRIBUTE_T:
            case OX_ATTRIBUTE_TENSORS:
                ZL_ERR_IF_ERR(OX_walkTensor(lx, f.data));
                break;
            case OX_ATTRIBUTE_SPARSE_TENSOR:
            case OX_ATTRIBUTE_SPARSE_TENSORS:
                ZL_ERR_IF_ERR(OX_walkSparseTensor(lx, f.data));
                break;
            case OX_ATTRIBUTE_G:
            case OX_ATTRIBUTE_GRAPHS:
                if (depth < ZL_ONNX_MAX_GRAPH_DEPTH) {
                    ZL_ERR_IF_ERR(OX_walkGraph(lx, f.data, depth + 1));
                }
                break;
            default:
                break;
        }
    }
    return ZL_returnSuccess();
}

static ZL_Report OX_walkNode(OX_Lexer* lx, OX_Cursor c, int depth)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(lx->errCtx);
    while (c.ptr < c.end) {
        OX_Field f;
        ZL_ERR_IF_NOT(
                OX_readField(&c, &f),
                graph_parser_malformedInput,
                "malformed node");
        if (f.number == OX_NODE_ATTRIBUTE && f.wireType == OX_Wire_len) {
            ZL_ERR_IF_ERR(OX_walkAttribute(lx, f.data, depth));
        }
    }
    return ZL_returnSuccess();
}

static ZL_Report OX_walkGraph(OX_Lexer* lx, OX_Cursor c, int depth)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(lx->errCtx);
    while (c.ptr < c.end) {
        OX_Field f;
        ZL_ERR_IF_NOT(
                OX_readField(&c, &f),
                graph_parser_malformedInput,
                "malformed graph");
        if (f.wireType != OX_Wire_len) {
            continue;
        }
        switch (f.number) {
            case OX_GRAPH_NODE:
                ZL_ERR_IF_ERR(OX_walkNode(lx, f.data, depth));
                break;
            case OX_GRAPH_INITIALIZER:
                ZL_ERR_IF_ERR(OX_walkTensor(lx, f.data));
                break;
            case OX_GRAPH_SPARSE_INITIALIZER:
                ZL_ERR_IF_ERR(OX_walkSparseTensor(lx, f.data));
                break;
            default:
                break;
        }
    }
    return ZL_returnSuccess();
}

ZL_Report ZL_ONNX_lex(
        const void* src,
        size_t srcSize,
        ZL_ONNXToken* tokens,
        size_t tokensCapacity,
        ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    OX_Lexer lx = {
        .tokens   = tokens,
        .capacity = tokensCapacity,
        .end      = (const char*)src,
        .errCtx   = errCtx,
    };
    OX_Cursor c       = { (const uint8_t*)src, (const uint8_t*)src + srcSize };
    bool hasIrVersion = false;
    bool hasGraph     = false;
    while (c.ptr < c.end) {
        OX_Field f;
        ZL_ERR_IF_NOT(
                OX_readField(&c, &f),
                graph_parser_malformedInput,
                "not a protobuf message: malformed field at offset %zu",
                (size_t)(c.ptr - (const uint8_t*)src));
        if (f.number == OX_MODEL_IR_VERSION && f.wireType == OX_Wire_varint) {
            hasIrVersion = true;
        } else if (f.number == OX_MODEL_GRAPH && f.wireType == OX_Wire_len) {
            hasGraph = true;
            ZL_ERR_IF_ERR(OX_walkGraph(&lx, f.data, 0));
        }
    }
    ZL_ERR_IF_NOT(
            hasIrVersion && hasGraph,
            graph_parser_malformedInput,
            "not an ONNX model: no ir_version or no graph");
    if (tokens == NULL) {
        // the header, then each tensor followed by a gap
        return ZL_returnValue(2 * lx.nbTensors + 1);
    }
    ZL_ERR_IF_ERR(OX_emitSpan(&lx, (const char*)src + srcSize));
    return ZL_returnValue(lx.nbTokens);
}
