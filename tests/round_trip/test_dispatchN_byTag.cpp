// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// standard C
#include <stdio.h> // printf

// Zstrong
#include "openzl/common/debug.h" // ZL_REQUIRE
#include "openzl/zl_compress.h"
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_graph_api.h"

namespace {

#if 0 // for debug only
void printHexa(const void* p, size_t size)
{
    const unsigned char* const b = (const unsigned char*)p;
    for (size_t n = 0; n < size; n++) {
        printf(" %02X ", b[n]);
    }
    printf("\n");
}
#endif

/* ------   create a custom nodes   -------- */

// None needed

/* ------   create custom parsers   -------- */

static unsigned const kTag = 0xdeadbeef;

static unsigned fillInstructions(
        unsigned* tags,
        size_t* segSizes,
        size_t nbSegments,
        size_t srcSize)
{
    ZL_REQUIRE(nbSegments == 5);
    segSizes[0] = srcSize / 5;
    tags[0]     = 0;
    segSizes[1] = srcSize / 4;
    tags[1]     = 1;
    segSizes[2] = srcSize / 5;
    tags[2]     = 0;
    segSizes[3] = srcSize / 6;
    tags[3]     = 2;
    segSizes[4] =
            srcSize - (segSizes[0] + segSizes[1] + segSizes[2] + segSizes[3]);
    tags[4] = 0;
    return 3;
}

// Following ZL_SplitParserFn() signature
static ZL_DispatchInstructions dispatchNBT_customParser(
        ZL_DispatchState* ds,
        const ZL_Input* in) noexcept
{
    // Verify that the opaque pointer is correct
    assert((void const*)&kTag == ZL_DispatchState_getOpaquePtr(ds));
    assert(kTag == *(unsigned const*)ZL_DispatchState_getOpaquePtr(ds));

    assert(in != nullptr);
    assert(ZL_Input_type(in) == ZL_Type_serial);
    size_t const srcSize = ZL_Input_numElts(in);
    // Let's arbitrarily split input into 5 segments divided into 3 tags
    size_t const nbSegments = 5;
    size_t* const segSizes =
            (size_t*)ZL_DispatchState_malloc(ds, nbSegments * sizeof(size_t));
    if (segSizes == nullptr)
        return (ZL_DispatchInstructions){ nullptr, nullptr, 0, 0 };
    unsigned* const tags = (unsigned*)ZL_DispatchState_malloc(
            ds, nbSegments * sizeof(unsigned));
    if (tags == nullptr)
        return (ZL_DispatchInstructions){ nullptr, nullptr, 0, 0 };
    const auto nbTags = fillInstructions(tags, segSizes, nbSegments, srcSize);
    return (ZL_DispatchInstructions){ segSizes, tags, nbSegments, nbTags };
}

// this parser just fails, on purpose, for tests
static ZL_DispatchInstructions dispatchN_fail(
        ZL_DispatchState* ds,
        const ZL_Input* in) noexcept
{
    (void)in;
    (void)ds;
    return (ZL_DispatchInstructions){ nullptr, nullptr, 0, 0 };
}

// This parser is incorrect, it provides an invalid size vector
static ZL_DispatchInstructions dispatchN_wrongSizes(
        ZL_DispatchState* ds,
        const ZL_Input* in) noexcept
{
    assert(in != nullptr);
    assert(ZL_Input_type(in) == ZL_Type_serial);
    size_t const srcSize = ZL_Input_numElts(in);
    // Let's arbitrarily split input into 3 segments,
    // sum length of these segments is intentionally too short.
    size_t const nbSegments = 3;
    size_t* const segSizes =
            (size_t*)ZL_DispatchState_malloc(ds, nbSegments * sizeof(size_t));
    if (segSizes == nullptr)
        return (ZL_DispatchInstructions){ nullptr, nullptr, 0, 0 };
    unsigned* const tags = (unsigned*)ZL_DispatchState_malloc(
            ds, nbSegments * sizeof(unsigned));
    if (tags == nullptr)
        return (ZL_DispatchInstructions){ nullptr, nullptr, 0, 0 };
    segSizes[0] = srcSize / 5;
    segSizes[1] = srcSize / 4;
    segSizes[2] = srcSize / 3;
    tags[0] = tags[1] = tags[2] = 0;
    // Condition for this parser to be wrong
    assert((segSizes[0] + segSizes[1] + segSizes[2]) < srcSize);
    return (ZL_DispatchInstructions){ segSizes, tags, nbSegments, 1 };
}

// This parser is incorrect, it provides an invalid tags vector
static ZL_DispatchInstructions dispatchN_wrongTags(
        ZL_DispatchState* ds,
        const ZL_Input* in) noexcept
{
    assert(in != nullptr);
    assert(ZL_Input_type(in) == ZL_Type_serial);
    size_t const srcSize = ZL_Input_numElts(in);
    // Let's arbitrarily split input into 3 segments
    // One tag value is intentionally out of bound
    size_t const nbSegments = 3;
    size_t* const segSizes =
            (size_t*)ZL_DispatchState_malloc(ds, nbSegments * sizeof(size_t));
    if (segSizes == nullptr)
        return (ZL_DispatchInstructions){ nullptr, nullptr, 0, 0 };
    unsigned* const tags = (unsigned*)ZL_DispatchState_malloc(
            ds, nbSegments * sizeof(unsigned));
    if (tags == nullptr)
        return (ZL_DispatchInstructions){ nullptr, nullptr, 0, 0 };
    segSizes[0]           = srcSize / 3;
    segSizes[1]           = srcSize / 4;
    segSizes[2]           = srcSize - (segSizes[0] + segSizes[1]);
    unsigned const nbTags = 2;
    tags[0]               = 0;
    tags[1]               = 1;
    tags[2]               = 2; // wrong value (>= nbTags)
    return (ZL_DispatchInstructions){ segSizes, tags, nbSegments, nbTags };
}

static ZL_Report dispatchN_specializeNode(
        ZL_Graph* gctx,
        ZL_Edge* sctxs[],
        size_t nbIns) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    assert(nbIns == 1);
    ZL_Edge* sctx            = sctxs[0];
    ZL_Input const* const in = ZL_Edge_getData(sctx);
    size_t const srcSize     = ZL_Input_numElts(in);
    // Let's arbitrarily split input into 3 segments
    size_t segSizes[5];
    unsigned tags[5];
    auto const nbTags = fillInstructions(tags, segSizes, 5, srcSize);
    ZL_DispatchInstructions instructions = {
        .segmentSizes = segSizes,
        .tags         = tags,
        .nbSegments   = 5,
        .nbTags       = nbTags,
    };
    ZL_TRY_LET(ZL_EdgeList, out, ZL_Edge_runDispatchNode(sctx, &instructions));
    for (size_t i = 0; i < out.nbEdges; ++i) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(out.edges[i], ZL_GRAPH_STORE));
    }
    return ZL_returnSuccess();
}

