// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_BENCHMARK_UNITBENCH_BENCHLIST_H
#define ZSTRONG_BENCHMARK_UNITBENCH_BENCHLIST_H

/*
 *  List transforms to be benchmarked
 *  Part of zstrong project
 */

/* ===   Dependencies   === */
#include <assert.h>
#include <stddef.h> // size_t
#include <stdio.h>  // printf, fflush

#include "openzl/shared/xxhash.h"

#include "benchmark/unitBench/bench_entry.h" // Bench_Entry
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_data.h"
#include "openzl/zl_public_nodes.h"
#include "tools/streamdump/stream_dump2.h"

/* ==================================================
 * Custom display functions
 * ================================================== */

// --8<-- [start:decoder-display]
/* display specialized for decompressors :
 * provide speed evaluation in relation to size generated
 * (instead of src, aka compressed size) */
static void decoderResult(
        const char* srcname,
        const char* fname,
        BMK_runTime_t rt,
        size_t srcSize)
{
    double const sec           = rt.nanoSecPerRun / 1000000000.;
    double const nbRunsPerSec  = 1. / sec;
    double const nbBytesPerSec = nbRunsPerSec * (double)rt.sumOfReturn;

    printf("decode %s (%llu KB) with %s into %llu KB (x%.2f) in %.2f ms  ==> %.1f MB/s",
           srcname,
           (unsigned long long)(srcSize >> 10),
           fname,
           (unsigned long long)(rt.sumOfReturn >> 10),
           (double)rt.sumOfReturn / (double)srcSize,
           sec * 1000.,
           nbBytesPerSec / (1 << 20));
}
// --8<-- [end:decoder-display]

/* ==================================================
 * Custom size allocation function
 * ================================================== */
// --8<-- [start:out-identical]
static size_t out_identical(const void* src, size_t srcSize)
{
    (void)src;
    return srcSize;
}
// --8<-- [end:out-identical]

/* ==================================================
 * List of wrappers
 * ==================================================
 * Each transform must be wrapped in a thin redirector conformant with
 * BMK_benchfn_t. BMK_benchfn_t is generic, not specifically designed for
 * transforms.
 *
 * The result of each transform is assumed to be provided as function return
 * value.
 */
// example: zstd
#include "benchmark/unitBench/scenarios/zstd.h"

// openzl standard codecs
#include "benchmark/unitBench/scenarios/codecs/bitSplit.h"
#include "benchmark/unitBench/scenarios/codecs/delta.h"
#include "benchmark/unitBench/scenarios/codecs/dispatch_by_tag.h"
#include "benchmark/unitBench/scenarios/codecs/dispatch_string.h"
#include "benchmark/unitBench/scenarios/codecs/entropy.h"
#include "benchmark/unitBench/scenarios/codecs/estimate.h"
#include "benchmark/unitBench/scenarios/codecs/flatpack.h"
#include "benchmark/unitBench/scenarios/codecs/huffman.h"
#include "benchmark/unitBench/scenarios/codecs/rolz.h"
#include "benchmark/unitBench/scenarios/codecs/tokenize.h"
#include "benchmark/unitBench/scenarios/codecs/transpose.h"

// format-specific parsers
#include "benchmark/unitBench/scenarios/misc/id_list_features.h"
#include "benchmark/unitBench/scenarios/misc/sao.h"

// ===============================================
// ****    Zstrong Graph benchmarks   ****
// ===============================================
#define CLEVEL_DEFAULT 3

/* genericGraphCreation
 * Generic Zstrong Graph creation function
 * Initialize versions and compression level,
 * then run the graph creation function passed as argument
 */
static size_t genericGraphCreation(void* customPayload)
{
    BenchPayload* const bp      = customPayload;
    ZL_Compressor* const cgraph = bp->cgraph;

    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, CLEVEL_DEFAULT))) {
        abort();
    }

    ZL_Report const r = ZL_Compressor_initUsingGraphFn(bp->cgraph, bp->graphF);
    if (ZL_isError(r)) {
        printf("Error initializing %s : %s \n",
               bp->name,
               ZL_ErrorCode_toString(r._code));
        exit(1);
    }
    return 0;
}

