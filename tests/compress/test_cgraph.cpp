// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <vector>

#include <gtest/gtest.h>

#include "openzl/compress/cgraph.h"
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_public_nodes.h"
#include "openzl/zl_reflection.h"
#include "openzl/zl_selector.h" // ZL_SelectorDesc

#include "tests/datagen/random_producer/PRNGWrapper.h"
#include "tests/datagen/structures/CompressorProducer.h"
#include "tests/utils.h"

using namespace ::testing;
using namespace ::openzl::tests;
using namespace ::openzl::tests::datagen;

namespace {

class CGraphTest : public Test {
   protected:
    void SetUp() override
    {
        // TODO: replace with impl with C++ bindings
        cgraph_ = ZL_Compressor_create();

        // Set up cctx, and input
        cctx_.setParameter(
                openzl::CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        cctx_.setParameter(openzl::CParam::StickyParameters, 1);
    }

    void TearDown() override
    {
        ZL_Compressor_free(cgraph_);
    }

    ZL_GraphID declareGraph(
            ZL_NodeID node,
            std::vector<ZL_GraphID> const& successors)
    {
        return ZL_Compressor_registerStaticGraph_fromNode(
                cgraph_, node, successors.data(), successors.size());
    }

    size_t nbOutcomes(ZL_NodeID node) const
    {
        return ZL_Compressor_Node_getNumOutcomes(cgraph_, node);
    }

    ZL_GraphID declareGraph(ZL_NodeID node)
    {
        std::vector<ZL_GraphID> successors(nbOutcomes(node), ZL_GRAPH_STORE);
        return declareGraph(node, successors);
    }

    ZL_Compressor* cgraph_{ nullptr };

    openzl::Compressor compressor_;
    openzl::CCtx cctx_;
    openzl::DCtx dctx_;
};

TEST_F(CGraphTest, referencingUnfinishedCGraphWithoutStartingGraphID)
{
    ZL_CCtx* const cctx = ZL_CCtx_create();
    ASSERT_TRUE(cctx);
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    ASSERT_TRUE(cgraph);

    ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx, cgraph);
    EXPECT_EQ(ZL_isError(rcgr), 1) << "CGraph reference should have failed\n";
    EXPECT_EQ(rcgr._code, ZL_ErrorCode_graph_invalid)
            << "expected this error code specifically";