static ZL_Report
dispatchN_manyTags(ZL_Graph* graph, ZL_Edge* ins[], size_t nbIns) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    assert(nbIns == 1);
    ZL_Edge* in                 = ins[0];
    ZL_Input const* const input = ZL_Edge_getData(in);
    size_t const srcSize        = ZL_Input_numElts(input);

    ZL_REQUIRE_EQ(srcSize % 4, 0);
    size_t const nbSegments = srcSize / 4;

    size_t* const segSizes = (size_t*)ZL_Graph_getScratchSpace(
            graph, nbSegments * sizeof(size_t));
    uint32_t* const tags = (uint32_t*)ZL_Graph_getScratchSpace(
            graph, nbSegments * sizeof(uint32_t));

    uint32_t nbTags = 0;
    for (size_t i = 0; i < nbSegments; ++i) {
        segSizes[i] = 4;
        tags[i]     = nbTags++;
    }

    ZL_DispatchInstructions instructions = {
        .segmentSizes = segSizes,
        .tags         = tags,
        .nbSegments   = nbSegments,
        .nbTags       = nbTags,
    };

    ZL_TRY_LET(ZL_EdgeList, out, ZL_Edge_runDispatchNode(in, &instructions));
    for (size_t i = 0; i < out.nbEdges; ++i) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(out.edges[i], ZL_GRAPH_STORE));
    }
    return ZL_returnSuccess();
}

