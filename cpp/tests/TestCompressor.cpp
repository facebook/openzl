// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/openzl.hpp"

#include "openzl/zl_reflection.h"

#include "cpp/tests/TestCustomCodec.hpp"
#include "cpp/tests/TestUtils.hpp"

using namespace testing;

namespace openzl::tests {

class TestCompressor : public Test {
   public:
    void SetUp() override
    {
        compressor_ = Compressor();
        cctx_       = CCtx();
        dctx_       = DCtx();
    }

    Compressor compressor_;
    CCtx cctx_;
    DCtx dctx_;
};

TEST_F(TestCompressor, get)
{
    ASSERT_NE(compressor_.get(), nullptr);
}

TEST_F(TestCompressor, parameters)
{
    ASSERT_EQ(compressor_.getParameter(CParam::FormatVersion), 0);
    compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    ASSERT_EQ(
            compressor_.getParameter(CParam::FormatVersion),
            ZL_MAX_FORMAT_VERSION);
}

TEST_F(TestCompressor, parameterizeGraph)
{
    auto graph = std::make_shared<RunNodeThenGraphFunctionGraph>(
            ZL_NODE_ILLEGAL, ZL_GRAPH_ILLEGAL);
    auto graphId          = compressor_.registerFunctionGraph(graph);
    std::vector<int> data = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    auto input            = Input::refNumeric(poly::span<const int>{ data });
    EXPECT_THROW(testRoundTrip(compressor_, input), Exception);
    LocalParams localParams;
    localParams.addIntParam(
            int(RunNodeThenGraphFunctionGraph::Params::GraphParam), 1);
    compressor_.parameterizeGraph(
            graphId,
            GraphParameters{
                    .customGraphs = { { ZL_GRAPH_ILLEGAL, ZL_GRAPH_ZSTD } },
                    .customNodes  = { { ZL_NODE_DELTA_INT } },
                    .localParams  = localParams,
            });
    testRoundTrip(compressor_, input);
}

TEST_F(TestCompressor, parameterizeNode)
{
    class MyEncoder : public NoOpCustomEncoder {
       public:
        MyEncoder() : NoOpCustomEncoder(0, "my_encoder", Type::Serial) {}

        void preEncodeHook(EncoderState& state) const override
        {
            auto param0 = state.getLocalIntParam(0);
            if (param0 != 42) {
                throw std::runtime_error("Bad parameter");
            }
            called = true;
        }

        mutable bool called{ false };
    };
    auto encoder = std::make_shared<MyEncoder>();
    auto node    = compressor_.registerCustomEncoder(encoder);
    compressor_.buildStaticGraph(node, { ZL_GRAPH_ZSTD });
    dctx_.registerCustomDecoder(
            std::make_shared<NoOpCustomDecoder>(0, "my_encoder", Type::Serial));
    auto input = Input::refSerial("hello world hello hello hello");
    EXPECT_THROW(testRoundTrip(compressor_, dctx_, input), std::runtime_error);
    ASSERT_FALSE(encoder->called);

    LocalParams params;
    params.addIntParam(0, 42);
    node = compressor_.parameterizeNode(
            node, NodeParameters{ .localParams = params });
    compressor_.buildStaticGraph(node, std::vector<GraphID>{ ZL_GRAPH_ZSTD });
    testRoundTrip(compressor_, dctx_, input);
}

TEST_F(TestCompressor, buildStaticGraph)
{
    std::vector<int> data;
    data.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        data.push_back(i);
    }
    auto graph = compressor_.buildStaticGraph(
            ZL_NODE_DELTA_INT, { ZL_GRAPH_CONSTANT });
    compressor_.selectStartingGraph(graph);
    ASSERT_EQ(compressor_.getStartingGraph(), graph);

    auto compressed = testRoundTrip(
            compressor_, Input::refNumeric(poly::span<const int>(data)));
    ASSERT_LT(compressed.size(), 100);
}

TEST_F(TestCompressor, getGraph)
{
    ASSERT_TRUE(compressor_.getGraph("zl.field_lz").has_value());
    ASSERT_FALSE(compressor_.getGraph("my_graph").has_value());

    auto graph = compressor_.buildStaticGraph(
            ZL_NODE_DELTA_INT,
            { ZL_GRAPH_FIELD_LZ },
            StaticGraphParameters{
                    .name = "!my_graph",
            });
    ASSERT_TRUE(compressor_.getGraph("my_graph").has_value());
    ASSERT_EQ(compressor_.getGraph("my_graph").value().gid, graph.gid);

    compressor_.buildStaticGraph(
            ZL_NODE_DELTA_INT,
            { ZL_GRAPH_FIELD_LZ },
            StaticGraphParameters{
                    .name = "my_graph2",
            });
    ASSERT_FALSE(compressor_.getGraph("my_graph2").has_value());
}

