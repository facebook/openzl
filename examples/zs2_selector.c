// Copyright (c) Meta Platforms, Inc. and affiliates.

/* Simple example for ZStrong selector API using sector declare helper */

#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h" // uint32_t, ZL_readLE32
#include "openzl/zl_data.h"
#include "openzl/zl_public_nodes.h"

/* ------   create custom transforms   -------- */
#include "openzl/zl_ctransform.h" // ZL_PipeEncoderDesc

// Made up custom transforms
#include <stdio.h>  // printf
#include <string.h> // memcpy

// Add index%256 to each byte
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
        dst8[i] += (unsigned char)i;
#pragma GCC diagnostic pop
    }
    return srcSize;
}

// Constant encode zeros
static size_t
zerobufferEncode(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    printf("processing `zerobuffer` \n");
    for (size_t i = 0; i < srcSize; i++) {
        ZL_ASSERT_EQ(((const uint8_t*)src)[i], 0);
    }
    ZL_ASSERT_GE(dstCapacity, 4);
    ZL_writeLE32(dst, (uint32_t)srcSize);
    return 4;
}

#define CT_ADDI_ID 1
#define CT_ZEROBUFFER_ID 2

static ZL_PipeEncoderDesc const addi_CDesc = {
    .CTid        = CT_ADDI_ID,
    .transform_f = addi,
};

static ZL_PipeEncoderDesc const zerobuffer_CDesc = {
    .CTid        = CT_ZEROBUFFER_ID,
    .transform_f = zerobufferEncode,
};

/* ------   Selector   -------- */

#include "openzl/common/assertion.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_registerPipeEncoder, ZL_Compressor_registerStaticGraph_fromNode1o, ZL_setStreamDestination_usingGraph
#include "openzl/zl_errors.h"
#include "openzl/zl_selector.h"

#include "openzl/zl_selector_declare_helper.h"

ZL_DECLARE_SELECTOR(
        my_selector,
        ZL_Type_serial,
        SUCCESSOR(myaddi),
        SUCCESSOR(myzerobuffer),
        SUCCESSOR(flatpack, ZL_GRAPH_FLATPACK))
ZL_GraphID my_selector_impl(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const my_selector_Successors* successors)
{
    (void)selCtx;
    if (ZL_Input_numElts(inputStream) < 4) {
        return successors->flatpack;
    }
    uint8_t const* src = ZL_Input_ptr(inputStream);
    if (!memcmp(src, "\x00\xff\xfe\xff", 2)) {
        return successors->myaddi;
    }
    if (!memcmp(src, "\x00\x00\x00\x00", 4)) {
        return successors->myzerobuffer;
    }
    return successors->flatpack;
}

/* ------   create node graph   -------- */

// Register transforms, build cgraph and return the starting Graph.
static ZL_GraphID dynSelector_graph(ZL_Compressor* cgraph)
{
    // Register custom transforms, creating corresponding Nodes
    ZL_NodeID const node_addi =
            ZL_Compressor_registerPipeEncoder(cgraph, &addi_CDesc);
    ZL_NodeID const node_zerobuffer =
            ZL_Compressor_registerPipeEncoder(cgraph, &zerobuffer_CDesc);

    ZL_GraphID const graph_zerobuffer =
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, node_zerobuffer, ZL_GRAPH_STORE);
    ZL_GraphID const graph_addi = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_addi, graph_zerobuffer);

    ZL_GraphID const graph_selector = my_selector_declareGraph(
            cgraph, my_selector_successors_init(graph_addi, graph_zerobuffer));

    return graph_selector;
}

/* ------   compress using the graph   -------- */

#include "openzl/zl_compress.h"

// This graph function follows the ZL_Node_1i_f definition
// It's a pass-through for dynSelector_graph(),
// and could be used to add global parameters on top of it.
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
        dst8[i] -= (unsigned char)i;
#pragma GCC diagnostic pop
    }
    return srcSize;
}
static ZL_PipeDecoderDesc const subi_DDesc = {
    .CTid        = CT_ADDI_ID,
    .transform_f = subi,
};

static size_t
zerobufferDecode(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    printf("decoding `zerobuffer` \n");
    ZL_ASSERT(srcSize >= 4);
    const uint32_t len = ZL_readLE32(src);
    ZL_ASSERT(len <= dstCapacity);
    memset(dst, 0, len);
    return (size_t)len;
}

static size_t zerobufferDecodeBound(const void* src, size_t srcSize)
{
    ZL_ASSERT(srcSize >= 4);
    const uint32_t len = ZL_readLE32(src);
    return (size_t)len;
}

static ZL_PipeDecoderDesc const zerobuffer_DDesc = {
    .CTid        = CT_ZEROBUFFER_ID,
    .dstBound_f  = zerobufferDecodeBound,
    .transform_f = zerobufferDecode,
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
    r = ZL_DCtx_registerPipeDecoder(dctx, &zerobuffer_DDesc);
    ZL_ASSERT(!ZL_isError(r));

    r = ZL_DCtx_decompress(dctx, dst, dstCapacity, src, srcSize);
    ZL_ASSERT(!ZL_isError(r));

    ZL_DCtx_free(dctx);

    return ZL_validResult(r);
}

/* ------   round trip test   ------ */

#include <stdio.h>                 // printf
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
            input[i] -= (unsigned char)(i % 256);
        if (mode & 2)
            input[i] -= 1;
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
