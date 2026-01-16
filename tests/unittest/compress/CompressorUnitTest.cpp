// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <unordered_map>
#include <vector>

#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/compress/cgraph.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_reflection.h"
#include "openzl/zl_selector.h"

#include "tests/utils.h"

using namespace ::testing;
using namespace ::openzl::tests;

// =============================================================================
// CompressorTest - Unified test fixture for all Compressor functionality
// =============================================================================

class CompressorTest : public Test {
   public:
    void SetUp() override
    {
        intParam_.paramId    = 1;
        intParam_.paramValue = 100;

        copyParam_.paramId   = 10;
        copyParam_.paramPtr  = "hello";
        copyParam_.paramSize = 6;

        refParam_.paramId  = 5;
        refParam_.paramRef = "world";

        localParams_.intParams.intParams   = &intParam_;
        localParams_.intParams.nbIntParams = 1;

        localParams_.copyParams.copyParams   = &copyParam_;
        localParams_.copyParams.nbCopyParams = 1;

        localParams_.refParams.refParams   = &refParam_;
        localParams_.refParams.nbRefParams = 1;

        // Set up cctx
        cctx_.setParameter(
                openzl::CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        cctx_.setParameter(openzl::CParam::StickyParameters, 1);
    }

    void setParameter(ZL_CParam param, int value)
    {
        compressor_.setParameter(static_cast<openzl::CParam>(param), value);
    }

    int getParameter(ZL_CParam param)
    {
        return compressor_.getParameter(static_cast<openzl::CParam>(param));
    }

    std::unordered_map<ZL_CParam, int> getParameters()
    {
        using Params = std::unordered_map<ZL_CParam, int>;
        Params params;
        ZL_REQUIRE_SUCCESS(ZL_Compressor_forEachParam(
                compressor_.get(),
                [](void* opaque, ZL_CParam param, int value) noexcept {
                    Params* paramsPtr    = (Params*)opaque;
                    auto [it, inserteed] = paramsPtr->emplace(param, value);
                    ZL_REQUIRE(inserteed);
                    return ZL_returnSuccess();
                },
                &params));
        return params;
    }

    std::vector<ZL_GraphID> getGraphs()
    {
        std::vector<ZL_GraphID> graphs;
        ZL_REQUIRE_SUCCESS(ZL_Compressor_forEachGraph(
                compressor_.get(),
                [](void* opaque,
                   const ZL_Compressor* compressor,
                   ZL_GraphID graph) noexcept {
                    auto* graphsPtr = (std::vector<ZL_GraphID>*)opaque;
                    graphsPtr->push_back(graph);
                    return ZL_returnSuccess();
                },
                &graphs));
        return graphs;
    }

    std::vector<ZL_NodeID> getNodes()
    {
        std::vector<ZL_NodeID> nodes;
        ZL_REQUIRE_SUCCESS(ZL_Compressor_forEachNode(
                compressor_.get(),
                [](void* opaque,
                   const ZL_Compressor* compressor,
                   ZL_NodeID node) noexcept {
                    auto* nodesPtr = (std::vector<ZL_NodeID>*)opaque;
                    nodesPtr->push_back(node);
                    return ZL_returnSuccess();
                },
                &nodes));
        return nodes;
    }

    void expectParamsEmpty(const ZL_LocalParams& params)
    {
        ZL_LocalParams empty{};
        EXPECT_EQ(memcmp(&params, &empty, sizeof(empty)), 0);
    }

    void expectParamsEq(const ZL_LocalParams& lhs, const ZL_LocalParams& rhs)
    {
        ASSERT_EQ(lhs.intParams.nbIntParams, rhs.intParams.nbIntParams);
        for (size_t i = 0; i < lhs.intParams.nbIntParams; ++i) {
            ASSERT_EQ(
                    0,
                    memcmp(&lhs.intParams.intParams[i],
                           &rhs.intParams.intParams[i],
                           sizeof(ZL_IntParam)));
        }

        ASSERT_EQ(lhs.copyParams.nbCopyParams, rhs.copyParams.nbCopyParams);
        for (size_t i = 0; i < lhs.copyParams.nbCopyParams; ++i) {
            ASSERT_EQ(
                    lhs.copyParams.copyParams[i].paramId,
                    rhs.copyParams.copyParams[i].paramId);
            ASSERT_EQ(
                    lhs.copyParams.copyParams[i].paramSize,
                    rhs.copyParams.copyParams[i].paramSize);
            ASSERT_EQ(
                    0,
                    memcmp(lhs.copyParams.copyParams[i].paramPtr,
                           rhs.copyParams.copyParams[i].paramPtr,
                           lhs.copyParams.copyParams[i].paramSize));
        }

        ASSERT_EQ(lhs.refParams.nbRefParams, rhs.refParams.nbRefParams);
        for (size_t i = 0; i < lhs.refParams.nbRefParams; ++i) {
            ASSERT_EQ(
                    0,
                    memcmp(&lhs.refParams.refParams[i],
                           &rhs.refParams.refParams[i],
                           sizeof(ZL_RefParam)));
        }
    }

    ZL_GraphID makeStaticGraph(bool isAnchor = false)
    {
        std::array<ZL_GraphID, 2> successors = { ZL_GRAPH_FIELD_LZ,
                                                 ZL_GRAPH_ZSTD };
        openzl::StaticGraphParameters params;
        params.name = isAnchor ? std::string("!static") : std::string("static");
        params.localParams = openzl::LocalParams(localParams_);

        return compressor_.buildStaticGraph(
                ZL_NODE_FLOAT16_DECONSTRUCT, successors, params);
    }

    ZL_GraphID makeSelectorGraph(bool isAnchor = false)
    {
        std::array<ZL_GraphID, 2> graphs = { ZL_GRAPH_FIELD_LZ_LITERALS,
                                             ZL_GRAPH_STORE };
        ZL_SelectorDesc desc             = {
                        .selector_f = [](const ZL_Selector*,
                             const ZL_Input*,
                             const ZL_GraphID* customGraphs,
                             size_t) noexcept { return customGraphs[0]; },
            .inStreamType                = (ZL_Type)(ZL_Type_struct | ZL_Type_numeric),
            .customGraphs                = graphs.data(),
            .nbCustomGraphs              = graphs.size(),
            .localParams                 = localParams_,
            .name                        = isAnchor ? "!selector" : "selector",
        };
        return compressor_.registerSelectorGraph(desc);
    }

    ZL_GraphID makeDynamicGraph(bool isAnchor = false)
    {
        ZL_GraphID successor           = ZL_GRAPH_COMPRESS_GENERIC;
        std::array<ZL_NodeID, 2> nodes = { ZL_NODE_ZSTD, ZL_NODE_FIELD_LZ };
        ZL_Type inputType              = ZL_Type_serial;
        ZL_FunctionGraphDesc desc      = {
                 .name    = isAnchor ? "!dynamic" : "dynamic",
                 .graph_f = [](ZL_Graph* gctx,
                          ZL_Edge* inputs[],
                          size_t nbIns) noexcept { return ZL_returnSuccess(); },
            .inputTypeMasks            = &inputType,
            .nbInputs                  = 1,
            .lastInputIsVariable       = false,
            .customGraphs              = &successor,
            .nbCustomGraphs            = 1,
            .customNodes               = nodes.data(),
            .nbCustomNodes             = nodes.size(),
            .localParams               = localParams_,
        };
        auto graph = compressor_.registerFunctionGraph(desc);
        EXPECT_NE(graph.gid, ZL_GRAPH_ILLEGAL.gid);
        return graph;
    }

    ZL_GraphID makeMultiInputGraph(
            bool variableInput = true,
            bool isAnchor      = false)
    {
        std::array<ZL_Type, 2> inputs{ ZL_Type_serial, ZL_Type_numeric };
        ZL_GraphID successor      = ZL_GRAPH_COMPRESS_GENERIC;
        ZL_NodeID node            = ZL_NODE_ZSTD;
        ZL_FunctionGraphDesc desc = {
            .name = isAnchor ? "!multi_input" : "multi_input",
            .graph_f =
                    [](ZL_Graph* gctx,
                       ZL_Edge* input[],
                       size_t nbInputs) noexcept { return ZL_returnSuccess(); },
            .inputTypeMasks       = inputs.data(),
            .nbInputs             = inputs.size(),
            .lastInputIsVariable  = variableInput,
            .customGraphs         = &successor,
            .nbCustomGraphs       = 1,
            .customNodes          = &node,
            .nbCustomNodes        = 1,
            .localParams          = localParams_,
        };
        auto graph = compressor_.registerFunctionGraph(desc);
        EXPECT_NE(graph, ZL_GRAPH_ILLEGAL);
        return graph;
    }

    ZL_GraphID makeParameterizedGraph(
            bool hasName  = false,
            bool isAnchor = false)
    {
        openzl::GraphParameters params;
        if (hasName) {
            params.name = isAnchor ? std::string("!parameterized")
                                   : std::string("parameterized");
        }
        params.localParams = openzl::LocalParams(localParams_);

        return compressor_.parameterizeGraph(ZL_GRAPH_FIELD_LZ, params);
    }

    ZL_NodeID makeCustomTransform(bool isAnchor = false)
    {
        const ZL_Type outType = ZL_Type_serial;
        ZL_TypedEncoderDesc desc = {
            .gd = {
                .CTid = isAnchor ? 0u : 1u,
                .inStreamType = ZL_Type_serial,
                .outStreamTypes = &outType,
                .nbOutStreams = 1,
            },
            .transform_f =
                    [](ZL_Encoder*, const ZL_Input*) noexcept {
                        return ZL_returnSuccess();
                    },
            .name = isAnchor ? "!custom_transform" : "custom_transform",
        };
        return ZL_Compressor_registerTypedEncoder(compressor_.get(), &desc);
    }

   protected:
    openzl::Compressor compressor_;
    ZL_LocalParams localParams_;
    ZL_IntParam intParam_;
    ZL_CopyParam copyParam_;
    ZL_RefParam refParam_;

    openzl::Compressor compressorCpp_;
    openzl::CCtx cctx_;
    openzl::DCtx dctx_;
};

// =============================================================================
// Graph Registration Tests
// =============================================================================

TEST_F(CompressorTest, RegisterStaticGraphWithSameName)
{
    // Illegal to register two graphs with the same anchor name
    auto graph = makeStaticGraph(true);
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);

