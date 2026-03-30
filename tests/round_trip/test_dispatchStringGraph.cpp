// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/codecs/dispatch_string/decode_dispatch_string_binding.h"
#include "openzl/codecs/dispatch_string/encode_dispatch_string_binding.h"
#include "openzl/common/debug.h" // ZL_REQUIRE
#include "openzl/cpp/CCtx.hpp"

#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_opaque_types.h"

using namespace ::testing;

namespace { // anonymous namespace

static constexpr const int DYNGRAPH_PID = 14;

static constexpr const std::string_view text =
        "O glaube, mein Herz, o glaube: "
        "Es geht dir nichts verloren! "
        "Dein ist, ja dein, was du gesehnt, "
        "Dein, was du geliebt, was du gestritten! "
        "O glaube: Du wardst nicht umsonst geboren! "
        "Hast nicht umsonst gelebt, gelitten! "
        "Was entstanden ist, das muß vergehen! "
        "Was vergangen, auferstehen! "
        "Hör auf zu beben! "
        "Bereite dich zu leben!";

static std::vector<uint32_t> genStrLens(std::string_view input)
{
    size_t ptr = 0;
    std::vector<uint32_t> sizes;
    if (input.empty()) {
        return sizes;
    }
    while (1) {
        const auto idx = input.find(' ', ptr + 1);
        if (idx == std::string::npos) {
            sizes.push_back(input.size() - ptr);
            break;
        } else {
            sizes.push_back(idx - ptr);
            ptr = idx;
        }
    }
    return sizes;
}

static const std::vector<uint16_t> genDispatchIndices(
        size_t nbStrs,
        int nbOutputs)
{
    if (nbOutputs == 0) {
        return { 0 }; // special case because otherwise .data() will return null
    }
    std::vector<uint16_t> indices;
    indices.reserve(nbStrs);
    for (size_t i = 0; i < nbStrs; i++) {
        indices.push_back(i % nbOutputs);
    }
    return indices;
}

static ZL_Report
oneToManyDynGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    EXPECT_EQ(nbInputs, 1);
    ZL_Edge* input = inputs[0];
    const int nbOutputs =
            ZL_Graph_getLocalIntParam(gctx, DYNGRAPH_PID).paramValue;
    const auto stream       = ZL_Edge_getData(input);
    const auto nbStrs       = ZL_Input_numElts(stream);
    uint16_t* const indices = (uint16_t*)malloc(nbStrs * sizeof(uint16_t));

    for (size_t i = 0; i < nbStrs; i++) {
        indices[i] = i % nbOutputs;
    }

    ZL_TRY_LET(
            ZL_EdgeList,
            so,
            ZL_Edge_runDispatchStringNode(input, nbOutputs, indices));
    size_t nbVariableOutputs = nbStrs != 0 ? nbOutputs : 0;
    EXPECT_EQ(
            so.nbEdges,
            (size_t)(nbVariableOutputs + 1)); // +1 for the indices stream

    for (size_t i = 0; i < so.nbEdges; ++i) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(so.edges[i], ZL_GRAPH_STORE));
    }

    free(indices);
    return ZL_returnSuccess();
}

static ZL_GraphID oneToManyStaticGraph(
        ZL_Compressor* cgraph,
        int nbOutputsParam,
        const uint16_t* dispatchIndicesParam) noexcept
{
    const ZL_GraphID gidList[] = { ZL_GRAPH_STORE, ZL_GRAPH_STORE };
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph,
            ZL_Compressor_registerDispatchStringNode(
                    cgraph, nbOutputsParam, dispatchIndicesParam),
            gidList,
            2);
}

class DispatchStringGraphTest : public ::testing::Test {
   protected:
    ZL_Type inputTypeMask    = ZL_Type_string;
    ZL_FunctionGraphDesc dgd = {
        .name                = "DispatchStringGraphTest",
        .graph_f             = oneToManyDynGraph,
        .inputTypeMasks      = &inputTypeMask,
        .nbInputs            = 1,
        .lastInputIsVariable = false,
        .localParams         = {}, // placeholder
    };