static size_t genericGraphCompression(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    assert(dstCapacity >= ZL_compressBound(srcSize));
    BenchPayload* const bp = customPayload;
    const char* graphName  = bp->name;

    ZL_CCtx* const cctx         = bp->cctx;
    ZL_Compressor* const cgraph = bp->cgraph;
    {
        ZL_Report const r = ZL_CCtx_refCompressor(cctx, cgraph);
        if (ZL_isError(r)) {
            printf("Failed loading graph %s : %s \n",
                   graphName,
                   ZL_ErrorCode_toString(r._code));
            exit(1);
        }
    }
    if (bp->intParam) {
        ZL_Report const r = ZL_CCtx_setParameter(
                cctx, ZL_CParam_compressionLevel, bp->intParam);
        if (ZL_isError(r)) {
            printf("Failed setting compression level (%i) => %s \n",
                   bp->intParam,
                   ZL_ErrorCode_toString(r._code));
            exit(1);
        }
    }
    ZL_Report const r = ZL_CCtx_compress(cctx, dst, dstCapacity, src, srcSize);
    if (ZL_isError(r)) {
        printf("Error compressing with %s : %s \n",
               graphName,
               ZL_ErrorCode_toString(r._code));
        exit(1);
    }

    return ZL_validResult(r);
}

#include "benchmark/unitBench/scenarios/sao_graph.h" // sao_graph_v1
#include "benchmark/unitBench/scenarios/sao_sddl1.h" // sao_graph_sddl1
#include "benchmark/unitBench/scenarios/sao_sddl2.h" // sao_graph_sddl2

static ZL_GraphID zstdGraph(ZL_Compressor* cgraph)
{
    (void)cgraph;
    return ZL_GRAPH_ZSTD;
}

// --8<-- [start:example-fn]
static ZL_GraphID fieldLZ32Graph(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_CONVERT_SERIAL_TO_TOKEN4, ZL_GRAPH_FIELD_LZ);
}
// --8<-- [end:example-fn]

static ZL_GraphID fieldLZ64Graph(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_CONVERT_SERIAL_TO_TOKEN8, ZL_GRAPH_FIELD_LZ);
}

static ZL_GraphID delta_fieldLZ32Graph(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_INTERPRET_AS_LE32, ZL_GRAPH_DELTA_FIELD_LZ);
}

static ZL_GraphID delta_fieldLZ64Graph(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_INTERPRET_AS_LE64, ZL_GRAPH_DELTA_FIELD_LZ);
}

static ZL_GraphID rangepack_fieldLZ32Graph(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_INTERPRET_AS_LE32, ZL_GRAPH_RANGE_PACK);
}

static ZL_GraphID rangepack_fieldLZ64Graph(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_INTERPRET_AS_LE64, ZL_GRAPH_RANGE_PACK);
}

static ZL_GraphID rangepack32_zstdGraph(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_INTERPRET_AS_LE32, ZL_GRAPH_RANGE_PACK_ZSTD);
}

static ZL_GraphID rangepack64_zstdGraph(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_INTERPRET_AS_LE64, ZL_GRAPH_RANGE_PACK_ZSTD);
}

static ZL_GraphID tokenize32_delta_fieldlz(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE32,
            ZL_GRAPH_TOKENIZE_DELTA_FIELD_LZ);
}

static ZL_GraphID tokenize64_delta_fieldlz(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE64,
            ZL_GRAPH_TOKENIZE_DELTA_FIELD_LZ);
}

static ZL_GraphID tokenize2Graph(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN2,
            ZL_Compressor_registerTokenizeGraph(
                    cgraph,
                    ZL_Type_struct,
                    false,
                    ZL_GRAPH_STORE,
                    ZL_GRAPH_STORE));
}

static ZL_GraphID tokenSort16bitGraph(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE16,
            ZL_Compressor_registerTokenizeGraph(
                    cgraph,
                    ZL_Type_numeric,
                    true,
                    ZL_GRAPH_STORE,
                    ZL_GRAPH_STORE));
}