    // Should throw when registering duplicate anchor name
    EXPECT_THROW(
            {
                try {
                    auto graph2 = makeStaticGraph(true);
                    ASSERT_NE(graph2, ZL_GRAPH_ILLEGAL);
                } catch (const openzl::Exception& e) {
                    std::string msg = e.what();
                    EXPECT_NE(
                            msg.find("Invalid name of graph component"),
                            std::string::npos);
                    throw;
                }
            },
            openzl::Exception);

    // Allowed to register two non-anchors with same name
    auto graph3 = makeStaticGraph(false);
    ASSERT_NE(graph3, ZL_GRAPH_ILLEGAL);
    ASSERT_NE(graph3, graph);
    auto graph4 = makeStaticGraph(false);
    ASSERT_NE(graph4, ZL_GRAPH_ILLEGAL);
    ASSERT_NE(graph4, graph);
}

TEST_F(CompressorTest, RegisterStaticGraphWithEmptyName)
{
    auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_DELTA_INT, ZL_GRAPH_ZSTD);
    ASSERT_EQ(
            std::string("zl.delta_int#0"),
            ZL_Compressor_Graph_getName(compressor_.get(), graph));
    ZL_StaticGraphDesc desc = {
        .name           = NULL,
        .headNodeid     = ZL_NODE_ZIGZAG,
        .successor_gids = &graph,
        .nbGids         = 1,
        .localParams    = &localParams_,
    };
    graph = ZL_Compressor_registerStaticGraph(compressor_.get(), &desc);
    ASSERT_EQ(
            std::string("#1"),
            ZL_Compressor_Graph_getName(compressor_.get(), graph));
    desc.name = "";
    graph     = ZL_Compressor_registerStaticGraph(compressor_.get(), &desc);
    ASSERT_EQ(
            std::string("#2"),
            ZL_Compressor_Graph_getName(compressor_.get(), graph));
    desc.name = "!";
    graph     = ZL_Compressor_registerStaticGraph(compressor_.get(), &desc);
    ASSERT_EQ(
            std::string(""),
            ZL_Compressor_Graph_getName(compressor_.get(), graph));

    graph = ZL_Compressor_getGraph(compressor_.get(), "zl.delta_int#0");

    ZL_ParameterizedGraphDesc paramDesc = {
        .graph = graph,
    };
    auto paramGraph = ZL_Compressor_registerParameterizedGraph(
            compressor_.get(), &paramDesc);
    ASSERT_NE(paramGraph, ZL_GRAPH_ILLEGAL);
    ASSERT_NE(paramGraph, graph);

    ASSERT_EQ(
            std::string("zl.delta_int#4"),
            ZL_Compressor_Graph_getName(compressor_.get(), paramGraph));
}

TEST_F(CompressorTest, RegisterParameterizedGraphName)
{
    auto graph                     = ZL_GRAPH_FIELD_LZ;
    ZL_ParameterizedGraphDesc desc = {
        .graph = graph,
    };
    graph = ZL_Compressor_registerParameterizedGraph(compressor_.get(), &desc);
    ASSERT_EQ(
            std::string("zl.field_lz#0"),
            ZL_Compressor_Graph_getName(compressor_.get(), graph));
    desc.graph = graph;
    graph = ZL_Compressor_registerParameterizedGraph(compressor_.get(), &desc);
    ASSERT_EQ(
            std::string("zl.field_lz#1"),
            ZL_Compressor_Graph_getName(compressor_.get(), graph));

    desc.graph = graph;
    desc.name  = "parameterized";
    graph = ZL_Compressor_registerParameterizedGraph(compressor_.get(), &desc);
    ASSERT_EQ(
            std::string("parameterized#2"),
            ZL_Compressor_Graph_getName(compressor_.get(), graph));

    desc.graph = graph;
    desc.name  = "!parameterized";
    graph = ZL_Compressor_registerParameterizedGraph(compressor_.get(), &desc);
    ASSERT_EQ(
            std::string("parameterized"),
            ZL_Compressor_Graph_getName(compressor_.get(), graph));

    desc.graph = graph;
    desc.name  = "parameterized";
    graph = ZL_Compressor_registerParameterizedGraph(compressor_.get(), &desc);
    ASSERT_EQ(
            std::string("parameterized#4"),
            ZL_Compressor_Graph_getName(compressor_.get(), graph));
}

TEST_F(CompressorTest, RegisterParameterizedGraphLocalParams)
{
    auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_DELTA_INT, ZL_GRAPH_ZSTD);
    ZL_ParameterizedGraphDesc desc = {
        .graph = graph,
    };
    auto noParam =
            ZL_Compressor_registerParameterizedGraph(compressor_.get(), &desc);
    desc.localParams = &localParams_;
    auto params =
            ZL_Compressor_registerParameterizedGraph(compressor_.get(), &desc);
    expectParamsEq(
            ZL_Compressor_Graph_getLocalParams(compressor_.get(), graph),
            ZL_Compressor_Graph_getLocalParams(compressor_.get(), noParam));
    expectParamsEq(
            localParams_,
            ZL_Compressor_Graph_getLocalParams(compressor_.get(), params));
}

