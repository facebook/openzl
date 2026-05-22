// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <climits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "cpp/tests/TestUtils.hpp"
#include "openzl/openzl.hpp"

using namespace ::testing;
namespace openzl {
namespace {

std::vector<std::pair<int, int>> collectIntParams(const LocalParams& params)
{
    std::vector<std::pair<int, int>> result;
    for (const auto& param : params.getIntParams()) {
        result.emplace_back(param.paramId, param.paramValue);
    }
    return result;
}

} // namespace

class TestCodecs : public testing::Test {
   public:
    void SetUp() override
    {
        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor_.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);
    }

    Compressor compressor_;
};

TEST_F(TestCodecs, bitpack)
{
    std::vector<int> data(10000, 7);
    data.push_back(0);
    const size_t bound = (data.size() * 3) / 8 + 100;
    compressor_.selectStartingGraph(graphs::Bitpack{}());
    auto compressed = testRoundTrip(
            compressor_, Input::refNumeric(poly::span<const int>{ data }));
    EXPECT_LE(compressed.size(), bound);
}

TEST_F(TestCodecs, lz4)
{
    std::string data(10000, 'a');
    auto graph = graphs::Lz4().parameterize(compressor_);
    compressor_.selectStartingGraph(graph);
    auto compressed = testRoundTrip(compressor_, Input::refSerial(data));
}

TEST_F(TestCodecs, lz4_hc)
{
    std::string data(10000, 'a');
    auto graph = graphs::Lz4(9).parameterize(compressor_);
    compressor_.selectStartingGraph(graph);
    auto compressed = testRoundTrip(compressor_, Input::refSerial(data));
}

TEST_F(TestCodecs, lzParameters)
{
    const auto muxLengthsGraph =
            nodes::MuxLengths{}(compressor_, graphs::Store::graph);
    const graphs::Lz lz(
            graphs::Lz::Parameters{
                    .nodeParams =
                            graphs::Lz::NodeParams{
                                    .compressionLevel = -1,
                                    .acceleration     = 2,
                                    .windowLog        = 16,
                            },
                    .literalsGraph        = graphs::Store::graph,
                    .offsetsGraph         = graphs::Store::graph,
                    .muxedBytesGraph      = graphs::Store::graph,
                    .overflowLengthsGraph = graphs::Store::graph,
                    .muxLengthsGraph      = muxLengthsGraph,
            });

    const auto graphParameters = lz.parameters();
    ASSERT_TRUE(graphParameters.has_value());
    ASSERT_TRUE(graphParameters->localParams.has_value());
    ASSERT_TRUE(graphParameters->customGraphs.has_value());

    const std::vector<std::pair<int, int>> expectedIntParams{
        { ZL_LzParam_compressionLevel, -1 },
        { ZL_LzParam_acceleration, 2 },
        { ZL_LzParam_windowLog, 16 },
        { ZL_LzParam_literalsGraphIdx, 0 },
        { ZL_LzParam_offsetsGraphIdx, 1 },
        { ZL_LzParam_muxedBytesGraphIdx, 2 },
        { ZL_LzParam_overflowLengthsGraphIdx, 3 },
        { ZL_LzParam_muxLengthsGraphIdx, 4 },
    };
    EXPECT_EQ(
            collectIntParams(*graphParameters->localParams), expectedIntParams);

    const std::vector<GraphID> expectedCustomGraphs{
        graphs::Store::graph, graphs::Store::graph, graphs::Store::graph,
        graphs::Store::graph, muxLengthsGraph,
    };
    EXPECT_EQ(*graphParameters->customGraphs, expectedCustomGraphs);

    std::string data(10000, 'a');
    auto graph = lz.parameterize(compressor_);
    compressor_.selectStartingGraph(graph);
    auto compressed = testRoundTrip(compressor_, Input::refSerial(data));
}

TEST_F(TestCodecs, lzNodeParameters)
{
    const nodes::Lz lz(
            nodes::Lz::Parameters{
                    .compressionLevel = 3,
                    .acceleration     = 4,
                    .windowLog        = 17,
            });

    const auto nodeParameters = lz.parameters();
    ASSERT_TRUE(nodeParameters.has_value());
    ASSERT_TRUE(nodeParameters->localParams.has_value());

    const std::vector<std::pair<int, int>> expectedIntParams{
        { ZL_LzParam_compressionLevel, 3 },
        { ZL_LzParam_acceleration, 4 },
        { ZL_LzParam_windowLog, 17 },
    };
    EXPECT_EQ(
            collectIntParams(*nodeParameters->localParams), expectedIntParams);
}

TEST_F(TestCodecs, segmentSerial_defaultChunkSize)
{
    /* chunkByteSize = 0 sentinel: use the segmenter's built-in default. */
    std::string data(4096, 'a');
    auto graph = graphs::SegmentSerial(ZL_GRAPH_COMPRESS_GENERIC)
                         .parameterize(compressor_);
    compressor_.selectStartingGraph(graph);
    auto compressed = testRoundTrip(compressor_, Input::refSerial(data));
}

TEST_F(TestCodecs, segmentSerial_explicitChunkSize)
{
    /* Explicit chunk size at the minimum threshold must round-trip. */
    std::string data(ZL_MIN_CHUNK_SIZE * 2, 'a');
    auto graph =
            graphs::SegmentSerial(ZL_GRAPH_COMPRESS_GENERIC, ZL_MIN_CHUNK_SIZE)
                    .parameterize(compressor_);
    compressor_.selectStartingGraph(graph);
    auto compressed = testRoundTrip(compressor_, Input::refSerial(data));
}

TEST_F(TestCodecs, segmentSerial_chunkSizeOverflowThrows)
{
    /* The C builder rejects chunk sizes that would not fit in int; the C++
     * wrapper surfaces that rejection as a typed Exception at parameterize
     * time (construction itself does not validate). */
    graphs::SegmentSerial wrapper(
            ZL_GRAPH_COMPRESS_GENERIC, static_cast<size_t>(INT_MAX) + 1);
    EXPECT_THROW(wrapper.parameterize(compressor_), Exception);
}
} // namespace openzl