TEST_F(TestCompressor, getNode)
{
    ASSERT_TRUE(compressor_.getNode("zl.field_lz").has_value());
    ASSERT_FALSE(compressor_.getNode("my_node").has_value());
    auto node = compressor_.parameterizeNode(
            ZL_NODE_DELTA_INT,
            {
                    .name = "!my_node",
            });
    ASSERT_TRUE(compressor_.getNode("my_node").has_value());
    ASSERT_EQ(compressor_.getNode("my_node").value().nid, node.nid);

    compressor_.parameterizeNode(
            node,
            {
                    .name = "my_node2",
            });
    ASSERT_FALSE(compressor_.getNode("my_node2").has_value());
}

TEST_F(TestCompressor, serializeSuccess)
{
    constexpr int mss = 12345;
    std::string ser;
    std::string ser_json;
    {
        Compressor c;
        c.setParameter(CParam::MinStreamSize, mss);
        c.unwrap(ZL_Compressor_selectStartingGraphID(c.get(), ZL_GRAPH_ZSTD));
        ser      = c.serialize();
        ser_json = c.serializeToJson();
    }

    const auto conv_json = Compressor::convertSerializedToJson(ser);
    EXPECT_EQ(ser_json, conv_json);
    const auto json = "Serialized Compressor JSON: '" + conv_json + "'";

    {
        Compressor c;

        const auto unmet = c.getUnmetDependencies(ser);
        ASSERT_EQ(unmet.graphNames.size(), 0) << json;
        ASSERT_EQ(unmet.nodeNames.size(), 0) << json;

        c.deserialize(ser);
        ASSERT_EQ(c.getParameter(CParam::MinStreamSize), mss) << json;
        ZL_GraphID gid;
        ASSERT_TRUE(ZL_Compressor_getStartingGraphID(c.get(), &gid)) << json;
        ASSERT_EQ(gid, ZL_GRAPH_ZSTD) << json;
    }
}

TEST_F(TestCompressor, serializeWithUnmet)
{
    constexpr int mss  = 12345;
    const auto encoder = std::make_shared<PlusOneEncoder>();
    std::string ser;
    std::string ser_json;
    {
        Compressor c;
        const auto node  = c.registerCustomEncoder(encoder);
        const auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
                c.get(), node, ZL_GRAPH_ZSTD);
        c.unwrap(ZL_Compressor_selectStartingGraphID(c.get(), graph));
        c.setParameter(CParam::MinStreamSize, mss);
        ser      = c.serialize();
        ser_json = c.serializeToJson();
    }

    const auto conv_json = Compressor::convertSerializedToJson(ser);
    EXPECT_EQ(ser_json, conv_json);
    const auto json = "Serialized Compressor JSON: '" + conv_json + "'";

    {
        Compressor c;

        const auto unmet = c.getUnmetDependencies(ser);
        ASSERT_EQ(unmet.graphNames.size(), 0) << json;
        ASSERT_EQ(unmet.nodeNames.size(), 1) << json;
    }

    try {
        Compressor c;
        c.deserialize(ser);
        ASSERT_TRUE(false) << "Should have thrown!" << "\n" << json;
    } catch (const std::exception& ex) {
        const std::string msg{ ex.what() };
        auto codec_name = encoder->multiInputDescription().name.value();
        ASSERT_EQ(codec_name[0], '!');
        codec_name.erase(codec_name.begin());
        ASSERT_NE(msg.find(codec_name), std::string::npos)
                << "Exception Message: " << msg << "\n"
                << json;
    }

    {
        Compressor c;
        c.registerCustomEncoder(encoder);

        const auto unmet = c.getUnmetDependencies(ser);
        ASSERT_EQ(unmet.graphNames.size(), 0) << json;
        ASSERT_EQ(unmet.nodeNames.size(), 0) << json;

        c.deserialize(ser);
        ASSERT_EQ(c.getParameter(CParam::MinStreamSize), mss) << json;
    }
}
} // namespace openzl::tests