TEST_F(CompressorTest, RegisterParameterizedGraphCustomGraphs)
{
    auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_DELTA_INT, ZL_GRAPH_ZSTD);
    ZL_ParameterizedGraphDesc desc = {
        .graph          = graph,
        .customGraphs   = &graph,
        .nbCustomGraphs = 1,
    };
    auto graphs =
            ZL_Compressor_registerParameterizedGraph(compressor_.get(), &desc);
    auto customGraphs =
            ZL_Compressor_Graph_getCustomGraphs(compressor_.get(), graphs);
    ASSERT_EQ(customGraphs.nbGraphIDs, 1u);
    ASSERT_EQ(customGraphs.graphids[0], graph);
}

TEST_F(CompressorTest, RegisterParameterizedGraphCustomNodes)
{
    auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_DELTA_INT, ZL_GRAPH_ZSTD);
    ZL_NodeID node                 = ZL_NODE_FIELD_LZ;
    ZL_ParameterizedGraphDesc desc = {
        .graph         = graph,
        .customNodes   = &node,
        .nbCustomNodes = 1,
    };
    auto nodes =
            ZL_Compressor_registerParameterizedGraph(compressor_.get(), &desc);
    auto customNodes =
            ZL_Compressor_Graph_getCustomNodes(compressor_.get(), nodes);
    ASSERT_EQ(customNodes.nbNodeIDs, 1u);
    ASSERT_EQ(customNodes.nodeids[0], node);
}

// =============================================================================
// Parameter Tests
// =============================================================================

TEST_F(CompressorTest, SetAndGetParameter)
{
    ASSERT_EQ(getParameter(ZL_CParam_formatVersion), 0);
    setParameter(ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION);
    ASSERT_EQ(getParameter(ZL_CParam_formatVersion), ZL_MAX_FORMAT_VERSION);
    auto params = getParameters();
    ASSERT_EQ(params.size(), 1u);
    ASSERT_EQ(params.at(ZL_CParam_formatVersion), ZL_MAX_FORMAT_VERSION);
    setParameter(ZL_CParam_formatVersion, 0);
    ASSERT_EQ(getParameter(ZL_CParam_formatVersion), 0);
    ASSERT_EQ(getParameters().size(), 0u);

    setParameter(ZL_CParam_compressionLevel, 1);
    setParameter(ZL_CParam_decompressionLevel, 2);
    ASSERT_EQ(getParameters().size(), 2u);
}

// =============================================================================
// Node Tests
// =============================================================================

TEST_F(CompressorTest, GetNode)
{
    auto node = ZL_Compressor_getNode(compressor_.get(), "zl.field_lz");
    ASSERT_NE(node, ZL_NODE_ILLEGAL);
    ASSERT_EQ(node, ZL_NODE_FIELD_LZ);

    node = ZL_Compressor_getNode(compressor_.get(), "zl.field_lz#0");
    ASSERT_EQ(node, ZL_NODE_ILLEGAL);

    openzl::NodeParameters nodeParams;
    nodeParams.localParams = openzl::LocalParams(localParams_);
    auto clone = compressor_.parameterizeNode(ZL_NODE_FIELD_LZ, nodeParams);

    node = ZL_Compressor_getNode(compressor_.get(), "zl.field_lz");
    ASSERT_NE(node, ZL_NODE_ILLEGAL);
    ASSERT_EQ(node, ZL_NODE_FIELD_LZ);

    ASSERT_EQ(
            std::string("zl.field_lz#0"),
            ZL_Compressor_Node_getName(compressor_.get(), clone));

    node = ZL_Compressor_getNode(compressor_.get(), "zl.field_lz#0");
    ASSERT_NE(node, ZL_NODE_ILLEGAL);
    ASSERT_EQ(node, clone);

    node = ZL_Compressor_getNode(compressor_.get(), "zl.field_lz#1");
    ASSERT_EQ(node, ZL_NODE_ILLEGAL);

    auto clone2 = compressor_.parameterizeNode(ZL_NODE_FIELD_LZ, nodeParams);
    ASSERT_EQ(
            std::string("zl.field_lz#1"),
            ZL_Compressor_Node_getName(compressor_.get(), clone2));

    node = ZL_Compressor_getNode(compressor_.get(), "zl.field_lz#1");
    ASSERT_NE(node, ZL_NODE_ILLEGAL);
    ASSERT_EQ(node, clone2);

    node = ZL_Compressor_getNode(compressor_.get(), "zl.field_lz#0");
    ASSERT_NE(node, ZL_NODE_ILLEGAL);
    ASSERT_EQ(node, clone);

    node = ZL_Compressor_getNode(compressor_.get(), "custom_transform");
    ASSERT_EQ(node, ZL_NODE_ILLEGAL);

    auto custom = makeCustomTransform(true);
    ASSERT_EQ(
            std::string("custom_transform"),
            ZL_Compressor_Node_getName(compressor_.get(), custom));

    node = ZL_Compressor_getNode(compressor_.get(), "custom_transform");
    ASSERT_NE(node, ZL_NODE_ILLEGAL);
    ASSERT_EQ(node, custom);

    node = ZL_Compressor_getNode(compressor_.get(), "custom_transform#0");
    ASSERT_EQ(node, ZL_NODE_ILLEGAL);
    node = ZL_Compressor_getNode(compressor_.get(), "custom_transform#1");
    ASSERT_EQ(node, ZL_NODE_ILLEGAL);
    node = ZL_Compressor_getNode(compressor_.get(), "custom_transform#2");
    ASSERT_EQ(node, ZL_NODE_ILLEGAL);
    node = ZL_Compressor_getNode(compressor_.get(), "custom_transform#3");
    ASSERT_EQ(node, ZL_NODE_ILLEGAL);

    auto custom2 = makeCustomTransform(false);
    ASSERT_EQ(
            std::string("custom_transform#3"),
            ZL_Compressor_Node_getName(compressor_.get(), custom2));

    node = ZL_Compressor_getNode(compressor_.get(), "custom_transform#3");
    ASSERT_NE(node, ZL_NODE_ILLEGAL);
    ASSERT_EQ(node, custom2);

    node = ZL_Compressor_getNode(compressor_.get(), "custom_transform");
    ASSERT_NE(node, ZL_NODE_ILLEGAL);
    ASSERT_EQ(node, custom);

    node = ZL_Compressor_getNode(compressor_.get(), "zl.field_lz#0");
    ASSERT_NE(node, ZL_NODE_ILLEGAL);
    ASSERT_EQ(node, clone);

    node = ZL_Compressor_getNode(compressor_.get(), "zl.field_lz#1");
    ASSERT_NE(node, ZL_NODE_ILLEGAL);
    ASSERT_EQ(node, clone2);

    node = ZL_Compressor_getNode(compressor_.get(), "zl.field_lz");
    ASSERT_NE(node, ZL_NODE_ILLEGAL);
    ASSERT_EQ(node, ZL_NODE_FIELD_LZ);
}

TEST_F(CompressorTest, RegisterParameterizedNode)
{
    auto node                     = ZL_NODE_FIELD_LZ;
    ZL_ParameterizedNodeDesc desc = {
        .name = "my_node",
        .node = node,
    };
    auto clone =
            ZL_Compressor_registerParameterizedNode(compressor_.get(), &desc);
    ASSERT_EQ(
            std::string("my_node#0"),
            ZL_Compressor_Node_getName(compressor_.get(), clone));
    ASSERT_NE(node, clone);
    ASSERT_EQ(node, ZL_Compressor_Node_getBaseNodeID(compressor_.get(), clone));

    desc.name = "!my_node";
    auto clone2 =
            ZL_Compressor_registerParameterizedNode(compressor_.get(), &desc);
    ASSERT_EQ(
            std::string("my_node"),
            ZL_Compressor_Node_getName(compressor_.get(), clone2));
    ASSERT_EQ(clone2, ZL_Compressor_getNode(compressor_.get(), "my_node"));
    ASSERT_NE(clone, clone2);
}

