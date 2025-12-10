// Copyright (c) Meta Platforms, Inc. and affiliates.

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
} // namespace openzl
