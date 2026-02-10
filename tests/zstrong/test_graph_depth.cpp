// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/common/assertion.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_selector.h"

#include "tests/utils.h"

using namespace ::testing;

class GraphDepthTest : public ::testing::Test {
   public:
    void SetUp() override
    {
        compressor_ = ZL_Compressor_create();
        cctx_       = ZL_CCtx_create();
        dctx_       = ZL_DCtx_create();
    }

    void TearDown() override
    {
        ZL_Compressor_free(compressor_);
        compressor_ = nullptr;
        ZL_CCtx_free(cctx_);
        cctx_ = nullptr;
        ZL_DCtx_free(dctx_);
        dctx_ = nullptr;
    }

    void testRoundTrip(ZL_GraphID graph)
    {
        ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
                compressor_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
        ZL_REQUIRE_SUCCESS(
                ZL_Compressor_selectStartingGraphID(compressor_, graph));
        ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, compressor_));
        std::string data(10000, 'a');
        std::string compressed(ZL_compressBound(data.size()), '\0');
        auto report = ZL_CCtx_compress(
                cctx_,
                compressed.data(),
                compressed.size(),
                data.data(),
                data.size());
        ZL_REQUIRE_SUCCESS(report);
        std::string roundTripped(10000, 'b');
        report = ZL_DCtx_decompress(
                dctx_,
                roundTripped.data(),
                roundTripped.size(),
                compressed.data(),
                ZL_validResult(report));
        ZL_REQUIRE_SUCCESS(report);
        ZL_REQUIRE_EQ(ZL_validResult(report), data.size());
        ZL_REQUIRE(data == roundTripped);
    }

   protected:
    ZL_Compressor* compressor_ = nullptr;
    ZL_CCtx* cctx_             = nullptr;
    ZL_DCtx* dctx_             = nullptr;
};

/* A root-level selector should see depth == 1 */
TEST_F(GraphDepthTest, SelectorDepthAtRoot)
{
    ZL_SelectorDesc desc = {
        .selector_f =
                [](const ZL_Selector* selector,
                   const ZL_Input*,
                   const ZL_GraphID*,
                   size_t) noexcept {
                    unsigned depth = ZL_Selector_getGraphDepth(selector);
                    ZL_REQUIRE_EQ(depth, 1);
                    return ZL_GRAPH_STORE;
                },
        .inStreamType = ZL_Type_serial,
    };
    auto graph = ZL_Compressor_registerSelectorGraph(compressor_, &desc);
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);
    testRoundTrip(graph);
}

/* A root-level function graph should see depth == 1 */
TEST_F(GraphDepthTest, FunctionGraphDepthAtRoot)
{
    ZL_Type type              = ZL_Type_serial;
    ZL_FunctionGraphDesc desc = {
        .graph_f =
                [](ZL_Graph* graph,
                   ZL_Edge* edges[],
                   size_t numEdges) noexcept {
                    unsigned depth = ZL_Graph_getDepth(graph);
                    ZL_REQUIRE_EQ(depth, 1);
                    for (size_t i = 0; i < numEdges; ++i) {
                        ZL_REQUIRE_SUCCESS(ZL_Edge_setDestination(
                                edges[i], ZL_GRAPH_STORE));
                    }
                    return ZL_returnSuccess();
                },
        .inputTypeMasks = &type,
        .nbInputs       = 1,
    };
    auto graph = ZL_Compressor_registerFunctionGraph(compressor_, &desc);
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);
    testRoundTrip(graph);
}

/* A selector at root picks a function graph successor;
 * the successor should see depth == 2. */
TEST_F(GraphDepthTest, FunctionGraphDepthNested)
{
    ZL_Type type              = ZL_Type_serial;
    ZL_FunctionGraphDesc desc = {
        .graph_f =
                [](ZL_Graph* graph,
                   ZL_Edge* edges[],
                   size_t numEdges) noexcept {
                    unsigned depth = ZL_Graph_getDepth(graph);
                    ZL_REQUIRE_EQ(depth, 2);
                    for (size_t i = 0; i < numEdges; ++i) {
                        ZL_REQUIRE_SUCCESS(ZL_Edge_setDestination(
                                edges[i], ZL_GRAPH_STORE));
                    }
                    return ZL_returnSuccess();
                },
        .inputTypeMasks = &type,
        .nbInputs       = 1,
    };
    auto innerGraph = ZL_Compressor_registerFunctionGraph(compressor_, &desc);
    ASSERT_NE(innerGraph, ZL_GRAPH_ILLEGAL);

    ZL_SelectorDesc selDesc = {
        .selector_f =
                [](const ZL_Selector* selector,
                   const ZL_Input*,
                   const ZL_GraphID* customGraphs,
                   size_t nbCustomGraphs) noexcept {
                    unsigned depth = ZL_Selector_getGraphDepth(selector);
                    ZL_REQUIRE_EQ(depth, 1);
                    ZL_REQUIRE_EQ(nbCustomGraphs, 1);
                    return customGraphs[0];
                },
        .inStreamType   = ZL_Type_serial,
        .customGraphs   = &innerGraph,
        .nbCustomGraphs = 1,
    };
    auto rootGraph = ZL_Compressor_registerSelectorGraph(compressor_, &selDesc);
    ASSERT_NE(rootGraph, ZL_GRAPH_ILLEGAL);
    testRoundTrip(rootGraph);
}