TEST_F(CompressorTest, GetGraph)
{
    // store is a special graph, make sure to test it directly
    auto graph = ZL_Compressor_getGraph(compressor_.get(), "zl.store");
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);
    ASSERT_EQ(graph, ZL_GRAPH_STORE);

    graph = ZL_Compressor_getGraph(compressor_.get(), "zl.zstd");
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);
    ASSERT_EQ(graph, ZL_GRAPH_ZSTD);

    auto clone = makeParameterizedGraph(false, false);
    ASSERT_NE(clone, ZL_GRAPH_FIELD_LZ);

    ASSERT_EQ(
            std::string("zl.field_lz#0"),
            ZL_Compressor_Graph_getName(compressor_.get(), clone));

    graph = ZL_Compressor_getGraph(compressor_.get(), "zl.field_lz#0");
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);
    ASSERT_EQ(graph, clone);

    graph = ZL_Compressor_getGraph(compressor_.get(), "zl.field_lz");
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);
    ASSERT_EQ(graph, ZL_GRAPH_FIELD_LZ);

    auto clone2 = makeParameterizedGraph(true, false);
    ASSERT_NE(clone2, ZL_GRAPH_FIELD_LZ);

    ASSERT_EQ(
            std::string("parameterized#1"),
            ZL_Compressor_Graph_getName(compressor_.get(), clone2));
    graph = ZL_Compressor_getGraph(compressor_.get(), "parameterized#1");
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);
    ASSERT_EQ(graph, clone2);

    graph = ZL_Compressor_getGraph(compressor_.get(), "parameterized");
    ASSERT_EQ(graph, ZL_GRAPH_ILLEGAL);

    auto clone3 = makeParameterizedGraph(true, true);
    ASSERT_NE(clone3, ZL_GRAPH_FIELD_LZ);

    ASSERT_EQ(
            std::string("parameterized"),
            ZL_Compressor_Graph_getName(compressor_.get(), clone3));
    graph = ZL_Compressor_getGraph(compressor_.get(), "parameterized");
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);
    ASSERT_EQ(graph, clone3);

    auto static0 = makeStaticGraph(false);
    auto static1 = makeStaticGraph(true);
    graph        = ZL_Compressor_getGraph(compressor_.get(), "static#3");
    ASSERT_EQ(static0, graph);
    graph = ZL_Compressor_getGraph(compressor_.get(), "static");
    ASSERT_EQ(static1, graph);

    auto selector1 = makeSelectorGraph(true);
    auto selector0 = makeSelectorGraph(false);
    graph          = ZL_Compressor_getGraph(compressor_.get(), "selector#6");
    ASSERT_EQ(selector0, graph);
    graph = ZL_Compressor_getGraph(compressor_.get(), "selector");
    ASSERT_EQ(selector1, graph);

    auto dynamic0 = makeDynamicGraph(false);
    auto dynamic1 = makeDynamicGraph(true);
    graph         = ZL_Compressor_getGraph(compressor_.get(), "dynamic#7");
    ASSERT_EQ(dynamic0, graph);
    graph = ZL_Compressor_getGraph(compressor_.get(), "dynamic");
    ASSERT_EQ(dynamic1, graph);

    auto multiInput1 = makeMultiInputGraph(true, true);
    auto multiInput0 = makeMultiInputGraph(false, false);
    graph            = ZL_Compressor_getGraph(compressor_.get(), "multi_input");
    ASSERT_EQ(multiInput1, graph);
    graph = ZL_Compressor_getGraph(compressor_.get(), "multi_input#10");
    ASSERT_EQ(multiInput0, graph);
}

TEST_F(CompressorTest, ForEachGraph)
{
    ASSERT_EQ(getGraphs().size(), 0u);

    openzl::NodeParameters nodeParams;
    nodeParams.localParams = openzl::LocalParams(localParams_);
    compressor_.parameterizeNode(ZL_NODE_DELTA_INT, nodeParams);
    ASSERT_EQ(getGraphs().size(), 0u);

    auto graph0 = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_INTERPRET_AS_LE64, ZL_GRAPH_CONSTANT);
    ASSERT_EQ(getGraphs().size(), 1u);
    ASSERT_EQ(getGraphs()[0], graph0);

    auto graph1 = makeDynamicGraph();
    ASSERT_EQ(getGraphs().size(), 2u);
    ASSERT_EQ(getGraphs()[0], graph0);
    ASSERT_EQ(getGraphs()[1], graph1);
}

TEST_F(CompressorTest, ForEachNode)
{
    ASSERT_EQ(getNodes().size(), 0u);

    ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_INTERPRET_AS_LE64, ZL_GRAPH_CONSTANT);
    ASSERT_EQ(getNodes().size(), 0u);

    openzl::NodeParameters nodeParams;
    nodeParams.localParams = openzl::LocalParams(localParams_);
    auto node0 = compressor_.parameterizeNode(ZL_NODE_DELTA_INT, nodeParams);
    ASSERT_EQ(getNodes().size(), 1u);
    ASSERT_EQ(getNodes()[0], node0);
}

TEST_F(CompressorTest, SelectStartingGraphID)
{
    ZL_GraphID startingGraph;
    ASSERT_FALSE(ZL_Compressor_getStartingGraphID(
            compressor_.get(), &startingGraph));
    ASSERT_EQ(startingGraph, ZL_GRAPH_ILLEGAL);

    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(
            compressor_.get(), ZL_GRAPH_FIELD_LZ));
    ASSERT_TRUE(ZL_Compressor_getStartingGraphID(
            compressor_.get(), &startingGraph));
    ASSERT_EQ(startingGraph, ZL_GRAPH_FIELD_LZ);
}

// =============================================================================
// Graph Property Tests
// =============================================================================

TEST_F(CompressorTest, GraphGetGraphType)
{
    ASSERT_EQ(
            ZL_GraphType_standard,
            ZL_Compressor_getGraphType(compressor_.get(), ZL_GRAPH_STORE));
    ASSERT_EQ(
            ZL_GraphType_standard,
            ZL_Compressor_getGraphType(compressor_.get(), ZL_GRAPH_CONSTANT));
    ASSERT_EQ(
            ZL_GraphType_standard,
            ZL_Compressor_getGraphType(compressor_.get(), ZL_GRAPH_DELTA_ZSTD));
    ASSERT_EQ(
            ZL_GraphType_standard,
            ZL_Compressor_getGraphType(compressor_.get(), ZL_GRAPH_FIELD_LZ));
    ASSERT_EQ(
            ZL_GraphType_standard,
            ZL_Compressor_getGraphType(compressor_.get(), ZL_GRAPH_ZSTD));
    ASSERT_EQ(
            ZL_GraphType_standard,
            ZL_Compressor_getGraphType(
                    compressor_.get(), ZL_GRAPH_GENERIC_LZ_BACKEND));
    ASSERT_EQ(
            ZL_GraphType_standard,
            ZL_Compressor_getGraphType(
                    compressor_.get(), ZL_GRAPH_COMPRESS_GENERIC));

    ASSERT_EQ(
            ZL_GraphType_static,
            ZL_Compressor_getGraphType(compressor_.get(), makeStaticGraph()));
    ASSERT_EQ(
            ZL_GraphType_selector,
            ZL_Compressor_getGraphType(compressor_.get(), makeSelectorGraph()));
    ASSERT_EQ(
            ZL_GraphType_multiInput,
            ZL_Compressor_getGraphType(compressor_.get(), makeDynamicGraph()));
    ASSERT_EQ(
            ZL_GraphType_multiInput,
            ZL_Compressor_getGraphType(
                    compressor_.get(), makeMultiInputGraph()));
    ASSERT_EQ(
            ZL_GraphType_parameterized,
            ZL_Compressor_getGraphType(
                    compressor_.get(), makeParameterizedGraph()));
}

