// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/shared_components/tensor_graphs.h"

#include <stdbool.h>
#include <stdlib.h>

#include "openzl/codecs/zl_ace.h"
#include "openzl/codecs/zl_conversion.h"
#include "openzl/codecs/zl_entropy.h"
#include "openzl/codecs/zl_float_deconstruct.h"
#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_split_by_struct.h"
#include "openzl/codecs/zl_store.h"
#include "openzl/codecs/zl_transformer.h"
#include "openzl/codecs/zl_zstd.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/utils.h"

ZL_STATIC_ASSERT(
        ZL_TensorClass_atypical32 == 11,
        "ZL_TensorClass is append-only: serialized compressors rely on it");

// A float tensor is atypical when an evenly spaced sample of its elements has
// fewer than 1/TG_DISTINCT_RATIO distinct values (masks, constants, ...).
#define TG_MIN_ELTS_FOR_SAMPLING 64
#define TG_DISTINCT_RATIO 8

static int TG_compareU32(const void* a, const void* b)
{
    const uint32_t x = *(const uint32_t*)a;
    const uint32_t y = *(const uint32_t*)b;
    return (x > y) - (x < y);
}

static bool TG_isAtypical(
        const uint8_t* data,
        size_t nbElts,
        size_t eltSize,
        uint32_t* sample)
{
    if (nbElts < TG_MIN_ELTS_FOR_SAMPLING) {
        return false;
    }
    const size_t nbSamples = ZL_MIN(nbElts, (size_t)ZL_TENSOR_SAMPLE_SIZE);
    const size_t stride    = nbElts / nbSamples;
    for (size_t i = 0; i < nbSamples; ++i) {
        const uint8_t* const elt = data + i * stride * eltSize;
        sample[i] = eltSize == 2 ? ZL_readLE16(elt) : ZL_readLE32(elt);
    }
    qsort(sample, nbSamples, sizeof(sample[0]), TG_compareU32);
    size_t nbDistinct = 1;
    for (size_t i = 1; i < nbSamples; ++i) {
        nbDistinct += sample[i] != sample[i - 1];
    }
    return nbDistinct * TG_DISTINCT_RATIO < nbSamples;
}

ZL_TensorClass ZL_TensorGraphs_refineClass(
        ZL_TensorClass cls,
        const uint8_t* data,
        size_t size,
        uint32_t* sample)
{
    if (cls != ZL_TensorClass_bf16 && cls != ZL_TensorClass_f16
        && cls != ZL_TensorClass_f32) {
        return cls;
    }
    const size_t eltSize = cls == ZL_TensorClass_f32 ? 4 : 2;
    if (!TG_isAtypical(data, size / eltSize, eltSize, sample)) {
        return cls;
    }
    return eltSize == 2 ? ZL_TensorClass_atypical16 : ZL_TensorClass_atypical32;
}

/// Trainable successor for numeric data of width @p interpretNode.
static ZL_GraphID TG_numericSuccessor(
        ZL_Compressor* compressor,
        ZL_NodeID interpretNode,
        ZL_GraphID defaultGraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor,
            interpretNode,
            ZL_Compressor_buildACEGraphWithDefault(compressor, defaultGraph));
}

static ZL_GraphID TG_floatSuccessor(
        ZL_Compressor* compressor,
        ZL_NodeID interpretNode,
        ZL_NodeID deconstructNode)
{
    const ZL_GraphID deconstruct = ZL_Compressor_registerStaticGraph_fromNode(
            compressor,
            deconstructNode,
            ZL_GRAPHLIST(ZL_GRAPH_STORE, ZL_GRAPH_FSE));
    return TG_numericSuccessor(compressor, interpretNode, deconstruct);
}

/// float16 keeps 2 mantissa bits next to sign and exponent in its high byte,
/// so it is split by byte. The low byte is noise in native float16 weights,
/// where FSE falls back to raw storage at no cost, but weights converted from
/// bfloat16 leave its 3 lowest bits at zero.
static ZL_GraphID TG_f16Successor(ZL_Compressor* compressor)
{
    const size_t fieldSizes[2]     = { 1, 1 };
    const ZL_GraphID byteGraphs[2] = { ZL_GRAPH_FSE, ZL_GRAPH_FSE };
    return ZL_Compressor_buildACEGraphWithDefault(
            compressor,
            ZL_Compressor_registerSplitByStructGraph(
                    compressor, fieldSizes, byteGraphs, 2));
}

void ZL_TensorGraphs_buildSuccessors(
        ZL_Compressor* compressor,
        ZL_GraphID successors[ZL_TensorClass_count])
{
    successors[ZL_TensorClass_header] =
            ZL_Compressor_buildACEGraphWithDefault(compressor, ZL_GRAPH_ZSTD);
    successors[ZL_TensorClass_raw] = ZL_Compressor_buildACEGraphWithDefault(
            compressor, ZL_GRAPH_COMPRESS_GENERIC);
    // Same default as raw, but kept apart so that training specializes typed
    // byte tensors (masks, quantized weights) separately from unknown bytes.
    successors[ZL_TensorClass_int8] = ZL_Compressor_buildACEGraphWithDefault(
            compressor, ZL_GRAPH_COMPRESS_GENERIC);
    successors[ZL_TensorClass_int16] = TG_numericSuccessor(
            compressor, ZL_NODE_INTERPRET_AS_LE16, ZL_GRAPH_COMPRESS_GENERIC);
    successors[ZL_TensorClass_int32] = TG_numericSuccessor(
            compressor, ZL_NODE_INTERPRET_AS_LE32, ZL_GRAPH_COMPRESS_GENERIC);
    successors[ZL_TensorClass_int64] = TG_numericSuccessor(
            compressor, ZL_NODE_INTERPRET_AS_LE64, ZL_GRAPH_COMPRESS_GENERIC);
    successors[ZL_TensorClass_f16]  = TG_f16Successor(compressor);
    successors[ZL_TensorClass_bf16] = TG_floatSuccessor(
            compressor,
            ZL_NODE_INTERPRET_AS_LE16,
            ZL_NODE_BFLOAT16_DECONSTRUCT);
    successors[ZL_TensorClass_f32] = TG_floatSuccessor(
            compressor, ZL_NODE_INTERPRET_AS_LE32, ZL_NODE_FLOAT32_DECONSTRUCT);
    successors[ZL_TensorClass_f64] = TG_numericSuccessor(
            compressor, ZL_NODE_INTERPRET_AS_LE64, ZL_GRAPH_COMPRESS_GENERIC);
    successors[ZL_TensorClass_atypical16] = TG_numericSuccessor(
            compressor,
            ZL_NODE_INTERPRET_AS_LE16,
            ZL_GRAPH_TRANSFORMER_NUMERIC);
    successors[ZL_TensorClass_atypical32] = TG_numericSuccessor(
            compressor,
            ZL_NODE_INTERPRET_AS_LE32,
            ZL_GRAPH_TRANSFORMER_NUMERIC);
}