/* ------   create custom graph   -------- */

// This graph will necessarily fail at runtime
// because it received no splitting instructions (no parsing function)
static ZL_GraphID dispatchNGraph_noInstructions(ZL_Compressor* cgraph) noexcept
{
    printf("running dispatchNGraph with no Instructions \n");
    ZL_REQUIRE(!ZL_isError(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION)));

    ZL_GraphID const gid[] = { ZL_GRAPH_STORE, ZL_GRAPH_STORE, ZL_GRAPH_STORE };

    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, ZL_NODE_DISPATCH, gid, 3);
}

static ZL_GraphID dispatchNGraph_byExtParser(
        ZL_Compressor* cgraph,
        ZL_DispatchParserFn dnbtepf,
        void const* opaque) noexcept
{
    printf("running dispatchNGraph_byExtParser \n");
    ZL_REQUIRE(!ZL_isError(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION)));

    const ZL_NodeID dispatchNByExtParser =
            ZL_Compressor_registerDispatchNode(cgraph, dnbtepf, opaque);

    ZL_GraphID const gid[] = { ZL_GRAPH_STORE, ZL_GRAPH_STORE, ZL_GRAPH_STORE };

    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, dispatchNByExtParser, gid, 3);
}

static ZL_GraphID dispatchNGraph_byDynGraph(
        ZL_Compressor* cgraph,
        ZL_FunctionGraphFn dynGraph) noexcept
{
    printf("running dispatchNGraph_bySpecializeNode \n");
    ZL_REQUIRE(!ZL_isError(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION)));

    ZL_Type inStreamType      = ZL_Type_serial;
    ZL_FunctionGraphDesc desc = {
        .name                = "dispatchNGraph_byDynGraph",
        .graph_f             = dynGraph,
        .inputTypeMasks      = &inStreamType,
        .nbInputs            = 1,
        .lastInputIsVariable = false,
    };

    return ZL_Compressor_registerFunctionGraph(cgraph, &desc);
}

static ZL_GraphID dispatchNGraph_success(ZL_Compressor* cgraph) noexcept
{
    return dispatchNGraph_byExtParser(cgraph, dispatchNBT_customParser, &kTag);
}

static ZL_GraphID dispatchNGraph_fail(ZL_Compressor* cgraph) noexcept
{
    return dispatchNGraph_byExtParser(cgraph, dispatchN_fail, nullptr);
}

static ZL_GraphID dispatchNGraph_wrongSizes(ZL_Compressor* cgraph) noexcept
{
    return dispatchNGraph_byExtParser(cgraph, dispatchN_wrongSizes, nullptr);
}

static ZL_GraphID dispatchNGraph_wrongTags(ZL_Compressor* cgraph) noexcept
{
    return dispatchNGraph_byExtParser(cgraph, dispatchN_wrongTags, nullptr);
}

static ZL_GraphID dispatchNGraph_dynGraph(ZL_Compressor* cgraph) noexcept
{
    return dispatchNGraph_byDynGraph(cgraph, dispatchN_specializeNode);
}

static ZL_GraphID dispatchNGraph_manyTags(ZL_Compressor* cgraph) noexcept
{
    return dispatchNGraph_byDynGraph(cgraph, dispatchN_manyTags);
}

/* ------   compress, using provided graph function   -------- */

static size_t compress(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_GraphFn graphf)
{
    ZL_REQUIRE_GE(dstCapacity, ZL_compressBound(srcSize));

    ZL_Report const r =
            ZL_compress_usingGraphFn(dst, dstCapacity, src, srcSize, graphf);

    EXPECT_EQ(ZL_isError(r), 0)
            << "compression failed: " << ZL_ErrorCode_toString(r._code) << "\n";

    return ZL_validResult(r);
}

/* ------   decompress   -------- */