   public:
    size_t compress(
            void* dst,
            size_t dstCapacity,
            const std::string_view src,
            int numDispatches,
            bool useDynGraph)
    {
        ZL_REQUIRE_GE(dstCapacity, ZL_compressBound(src.size()));
        ZL_CCtx* cctx = ZL_CCtx_create();
        ZL_REQUIRE_NN(cctx);

        // massage input
        const auto strLens  = genStrLens(src);
        ZL_TypedRef* strRef = ZL_TypedRef_createString(
                src.data(), src.size(), strLens.data(), strLens.size());
        ZL_REQUIRE_NN(strRef);

        // generate static graph params
        const auto dispatchIndicesVec =
                genDispatchIndices(strLens.size(), numDispatches);

        // generate dyngraph param
        ZL_IntParam numDispatchesParam = {
            .paramId    = DYNGRAPH_PID,
            .paramValue = numDispatches,
        };
        ZL_LocalIntParams ips = {
            .intParams   = &numDispatchesParam,
            .nbIntParams = 1,
        };
        dgd.localParams.intParams = ips;

        // CGraph setup
        ZL_Compressor* cgraph = ZL_Compressor_create();
        ZL_REQUIRE_NN(cgraph);
        ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

        const auto gid = useDynGraph
                ? ZL_Compressor_registerFunctionGraph(cgraph, &dgd)
                : oneToManyStaticGraph(
                          cgraph, numDispatches, dispatchIndicesVec.data());
        ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph, gid));
        ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx, cgraph));

        ZL_Report const r =
                ZL_CCtx_compressTypedRef(cctx, dst, dstCapacity, strRef);
        printf("%s\n", ZL_ErrorCode_toString(ZL_errorCode(r)));
        fflush(stdout);
        ZL_REQUIRE(!ZL_isError(r));

        ZL_Compressor_free(cgraph);
        ZL_CCtx_free(cctx);
        ZL_TypedRef_free(strRef);
        return ZL_validResult(r);
    }

    size_t
    decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
    {
        ZL_Report decompRep = ZL_getDecompressedSize(src, srcSize);
        ZL_REQUIRE(!ZL_isError(decompRep));
        const size_t dstSize = ZL_validResult(decompRep);
        ZL_REQUIRE_GE(dstCapacity, dstSize);

        ZL_DCtx* dctx = ZL_DCtx_create();
        ZL_REQUIRE_NN(dctx);
        auto strOut = ZL_TypedBuffer_create();
        ZL_REQUIRE_NN(strOut);
        ZL_Report const r =
                ZL_DCtx_decompressTBuffer(dctx, strOut, src, srcSize);
        ZL_REQUIRE(!ZL_isError(r));

        ZL_REQUIRE(ZL_TypedBuffer_type(strOut) == ZL_Type_string);
        memcpy(dst,
               ZL_TypedBuffer_rPtr(strOut),
               ZL_TypedBuffer_byteSize(strOut));

        ZL_TypedBuffer_free(strOut);
        ZL_DCtx_free(dctx);
        return ZL_validResult(r);
    }

    int roundTripTest(int numDispatches, bool useDynGraph = true)
    {
        // special case for 0/degenerate case
        const std::string_view src = (numDispatches == 0) ? "" : text;
        const size_t srcSize       = (numDispatches == 0) ? 0 : text.size();

        size_t const compressedBound = ZL_compressBound(srcSize);
        void* const compressed       = malloc(compressedBound);
        ZL_REQUIRE_NN(compressed);

        size_t const compressedSize = compress(
                compressed, compressedBound, src, numDispatches, useDynGraph);
        printf("compressed %zu input bytes into %zu compressed bytes \n",
               srcSize,
               compressedSize);
        void* const decompressed = malloc(srcSize);
        ZL_REQUIRE_NN(decompressed);

        size_t const decompressedSize =
                decompress(decompressed, srcSize, compressed, compressedSize);
        printf("decompressed %zu input bytes into %zu original bytes \n",
               compressedSize,
               decompressedSize);

        // round-trip check
        EXPECT_EQ(decompressedSize, srcSize)
                << "Error : decompressed size != original size \n";
        EXPECT_EQ(memcmp(text.data(), decompressed, srcSize), 0)
                << "Error : decompressed content differs from original (corruption issue) !!! \n";

        free(decompressed);
        free(compressed);
        return 0;
    }
};

TEST_F(DispatchStringGraphTest, noneToNone)
{
    roundTripTest(0);
}

TEST_F(DispatchStringGraphTest, oneToOne)
{
    roundTripTest(1);
}

TEST_F(DispatchStringGraphTest, oneToFour)
{
    roundTripTest(4);
}

TEST_F(DispatchStringGraphTest, oneToFourStaticGraph)
{
    roundTripTest(4, false);
}

TEST_F(DispatchStringGraphTest, emptyStringDispatchedRoundTrip)
{
    ZL_IntParam numDispatchesParam = {
        .paramId    = DYNGRAPH_PID,
        .paramValue = 2,
    };
    ZL_LocalIntParams ips = {
        .intParams   = &numDispatchesParam,
        .nbIntParams = 1,
    };
    // Create a single input
    ZL_FunctionGraphDesc fgd = { .graph_f             = oneToManyDynGraph,
                                 .inputTypeMasks      = &inputTypeMask,
                                 .nbInputs            = 1,
                                 .lastInputIsVariable = false,
                                 .localParams         = { .intParams = ips } };
    openzl::Compressor compressor;
    compressor.setParameter(
            openzl::CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    auto graph = compressor.registerFunctionGraph(fgd);
    compressor.selectStartingGraph(graph);

    std::string data              = "";
    std::vector<uint32_t> lengths = {};
    openzl::Input input           = openzl::Input::refString(data, lengths);
    openzl::CCtx cctx;
    cctx.refCompressor(compressor);
    auto compressed = cctx.compressOne(input);
    openzl::DCtx dctx;
    auto regen = dctx.decompressOne(compressed);
    EXPECT_EQ(regen.contentSize(), 0);
}

} // namespace
