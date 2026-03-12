// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "cpp/tests/TestUtils.hpp"
#include "openzl/zl_config.h"

using namespace testing;

namespace openzl::tests {

namespace {
class TestCCtx : public testing::Test {
   public:
    void SetUp() override
    {
        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor_.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);
    }

    Compressor compressor_;
};
} // namespace

TEST_F(TestCCtx, get)
{
    CCtx cctx;
    ASSERT_NE(cctx.get(), nullptr);
}

TEST_F(TestCCtx, parameters)
{
    CCtx cctx;
    ASSERT_EQ(cctx.getParameter(CParam::CompressionLevel), 0);
    cctx.setParameter(CParam::CompressionLevel, 1);
    ASSERT_EQ(cctx.getParameter(CParam::CompressionLevel), 1);
    cctx.resetParameters();
    ASSERT_EQ(cctx.getParameter(CParam::CompressionLevel), 0);
}

TEST_F(TestCCtx, compressSerial)
{
    CCtx cctx;
    cctx.refCompressor(compressor_);
    const auto input =
            "hello world this is some test input hello hello hello world hello test input";
    auto compressed = cctx.compressSerial(input);

    auto decompressed = DCtx().decompressSerial(compressed);
    ASSERT_EQ(input, decompressed);
}

TEST_F(TestCCtx, compressOne)
{
    CCtx cctx;
    cctx.refCompressor(compressor_);
    std::array<int64_t, 100> data = {};
    data[50]                      = 50;
    auto input      = Input::refStruct(poly::span<const int64_t>(data));
    auto compressed = cctx.compressOne(input);

    auto decompressed = DCtx().decompressOne(compressed);
    ASSERT_EQ(input, decompressed);
}

TEST_F(TestCCtx, compress)
{
    std::vector<Input> inputs;
    CCtx cctx;
    cctx.refCompressor(compressor_);
    std::array<int64_t, 100> data = {};
    data[50]                      = 50;
    inputs.push_back(Input::refStruct(poly::span<const int64_t>(data)));
    inputs.push_back(Input::refNumeric(poly::span<const int64_t>(data)));
    inputs.push_back(
            Input::refSerial(
                    "hello world this is some test input hello hello hello world hello test input"));
    std::array<uint32_t, 5> lengths = { 1, 3, 2, 1, 2 };
    inputs.push_back(Input::refString("133322122", lengths));
    auto compressed = cctx.compress(inputs);

    auto decompressed = DCtx().decompress(compressed);
    ASSERT_EQ(inputs.size(), decompressed.size());
    for (size_t i = 0; i < decompressed.size(); ++i) {
        ASSERT_EQ(inputs[i], decompressed[i]);
    }
}

TEST_F(TestCCtx, selectStartingGraph)
{
    CCtx cctx;
    std::array<int64_t, 100> data = {};
    data[50]                      = 50;
    auto numeric = Input::refNumeric(poly::span<const int64_t>(data));
    auto serial  = Input::refSerial("hello world hello hello hello hello");

    cctx.selectStartingGraph(compressor_, ZL_GRAPH_COMPRESS_GENERIC);
    testRoundTrip(cctx, serial);

    cctx.selectStartingGraph(compressor_, ZL_GRAPH_COMPRESS_GENERIC);
    testRoundTrip(cctx, numeric);

    cctx.selectStartingGraph(compressor_, ZL_GRAPH_FIELD_LZ);
    EXPECT_THROW(testRoundTrip(cctx, serial), Exception);

    cctx.selectStartingGraph(compressor_, ZL_GRAPH_FIELD_LZ);
    testRoundTrip(cctx, numeric);

    cctx.refCompressor(compressor_);
    cctx.selectStartingGraph(ZL_GRAPH_ZSTD);
    testRoundTrip(cctx, serial);

    cctx.refCompressor(compressor_);
    cctx.selectStartingGraph(ZL_GRAPH_ZSTD);
    testRoundTrip(cctx, numeric);

    auto graph = compressor_.registerFunctionGraph(
            std::make_shared<RunNodeThenGraphFunctionGraph>());
    compressor_.selectStartingGraph(graph);
    cctx.refCompressor(compressor_);
    EXPECT_THROW(testRoundTrip(cctx, serial), Exception);

    LocalParams localParams;
    localParams.addIntParam(
            int(RunNodeThenGraphFunctionGraph::Params::NodeParam), 1);
    localParams.addIntParam(
            int(RunNodeThenGraphFunctionGraph::Params::GraphParam), 1);
    cctx.selectStartingGraph(
            compressor_,
            graph,
            GraphParameters{
                    .customGraphs = { { ZL_GRAPH_ILLEGAL, ZL_GRAPH_ZSTD } },
                    .customNodes  = { { ZL_NODE_ILLEGAL, ZL_NODE_DELTA_INT } },
                    .localParams  = localParams,
            });
    testRoundTrip(cctx, numeric);
}

#if ZL_ALLOW_INTROSPECTION

TEST_F(TestCCtx, writeMultipleTraces)
{
    CCtx cctx;
    cctx.setParameter(CParam::StickyParameters, 1);
    const auto getThrows = [&cctx]() {
        EXPECT_THROW(
                {
                    try {
                        cctx.getLatestTrace();
                    } catch (const openzl::Exception& e) {
                        EXPECT_EQ(e.msg(), "Tracing is not enabled");
                        throw;
                    }
                },
                openzl::Exception);
    };

    cctx.refCompressor(compressor_);
    getThrows();
    cctx.writeTraces(true);
    auto empty{ cctx.getLatestTrace() };
    EXPECT_EQ(empty.first, "");
    std::array<int64_t, 100> data = {};
    data[50]                      = 50;
    auto numeric = Input::refNumeric(poly::span<const int64_t>(data));
    std::vector<std::pair<
            std::string,
            std::map<std::string, std::pair<std::string, std::string>>>>
            traces;
    // convenience function to copy the trace internals, since we get returned
    // string_views
    const auto genTrace =
            [](const std::pair<
                    poly::string_view,
                    std::map<
                            std::string,
                            std::pair<poly::string_view, poly::string_view>>>&
                       trace) {
                std::pair<
                        std::string,
                        std::map<
                                std::string,
                                std::pair<std::string, std::string>>>
                        copy;
                copy.first = trace.first;
                for (const auto& [k, v] : trace.second) {
                    copy.second[k] = { std::string(v.first),
                                       std::string(v.second) };
                }
                return copy;
            };

    auto compressed = cctx.compressOne(numeric);
    traces.emplace_back(genTrace(cctx.getLatestTrace()));
    traces.emplace_back(genTrace(cctx.getLatestTrace())); // to test determinism
    compressed = cctx.compressOne(numeric);
    traces.emplace_back(genTrace(cctx.getLatestTrace()));
    compressed = cctx.compressOne(numeric);
    traces.emplace_back(genTrace(cctx.getLatestTrace()));
    cctx.writeTraces(false);
    getThrows();
    for (size_t i = 1; i < traces.size(); ++i) {
        EXPECT_EQ(traces[i], traces[0]);
    }
}

#endif // ZL_ALLOW_INTROSPECTION

} // namespace openzl::tests