static size_t
decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    // Check buffer size
    ZL_Report const dr = ZL_getDecompressedSize(src, srcSize);
    ZL_REQUIRE(!ZL_isError(dr));
    size_t const dstSize = ZL_validResult(dr);
    ZL_REQUIRE_GE(dstCapacity, dstSize);

    // Create decompression state, to store the custom decoder(s)
    ZL_DCtx* const dctx = ZL_DCtx_create();
    ZL_REQUIRE_NN(dctx);

    // Decompress, using custom decoder(s)
    ZL_Report const r =
            ZL_DCtx_decompress(dctx, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 0) << "decompression failed \n";

    ZL_DCtx_free(dctx);
    return ZL_validResult(r);
}

/* ------   test internals   ------ */

static int roundTripTest(ZL_GraphFn graphf, const char* name, size_t arraySize)
{
    printf("\n=========================== \n");
    printf(" %s \n", name);
    printf("--------------------------- \n");
    // Generate test input
    size_t inputSize = arraySize * sizeof(int);
    int* const input = (int*)malloc(inputSize);

    for (size_t i = 0; i < arraySize; i++)
        input[i] = (int)i;

    size_t cBoundSize      = ZL_compressBound(inputSize);
    char* const compressed = (char*)malloc(cBoundSize);

    size_t const compressedSize =
            compress(compressed, cBoundSize, input, inputSize, graphf);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           inputSize,
           compressedSize);

    int* const decompressed = (int*)malloc(inputSize);

    size_t const decompressedSize =
            decompress(decompressed, inputSize, compressed, compressedSize);
    printf("decompressed %zu input bytes into %zu original bytes \n",
           compressedSize,
           decompressedSize);

    // round-trip check
    EXPECT_EQ(decompressedSize, inputSize)
            << "Error : decompressed size != original size \n";
    EXPECT_EQ(memcmp(input, decompressed, inputSize), 0)
            << "Error : decompressed content differs from original (corruption issue) !!!  \n";

    free(input);
    free(decompressed);
    free(compressed);

    printf("round-trip success \n");
    return 0;
}

static int cFailTest(ZL_GraphFn graphf, const char* testName)
{
    printf("\n=========================== \n");
    printf(" %s \n", testName);
    printf("--------------------------- \n");
    // Generate test input => too short, will fail
    char input[40];
    for (int i = 0; i < 40; i++)
        input[i] = (char)i;

#define COMPRESSED_BOUND ZL_COMPRESSBOUND(sizeof(input))
    char compressed[COMPRESSED_BOUND] = { 0 };

    ZL_Report const r = ZL_compress_usingGraphFn(
            compressed, COMPRESSED_BOUND, input, sizeof(input), graphf);
    EXPECT_EQ(ZL_isError(r), 1) << "compression should have failed \n";

    printf("Compression failure observed as expected : %s \n",
           ZL_ErrorCode_toString(r._code));
    return 0;
}

/* ------   published tests   ------ */

TEST(DispatchNByTags, roundTripTest)
{
    roundTripTest(
            dispatchNGraph_success, "simple dispatchN_byTag round trip", 78);
}

TEST(DispatchNByTags, parserFailure)
{
    cFailTest(
            dispatchNGraph_fail,
            "dispatchN_byTag : parser fails => failure expected");
}

TEST(DispatchNByTags, noParser)
{
    cFailTest(
            dispatchNGraph_noInstructions,
            "dispatchN_byTag : no parser => failure expected");
}

TEST(DispatchNByTags, parserReturnsWrongSizes)
{
    cFailTest(
            dispatchNGraph_wrongSizes,
            "dispatchN_byTag : parser provides invalid vector of sizes => failure expected");
}

TEST(DispatchNByTags, parserReturnsWrongTags)
{
    cFailTest(
            dispatchNGraph_wrongTags,
            "dispatchN_byTag : parser provides invalid vector of tags => failure expected");
}

TEST(DispatchNByTags, dynGraphSpecializeNode)
{
    roundTripTest(
            dispatchNGraph_dynGraph,
            "dispatchN_byTag round trip with dynamic graph",
            78);
}

TEST(DispatchNByTags, moreThan256Tags)
{
    roundTripTest(
            dispatchNGraph_manyTags,
            "dispatchN_byTag round trip with dynamic graph",
            2000);
}

} // namespace
