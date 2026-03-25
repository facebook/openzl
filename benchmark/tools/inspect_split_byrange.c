// Copyright (c) Meta Platforms, Inc. and affiliates.

/*
 * Inspect split_byrange segmentation on a raw numeric file.
 *
 * Compresses the input with INTERPRET_AS_LE{32,64} → SPLIT_BYRANGE → STORE,
 * then uses the Reflection API to enumerate each segment: count, size,
 * min/max values, and value range width.
 *
 * Usage:
 *   inspect_split_byrange <file> [mode]
 *
 * Modes:
 *   split32          - INTERPRET_AS_LE32 → SPLIT_BYRANGE → STORE
 *   split64          - INTERPRET_AS_LE64 → SPLIT_BYRANGE → STORE (default)
 *   tokenSort64      - INTERPRET_AS_LE64 → tokenize_sorted(delta_int+numeric,
 * numeric) splitTokenSort64 - INTERPRET_AS_LE64 → SPLIT_BYRANGE →
 * tokenize_sorted(delta_int+numeric, numeric)
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openzl/codecs/zl_split.h"
#include "openzl/common/assertion.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_public_nodes.h"
#include "openzl/zl_reflection.h"
#include "tools/fileio/fileio.h"

static ZL_GraphID splitByRange32_store(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE32,
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, ZL_NODE_SPLIT_BYRANGE, ZL_GRAPH_STORE));
}

static ZL_GraphID splitByRange64_store(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE64,
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, ZL_NODE_SPLIT_BYRANGE, ZL_GRAPH_STORE));
}

/* --- tokenSort variants for introspection --- */

#include "openzl/codecs/zl_delta.h"
#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_tokenize.h"

static ZL_GraphID tokenSort64_numeric(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }
    ZL_GraphID alphabetGraph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_DELTA_INT, ZL_GRAPH_NUMERIC);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE64,
            ZL_Compressor_registerTokenizeGraph(
                    cgraph,
                    ZL_Type_numeric,
                    true,
                    alphabetGraph,
                    ZL_GRAPH_NUMERIC));
}

static ZL_GraphID splitByRange64_tokenSort_numeric(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }
    ZL_GraphID alphabetGraph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_DELTA_INT, ZL_GRAPH_NUMERIC);
    ZL_GraphID tokenSort = ZL_Compressor_registerTokenizeGraph(
            cgraph, ZL_Type_numeric, true, alphabetGraph, ZL_GRAPH_NUMERIC);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE64,
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, ZL_NODE_SPLIT_BYRANGE, tokenSort));
}

static void print_minmax_u32(const void* data, size_t nbElts)
{
    const uint32_t* p = (const uint32_t*)data;
    uint32_t lo = p[0], hi = p[0];
    for (size_t i = 1; i < nbElts; i++) {
        if (p[i] < lo)
            lo = p[i];
        if (p[i] > hi)
            hi = p[i];
    }
    printf("  min = %" PRIu32 "  max = %" PRIu32 "  range = %" PRIu32 "\n",
           lo,
           hi,
           hi - lo);
}

static void print_minmax_u64(const void* data, size_t nbElts)
{
    const uint64_t* p = (const uint64_t*)data;
    uint64_t lo = p[0], hi = p[0];
    for (size_t i = 1; i < nbElts; i++) {
        if (p[i] < lo)
            lo = p[i];
        if (p[i] > hi)
            hi = p[i];
    }
    printf("  min = %" PRIu64 "  max = %" PRIu64 "  range = %" PRIu64 "\n",
           lo,
           hi,
           hi - lo);
}