TEST_F(CompressorTest, GraphGetName)
{
    ASSERT_EQ(
            std::string("zl.store"),
            ZL_Compressor_Graph_getName(compressor_.get(), ZL_GRAPH_STORE));
    ASSERT_EQ(
            std::string("zl.zstd"),
            ZL_Compressor_Graph_getName(compressor_.get(), ZL_GRAPH_ZSTD));
    ASSERT_EQ(
            std::string("zl.field_lz"),
            ZL_Compressor_Graph_getName(compressor_.get(), ZL_GRAPH_FIELD_LZ));

    ASSERT_EQ(
            std::string("static#0"),
            ZL_Compressor_Graph_getName(compressor_.get(), makeStaticGraph()));
    ASSERT_EQ(
            std::string("selector#1"),
            ZL_Compressor_Graph_getName(
                    compressor_.get(), makeSelectorGraph()));
    ASSERT_EQ(
            std::string("dynamic#2"),
            ZL_Compressor_Graph_getName(compressor_.get(), makeDynamicGraph()));
    ASSERT_EQ(
            std::string("multi_input#3"),
            ZL_Compressor_Graph_getName(
                    compressor_.get(), makeMultiInputGraph()));
}

TEST_F(CompressorTest, GraphGetInputMask)
{
    auto test = [&](std::initializer_list<ZL_Type> types, ZL_GraphID graph) {
        if (types.size() == 1) {
            ASSERT_EQ(
                    *types.begin(),
                    ZL_Compressor_Graph_getInput0Mask(
                            compressor_.get(), graph));
        }
        ASSERT_EQ(
                ZL_Compressor_Graph_getNumInputs(compressor_.get(), graph),
                types.size());
        for (size_t i = 0; i < types.size(); ++i) {
            ASSERT_EQ(
                    types.begin()[i],
                    ZL_Compressor_Graph_getInputMask(
                            compressor_.get(), graph, i));
        }
    };

    test({ ZL_Type_serial }, ZL_GRAPH_ZSTD);
    test({ (ZL_Type)(ZL_Type_struct | ZL_Type_numeric) }, ZL_GRAPH_FIELD_LZ);
    test({ ZL_Type_numeric }, makeStaticGraph());
    test({ (ZL_Type)(ZL_Type_struct | ZL_Type_numeric) }, makeSelectorGraph());
    test({ ZL_Type_serial }, makeDynamicGraph());
    test({ ZL_Type_serial, ZL_Type_numeric }, makeMultiInputGraph());
}

TEST_F(CompressorTest, GraphIsVariableInput)
{
    ASSERT_TRUE(ZL_Compressor_Graph_isVariableInput(
            compressor_.get(), ZL_GRAPH_STORE));
    ASSERT_TRUE(ZL_Compressor_Graph_isVariableInput(
            compressor_.get(), ZL_GRAPH_COMPRESS_GENERIC));
    ASSERT_TRUE(ZL_Compressor_Graph_isVariableInput(
            compressor_.get(), makeMultiInputGraph()));

    ASSERT_FALSE(ZL_Compressor_Graph_isVariableInput(
            compressor_.get(), ZL_GRAPH_ZSTD));
    ASSERT_FALSE(ZL_Compressor_Graph_isVariableInput(
            compressor_.get(), makeStaticGraph()));
    ASSERT_FALSE(ZL_Compressor_Graph_isVariableInput(
            compressor_.get(), makeSelectorGraph()));
    ASSERT_FALSE(ZL_Compressor_Graph_isVariableInput(
            compressor_.get(), makeDynamicGraph()));
    ASSERT_FALSE(ZL_Compressor_Graph_isVariableInput(
            compressor_.get(), makeMultiInputGraph(false)));
}

TEST_F(CompressorTest, GraphGetHeadNode)
{
    ASSERT_EQ(
            ZL_NODE_ILLEGAL,
            ZL_Compressor_Graph_getHeadNode(compressor_.get(), ZL_GRAPH_STORE));
    ASSERT_EQ(
            ZL_NODE_ILLEGAL,
            ZL_Compressor_Graph_getHeadNode(compressor_.get(), ZL_GRAPH_ZSTD));
    ASSERT_EQ(
            ZL_NODE_ILLEGAL,
            ZL_Compressor_Graph_getHeadNode(
                    compressor_.get(), ZL_GRAPH_FIELD_LZ));
    ASSERT_EQ(
            ZL_NODE_ILLEGAL,
            ZL_Compressor_Graph_getHeadNode(
                    compressor_.get(), ZL_GRAPH_DELTA_ZSTD));

    ASSERT_EQ(
            ZL_NODE_FLOAT16_DECONSTRUCT,
            ZL_Compressor_Graph_getHeadNode(
                    compressor_.get(), makeStaticGraph()));
    ASSERT_EQ(
            ZL_NODE_ILLEGAL,
            ZL_Compressor_Graph_getHeadNode(
                    compressor_.get(), makeSelectorGraph()));
    ASSERT_EQ(
            ZL_NODE_ILLEGAL,
            ZL_Compressor_Graph_getHeadNode(
                    compressor_.get(), makeDynamicGraph()));
    ASSERT_EQ(
            ZL_NODE_ILLEGAL,
            ZL_Compressor_Graph_getHeadNode(
                    compressor_.get(), makeMultiInputGraph()));
}

TEST_F(CompressorTest, GraphGetSuccessors)
{
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getSuccessors(compressor_.get(), ZL_GRAPH_STORE)
                    .nbGraphIDs);
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getSuccessors(compressor_.get(), ZL_GRAPH_ZSTD)
                    .nbGraphIDs);
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getSuccessors(
                    compressor_.get(), ZL_GRAPH_FIELD_LZ)
                    .nbGraphIDs);
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getSuccessors(
                    compressor_.get(), ZL_GRAPH_DELTA_ZSTD)
                    .nbGraphIDs);

    ASSERT_EQ(
            2u,
            ZL_Compressor_Graph_getSuccessors(
                    compressor_.get(), makeStaticGraph())
                    .nbGraphIDs);
    ASSERT_EQ(
            ZL_GRAPH_FIELD_LZ,
            ZL_Compressor_Graph_getSuccessors(
                    compressor_.get(), makeStaticGraph())
                    .graphids[0]);
    ASSERT_EQ(
            ZL_GRAPH_ZSTD,
            ZL_Compressor_Graph_getSuccessors(
                    compressor_.get(), makeStaticGraph())
                    .graphids[1]);
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getSuccessors(
                    compressor_.get(), makeSelectorGraph())
                    .nbGraphIDs);
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getSuccessors(
                    compressor_.get(), makeDynamicGraph())
                    .nbGraphIDs);
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getSuccessors(
                    compressor_.get(), makeMultiInputGraph())
                    .nbGraphIDs);
}

TEST_F(CompressorTest, GraphGetCustomNodes)
{
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getCustomNodes(
                    compressor_.get(), ZL_GRAPH_STORE)
                    .nbNodeIDs);
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getCustomNodes(compressor_.get(), ZL_GRAPH_ZSTD)
                    .nbNodeIDs);
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getCustomNodes(
                    compressor_.get(), ZL_GRAPH_FIELD_LZ)
                    .nbNodeIDs);
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getCustomNodes(
                    compressor_.get(), ZL_GRAPH_DELTA_ZSTD)
                    .nbNodeIDs);

    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getCustomNodes(
                    compressor_.get(), makeStaticGraph())
                    .nbNodeIDs);
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getCustomNodes(
                    compressor_.get(), makeSelectorGraph())
                    .nbNodeIDs);
    ASSERT_EQ(
            2u,
            ZL_Compressor_Graph_getCustomNodes(
                    compressor_.get(), makeDynamicGraph())
                    .nbNodeIDs);
    ASSERT_EQ(
            1u,
            ZL_Compressor_Graph_getCustomNodes(
                    compressor_.get(), makeMultiInputGraph())
                    .nbNodeIDs);
}

