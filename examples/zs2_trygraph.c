// Copyright (c) Meta Platforms, Inc. and affiliates.

/* Simple example for ZStrong bruteforce selector API
 *
 * 1) Create a few custom transforms
 * 2) Create bruteforce selector nodes
 * 3) Finalize the pipeline, Compress, Roundtrip
 *
 */

#include "openzl/common/assertion.h"
#include "openzl/zl_data.h"

/* ------   create custom transforms   -------- */
#include "openzl/zl_ctransform.h" // ZL_PipeEncoderDesc

// Made up custom transforms
#include <stdio.h>  // printf
#include <string.h> // memcpy

// Add index%253 to each byte
static size_t
addi(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    printf("processing `addi` \n");
    ZL_ASSERT(dstCapacity >= srcSize);
    memcpy(dst, src, srcSize);
    unsigned char* const dst8 = dst;
    for (unsigned int i = 0; i < srcSize; i++) {
// We turn off conversion errors for this line as they
// trigger on gcc-9 and are problematic to get rid of
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
        dst8[i] += (unsigned char)(i % 253);
#pragma GCC diagnostic pop
    }
    return srcSize;
}

// Add index^2 to each byte
static size_t
addisquare(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    printf("processing `addisquare` \n");
    ZL_ASSERT(dstCapacity >= srcSize);
    memcpy(dst, src, srcSize);
    unsigned char* const dst8 = dst;
    for (unsigned int i = 0; i < srcSize; i++) {
// We turn off conversion errors for this line as they
// trigger on gcc-9 and are problematic to get rid of
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
        dst8[i] += (unsigned char)(i * i);
#pragma GCC diagnostic pop
    }
    return srcSize;
}

#define CT_ADDI_ID 1
#define CT_ADDISQUARE_ID 2

static ZL_PipeEncoderDesc const addi_CDesc = {
    .CTid        = CT_ADDI_ID,
    .transform_f = addi,
};

static ZL_PipeEncoderDesc const addisquare_CDesc = {
    .CTid        = CT_ADDISQUARE_ID,
    .transform_f = addisquare,
};

/* ------   Bruteforce selector utils   -------- */

#include "openzl/common/assertion.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_registerPipeEncoder, ZL_Compressor_registerStaticGraph_fromNode1o, ZL_setStreamDestination_usingGraph
#include "openzl/zl_errors.h"
#include "openzl/zl_selector.h"

static ZL_GraphID bruteforceSelector_f(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* cfns,
        size_t nbCfns)
{
    ZL_ASSERT(nbCfns >= 1);
    ZL_ASSERT(cfns != NULL);

    if (nbCfns == 1) {
        // Don't waste time if we have only one choice here
        return cfns[0];
    }

    const size_t srcSize =
            ZL_Input_numElts(inputStream) * ZL_Input_eltWidth(inputStream);
    size_t min_size         = srcSize;
    ZL_GraphID min_size_gid = ZL_GRAPH_STORE;

    // Find the graph that yields the smallest output and return it
    for (size_t i = 0; i < nbCfns; i++) {
        ZL_Report const ret = ZL_Selector_tryGraph(selCtx, inputStream, cfns[i])
                                      .finalCompressedSize;
        if (ZL_isError(ret))
            continue;
        size_t csize = ZL_validResult(ret);
        if (csize < min_size) {
            min_size     = csize;
            min_size_gid = cfns[i];
        }
    }
    return min_size_gid;
}

/*
 * Creates a typed Graph that selects between multiple other typed Graphs
 * by trying all of them and choosing the one resulting in the smallest output.
 * This operation is wasteful and might be slow.
 * Pay close attention to recursions as this can easily lead to endless
 * recursions. Notes on current implementation:
 * 1. The chosen Graph will execute twice - once when looking for the best Graph
 * and once when its actually exectued.
 * 2. Requires allcations of temporary compression contexts and output buffer.
 */