// =============================================================
// ****    zs2_decompress (Compatible with custom graphs)   ****
// =============================================================

// zstrong2 decompression
#include <zstd.h>
#include "openzl/shared/mem.h"
#include "openzl/zl_dtransform.h"

static size_t zs2_decompress_outdSize(const void* src, size_t srcSize)
{
    ZL_Report const r = ZL_getDecompressedSize(src, srcSize);
    assert(!ZL_isError(r));
    return ZL_validResult(r);
}
static size_t zs2_decompress_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    const BenchPayload* const payload = customPayload;

    assert(payload != NULL);
    assert(payload->dctx != NULL);
    stream_dump_register_decoders(payload->dctx);
    ZL_Report const r =
            ZL_DCtx_decompress(payload->dctx, dst, dstCapacity, src, srcSize);
    ZL_REQUIRE_SUCCESS(r);

    return ZL_validResult(r);
}

#include "openzl/decompress/dctx2.h"

static ZL_DCtx* g_dctx;

static size_t
zs2_decompress_transform_prep(void* src, size_t srcSize, const BenchPayload* bp)
{
    (void)bp;
    ZL_Report r = ZL_getDecompressedSize(src, srcSize);
    ZL_REQUIRE(!ZL_isError(r));
    size_t const dstSize = ZL_validResult(r);
    void* const dst      = malloc(dstSize);
    ZL_REQUIRE_NN(dst);

    ZL_DCtx* const dctx = ZL_DCtx_create();

    DCTX_preserveStreams(dctx);

    stream_dump_register_decoders(dctx);
    r = ZL_DCtx_decompress(dctx, dst, dstSize, src, srcSize);
    ZL_REQUIRE_SUCCESS(r);
    ZL_REQUIRE_EQ(ZL_validResult(r), dstSize);

    free(dst);

    ZL_REQUIRE_NULL(g_dctx);
    g_dctx = dctx;

    return srcSize;
}

static size_t zs2_decompress_transform_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)src, (void)srcSize, (void)dst, (void)dstCapacity;

    ZL_DCtx* const dctx = g_dctx;
    assert(dctx != NULL);

    assert(customPayload != NULL);
    int const transformID = ((BenchPayload*)customPayload)->intParam;

    ZL_Report const r = DCTX_runTransformID(dctx, (ZL_IDType)transformID);
    ZL_REQUIRE_SUCCESS(r);

    return ZL_validResult(r);
}

static void zs2_decompress_transform_display(
        const char* srcname,
        const char* fname,
        BMK_runTime_t rt,
        size_t srcSize)
{
    ZL_DCtx* const dctx = g_dctx;
    ZL_REQUIRE_NN(dctx);
    ZL_DCtx_free(dctx);
    g_dctx = NULL;
    decoderResult(srcname, fname, rt, srcSize);
}

#include "openzl/shared/varint.h"

static size_t varintEncode32_outdSize(const void* src, size_t srcSize)
{
    (void)src;
    // Upper bound - 5 bytes per 32bit item
    return (srcSize / 4) * 5 + 8;
}

static size_t varintEncode32_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)dstCapacity;
    (void)customPayload;

    size_t nbElts         = srcSize / 4;
    size_t dstSize        = 0;
    const uint32_t* src32 = (const uint32_t*)src;
    for (size_t i = 0; i < nbElts; i++) {
        dstSize += ZL_varintEncode32Fast(src32[i], (uint8_t*)dst + dstSize);
    }
    return dstSize;
}

// --8<-- [start:scenario-list]
/* ==================================================
 * Table of scenarios
 * =============================================== */

#define NB_FUNCS (sizeof(scenarioList) / sizeof(scenarioList[0]))
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