    ZL_Compressor_free(cgraph);
    ZL_CCtx_free(cctx);
}

/* Note(@Cyan): since zstrong supports Typed Inputs, there is no longer a
 * requirement for the first default Graph to support Serial Inputs. */

TEST_F(CGraphTest, graphName)
{
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    ASSERT_TRUE(cgraph);

    static const char graphName[]      = "!test graph name";
    const ZL_GraphID successor[]       = { ZL_GRAPH_STORE };
    ZL_StaticGraphDesc const testGraph = {
        .name           = graphName,
        .headNodeid     = ZL_NODE_DELTA_INT,
        .successor_gids = successor,
        .nbGids         = 1,
    };

    ZL_GraphID const graphid =
            ZL_Compressor_registerStaticGraph(cgraph, &testGraph);

    const char* const testName = ZL_Compressor_Graph_getName(cgraph, graphid);
    EXPECT_EQ(strcmp(testName, graphName + 1), 0);

    ZL_Compressor_free(cgraph);
}

TEST_F(CGraphTest, nullGraphName)
{
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    ASSERT_TRUE(cgraph);

    const ZL_GraphID successor[]       = { ZL_GRAPH_STORE };
    ZL_StaticGraphDesc const testGraph = {
        // .name intentionally not set
        .headNodeid     = ZL_NODE_DELTA_INT,
        .successor_gids = successor,
        .nbGids         = 1,
    };

    ZL_GraphID const graphid =
            ZL_Compressor_registerStaticGraph(cgraph, &testGraph);

    const char* const testName = ZL_Compressor_Graph_getName(cgraph, graphid);
    EXPECT_EQ(strcmp(testName, "#0"), 0);

    ZL_Compressor_free(cgraph);
}

TEST_F(CGraphTest, selectorName)
{
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    ASSERT_TRUE(cgraph);

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
            ZL_Compressor_registerSelectorGraph(cgraph, &desc);

    const char* const testName = ZL_Compressor_Graph_getName(cgraph, graphid);
    EXPECT_EQ(strcmp(testName, graphName + 1), 0);

    ZL_Compressor_free(cgraph);
}

TEST_F(CGraphTest, baseNodeStandardTransform)
{
    ZL_Compressor* const compressor = ZL_Compressor_create();
    ASSERT_TRUE(compressor);

    const auto std_nid = ZL_NODE_ZIGZAG;

    {
        // Standard nodes don't expose their base nodes.
        const auto std_base_nid =
                ZL_Compressor_Node_getBaseNodeID(compressor, std_nid);
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
    const auto cp_nid =
            ZL_Compressor_cloneNode(compressor, std_nid, &local_params);
    EXPECT_NE(cp_nid, ZL_NODE_ILLEGAL);
    EXPECT_NE(cp_nid, std_nid);

    {
        // Copied nodes should point back to their parent.
        const auto cp_base_nid =
                ZL_Compressor_Node_getBaseNodeID(compressor, cp_nid);
        EXPECT_NE(cp_base_nid, ZL_NODE_ILLEGAL);
        EXPECT_EQ(cp_base_nid, std_nid);
    }

    int_param.paramValue++;
    const auto cp_cp_nid =
            ZL_Compressor_cloneNode(compressor, cp_nid, &local_params);
    EXPECT_NE(cp_cp_nid, ZL_NODE_ILLEGAL);
    EXPECT_NE(cp_cp_nid, cp_nid);

    {
        // Multiply-copied nodes should point back to their immediate parent.
        const auto cp_cp_base_nid =
                ZL_Compressor_Node_getBaseNodeID(compressor, cp_cp_nid);
        EXPECT_NE(cp_cp_base_nid, ZL_NODE_ILLEGAL);
        EXPECT_EQ(cp_cp_base_nid, cp_nid);
    }

    ZL_Compressor_free(compressor);
}

TEST_F(CGraphTest, baseNodeCustomTransform)
{
    ZL_Compressor* const compressor = ZL_Compressor_create();
    ASSERT_TRUE(compressor);

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

    const auto nid = ZL_Compressor_registerTypedEncoder(compressor, &tr_desc);
    EXPECT_NE(nid, ZL_NODE_ILLEGAL);

    {
        // Registered custom nodes don't have a base node.
        const auto base_nid = ZL_Compressor_Node_getBaseNodeID(compressor, nid);
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
    const auto cp_nid = ZL_Compressor_cloneNode(compressor, nid, &local_params);
    EXPECT_NE(cp_nid, ZL_NODE_ILLEGAL);
    EXPECT_NE(cp_nid, nid);

    {
        // Copied nodes should point back to their parent.
        const auto cp_base_nid =
                ZL_Compressor_Node_getBaseNodeID(compressor, cp_nid);
        EXPECT_NE(cp_base_nid, ZL_NODE_ILLEGAL);
        EXPECT_EQ(cp_base_nid, nid);
    }

    int_param.paramValue++;
    const auto cp_cp_nid =
            ZL_Compressor_cloneNode(compressor, cp_nid, &local_params);
    EXPECT_NE(cp_cp_nid, ZL_NODE_ILLEGAL);
    EXPECT_NE(cp_cp_nid, cp_nid);

    {
        // Multiply-copied nodes should point back to their immediate parent.
        const auto cp_cp_base_nid =
                ZL_Compressor_Node_getBaseNodeID(compressor, cp_cp_nid);
        EXPECT_NE(cp_cp_base_nid, ZL_NODE_ILLEGAL);
        EXPECT_EQ(cp_cp_base_nid, cp_nid);
    }

    ZL_Compressor_free(compressor);
}

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

TEST_F(CGraphTest, baseGraphStandard)
{
    ZL_Compressor* const compressor = ZL_Compressor_create();
    ASSERT_TRUE(compressor);

    const auto std_gid = ZL_GRAPH_FIELD_LZ;

    cloneAndCheckGetBaseGraphID(compressor, std_gid);

    ZL_Compressor_free(compressor);
}

TEST_F(CGraphTest, baseGraphStatic)
{
    ZL_Compressor* const compressor = ZL_Compressor_create();
    ASSERT_TRUE(compressor);

    const auto successor = ZL_GRAPH_ZSTD;
    const auto gid       = ZL_Compressor_registerStaticGraph_fromNode(
            compressor, ZL_NODE_ZIGZAG, &successor, 1);

    cloneAndCheckGetBaseGraphID(compressor, gid);

    ZL_Compressor_free(compressor);
}

TEST_F(CGraphTest, baseGraphDynamic)
{
    ZL_Compressor* const compressor = ZL_Compressor_create();
    ASSERT_TRUE(compressor);

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
    const auto gid = ZL_Compressor_registerFunctionGraph(compressor, &desc);

    cloneAndCheckGetBaseGraphID(compressor, gid);

    ZL_Compressor_free(compressor);
}

TEST_F(CGraphTest, replaceGraphParams)
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

    auto customStore = compressor_.parameterizeGraph(ZL_GRAPH_STORE, params);
    EXPECT_ZS_VALID(ZL_Compressor_overrideGraphParams(
            compressor_.get(), customStore, &rparams));
    auto r = ZL_Compressor_Graph_getLocalParams(compressor_.get(), customStore);
    EXPECT_EQ(r.intParams.intParams[0].paramId, 1);
    EXPECT_EQ(r.intParams.intParams[0].paramValue, 1);
    auto cg =
            ZL_Compressor_Graph_getCustomGraphs(compressor_.get(), customStore);
    EXPECT_EQ(cg.nbGraphIDs, 1);
    EXPECT_EQ(cg.graphids[0].gid, ZL_GRAPH_ZSTD.gid);
    auto cn =
            ZL_Compressor_Graph_getCustomNodes(compressor_.get(), customStore);
    EXPECT_EQ(cn.nbNodeIDs, 1);
    EXPECT_EQ(cn.nodeids[0], ZL_NODE_ZIGZAG);
}

TEST_F(CGraphTest, cannotReplaceName)
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
            ZL_Compressor_registerFunctionGraph(compressor_.get(), &desc);
    auto newGraph = ZL_RES_value(ZL_Compressor_parameterizeGraph(
            compressor_.get(), newBaseGraph, &params));
    EXPECT_ZS_ERROR(ZL_Compressor_overrideGraphParams(
            compressor_.get(), newGraph, &replaceParams));
}

TEST_F(CGraphTest, cannotReplaceWhenGraphIsNotParameterized)
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
            ZL_Compressor_registerFunctionGraph(compressor_.get(), &desc);
    EXPECT_NE(newBaseGraph.gid, ZL_GRAPH_ILLEGAL.gid);