TEST_F(CompressorTest, GraphGetCustomGraphs)
{
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getCustomGraphs(
                    compressor_.get(), ZL_GRAPH_STORE)
                    .nbGraphIDs);
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getCustomGraphs(
                    compressor_.get(), ZL_GRAPH_ZSTD)
                    .nbGraphIDs);
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getCustomGraphs(
                    compressor_.get(), ZL_GRAPH_FIELD_LZ)
                    .nbGraphIDs);
    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getCustomGraphs(
                    compressor_.get(), ZL_GRAPH_DELTA_ZSTD)
                    .nbGraphIDs);

    ASSERT_EQ(
            0u,
            ZL_Compressor_Graph_getCustomGraphs(
                    compressor_.get(), makeStaticGraph())
                    .nbGraphIDs);
    ASSERT_EQ(
            2u,
            ZL_Compressor_Graph_getCustomGraphs(
                    compressor_.get(), makeSelectorGraph())
                    .nbGraphIDs);
    ASSERT_EQ(
            1u,
            ZL_Compressor_Graph_getCustomGraphs(
                    compressor_.get(), makeDynamicGraph())
                    .nbGraphIDs);
    ASSERT_EQ(
            1u,
            ZL_Compressor_Graph_getCustomGraphs(
                    compressor_.get(), makeMultiInputGraph())
                    .nbGraphIDs);
}

TEST_F(CompressorTest, GraphGetLocalParams)
{
    expectParamsEmpty(ZL_Compressor_Graph_getLocalParams(
            compressor_.get(), ZL_GRAPH_STORE));
    expectParamsEmpty(ZL_Compressor_Graph_getLocalParams(
            compressor_.get(), ZL_GRAPH_ZSTD));
    expectParamsEmpty(ZL_Compressor_Graph_getLocalParams(
            compressor_.get(), ZL_GRAPH_FIELD_LZ));

    expectParamsEq(
            localParams_,
            ZL_Compressor_Graph_getLocalParams(
                    compressor_.get(), makeStaticGraph()));
    expectParamsEq(
            localParams_,
            ZL_Compressor_Graph_getLocalParams(
                    compressor_.get(), makeSelectorGraph()));
    expectParamsEq(
            localParams_,
            ZL_Compressor_Graph_getLocalParams(
                    compressor_.get(), makeDynamicGraph()));
    expectParamsEq(
            localParams_,
            ZL_Compressor_Graph_getLocalParams(
                    compressor_.get(), makeMultiInputGraph()));

    auto graph = makeParameterizedGraph();
    expectParamsEq(
            localParams_,
            ZL_Compressor_Graph_getLocalParams(compressor_.get(), graph));
}

// =============================================================================
// Node Property Tests
// =============================================================================

TEST_F(CompressorTest, NodeIsVariableInput)
{
    ASSERT_FALSE(ZL_Compressor_Node_isVariableInput(
            compressor_.get(), ZL_NODE_ZSTD));
    ASSERT_FALSE(ZL_Compressor_Node_isVariableInput(
            compressor_.get(), ZL_NODE_FIELD_LZ));
    ASSERT_TRUE(ZL_Compressor_Node_isVariableInput(
            compressor_.get(), ZL_NODE_CONCAT_SERIAL));
    ASSERT_TRUE(ZL_Compressor_Node_isVariableInput(
            compressor_.get(), ZL_NODE_DEDUP_NUMERIC));
}

TEST_F(CompressorTest, NodeGetLocalParams)
{
    expectParamsEmpty(
            ZL_Compressor_Node_getLocalParams(compressor_.get(), ZL_NODE_ZSTD));
    expectParamsEmpty(ZL_Compressor_Node_getLocalParams(
            compressor_.get(), ZL_NODE_FIELD_LZ));
    expectParamsEmpty(ZL_Compressor_Node_getLocalParams(
            compressor_.get(), ZL_NODE_DELTA_INT));

    openzl::NodeParameters nodeParams;
    nodeParams.localParams = openzl::LocalParams(localParams_);
    auto node = compressor_.parameterizeNode(ZL_NODE_DELTA_INT, nodeParams);
    expectParamsEq(
            localParams_,
            ZL_Compressor_Node_getLocalParams(compressor_.get(), node));
}

// =============================================================================
// Graph Validation Tests
// =============================================================================

TEST_F(CompressorTest, ReferencingUnfinishedCGraphWithoutStartingGraphID)
{
    ZL_CCtx* const cctx = ZL_CCtx_create();
    ASSERT_TRUE(cctx);
    // Use a fresh compressor for this specific test
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    ASSERT_TRUE(cgraph);

    ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx, cgraph);
    EXPECT_EQ(ZL_isError(rcgr), 1) << "CGraph reference should have failed\n";
    EXPECT_EQ(rcgr._code, ZL_ErrorCode_graph_invalid)
            << "expected this error code specifically";

    ZL_Compressor_free(cgraph);
    ZL_CCtx_free(cctx);
}

// =============================================================================
// Graph Naming Tests
// =============================================================================

TEST_F(CompressorTest, GraphName)
{
    static const char graphName[]      = "!test graph name";
    const ZL_GraphID successor[]       = { ZL_GRAPH_STORE };
    ZL_StaticGraphDesc const testGraph = {
        .name           = graphName,
        .headNodeid     = ZL_NODE_DELTA_INT,
        .successor_gids = successor,
        .nbGids         = 1,
    };

    ZL_GraphID const graphid =
            ZL_Compressor_registerStaticGraph(compressor_.get(), &testGraph);

    const char* const testName =
            ZL_Compressor_Graph_getName(compressor_.get(), graphid);
    EXPECT_EQ(strcmp(testName, graphName + 1), 0);
}

TEST_F(CompressorTest, NullGraphName)
{
    const ZL_GraphID successor[]       = { ZL_GRAPH_STORE };
    ZL_StaticGraphDesc const testGraph = {
        // .name intentionally not set
        .headNodeid     = ZL_NODE_DELTA_INT,
        .successor_gids = successor,
        .nbGids         = 1,
    };

    ZL_GraphID const graphid =
            ZL_Compressor_registerStaticGraph(compressor_.get(), &testGraph);

    const char* const testName =
            ZL_Compressor_Graph_getName(compressor_.get(), graphid);
    EXPECT_EQ(strcmp(testName, "#0"), 0);
}

TEST_F(CompressorTest, SelectorName)
{
    static const char graphName[] = "!test selector name";
    const ZL_GraphID successor[]  = { ZL_GRAPH_STORE };
    const ZL_SelectorDesc desc    = {
           .selector_f =
                [](auto, auto, auto, auto) noexcept { return ZL_GRAPH_STORE; },
        .inStreamType             = ZL_Type_serial,
        .customGraphs             = successor,
        .nbCustomGraphs           = 1,
        .name                     = graphName,
    };

    ZL_GraphID const graphid =
            ZL_Compressor_registerSelectorGraph(compressor_.get(), &desc);

    const char* const testName =
            ZL_Compressor_Graph_getName(compressor_.get(), graphid);
    EXPECT_EQ(strcmp(testName, graphName + 1), 0);
}

// =============================================================================
// Base Node/Graph ID Tests
// =============================================================================

