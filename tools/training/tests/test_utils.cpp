// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "openzl/codecs/zl_concat.h"
#include "openzl/codecs/zl_conversion.h"
#include "openzl/codecs/zl_lz.h"
#include "openzl/codecs/zl_store.h"
#include "openzl/codecs/zl_zstd.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/zl_version.h"
#include "tools/training/utils/utils.h"

namespace openzl::training {
namespace {

std::vector<ZL_IDType> graphIds(const std::vector<GraphID>& graphs)
{
    std::vector<ZL_IDType> ids;
    ids.reserve(graphs.size());
    for (const auto graph : graphs) {
        ids.push_back(graph.gid);
    }
    return ids;
}

std::vector<MultiInput> serialInputs()
{
    static const std::array<uint8_t, 1024> data = [] {
        std::array<uint8_t, 1024> result{};
        for (size_t i = 0; i < result.size(); ++i) {
            result[i] = static_cast<uint8_t>(i);
        }
        return result;
    }();
    MultiInput input;
    input.add(Input::refSerial(data.data(), data.size()));
    return { std::move(input) };
}

TEST(CompressorIsFormatCompatibleTest, UsesCompressorFormatVersion)
{
    Compressor compressor;
    compressor.selectStartingGraph(ZL_GRAPH_LZ);
    compressor.setParameter(CParam::FormatVersion, 23);
    EXPECT_FALSE(compressorIsFormatCompatible(compressor, serialInputs()));

    compressor.setParameter(CParam::FormatVersion, 24);
    EXPECT_TRUE(compressorIsFormatCompatible(compressor, serialInputs()));
}

TEST(FilterGraphsByFormatVersionTest, ThrowsWhenFormatVersionBelowMinimum)
{
    Compressor compressor;
    const auto customGraph = compressor.buildStaticGraph(
            ZL_NODE_CONVERT_STRUCT_TO_SERIAL, { ZL_GRAPH_STORE });
    compressor.selectStartingGraph(ZL_GRAPH_STORE);
    const std::vector<GraphID> graphs = { ZL_GRAPH_STORE,
                                          ZL_GRAPH_ZSTD,
                                          customGraph };

    // Leaving the format version unset reads back as 0, which is below
    // ZL_MIN_FORMAT_VERSION. setParameter rejects any explicit value below the
    // minimum, so an unset compressor is the way to exercise the guard.
    EXPECT_THROW(
            filterGraphsByFormatVersion(compressor, graphs, serialInputs()),
            Exception);
}

TEST(FilterGraphsByFormatVersionTest, FiltersLZByVersion)
{
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, 23);
    const auto beforeVersion24 = filterGraphsByFormatVersion(
            compressor, { ZL_GRAPH_LZ }, serialInputs());
    EXPECT_TRUE(beforeVersion24.empty());

    compressor.setParameter(CParam::FormatVersion, 24);
    const auto atVersion24 = filterGraphsByFormatVersion(
            compressor, { ZL_GRAPH_LZ }, serialInputs());
    EXPECT_EQ(graphIds(atVersion24), graphIds({ ZL_GRAPH_LZ }));
    EXPECT_EQ(compressor.getParameter(CParam::FormatVersion), 24);
}

TEST(FilterGraphsByFormatVersionTest, RestoresCompressorStateAfterException)
{
    Compressor compressor;
    compressor.selectStartingGraph(ZL_GRAPH_STORE);
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

    EXPECT_THROW(
            filterGraphsByFormatVersion(
                    compressor, { ZL_GRAPH_ILLEGAL }, serialInputs()),
            Exception);

    EXPECT_EQ(
            compressor.getParameter(CParam::FormatVersion),
            ZL_MAX_FORMAT_VERSION);
    EXPECT_EQ(compressor.getStartingGraph(), ZL_GRAPH_STORE);
}

TEST(FilterGraphsByFormatVersionTest, FiltersCustomGraphByConversionVersion)
{
    Compressor compressor;
    const auto graph = compressor.buildStaticGraph(
            ZL_NODE_CONVERT_STRUCT_TO_NUM_BE, { ZL_GRAPH_STORE });
    const std::array<uint32_t, 64> data{};
    MultiInput input;
    input.add(Input::refStruct(data.data(), data.size()));
    const std::vector<MultiInput> inputs = { std::move(input) };

    compressor.setParameter(CParam::FormatVersion, 20);
    const auto beforeVersion21 =
            filterGraphsByFormatVersion(compressor, { graph }, inputs);
    EXPECT_TRUE(beforeVersion21.empty());

    compressor.setParameter(CParam::FormatVersion, 21);
    const auto atVersion21 =
            filterGraphsByFormatVersion(compressor, { graph }, inputs);
    EXPECT_EQ(graphIds(atVersion21), graphIds({ graph }));
}

TEST(FilterGraphsByFormatVersionTest, SupportsMultiInputGraphs)
{
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    const auto graph = compressor.buildStaticGraph(
            ZL_NODE_CONCAT_SERIAL, { ZL_GRAPH_STORE, ZL_GRAPH_STORE });
    MultiInput input;
    input.add(Input::refSerial("first", 5));
    input.add(Input::refSerial("second", 6));
    const std::vector<MultiInput> inputs = { std::move(input) };

    const auto supported =
            filterGraphsByFormatVersion(compressor, { graph }, inputs);

    EXPECT_EQ(graphIds(supported), graphIds({ graph }));
}

} // namespace
} // namespace openzl::training