// clang-format off
Bench_Entry const scenarioList[] = {
    { "bitSplitDecode_bf16", bitSplitDecode_bf16_wrapper, .prep = bitSplitDecode_bf16_prep, .outSize = bitSplitDecode_bf16_outSize },
    { "bitSplitDecode_bounded32", bitSplitDecode_bounded32_wrapper, .prep = bitSplitDecode_bounded32_prep, .outSize = out_identical },
    { "bitSplitDecode_fp32", bitSplitDecode_fp32_wrapper, .prep = bitSplitDecode_fp32_prep, .outSize = out_identical },
    { "bitSplitEncode_bf16", bitSplitEncode_bf16_wrapper, .prep = bitSplitEncode_bf16_prep, .outSize = bitSplitEncode_bf16_outSize },
    { "bitSplitEncode_bounded32", bitSplitEncode_bounded32_wrapper, .prep = bitSplitEncode_bounded32_prep, .outSize = out_identical },
    { "bitSplitEncode_fp32", bitSplitEncode_fp32_wrapper, .prep = bitSplitEncode_fp32_prep, .outSize = out_identical },
    { "deltaDecode8", deltaDecode8_wrapper, .outSize = out_identical },
    { "deltaDecode16", deltaDecode16_wrapper, .outSize = out_identical },
    { "deltaEncode32", deltaEncode32_wrapper, .outSize = out_identical },
// --8<-- [end:scenario-list]
    { "deltaDecode32", deltaDecode32_wrapper, .outSize = out_identical },
    { "deltaEncode64", deltaEncode64_wrapper, .outSize = out_identical },
    { "deltaDecode64", deltaDecode64_wrapper, .outSize = out_identical },
    { "deltaFieldLZ32", .graphF = delta_fieldLZ32Graph },
    { "deltaFieldLZ64", .graphF = delta_fieldLZ64Graph },
    { "dimensionality1", dimensionality1_wrapper, .outSize=out_identical },
    { "dimensionality2", dimensionality2_wrapper, .outSize=out_identical },
    { "dimensionality3", dimensionality3_wrapper, .outSize=out_identical },
    { "dimensionality4", dimensionality4_wrapper, .outSize=out_identical },
    { "dimensionality8", dimensionality8_wrapper, .outSize=out_identical },
    { "dispatchStringEncode", dispatchStringEncode_wrapper, .outSize = dispatchStringEncode_outSize },
    { "dispatchStringDecode", dispatchStringDecode_wrapper, .display = decoderResult },
    { "entropyEncode", entropyEncode_wrapper },
    { "entropyDecode", entropyDecode_wrapper, .prep = entropyDecode_preparation, .outSize = entropyDecode_outSize, .display = entropyDecode_displayResult },
    { "estimate1", estimate1_wrapper, .outSize = out_identical },
    { "estimate2", estimate2_wrapper, .outSize = out_identical },
    { "estimateLC4", estimateLC4_wrapper, .outSize = out_identical },
    { "estimateHLL4", estimateHLL4_wrapper, .outSize = out_identical },
    { "estimateLC8", estimateLC8_wrapper, .outSize = out_identical },
    { "estimateHLL8", estimateHLL8_wrapper, .outSize = out_identical },
    // --8<-- [start:example-wrapper-scenario]
    { "exact2", exact2_wrapper, .outSize = out_identical },
    // --8<-- [end:example-wrapper-scenario]
    { "fastlz", fastlz_wrapper },
    // --8<-- [start:example-fn-scenario]
    { "fieldLZ32", .graphF = fieldLZ32Graph },
    // --8<-- [end:example-fn-scenario]
    { "fieldLZ64", .graphF = fieldLZ64Graph },
    { "flatpackDecode16", flatpackDecode_wrapper, .prep = flatpackDecode16_prep, .display = decoderResult },
    { "flatpackDecode32", flatpackDecode_wrapper, .prep = flatpackDecode32_prep, .display = decoderResult },
    { "flatpackDecode48", flatpackDecode_wrapper, .prep = flatpackDecode48_prep, .display = decoderResult },
    { "flatpackDecode64", flatpackDecode_wrapper, .prep = flatpackDecode64_prep, .display = decoderResult },
    { "flatpackDecode128", flatpackDecode_wrapper, .prep = flatpackDecode128_prep, .display = decoderResult },
    { "fseEncode", fseEncode_wrapper },
    { "fseDecode", entropyDecode_wrapper, .prep    = fseDecode_preparation, .outSize = entropyDecode_outSize, .display = entropyDecode_displayResult },
    { "id_list_features", id_list_features_wrapper },
    { "id_score_list_features", id_score_list_features_wrapper },
    { "largeHuffmanEncode", largeHuffmanEncode_wrapper, .display = largeHuffmanEncode_displayResult },
    { "largeHuffmanDecode", largeHuffmanDecode_wrapper, .display = largeHuffmanDecode_displayResult },
    { "rangePack32", .graphF = rangepack_fieldLZ32Graph },
    { "rangePack64", .graphF = rangepack_fieldLZ64Graph },
    { "rangePack32zstd", .graphF = rangepack32_zstdGraph },
    { "rangePack64zstd", .graphF = rangepack64_zstdGraph },
    { "rolz_c", rolzc_wrapper },
    { "sao_v1", .graphF=sao_graph_v1 },
    { "sao_sddl1", .graphF=sao_graph_sddl1 },
    { "sao_sddl2", .graphF=sao_graph_sddl2 },
    { "saoIngest", saoIngest_wrapper },
    { "saoIngestCompiled", saoIngestCompiled_wrapper },
    { "splitBy4", splitBy4_wrapper, .prep = splitBy4_preparation },
    { "splitBy8", splitBy8_wrapper, .prep = splitBy8_preparation },
    { "tokenize2", .graphF=tokenize2Graph },
    { "tokenize2to1Encode", tokenize2to1Encode_wrapper },
    { "tokenize2to1Decode", tokenize2to1Decode_wrapper, .display = tokenize2to1Decode_displayResult },
    { "tokenize4to2Encode", tokenize4to2Encode_wrapper },
    { "tokenizeVarto4Encode", tokenizeVarto4Encode_wrapper, .prep = tokenizeVarto4_preparation },
    { "tokenizeVarto4Decode", tokenizeVarto4Decode_wrapper, .prep = tokVarDecode_prep, .outSize = tokVarDecode_outSize, .display = decoderResult },
    { "tokenize32_delta_fieldlz", .graphF=tokenize32_delta_fieldlz },
    { "tokenize64_delta_fieldlz", .graphF=tokenize64_delta_fieldlz },
    { "tokenSort16", .graphF=tokenSort16bitGraph },
    { "transposeEncode16", transposeEncode16_wrapper, .outSize = out_identical },
    { "transposeDecode16", transposeDecode16_wrapper, .outSize = out_identical },
    { "transposeEncode32", transposeEncode32_wrapper, .outSize = out_identical },
    { "transposeDecode32", transposeDecode32_wrapper, .outSize = out_identical },
    { "transposeEncode64", transposeEncode64_wrapper, .outSize = out_identical },
    { "transposeDecode64", transposeDecode64_wrapper, .outSize = out_identical },
    { "varintEncode32", varintEncode32_wrapper, .outSize = varintEncode32_outdSize },
    { "zs2_decompress", zs2_decompress_wrapper, .outSize = zs2_decompress_outdSize, .display = decoderResult },
    { "zs2_decompress_transform", zs2_decompress_transform_wrapper, .prep = zs2_decompress_transform_prep, .display = zs2_decompress_transform_display },
    // Note: employing genericGraphCreation and genericGraphCompression here just to silence linter warning.
    // These assignments are not required, because these functions are default value when .graphF != NULL
    { "zstd", .graphF=zstdGraph, .init=genericGraphCreation, .func=genericGraphCompression,  },
    { "zstdDirect", zstd_wrapper, .outSize = zstd_outcSize },
    { "zstdd", zstdd_wrapper, .outSize = zstd_outdSize, .display = decoderResult },
    { "zstd_dctx", zstddctx_wrapper, .outSize = zstd_outdSize, .display = decoderResult },
};

#endif // ZSTRONG_BENCHMARK_UNITBENCH_BENCHLIST_H