TEST_F(CompressorTest, BaseNodeStandardTransform)
{
    const auto std_nid = ZL_NODE_ZIGZAG;

    {
        // Standard nodes don't expose their base nodes.
        const auto std_base_nid =
                ZL_Compressor_Node_getBaseNodeID(compressor_.get(), std_nid);
        EXPECT_EQ(std_base_nid, ZL_NODE_ILLEGAL);
    }

    ZL_IntParam int_param = (ZL_IntParam){
        .paramId    = 1,
        .paramValue = 1,
    };
    const ZL_LocalParams local_params{ .intParams = (ZL_LocalIntParams){
                                               .intParams   = &int_param,
                                               .nbIntParams = 0,
                                       } };
    openzl::NodeParameters nodeParams;
    nodeParams.localParams = openzl::LocalParams(local_params);
    const auto cp_nid      = compressor_.parameterizeNode(std_nid, nodeParams);
    EXPECT_NE(cp_nid, ZL_NODE_ILLEGAL);
    EXPECT_NE(cp_nid, std_nid);

    {
        // Copied nodes should point back to their parent.
        const auto cp_base_nid =
                ZL_Compressor_Node_getBaseNodeID(compressor_.get(), cp_nid);
        EXPECT_NE(cp_base_nid, ZL_NODE_ILLEGAL);
        EXPECT_EQ(cp_base_nid, std_nid);
    }

    int_param.paramValue++;
    nodeParams.localParams = openzl::LocalParams(local_params);
    const auto cp_cp_nid   = compressor_.parameterizeNode(cp_nid, nodeParams);
    EXPECT_NE(cp_cp_nid, ZL_NODE_ILLEGAL);
    EXPECT_NE(cp_cp_nid, cp_nid);

    {
        // Multiply-copied nodes should point back to their immediate parent.
        const auto cp_cp_base_nid =
                ZL_Compressor_Node_getBaseNodeID(compressor_.get(), cp_cp_nid);
        EXPECT_NE(cp_cp_base_nid, ZL_NODE_ILLEGAL);
        EXPECT_EQ(cp_cp_base_nid, cp_nid);
    }
}

TEST_F(CompressorTest, BaseNodeCustomTransform)
{
    const auto outputStreamType = ZL_Type_serial;
    const auto tr_func          = [](ZL_Encoder*, const ZL_Input*) noexcept {
        return ZL_returnSuccess();
    };
    const auto tr_desc = (ZL_TypedEncoderDesc){
        .gd =
                (ZL_TypedGraphDesc){
                        .CTid           = 12345,
                        .inStreamType   = ZL_Type_serial,
                        .outStreamTypes = &outputStreamType,
                        .nbOutStreams   = 1,
                },
        .transform_f = tr_func,
        .localParams = (ZL_LocalParams){},
        .name        = "!custom.test.noop",
    };

    const auto nid =
            ZL_Compressor_registerTypedEncoder(compressor_.get(), &tr_desc);
    EXPECT_NE(nid, ZL_NODE_ILLEGAL);

    {
        // Registered custom nodes don't have a base node.
        const auto base_nid =
                ZL_Compressor_Node_getBaseNodeID(compressor_.get(), nid);
        EXPECT_EQ(base_nid, ZL_NODE_ILLEGAL);
    }

    ZL_IntParam int_param = (ZL_IntParam){
        .paramId    = 1,
        .paramValue = 1,
    };
    const ZL_LocalParams local_params{ .intParams = (ZL_LocalIntParams){
                                               .intParams   = &int_param,
                                               .nbIntParams = 0,
                                       } };
    openzl::NodeParameters nodeParams;
    nodeParams.localParams = openzl::LocalParams(local_params);
    const auto cp_nid      = compressor_.parameterizeNode(nid, nodeParams);
    EXPECT_NE(cp_nid, ZL_NODE_ILLEGAL);
    EXPECT_NE(cp_nid, nid);

    {
        // Copied nodes should point back to their parent.
        const auto cp_base_nid =
                ZL_Compressor_Node_getBaseNodeID(compressor_.get(), cp_nid);
        EXPECT_NE(cp_base_nid, ZL_NODE_ILLEGAL);
        EXPECT_EQ(cp_base_nid, nid);
    }

    int_param.paramValue++;
    nodeParams.localParams = openzl::LocalParams(local_params);
    const auto cp_cp_nid   = compressor_.parameterizeNode(cp_nid, nodeParams);
    EXPECT_NE(cp_cp_nid, ZL_NODE_ILLEGAL);
    EXPECT_NE(cp_cp_nid, cp_nid);

    {
        // Multiply-copied nodes should point back to their immediate parent.
        const auto cp_cp_base_nid =
                ZL_Compressor_Node_getBaseNodeID(compressor_.get(), cp_cp_nid);
        EXPECT_NE(cp_cp_base_nid, ZL_NODE_ILLEGAL);
        EXPECT_EQ(cp_cp_base_nid, cp_nid);
    }
}

namespace {

void cloneAndCheckGetBaseGraphID(
        ZL_Compressor* const compressor,
        const ZL_GraphID gid)
{
    EXPECT_NE(gid, ZL_GRAPH_ILLEGAL);

    {
        // Graphs produced other than by registerParameterizedGraph (standard,
        // static, dynamic, etc.) don't expose their base graphs.
        const auto base_gid =
                ZL_Compressor_Graph_getBaseGraphID(compressor, gid);
        EXPECT_EQ(base_gid, ZL_GRAPH_ILLEGAL);
    }

    ZL_IntParam int_param = (ZL_IntParam){
        .paramId    = 1,
        .paramValue = 1,
    };
    const ZL_LocalParams local_params{ .intParams = (ZL_LocalIntParams){
                                               .intParams   = &int_param,
                                               .nbIntParams = 0,
                                       } };
    const auto desc1 = (ZL_ParameterizedGraphDesc){
        .name           = NULL,
        .graph          = gid,
        .customGraphs   = NULL,
        .nbCustomGraphs = 0,
        .customNodes    = NULL,
        .nbCustomNodes  = 0,
        .localParams    = &local_params,
    };
    const auto cp_gid =
            ZL_Compressor_registerParameterizedGraph(compressor, &desc1);
    EXPECT_NE(cp_gid, ZL_GRAPH_ILLEGAL);
    EXPECT_NE(cp_gid, gid);

    {
        // Copied nodes should point back to their parent.
        const auto cp_base_gid =
                ZL_Compressor_Graph_getBaseGraphID(compressor, cp_gid);
        EXPECT_NE(cp_base_gid, ZL_GRAPH_ILLEGAL);
        EXPECT_EQ(cp_base_gid, gid);
    }

    int_param.paramValue++;
    const auto desc2 = (ZL_ParameterizedGraphDesc){
        .name           = NULL,
        .graph          = cp_gid,
        .customGraphs   = NULL,
        .nbCustomGraphs = 0,
        .customNodes    = NULL,
        .nbCustomNodes  = 0,
        .localParams    = &local_params,
    };
    const auto cp_cp_gid =
            ZL_Compressor_registerParameterizedGraph(compressor, &desc2);
    EXPECT_NE(cp_cp_gid, ZL_GRAPH_ILLEGAL);
    EXPECT_NE(cp_cp_gid, cp_gid);
    EXPECT_NE(cp_cp_gid, gid);

    {
        // Multiply-copied nodes should point back to their immediate parent.
        const auto cp_cp_base_gid =
                ZL_Compressor_Graph_getBaseGraphID(compressor, cp_cp_gid);
        EXPECT_NE(cp_cp_base_gid, ZL_GRAPH_ILLEGAL);
        EXPECT_EQ(cp_cp_base_gid, cp_gid);
    }
}

} // namespace

TEST_F(CompressorTest, BaseGraphStandard)
{
    const auto std_gid = ZL_GRAPH_FIELD_LZ;

    cloneAndCheckGetBaseGraphID(compressor_.get(), std_gid);
}

TEST_F(CompressorTest, BaseGraphStatic)
{
    const auto successor = ZL_GRAPH_ZSTD;
    const auto gid       = ZL_Compressor_registerStaticGraph_fromNode(
            compressor_.get(), ZL_NODE_ZIGZAG, &successor, 1);

    cloneAndCheckGetBaseGraphID(compressor_.get(), gid);
}

