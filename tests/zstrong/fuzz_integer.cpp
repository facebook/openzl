// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "security/lionhead/utils/lib_ftest/ftest.h"

#include "openzl/compress/private_nodes.h"
#include "openzl/shared/mem.h"
#include "tests/fuzz_utils.h"
#include "tests/zstrong/test_integer_fixture.h"

#include "openzl/zl_compressor.h" // ZL_Compressor_*, ZL_Compressor_registerStaticGraph_fromNode1o

namespace zstrong {
namespace tests {
namespace {

FUZZ_F(IntegerTest, FuzzConvertIntToTokenRoundTrip)
{
    size_t const eltWidth = f.choices("elt_width", { 1, 2, 4, 8 });
    std::string input =
            gen_str(f, "input_data", ShortInputLengthInBytes(eltWidth));
    testNodeOnInput(ZL_NODE_CONVERT_NUM_TO_TOKEN, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzConvertIntToSerialRoundTrip)
{
    size_t const eltWidth = f.choices("elt_width", { 1, 2, 4, 8 });
    std::string input =
            gen_str(f, "input_data", ShortInputLengthInBytes(eltWidth));
    testNodeOnInput(ZL_NODE_CONVERT_NUM_TO_SERIAL, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzQuantizeOffsetsRoundTrip)
{
    auto input = f.vec_args(
            "input_data",
            d_range_u32(1, (uint32_t)-1),
            InputLengthInElts(sizeof(uint32_t)));
    // TODO(terrelln): This hack is here to avoid null input pointers.
    // But we should fix the engine to accept NULL empty inputs.
    if (input.capacity() == 0)
        input.reserve(1);
    std::string_view inputView{ (char const*)input.data(),
                                input.size() * sizeof(input[0]) };
    testNodeOnInput(ZL_NODE_QUANTIZE_OFFSETS, 4, inputView);
}

FUZZ_F(IntegerTest, FuzzQuantizeLengthsRoundTrip)
{
    const size_t eltWidth = 4;
    std::string input = gen_str(f, "input_data", InputLengthInBytes(eltWidth));
    testNodeOnInput(ZL_NODE_QUANTIZE_LENGTHS, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzDeltaRoundTrip)
{
    size_t const eltWidth = f.choices("elt_width", { 1, 2, 4, 8 });
    std::string input = gen_str(f, "input_data", InputLengthInBytes(eltWidth));
    testNodeOnInput(ZL_NODE_DELTA_INT, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzZigzagRoundTrip)
{
    size_t const eltWidth = f.choices("elt_width", { 1, 2, 4, 8 });
    std::string input = gen_str(f, "input_data", InputLengthInBytes(eltWidth));
    testNodeOnInput(ZL_NODE_ZIGZAG, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzBitpackRoundTrip)
{
    size_t const eltWidth = f.choices("elt_width", { 1, 2, 4, 8 });
    std::string input = gen_str(f, "input_data", InputLengthInBytes(eltWidth));
    testNodeOnInput(ZL_NODE_BITPACK_INT, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzRangePackRoundTrip)
{
    size_t const eltWidth = f.choices("elt_width", { 1, 2, 4, 8 });
    std::string input = gen_str(f, "input_data", InputLengthInBytes(eltWidth));
    testNodeOnInput(ZL_NODE_RANGE_PACK, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzMergeSortedRoundTrip)
{
    size_t const eltWidth = 4;
    std::string input = gen_str(f, "input_data", InputLengthInBytes(eltWidth));
    reset();
    ZL_GraphID graph = ZL_Compressor_registerMergeSortedGraph(
            cgraph_, ZL_GRAPH_STORE, ZL_GRAPH_STORE, ZL_GRAPH_STORE);
    finalizeGraph(graph, eltWidth);
    setLargeCompressBound(8);
    testRoundTrip(input);
}

FUZZ_F(IntegerTest, FuzzSplitNRoundTrip)
{
    size_t const eltWidth = f.choices("elt_width", { 1, 2, 4, 8 });
    std::string input = gen_str(f, "input_str", InputLengthInBytes(eltWidth));

    StructuredFDP<HarnessMode>* fPtr = &f;

    reset();
    if (f.u8("split_by_param") >= 128) {
        auto segmentSizes = getSplitNSegments(f, input.size() / eltWidth);
        std::vector<ZL_GraphID> successors(segmentSizes.size(), ZL_GRAPH_STORE);
        auto graph = ZL_Compressor_registerSplitGraph(
                cgraph_,
                ZL_Type_numeric,
                segmentSizes.data(),
                successors.data(),
                successors.size());
        finalizeGraph(graph, eltWidth);
    } else {
        auto parser = [](ZL_SplitState* state, ZL_Input const* in) {
            auto& fdp = **static_cast<StructuredFDP<HarnessMode>* const*>(
                    ZL_SplitState_getOpaquePtr(state));
            auto const segments = getSplitNSegments(fdp, ZL_Input_numElts(in));
            auto segmentSizes   = (size_t*)ZL_SplitState_malloc(
                    state, segments.size() * sizeof(segments[0]));
            ZL_SplitInstructions instructions = { segmentSizes, 0 };
            if (segmentSizes == NULL) {
                return instructions;
            }
            memcpy(segmentSizes,
                   segments.data(),
                   segments.size() * sizeof(segments[0]));
            instructions.nbSegments = segments.size();
            return instructions;
        };
        ZL_NodeID node = ZL_Compressor_registerSplitNode_withParser(
                cgraph_, ZL_Type_numeric, parser, &fPtr);
        finalizeGraph(declareGraph(node), eltWidth);
    }
    setLargeCompressBound(8);
    testRoundTrip(input);
}

FUZZ_F(IntegerTest, FuzzFSENCountRoundTrip)
{
    size_t const eltWidth = 2;
    std::string input =
            gen_str(f, "input_data", ShortInputLengthInBytes(eltWidth));
    reset();
    finalizeGraph({ ZL_PrivateStandardGraphID_fse_ncount }, eltWidth);
    testRoundTripCompressionMayFail(input);
}

FUZZ_F(IntegerTest, FuzzIntegerSelector)
{
    size_t const eltWidth = f.choices("elt_width", { 1, 2, 4, 8 });
    std::string input =
            gen_str(f, "input_data", ShortInputLengthInBytes(eltWidth));
    reset();
    finalizeGraph(ZL_GRAPH_NUMERIC, eltWidth);
    testRoundTrip(input);
}

FUZZ_F(IntegerTest, FuzzIntegerDivideBy)
{
    size_t const eltWidth = f.choices("elt_width", { 1, 2, 4, 8 });
    std::string input =
            gen_str(f, "input_data", ShortInputLengthInBytes(eltWidth));
    reset();
    if (f.boolean("set_divisor")) {
        const auto divisor = f.u64("divisor");
        ZL_GraphID const graphDivideBy =
                ZL_Compressor_registerStaticGraph_fromNode1o(
                        cgraph_,
                        ZL_Compressor_registerDivideByNode(cgraph_, divisor),
                        ZL_GRAPH_COMPRESS_GENERIC);
        finalizeGraph(graphDivideBy, eltWidth);
        testRoundTripCompressionMayFail(input);
    } else {
        ZL_GraphID const graphDivideBy =
                ZL_Compressor_registerStaticGraph_fromNode1o(
                        cgraph_,
                        ZL_NodeID{ ZL_StandardNodeID_divide_by },
                        ZL_GRAPH_STORE);
        finalizeGraph(graphDivideBy, eltWidth);
        testRoundTrip(input);
    }
}
} // namespace
} // namespace tests
} // namespace zstrong