static ZL_GraphID declareGraph_bruteforceSelectorTyped(
        ZL_Compressor* cgraph,
        ZL_Type stream_type,
        const ZL_GraphID* dst_gids,
        size_t nbGids)
{
    ZL_ASSERT_NN(cgraph);
    ZL_ASSERT_NN(dst_gids);
    ZL_ASSERT_GE(nbGids, 1);

    ZL_SelectorDesc selector_desc = {
        .name           = "brute-force Selector",
        .selector_f     = bruteforceSelector_f,
        .inStreamType   = stream_type,
        .customGraphs   = dst_gids,
        .nbCustomGraphs = nbGids,
    };

    return ZL_Compressor_registerSelectorGraph(cgraph, &selector_desc);
}

// Create an graph from a node and a following graph.
// The node is optional and will be used for compression only if it helps reduce
// the compressed size. Works by utilizing a bruteforce selector to either use
// the node or bypass it directly to nextFnode.
static ZL_GraphID create_graph_from_optional_node_1o(
        ZL_Compressor* cgraph,
        const ZL_NodeID node,
        const ZL_GraphID nextFnode)
{
    ZL_GraphID const graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node, nextFnode);
    ZL_GraphID const selector_graph = declareGraph_bruteforceSelectorTyped(
            cgraph, ZL_Type_serial, ZL_GRAPHLIST(graph, nextFnode));
    return selector_graph;
}

/* ------   Graph function   -------- */

// Register transforms, populate cgraph and return the starting Graph.
static ZL_GraphID dynSelector_graph(ZL_Compressor* cgraph)
{
    // Register custom transforms, creating corresponding Nodes
    ZL_NodeID const node_addi =
            ZL_Compressor_registerPipeEncoder(cgraph, &addi_CDesc);
    ZL_NodeID const node_addisqaure =
            ZL_Compressor_registerPipeEncoder(cgraph, &addisquare_CDesc);

    // Convert nodes into graphs, by optionally including them in the follow.
    // Always end with a ROLZ graph.
    ZL_GraphID const graph_addi = create_graph_from_optional_node_1o(
            cgraph, node_addi, ZL_GRAPH_ZSTD);
    ZL_GraphID const graph_addisqaure = create_graph_from_optional_node_1o(
            cgraph, node_addisqaure, graph_addi);

    return graph_addisqaure;
}

/* ------   compress using the graph   -------- */

#include "openzl/zl_compress.h"

// This graph function is a pass-through for dynSelector_graph(),
// it could be used to add global parameters if need be.
static ZL_GraphID graph_and_parameters(ZL_Compressor* cgraph)
{
    ZL_GraphID const gid = dynSelector_graph(cgraph);

    // If there were some global parameters to setup,
    // they would be setup here, using ZL_Compressor_setParameter().
    //
    // From a design perspective, it's better to separate
    // pure graph functions, from graph + parameters ones.
    //
    // In the future, this will allow composition of complex graphs
    // which include multiple simpler graphs.
    // The last thing we wish when writing complex graphs
    // is a "war on global parameters" among its components.
    //
    // That being said, with "last one wins" rule applied,
    // setting global parameters after the graph
    // guarantees that it would overwrite any global parameter
    // potentially set previously by any inner-node.
    //
    // User could still overwrite these parameters
    // by setting them manually _after_ registering this node function.
    // (an option which is not possible with ZL_compress_usingGraphFn()).
    //
    ZL_REQUIRE(!ZL_isError(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION)));

    return gid;
}

static size_t
compress(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    ZL_ASSERT(dstCapacity >= ZL_compressBound(srcSize));

    ZL_Report const r = ZL_compress_usingGraphFn(
            dst, dstCapacity, src, srcSize, graph_and_parameters);
    ZL_ASSERT(!ZL_isError(r));

    return ZL_validResult(r);
}

/* ------   decompress    -------- */

#include "openzl/zl_dtransform.h"

// Made up transform : just add 1 to first byte. Reverse is subtract 1.
#include <stdio.h>  // printf
#include <string.h> // memcpy

