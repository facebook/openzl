// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <numeric>

#include <gtest/gtest.h>
#include "openzl/openzl.hpp"

using namespace testing;
using namespace openzl;

class TestFunctionGraph : public Test {
   public:
    void SetUp() override
    {
        compressor_ = Compressor();
        cctx_       = CCtx();
        dctx_       = DCtx();

        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    }

    std::string testRoundTrip(poly::span<const Input> inputs)
    {
        auto compressed   = cctx_.compress(inputs);
        auto decompressed = dctx_.decompress(compressed);
        EXPECT_EQ(decompressed.size(), inputs.size());
        for (size_t i = 0; i < inputs.size(); ++i) {
            EXPECT_EQ(decompressed[i], inputs[i]);
        }
        return compressed;
    }

    void testRoundTrip(const Input& input)
    {
        testRoundTrip({ &input, 1 });
    }

    Compressor compressor_;
    CCtx cctx_;
    DCtx dctx_;
};

namespace {
class ZstdFunctionGraph : public FunctionGraph {
   public:
    FunctionGraphDescription functionGraphDescription() const override
    {
        return {
            .name           = "zstd_function_graph",
            .inputTypeMasks = { TypeMask::Serial },
        };
    }

    void graph(GraphState& state) const override
    {
        state.edges()[0].setDestination(ZL_GRAPH_ZSTD);
    }
};

class BruteForceFunctionGraph : public FunctionGraph {
   public:
    BruteForceFunctionGraph(
            std::vector<TypeMask> inputTypeMasks,
            std::vector<GraphID> customGraphs)
            : inputTypeMasks_(std::move(inputTypeMasks)),
              customGraphs_(std::move(customGraphs))
    {
    }

    FunctionGraphDescription functionGraphDescription() const override
    {
        return {
            .inputTypeMasks = inputTypeMasks_,
            .customGraphs   = customGraphs_,
        };
    }

    void graph(GraphState& state) const override
    {
        auto edges = state.edges();
        std::vector<const ZL_Input*> inputs;
        inputs.reserve(edges.size());
        for (const auto& edge : edges) {
            inputs.push_back(edge.getInput().get());
        }
        auto storeResult = state.tryGraph(inputs, graphs::Store{}());
        if (!storeResult.has_value()) {
            throw Exception("Store must succeed");
        }
        GraphID bestGraph = graphs::Store{}();
        size_t bestSize   = storeResult->compressedSize;
        for (const auto graph : state.customGraphs()) {
            auto perf = state.tryGraph(inputs, graph);
            if (perf.has_value() && perf->compressedSize < bestSize) {
                bestGraph = graph;
                bestSize  = perf->compressedSize;
            }
        }
        Edge::setMultiInputDestination(edges, bestGraph);
    }

   private:
    std::vector<TypeMask> inputTypeMasks_;
    std::vector<GraphID> customGraphs_;
};
} // namespace

TEST_F(TestFunctionGraph, basic)
{
    auto graph = std::make_shared<ZstdFunctionGraph>();
    compressor_.registerFunctionGraph(graph);
    cctx_.refCompressor(compressor_);
    std::string data(1000, 'a');
    data += "hello world";
    testRoundTrip(Input::refSerial(data));
}

TEST_F(TestFunctionGraph, BruteForceFunctionGraph)
{
    auto dedup = nodes::DedupNumeric{}(compressor_, graphs::Compress{}());
    auto graph = std::make_shared<BruteForceFunctionGraph>(
            std::vector<TypeMask>{ TypeMask::Any, TypeMask::Any },
            std::vector<GraphID>{ graphs::Compress{}(), dedup });
    compressor_.selectStartingGraph(compressor_.registerFunctionGraph(graph));

    std::vector<int64_t> lhs(1000);
    std::vector<int64_t> rhs(1000);

    std::iota(lhs.begin(), lhs.end(), 0);
    std::iota(rhs.begin(), rhs.end(), 0);

    cctx_.refCompressor(compressor_);
    auto compressed = testRoundTrip(
            std::initializer_list<Input>{
                    Input::refNumeric(poly::span<const int64_t>(lhs)),
                    Input::refNumeric(poly::span<const int64_t>(rhs)) });

    std::iota(lhs.begin(), lhs.end(), 0);
    std::iota(rhs.begin(), rhs.end(), 1);

    cctx_.refCompressor(compressor_);
    auto compressed2 = testRoundTrip(
            std::initializer_list<Input>{
                    Input::refNumeric(poly::span<const int64_t>(lhs)),
                    Input::refNumeric(poly::span<const int64_t>(rhs)) });

    // first can dedup, second cannot
    ASSERT_GT(compressed2.size(), 1.75 * compressed.size());
}
