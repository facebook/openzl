// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <optional>
#include "openzl/common/limits.h"
#include "openzl/zl_public_nodes.h"
#include "tests/zstrong/test_zstrong_fixture.h"

/**
 * These tests cover cases where library limits are exceeded.
 * We want to make sure that exceeding these limits doesn't crash.
 */

namespace openzl::tests {

class SurpassingLimitsTest : public ZStrongTest {
   public:
    ZL_GraphID makeSplitGraph(size_t numSplitsPerLevel, size_t numLevels)
    {
        numSplitsPerLevel_ = numSplitsPerLevel;
        auto node          = ZL_Compressor_registerSplitNode_withParser(
                cgraph_,
                ZL_Type_serial,
                [](ZL_SplitState* state, const ZL_Input* in) {
                    const size_t numSplits =
                            *(const size_t*)ZL_SplitState_getOpaquePtr(state);
                    ZL_SplitInstructions instructions{};
                    size_t* segmentSizes = (size_t*)ZL_SplitState_malloc(
                            state, sizeof(size_t) * numSplits);
                    if (segmentSizes == nullptr) {
                        return instructions;
                    }
                    const size_t nbElts    = ZL_Input_numElts(in);
                    const size_t splitSize = nbElts / numSplits;
                    for (size_t i = 0; i < numSplits; ++i) {
                        segmentSizes[i] = splitSize;
                    }
                    segmentSizes[numSplits - 1] = 0;

                    instructions.segmentSizes = segmentSizes;
                    instructions.nbSegments   = numSplits;
                    return instructions;
                },
                &numSplitsPerLevel_.value());
        ZL_GraphID graph = ZL_GRAPH_STORE;
        for (size_t i = 0; i < numLevels; ++i) {
            graph = ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph_, node, graph);
        }
        return graph;
    }

    std::string input(size_t numSplitsPerLevel, size_t numLevels)
    {
        size_t inputSize = 1;
        for (size_t i = 0; i < numLevels; ++i) {
            inputSize *= numSplitsPerLevel;
        }
        return std::string(inputSize, 'a');
    }

    void testSplitGraphSucceeds(size_t numSplitsPerLevel, size_t numLevels)
    {
        reset();
        finalizeGraph(makeSplitGraph(numSplitsPerLevel, numLevels), 1);
        setLargeCompressBound(10);
        testRoundTrip(input(numSplitsPerLevel, numLevels));
    }

    void testSplitGraphFails(size_t numSplitsPerLevel, size_t numLevels)
    {
        finalizeGraph(makeSplitGraph(numSplitsPerLevel, numLevels), 1);
        setLargeCompressBound(10);
        auto [csize, compressedOpt] =
                compress(input(numSplitsPerLevel, numLevels));
        ZL_REQUIRE(ZL_isError(csize));
    }

   private:
    std::optional<size_t> numSplitsPerLevel_;
};

TEST_F(SurpassingLimitsTest, TestTransformOutStreamLimit)
{
    // Test that using the limit - 1 succeeds
    for (int formatVersion = ZL_MIN_FORMAT_VERSION;
         formatVersion <= ZL_MAX_FORMAT_VERSION;
         ++formatVersion) {
        reset();
        setParameter(ZL_CParam_formatVersion, formatVersion);
        testSplitGraphSucceeds(
                ZL_transformOutStreamsLimit(formatVersion) - 1, 1);
    }
    // Test that using the limit + 1 fails
    for (int formatVersion = ZL_MIN_FORMAT_VERSION;
         formatVersion <= ZL_MAX_FORMAT_VERSION;
         ++formatVersion) {
        reset();
        setParameter(ZL_CParam_formatVersion, formatVersion);
        testSplitGraphFails(ZL_transformOutStreamsLimit(formatVersion) + 1, 1);
    }
    // Test that using the limit * 2 fails
    for (int formatVersion = ZL_MIN_FORMAT_VERSION;
         formatVersion <= ZL_MAX_FORMAT_VERSION;
         ++formatVersion) {
        reset();
        setParameter(ZL_CParam_formatVersion, formatVersion);
        testSplitGraphFails(ZL_transformOutStreamsLimit(formatVersion) * 2, 1);
    }
}

TEST_F(SurpassingLimitsTest, TestRuntimeStreamLimit)
{
    // Test that using the exact limit succeeds
    const size_t numSplitsPerLevel = 128;
    size_t numLastLayerStreams     = 1;
    size_t totalStreams            = 1;
    for (size_t numLevels = 1; numLevels <= 3; ++numLevels) {
        numLastLayerStreams *= numSplitsPerLevel;
        totalStreams += numLastLayerStreams;
        bool anySucceed = false;
        for (int formatVersion = std::max(10, ZL_MIN_FORMAT_VERSION);
             formatVersion <= ZL_MAX_FORMAT_VERSION;
             ++formatVersion) {
            reset();
            setParameter(ZL_CParam_formatVersion, formatVersion);
            ZL_LOG(ALWAYS,
                   "Testing format version %d, numLevels %zu, numLastLayer = %zu, total = %zu, limit = %u",
                   formatVersion,
                   numLevels,
                   numLastLayerStreams,
                   totalStreams,
                   ZL_runtimeStreamLimit(formatVersion));
            if (totalStreams < ZL_runtimeStreamLimit(formatVersion)) {
                testSplitGraphSucceeds(numSplitsPerLevel, numLevels);
                anySucceed = true;
            } else {
                testSplitGraphFails(numSplitsPerLevel, numLevels);
            }
        }
        if (!anySucceed) {
            break;
        }
    }
}
} // namespace openzl::tests