static size_t
subi(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    printf("decoding `addi` \n");
    ZL_ASSERT(dstCapacity >= srcSize);
    ZL_ASSERT(dst != NULL);
    ZL_ASSERT(src != NULL);
    memcpy(dst, src, srcSize);
    unsigned char* const dst8 = dst;
    ZL_ASSERT(srcSize >= 1);
    for (unsigned int i = 0; i < srcSize; i++) {
// We turn off conversion errors for this line as they
// trigger on gcc-9 and are problematic to get rid of
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
        dst8[i] -= (unsigned char)(i % 253);
#pragma GCC diagnostic pop
    }
    return srcSize;
}
static ZL_PipeDecoderDesc const subi_DDesc = {
    .CTid        = CT_ADDI_ID,
    .transform_f = subi,
};

static size_t
subisqaure(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    printf("decoding `addisqaure` \n");
    ZL_ASSERT(dstCapacity >= srcSize);
    ZL_ASSERT(dst != NULL);
    ZL_ASSERT(src != NULL);
    memcpy(dst, src, srcSize);
    unsigned char* const dst8 = dst;
    ZL_ASSERT(srcSize >= 1);
    for (unsigned int i = 0; i < srcSize; i++) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
        dst8[i] -= (unsigned char)(i * i);
#pragma GCC diagnostic pop
    }
    return srcSize;
}
static ZL_PipeDecoderDesc const subisqaure_DDesc = {
    .CTid        = CT_ADDISQUARE_ID,
    .transform_f = subisqaure,
};

static size_t
decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    size_t const dstSize = ZL_validResult(ZL_getDecompressedSize(src, srcSize));
    ZL_ASSERT(dstCapacity >= dstSize);

    ZL_DCtx* const dctx = ZL_DCtx_create();
    ZL_ASSERT(dctx != NULL);

    ZL_Report r;
    r = ZL_DCtx_registerPipeDecoder(dctx, &subi_DDesc);
    ZL_ASSERT(!ZL_isError(r));
    r = ZL_DCtx_registerPipeDecoder(dctx, &subisqaure_DDesc);
    ZL_ASSERT(!ZL_isError(r));

    r = ZL_DCtx_decompress(dctx, dst, dstCapacity, src, srcSize);
    ZL_ASSERT(!ZL_isError(r));

    ZL_DCtx_free(dctx);

    return ZL_validResult(r);
}

/* ------   round trip test   ------ */

#include <stdlib.h>                // exit
#include <string.h>                // memcpy
#include "openzl/common/logging.h" // ZL_g_logLevel

int main(int argc, char const** argv)
{
    printf("\n================== \n");
    printf("zs2_bruteforce_selector example \n");
    printf("------------------ \n");
    printf("ZL_g_logLevel = %i \n", ZL_g_logLevel);
    size_t mode = 3;
    if (argc > 1 && argv[1][0] >= '0' && argv[1][0] <= '3') {
        mode = (size_t)(argv[1][0] - '0');
    }

#define INPUT_SIZE ((size_t)100000)
    unsigned char input[INPUT_SIZE];

// We turn off conversion errors for this loop as they
// trigger on gcc-9 and are problematic to get rid of
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
    for (unsigned int i = 0; i < INPUT_SIZE; i++) {
        input[i] = 0;
        if (mode & 1)
            input[i] -= (unsigned char)(i % 253);
        if (mode & 2)
            input[i] -= (unsigned char)(i * i);
    }
#pragma GCC diagnostic pop
#define COMPRESSED_BOUND ZL_COMPRESSBOUND(INPUT_SIZE)
    char compressed[COMPRESSED_BOUND] = { 0 };

    size_t const compressedSize =
            compress(compressed, COMPRESSED_BOUND, input, INPUT_SIZE);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           INPUT_SIZE,
           compressedSize);

    char decompressed[INPUT_SIZE] = { 2 };

    size_t const decompressedSize =
            decompress(decompressed, INPUT_SIZE, compressed, compressedSize);
    printf("decompressed %zu input bytes into %zu original bytes \n",
           compressedSize,
           decompressedSize);

    // round-trip check
    if (decompressedSize != INPUT_SIZE) {
        printf("Error : decompressed size (%zu) != original size (%zu) \n",
               decompressedSize,
               INPUT_SIZE);
        exit(1);
    }
    if (memcmp(input, decompressed, INPUT_SIZE) != 0) {
        printf("Error : decompressed content differs from original (corruption issue) !!!  \n");
        exit(1);
    }

    printf("round-trip success \n");
    return 0;
}
