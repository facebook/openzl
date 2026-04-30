// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/compress/private_nodes.h"
#include "tests/utils.h"
#include "tests/zstrong/test_zstrong_fixture.h"

using namespace ::testing;

namespace openzl::tests {
namespace {
class GraphsTest : public ZStrongTest {
   public:
   protected:
};

ZL_SplitInstructions splitter(ZL_SplitState* state, ZL_Input const* in)
{
    size_t constexpr kNumSegments = 4;
    auto const segmentSizes =
            (size_t*)ZL_SplitState_malloc(state, kNumSegments * sizeof(size_t));
    ZL_SplitInstructions instructions = { segmentSizes, 0 };
    if (segmentSizes == nullptr) {
        return instructions;
    }
    instructions.nbSegments = kNumSegments;

    size_t remaining = ZL_Input_numElts(in);
    for (size_t i = 0; i < kNumSegments; ++i) {
        if ((i % 2) == 1) {
            segmentSizes[i] = 0;
        } else {
            auto const segmentSize = std::min(remaining, size_t(10));
            segmentSizes[i]        = segmentSize;
            remaining -= segmentSize;
        }
    }
    assert(segmentSizes[kNumSegments - 1] == 0);

    return instructions;
}

TEST_F(GraphsTest, ZeroOutputNodes)
{
    // Test graph with many 0 output nodes combined with nodes with non-zero #
    // of outputs

    reset();

    auto node = ZL_Compressor_registerSplitNode_withParser(
            cgraph_, ZL_Type_serial, splitter, nullptr);
    auto graph = declareGraph(node, ZL_GRAPH_STORE);
    graph      = declareGraph(ZL_NODE_DELTA_INT, graph);
    graph      = declareGraph(ZL_NODE_INTERPRET_AS_LE8, graph);
    graph      = declareGraph(node, graph);
    graph      = declareGraph(ZL_NODE_TOKENIZE, { graph, graph });
    graph      = declareGraph(ZL_NODE_INTERPRET_AS_LE8, graph);
    graph      = declareGraph(node, graph);
    finalizeGraph(graph, 1);

    testRoundTrip(kLoremTestInput);
}

TEST_F(GraphsTest, UndersizedDstBuffer)
{
    reset();

    const auto& input = kLoremTestInput;

    auto cgraph = finalizeGraph(ZL_GRAPH_STORE, 1);

    auto [cres, compressed_opt] = compress(input);
    ZL_REQUIRE_SUCCESS(cres);
    EXPECT_TRUE(compressed_opt);
    auto compressed = std::move(*compressed_opt);

    auto [dres, decompressed_opt] = decompress(compressed);
    ZL_REQUIRE_SUCCESS(dres);
    EXPECT_TRUE(decompressed_opt);
    auto decompressed = std::move(*decompressed_opt);

    EXPECT_EQ(decompressed, input);

    // Test that compressions with a smaller buffer all fail with the correct
    // error.
    for (size_t dstCapacity = 0; dstCapacity < compressed.size();
         dstCapacity++) {
        std::string compressedTooSmall(dstCapacity, '\0');
        auto const res = ZL_compress_usingCompressor(
                compressedTooSmall.data(),
                compressedTooSmall.size(),
                input.data(),
                input.size(),
                cgraph);
        ASSERT_TRUE(ZL_isError(res));
        ASSERT_EQ(
                ZL_E_code(ZL_RES_error(res)),
                ZL_ErrorCode_dstCapacity_tooSmall);
    }

    // Test that decompressions with a smaller buffer all fail with the correct
    // error.
    for (size_t dstCapacity = 0; dstCapacity < input.size(); dstCapacity++) {
        std::string decompressedTooSmall(dstCapacity, '\0');
        auto const res = ZL_decompress(
                decompressedTooSmall.data(),
                decompressedTooSmall.size(),
                compressed.data(),
                compressed.size());
        ASSERT_TRUE(ZL_isError(res));
        ASSERT_EQ(
                ZL_E_code(ZL_RES_error(res)),
                ZL_ErrorCode_dstCapacity_tooSmall);
    }
}

} // namespace
} // namespace openzl::tests
