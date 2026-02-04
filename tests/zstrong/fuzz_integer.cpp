// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/private_nodes.h"
#include "openzl/codecs/bitSplit/encode_bitSplit_binding.h" // ZL_Compressor_registerBitSplitNode
#include "openzl/shared/mem.h"
#include "tests/datagen/DataGen.h"
#include "tests/fuzz_utils.h"
#include "tests/zstrong/test_integer_fixture.h"

#include "openzl/zl_compressor.h" // ZS2_Compressor_*, ZL_Compressor_registerStaticGraph_fromNode1o

namespace openzl {
namespace tests {
namespace {

FUZZ_F(IntegerTest, FuzzConvertIntToTokenRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
    std::string input =
            dg.randStringWithQuantizedLength("input_data", eltWidth);
    testNodeOnInput(ZL_NODE_CONVERT_NUM_TO_TOKEN, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzConvertIntToSerialRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
    std::string input =
            dg.randStringWithQuantizedLength("input_data", eltWidth);
    testNodeOnInput(ZL_NODE_CONVERT_NUM_TO_SERIAL, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzQuantizeOffsetsRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    auto input =
            dg.randLongVector<uint32_t>("input_data", 1, (uint32_t)-1, 0, 512);
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
    datagen::DataGen dg   = fromFDP(f);
    const size_t eltWidth = 4;
    std::string input =
            dg.randStringWithQuantizedLength("input_data", eltWidth);
    testNodeOnInput(ZL_NODE_QUANTIZE_LENGTHS, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzDeltaRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
    std::string input =
            dg.randStringWithQuantizedLength("input_data", eltWidth);
    testNodeOnInput(ZL_NODE_DELTA_INT, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzZigzagRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
    std::string input =
            dg.randStringWithQuantizedLength("input_data", eltWidth);
    testNodeOnInput(ZL_NODE_ZIGZAG, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzBitpackRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
    std::string input =
            dg.randStringWithQuantizedLength("input_data", eltWidth);
    testNodeOnInput(ZL_NODE_BITPACK_INT, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzRangePackRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
    std::string input =
            dg.randStringWithQuantizedLength("input_data", eltWidth);
    testNodeOnInput(ZL_NODE_RANGE_PACK, eltWidth, input);
}

FUZZ_F(IntegerTest, FuzzMergeSortedRoundTrip)
{
    datagen::DataGen dg   = fromFDP(f);
    size_t const eltWidth = 4;
    std::string input =
            dg.randStringWithQuantizedLength("input_data", eltWidth);
    reset();
    ZL_GraphID graph = ZL_Compressor_registerMergeSortedGraph(
            cgraph_, ZL_GRAPH_STORE, ZL_GRAPH_STORE, ZL_GRAPH_STORE);
    finalizeGraph(graph, eltWidth);
    setLargeCompressBound(8);
    testRoundTrip(input);
}

FUZZ_F(IntegerTest, FuzzSplitNRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
    std::string input = dg.randStringWithQuantizedLength("input_str", eltWidth);

    StructuredFDP<HarnessMode>* fPtr = &f;

    reset();
    if (dg.u8("split_by_param") >= 128) {
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
    datagen::DataGen dg   = fromFDP(f);
    size_t const eltWidth = 2;
    std::string input =
            dg.randStringWithQuantizedLength("input_data", eltWidth);
    reset();
    finalizeGraph({ ZL_PrivateStandardGraphID_fse_ncount }, eltWidth);
    testRoundTripCompressionMayFail(input);
}

FUZZ_F(IntegerTest, FuzzIntegerSelector)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
    std::string input =
            dg.randStringWithQuantizedLength("input_data", eltWidth);
    reset();
    finalizeGraph(ZL_GRAPH_NUMERIC, eltWidth);
    testRoundTrip(input);
}

FUZZ_F(IntegerTest, FuzzIntegerDivideBy)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
    std::string input =
            dg.randStringWithQuantizedLength("input_data", eltWidth);
    reset();
    if (dg.boolean("set_divisor")) {
        const auto divisor = dg.u64("divisor");
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

FUZZ_F(IntegerTest, FuzzBitSplitRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
    size_t const eltWidthBits = eltWidth * 8;

    size_t const maxNbWidths = std::min(size_t(64), eltWidthBits);
    size_t const nbWidths    = dg.usize_range("nb_widths", 1, maxNbWidths);

    std::vector<uint8_t> bitWidths(nbWidths);
    size_t sumWidths = 0;
    for (size_t i = 0; i < nbWidths - 1; i++) {
        size_t const remaining = eltWidthBits - sumWidths - (nbWidths - i - 1);
        size_t const maxBitWidth = std::min(size_t(64), remaining);
        bitWidths[i] = (uint8_t)dg.usize_range("bit_width", 1, maxBitWidth);
        sumWidths += bitWidths[i];
    }
    bitWidths[nbWidths - 1] = (uint8_t)(eltWidthBits - sumWidths);

    std::string input =
            dg.randStringWithQuantizedLength("input_data", eltWidth);

    reset();
    ZL_NodeID node = ZL_Compressor_registerBitSplitNode(
            cgraph_, bitWidths.data(), nbWidths);
    if (node.nid == ZL_NODE_ILLEGAL.nid) {
        return;
    }
    ZL_GraphID graph = declareGraph(node);
    finalizeGraph(graph, eltWidth);
    testRoundTrip(input);
}
} // namespace
} // namespace tests
} // namespace openzl