int main(int argc, char* argv[])
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr,
                "Usage: %s <file> [mode]\n"
                "Modes: split32, split64 (default), tokenSort64, splitTokenSort64\n",
                argv[0]);
        return 1;
    }

    const char* filename = argv[1];
    const char* mode     = (argc >= 3) ? argv[2] : "split64";

    int eltWidth = 64; // NOLINT(clang-analyzer-deadcode.DeadStores)
    ZL_GraphFn graphFn;
    if (strcmp(mode, "split32") == 0) {
        eltWidth = 32;
        graphFn  = splitByRange32_store;
    } else if (strcmp(mode, "split64") == 0) {
        eltWidth = 64;
        graphFn  = splitByRange64_store;
    } else if (strcmp(mode, "tokenSort64") == 0) {
        eltWidth = 64;
        graphFn  = tokenSort64_numeric;
    } else if (strcmp(mode, "splitTokenSort64") == 0) {
        eltWidth = 64;
        graphFn  = splitByRange64_tokenSort_numeric;
    } else {
        fprintf(stderr, "Error: unknown mode '%s'\n", mode);
        return 1;
    }
    printf("Mode: %s\n", mode);

    /* 1. Read input */
    ZL_Buffer input     = FIO_createBuffer_fromFilename_orDie(filename);
    ZL_RC inputRC       = ZL_B_getRC(&input);
    size_t srcSize      = ZL_RC_avail(&inputRC);
    void const* srcData = ZL_RC_ptr(&inputRC);

    size_t const eltBytes = (size_t)eltWidth / 8;
    size_t const nbElts   = srcSize / eltBytes;
    printf("Input: %s\n", filename);
    printf("  %zu bytes, %zu elements of %d bits\n", srcSize, nbElts, eltWidth);

    /* Print global min/max */
    printf("  Global value range:\n");
    if (eltWidth == 32) {
        print_minmax_u32(srcData, nbElts);
    } else {
        print_minmax_u64(srcData, nbElts);
    }
    printf("\n");

    /* 2. Compress with splitByRange → STORE */
    ZL_Compressor* cgraph = ZL_Compressor_create();
    ZL_REQUIRE_NN(cgraph);
    ZL_REQUIRE_SUCCESS(ZL_Compressor_initUsingGraphFn(cgraph, graphFn));

    ZL_CCtx* cctx = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx);
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx, cgraph));

    size_t dstCapacity = ZL_compressBound(srcSize);
    void* dst          = malloc(dstCapacity);
    ZL_REQUIRE_NN(dst);

    ZL_Report r = ZL_CCtx_compress(cctx, dst, dstCapacity, srcData, srcSize);
    ZL_REQUIRE_SUCCESS(r);
    size_t compressedSize = ZL_validResult(r);
    printf("Compressed: %zu bytes (ratio x%.2f)\n\n",
           compressedSize,
           (double)srcSize / (double)compressedSize);

    /* 3. Reflect on compressed frame */
    ZL_ReflectionCtx* rctx = ZL_ReflectionCtx_create();
    ZL_REQUIRE_NN(rctx);
    /* no custom decoders needed — only standard codecs */
    ZL_REQUIRE_SUCCESS(
            ZL_ReflectionCtx_setCompressedFrame(rctx, dst, compressedSize));

    size_t nbCodecs  = ZL_ReflectionCtx_getNumCodecs_lastChunk(rctx);
    size_t nbStreams = ZL_ReflectionCtx_getNumStreams_lastChunk(rctx);

    printf("Reflection: %zu codecs, %zu streams\n\n", nbCodecs, nbStreams);

    /* 4. Print per-codec info */
    for (size_t ci = 0; ci < nbCodecs; ci++) {
        ZL_CodecInfo const* codec =
                ZL_ReflectionCtx_getCodec_lastChunk(rctx, ci);
        const char* name = ZL_CodecInfo_getName(codec);
        size_t numIn     = ZL_CodecInfo_getNumInputs(codec);
        size_t numOut    = ZL_CodecInfo_getNumOutputs(codec);
        size_t numVarOut = ZL_CodecInfo_getNumVariableOutputs(codec);
        size_t hdrSize   = ZL_CodecInfo_getHeaderSize(codec);
        printf("Codec %zu: %s  (inputs=%zu, outputs=%zu, varOutputs=%zu, headerSize=%zu)\n",
               ci,
               name,
               numIn,
               numOut,
               numVarOut,
               hdrSize);

        /* Print output segments with value details */
        for (size_t oi = 0; oi < numOut; oi++) {
            ZL_DataInfo const* out = ZL_CodecInfo_getOutput(codec, oi);
            size_t segNbElts       = ZL_DataInfo_getNumElts(out);
            size_t segEltW         = ZL_DataInfo_getEltWidth(out);
            size_t segSize         = ZL_DataInfo_getContentSize(out);
            void const* segData    = ZL_DataInfo_getDataPtr(out);
            size_t segIdx          = ZL_DataInfo_getIndex(out);
            ZL_Type segType        = ZL_DataInfo_getType(out);

            const char* typeStr =
                    "?"; // NOLINT(clang-analyzer-deadcode.DeadStores)
            switch (segType) {
                case ZL_Type_serial:
                    typeStr = "serial";
                    break;
                case ZL_Type_struct:
                    typeStr = "struct";
                    break;
                case ZL_Type_numeric:
                    typeStr = "numeric";
                    break;
                case ZL_Type_string:
                    typeStr = "string";
                    break;
                default:
                    typeStr = "other";
            }

            printf("  output[%zu] (stream %zu): type=%s  eltWidth=%zu  "
                   "nbElts=%zu  size=%zu bytes\n",
                   oi,
                   segIdx,
                   typeStr,
                   segEltW,
                   segNbElts,
                   segSize);

            /* Print min/max for numeric segments */
            if (segData != NULL && segNbElts > 0
                && segType == ZL_Type_numeric) {
                if (segEltW == 4) {
                    print_minmax_u32(segData, segNbElts);
                } else if (segEltW == 8) {
                    print_minmax_u64(segData, segNbElts);
                }
            }
        }
        printf("\n");
    }

    /* Cleanup */
    ZL_ReflectionCtx_free(rctx);
    free(dst);
    ZL_CCtx_free(cctx);
    ZL_Compressor_free(cgraph);
    ZL_B_destroy(&input);

    return 0;
}
