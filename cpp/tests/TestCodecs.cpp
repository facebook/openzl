// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <climits>

#include <gtest/gtest.h>

#include "cpp/tests/TestUtils.hpp"
#include "openzl/openzl.hpp"

using namespace ::testing;
namespace openzl {

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
