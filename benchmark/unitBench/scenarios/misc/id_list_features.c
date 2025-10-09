// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "benchmark/unitBench/scenarios/misc/id_list_features.h"

#include <zstd.h>
#include "openzl/shared/mem.h"
#include "openzl/zl_ctransform.h"

// ===============================================
// ****    id_score_list_features map.value   ****
// ===============================================

static void shiftToU16(uint16_t* dst16, const uint32_t* src32, size_t nbElts)
{
    uint32_t min = (uint32_t)-1;
    // find min
    for (size_t n = 0; n < nbElts; n++) {
        if (src32[n] < min)
            min = src32[n];
    }
    ZL_writeLE32(dst16, min);
    dst16 += 2;
    // convert
    for (size_t n = 0; n < nbElts; n++) {
        uint32_t const v = src32[n] - min;
        assert(v < 65536);
        dst16[n] = (uint16_t)v;
    }
}

static size_t s16Capa(const void* src, size_t srcSize)
{
    (void)src;
    assert(srcSize % 4 == 0);
    return 4 + (srcSize / 2);
}

static size_t
s16_enc(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    assert(dstCapacity >= s16Capa(src, srcSize));
    (void)dstCapacity;
    shiftToU16(dst, src, srcSize / 4);
    return s16Capa(src, srcSize);
}

#define CT_S16_ID 10
static ZL_PipeEncoderDesc const s16_CDesc = {
    .CTid        = CT_S16_ID,
    .dstBound_f  = s16Capa,
    .transform_f = s16_enc,
};

// zstd as a custom transform
static size_t zstd_dstCapacity(const void* src, size_t srcSize)
{
    (void)src;
    return ZSTD_compressBound(srcSize);
}

#define CLEVEL 1
static size_t
zstd_compress(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    return ZSTD_compress(dst, dstCapacity, src, srcSize, CLEVEL);
}

#define CT_ZSTD_ID 1 // ID must be identical on both encoding and decoding sides
static ZL_PipeEncoderDesc const zstd_CDesc = {
    .CTid        = CT_ZSTD_ID,
    .dstBound_f  = zstd_dstCapacity,
    .transform_f = zstd_compress,
};

static ZL_GraphID id_score_list_features_graph(ZL_Compressor* cgraph)
{
    // Register the custom transform, creating a corresponding Node
    ZL_NodeID const node_s16 =
            ZL_Compressor_registerPipeEncoder(cgraph, &s16_CDesc);
    ZL_NodeID const node_zstd =
            ZL_Compressor_registerPipeEncoder(cgraph, &zstd_CDesc);

    ZL_GraphID const graph_zstd = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_zstd, ZL_GRAPH_STORE);
    (void)graph_zstd; // ZL_GRAPH_FASTLZ ZL_GRAPH_ROLZ
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_s16, graph_zstd);
}

size_t id_score_list_features_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)customPayload;
    assert(dstCapacity >= ZL_compressBound(srcSize));

    ZL_Report const r = ZL_compress_usingGraphFn(
            dst, dstCapacity, src, srcSize, id_score_list_features_graph);
    assert(!ZL_isError(r));

    return ZL_validResult(r);
}

// ===============================================
// ****    id_list_features array.value   ****
// ===============================================

static void shiftU64ToU16(uint16_t* dst16, const uint64_t* src64, size_t nbElts)
{
    uint64_t min = (uint64_t)-1;
    // find min
    for (size_t n = 0; n < nbElts; n++) {
        if (src64[n] < min)
            min = src64[n];
    }
    ZL_writeLE64(dst16, min);
    dst16 += 4;
    // convert
    for (size_t n = 0; n < nbElts; n++) {
        uint64_t const v = src64[n] - min;
        assert(v < 65536);
        dst16[n] = (uint16_t)v;
    }
}

static size_t s64to16Capa(const void* src, size_t srcSize)
{
    (void)src;
    assert(srcSize % 8 == 0);
    return 8 + (srcSize / 4);
}

static size_t
s64to16_enc(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    assert(dstCapacity >= s64to16Capa(src, srcSize));
    (void)dstCapacity;
    shiftU64ToU16(dst, src, srcSize / 8);
    return s64to16Capa(src, srcSize);
}

#define CT_S64to16_ID 20
static ZL_PipeEncoderDesc const s64to16_CDesc = {
    .CTid        = CT_S64to16_ID,
    .dstBound_f  = s64to16Capa,
    .transform_f = s64to16_enc,
};

static ZL_GraphID id_list_features_graph(ZL_Compressor* cgraph)
{
    // Register the custom transform, creating a corresponding Node
    ZL_NodeID const node_s64to16 =
            ZL_Compressor_registerPipeEncoder(cgraph, &s64to16_CDesc);
    ZL_NodeID const node_zstd =
            ZL_Compressor_registerPipeEncoder(cgraph, &zstd_CDesc);

    ZL_GraphID const graph_zstd = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_zstd, ZL_GRAPH_STORE);
    ZL_GraphID const graph_tr16 =
            ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
                    cgraph,
                    ZL_NODELIST(
                            ZL_NODE_CONVERT_SERIAL_TO_TOKEN2,
                            ZL_NODE_TRANSPOSE_SPLIT),
                    graph_zstd);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_s64to16, graph_tr16);
}

size_t id_list_features_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)customPayload;
    assert(dstCapacity >= ZL_compressBound(srcSize));

    ZL_Report const r = ZL_compress_usingGraphFn(
            dst, dstCapacity, src, srcSize, id_list_features_graph);
    assert(!ZL_isError(r));

    return ZL_validResult(r);
}
