// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/codecs/zl_clustering.h"
#include "openzl/zl_reflection.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"
#include "tools/training/sample_collection/training_sample_collector.h"

namespace openzl {
namespace training {
namespace tests {
namespace {

std::vector<MultiInput> getMISamples(
        const std::vector<std::vector<std::string>>& samples)
{
    std::vector<MultiInput> mi_samples;
    for (auto& sample : samples) {
        MultiInput mi;
        for (size_t i = 0; i < sample.size(); i++) {
            auto input = Input::refSerial(sample[i]);
            input.setIntMetadata(0, i);
            mi.add(std::move(input));
        }
        mi_samples.emplace_back(std::move(mi));
    }
    return mi_samples;
}

class TestSampleCollection : public testing::Test {
    void SetUp() override
    {
        // Create the clustering graph with the ACE graph as successor
        auto ace = ZL_Compressor_buildACEGraph(compressor_.get());
        const ZL_GraphID successors[1] = {
            ace,
        };
        ZL_ClusteringConfig config = {};
        auto cluster               = ZL_Clustering_registerGraph(
                compressor_.get(), &config, successors, 1);

        // Set up the compressor
        compressor_.selectStartingGraph(cluster);
        cctx_.setParameter(CParam::CompressionLevel, 1);
        cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        cctx_.setParameter(CParam::StickyParameters, 1);
        cctx_.refCompressor(compressor_);
    };

   protected:
    CCtx cctx_;
    Compressor compressor_;
};
} // namespace

TEST_F(TestSampleCollection, BasicSampleCollection)
{
    std::vector<std::vector<std::string>> samples = { { "foo", "bar", "baz" },
                                                      { "foo" } };
    std::vector<MultiInput> mi_samples            = getMISamples(samples);

    // Get all the clustering instances (we expect 1)
    auto graphs =
            graph_mutation::findAllGraphsWithPrefix(compressor_, "zl.cluster");
    ASSERT_EQ(graphs.size(), 1);
    std::vector<std::string> names = { ZL_Compressor_Graph_getName(
            compressor_.get(), graphs[0]) };

    // Collect the input streams for the clustering instance
    auto inputs = collectInputStreamsForGraphs(mi_samples, names, cctx_);
    ASSERT_EQ(inputs.size(), 1);

    // Check that the input streams are correct
    auto& mis = inputs[names[0]];

    for (size_t i = 0; i < mis.size(); i++) {
        ASSERT_EQ((*mis[i]).size(), samples[i].size());
    }
}

} // namespace tests
} // namespace training
} // namespace openzl
