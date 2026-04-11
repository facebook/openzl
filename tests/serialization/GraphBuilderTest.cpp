// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/cpp/Compressor.hpp"
#include "openzl/zl_reflection.h"
#include "tests/datagen/DataGen.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/serialization/GraphBuilder.h"
#include "tests/serialization/GraphBuilderUtils.h"

namespace openzl::tests {

class GraphBuilderTest : public ::testing::Test {
   protected:
    void SetUp() override
    {
        auto gen = std::make_shared<std::mt19937>(0xdeadbeef);
        rw_      = std::make_shared<datagen::PRNGWrapper>(gen);
        dataGen_ = std::make_unique<datagen::DataGen>(rw_);
    }

    GraphBuilder createGraphBuilder(Compressor& compressor)
    {
        GraphBuilder builder(*dataGen_, compressor);
        builder.addAllComponents();
        return builder;
    }

    std::shared_ptr<datagen::PRNGWrapper> rw_;
    std::unique_ptr<datagen::DataGen> dataGen_;
};

TEST_F(GraphBuilderTest, testStringPartitioning)
{
    auto emptyPartitions = getPartitionedStringLengths("", 2, *dataGen_);
    EXPECT_EQ(emptyPartitions.size(), 0);
    auto onePartition = getPartitionedStringLengths("aaaaa", 1, *dataGen_);
    EXPECT_EQ(onePartition.size(), 1);
    EXPECT_EQ(onePartition[0], 5);
    auto maximumPartitions = getPartitionedStringLengths("aaaaa", 5, *dataGen_);
    EXPECT_EQ(maximumPartitions.size(), 5);
    for (size_t i = 0; i < maximumPartitions.size(); i++) {
        EXPECT_EQ(maximumPartitions[i], 1);
    }
    for (size_t i = 0; i < 100; i++) {
        std::string data = dataGen_->randString("data", 1000);
        int numSegments  = dataGen_->i32_range("num_segments", 0, 1000);
        auto partitioned =
                getPartitionedStringLengths(data, numSegments, *dataGen_);
        EXPECT_EQ(
                std::accumulate(partitioned.begin(), partitioned.end(), 0),
                data.size());
        EXPECT_EQ(
                partitioned.size(), std::min<size_t>(numSegments, data.size()));
    }
}

TEST_F(GraphBuilderTest, testGraphBuiltIsSerializable)
{
    // Validate serialization and deserialization succeed
    for (size_t i = 0; i < 100; i++) {
        Compressor compressor;
        // It is necessary to use a new builder each time, as continuously
        // adding to the compressor will eventually result in the compressor
        // growing too large.
        auto builder = createGraphBuilder(compressor);
        builder.buildCompressor();
        auto serialized = compressor.serialize();
        Compressor deserializedCompressor;
        deserializedCompressor.deserialize(serialized);
    }
}

TEST_F(GraphBuilderTest, testSegmentNumFromSerialGeneratedGraphsAreSerializable)
{
    Compressor compressor;
    auto component =
            makeOpenZLComponent(OpenZLComponentID::SegmentNumFromSerial);

    auto generatedGraphs = component->generateGraphs(compressor, *dataGen_, 3);
    ASSERT_EQ(generatedGraphs.size(), 3);

    for (auto graph : generatedGraphs) {
        compressor.selectStartingGraph(graph);
        auto serialized = compressor.serialize();
        Compressor deserializedCompressor;
        deserializedCompressor.deserialize(serialized);
    }
}

TEST_F(GraphBuilderTest, testDeterministicWithSameSeed)
{
    // Build two compressors with same seed
    auto gen1 = std::make_shared<std::mt19937>(0x12345);
    auto rw1  = std::make_shared<datagen::PRNGWrapper>(gen1);
    datagen::DataGen dataGen1(rw1);

    auto gen2 = std::make_shared<std::mt19937>(0x12345);
    auto rw2  = std::make_shared<datagen::PRNGWrapper>(gen2);
    datagen::DataGen dataGen2(rw2);

    Compressor compressor1;
    GraphBuilder builder1(dataGen1, compressor1);
    builder1.addAllComponents();
    builder1.buildCompressor();

    Compressor compressor2;
    GraphBuilder builder2(dataGen2, compressor2);
    builder2.addAllComponents();
    builder2.buildCompressor();

    EXPECT_EQ(compressor1.serialize(), compressor2.serialize());
}
} // namespace openzl::tests