    EXPECT_ZS_ERROR(ZL_Compressor_overrideGraphParams(
            compressor_.get(), ZL_GRAPH_ILLEGAL, &params));

    EXPECT_ZS_ERROR(ZL_Compressor_overrideGraphParams(
            compressor_.get(), ZL_GRAPH_ZSTD, &params));

    EXPECT_ZS_ERROR(ZL_Compressor_overrideGraphParams(
            compressor_.get(), newBaseGraph, &params));
}

TEST_F(CGraphTest, replaceGraphParamsOnlyWhenParamExists)
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
            ZL_Compressor_registerFunctionGraph(compressor_.get(), &desc);
    auto newGraph             = ZL_RES_value(ZL_Compressor_parameterizeGraph(
            compressor_.get(), baseGraph, &initialParams));
    ZL_GraphParameters params = { .nbCustomGraphs = 0,
                                  .nbCustomNodes  = 0,
                                  .localParams    = NULL };
    EXPECT_ZS_VALID(ZL_Compressor_overrideGraphParams(
            compressor_.get(), newGraph, &params));
    auto lp = ZL_Compressor_Graph_getLocalParams(compressor_.get(), newGraph);
    EXPECT_EQ(lp.intParams.intParams[0].paramId, 1);
    EXPECT_EQ(lp.intParams.intParams[0].paramValue, 1);
    auto cg = ZL_Compressor_Graph_getCustomGraphs(compressor_.get(), newGraph);
    EXPECT_EQ(cg.nbGraphIDs, 1);
    EXPECT_EQ(cg.graphids[0].gid, ZL_GRAPH_ZSTD.gid);
    auto cn = ZL_Compressor_Graph_getCustomNodes(compressor_.get(), newGraph);
    EXPECT_EQ(cn.nbNodeIDs, 1);
    EXPECT_EQ(cn.nodeids[0], ZL_NODE_ZIGZAG);
}

} // namespace