TEST_F(CompressorTest, BaseGraphDynamic)
{
    const auto name       = "!tests.graph.dyn.stub";
    const auto graph_func = [](ZL_Graph*, ZL_Edge*[], size_t) noexcept {
        ZL_RET_R_ERR(GENERIC, "Unimplemented! Can't actually run.");
    };
    const auto validate_func = [](const ZL_Compressor*,
                                  const ZL_FunctionGraphDesc*) noexcept {
        // TODO: actually do some validation?
        return 1;
    };

    const auto inputTypes = ZL_Type_any;
    const auto desc       = (ZL_FunctionGraphDesc){
              .name                = name,
              .graph_f             = graph_func,
              .validate_f          = validate_func,
              .inputTypeMasks      = &inputTypes,
              .nbInputs            = 1,
              .lastInputIsVariable = false,
              .customGraphs        = NULL,
              .nbCustomGraphs      = 0,
              .customNodes         = NULL,
              .nbCustomNodes       = 0,
              .localParams         = {},
    };
    const auto gid =
            ZL_Compressor_registerFunctionGraph(compressor_.get(), &desc);

    cloneAndCheckGetBaseGraphID(compressor_.get(), gid);
}

// =============================================================================
// Graph Parameter Replacement Tests
// =============================================================================

TEST_F(CompressorTest, ReplaceGraphParams)
{
    openzl::LocalParams nullParams;
    openzl::LocalParams intParams;
    intParams.addIntParam(1, 1);
    ZL_GraphID customGraphs[1] = { ZL_GRAPH_ZSTD };
    ZL_NodeID customNodes[1]   = { ZL_NODE_ZIGZAG };
    // Set up cctx, compressor and input
    auto params        = (openzl::GraphParameters){ .localParams = nullParams };
    const auto rparams = (ZL_GraphParameters){ .customGraphs   = customGraphs,
                                               .nbCustomGraphs = 1,
                                               .customNodes    = customNodes,
                                               .nbCustomNodes  = 1,
                                               .localParams = intParams.get() };

    auto customStore = compressorCpp_.parameterizeGraph(ZL_GRAPH_STORE, params);
    EXPECT_ZS_VALID(ZL_Compressor_overrideGraphParams(
            compressorCpp_.get(), customStore, &rparams));
    auto r = ZL_Compressor_Graph_getLocalParams(
            compressorCpp_.get(), customStore);
    EXPECT_EQ(r.intParams.intParams[0].paramId, 1);
    EXPECT_EQ(r.intParams.intParams[0].paramValue, 1);
    auto cg = ZL_Compressor_Graph_getCustomGraphs(
            compressorCpp_.get(), customStore);
    EXPECT_EQ(cg.nbGraphIDs, 1);
    EXPECT_EQ(cg.graphids[0].gid, ZL_GRAPH_ZSTD.gid);
    auto cn = ZL_Compressor_Graph_getCustomNodes(
            compressorCpp_.get(), customStore);
    EXPECT_EQ(cn.nbNodeIDs, 1);
    EXPECT_EQ(cn.nodeids[0], ZL_NODE_ZIGZAG);
}

TEST_F(CompressorTest, CannotReplaceName)
{
    auto params = (ZL_GraphParameters){
        .name = "replaceName",
    };
    auto replaceParams = (ZL_GraphParameters){
        .name = "testParameterized",
    };
    const auto graph_func = [](ZL_Graph*, ZL_Edge*[], size_t) noexcept {
        ZL_RET_R_ERR(GENERIC, "Unimplemented! Can't actually run.");
    };
    ZL_FunctionGraphDesc desc = {
        .name    = "testBase",
        .graph_f = graph_func,
    };
    auto newBaseGraph =
            ZL_Compressor_registerFunctionGraph(compressorCpp_.get(), &desc);
    auto newGraph = ZL_RES_value(ZL_Compressor_parameterizeGraph(
            compressorCpp_.get(), newBaseGraph, &params));
    EXPECT_ZS_ERROR(ZL_Compressor_overrideGraphParams(
            compressorCpp_.get(), newGraph, &replaceParams));
}

TEST_F(CompressorTest, CannotReplaceWhenGraphIsNotParameterized)
{
    ZL_GraphID customGraphs[1] = { ZL_GRAPH_ZSTD };
    auto params                = (ZL_GraphParameters){
                       .customGraphs   = customGraphs,
                       .nbCustomGraphs = 1,
    };
    const auto graph_func = [](ZL_Graph*, ZL_Edge*[], size_t) noexcept {
        ZL_RET_R_ERR(GENERIC, "Unimplemented! Can't actually run.");
    };
    ZL_FunctionGraphDesc desc = {
        .name    = "testBase",
        .graph_f = graph_func,
    };
    auto newBaseGraph =
            ZL_Compressor_registerFunctionGraph(compressorCpp_.get(), &desc);
    EXPECT_NE(newBaseGraph.gid, ZL_GRAPH_ILLEGAL.gid);

    EXPECT_ZS_ERROR(ZL_Compressor_overrideGraphParams(
            compressorCpp_.get(), ZL_GRAPH_ILLEGAL, &params));

    EXPECT_ZS_ERROR(ZL_Compressor_overrideGraphParams(
            compressorCpp_.get(), ZL_GRAPH_ZSTD, &params));

    EXPECT_ZS_ERROR(ZL_Compressor_overrideGraphParams(
            compressorCpp_.get(), newBaseGraph, &params));
}

TEST_F(CompressorTest, ReplaceGraphParamsOnlyWhenParamExists)
{
    const auto graph_func = [](ZL_Graph*, ZL_Edge*[], size_t) noexcept {
        ZL_RET_R_ERR(GENERIC, "Unimplemented! Can't actually run.");
    };
    ZL_GraphID customGraphs[1] = { ZL_GRAPH_ZSTD };
    ZL_NodeID customNodes[1]   = { ZL_NODE_ZIGZAG };
    openzl::LocalParams localParams;
    localParams.addIntParam(1, 1);
    ZL_FunctionGraphDesc desc = {
        .name    = "testBase",
        .graph_f = graph_func,
    };
    ZL_GraphParameters initialParams = {
        .customGraphs   = customGraphs,
        .nbCustomGraphs = 1,
        .customNodes    = customNodes,
        .nbCustomNodes  = 1,
        .localParams    = localParams.get(),
    };
    auto baseGraph =
            ZL_Compressor_registerFunctionGraph(compressorCpp_.get(), &desc);
    auto newGraph             = ZL_RES_value(ZL_Compressor_parameterizeGraph(
            compressorCpp_.get(), baseGraph, &initialParams));
    ZL_GraphParameters params = { .nbCustomGraphs = 0,
                                  .nbCustomNodes  = 0,
                                  .localParams    = NULL };
    EXPECT_ZS_VALID(ZL_Compressor_overrideGraphParams(
            compressorCpp_.get(), newGraph, &params));
    auto lp =
            ZL_Compressor_Graph_getLocalParams(compressorCpp_.get(), newGraph);
    EXPECT_EQ(lp.intParams.intParams[0].paramId, 1);
    EXPECT_EQ(lp.intParams.intParams[0].paramValue, 1);
    auto cg =
            ZL_Compressor_Graph_getCustomGraphs(compressorCpp_.get(), newGraph);
    EXPECT_EQ(cg.nbGraphIDs, 1);
    EXPECT_EQ(cg.graphids[0].gid, ZL_GRAPH_ZSTD.gid);
    auto cn =
            ZL_Compressor_Graph_getCustomNodes(compressorCpp_.get(), newGraph);
    EXPECT_EQ(cn.nbNodeIDs, 1);
    EXPECT_EQ(cn.nodeids[0], ZL_NODE_ZIGZAG);
}
