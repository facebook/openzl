// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "security/lionhead/utils/lib_ftest/ftest.h"

#include "openzl/compress/private_nodes.h"
#include "tests/fuzz_utils.h"
#include "tests/zstrong/test_serialized_fixture.h"

namespace zstrong {
namespace tests {
namespace {
ZL_NodeID selectNode(
        size_t eltWidth,
        ZL_NodeID v1,
        ZL_NodeID v2,
        ZL_NodeID v4,
        ZL_NodeID v8)
{
    switch (eltWidth) {
        case 1:
            return v1;
        case 2:
            return v2;
        case 4:
            return v4;
        case 8:
            return v8;
        default:
            ZL_REQUIRE_FAIL("bad eltWidth");
    }
}

FUZZ_F(SerializedTest, FuzzInterpretSerializedAsLEIntRoundTrip)
{
    size_t const eltWidth = f.choices("elt_width", { 1, 2, 4, 8 });
    std::string input =
            gen_str(f, "input_data", ShortInputLengthInBytes(eltWidth));
    ZL_NodeID const node = selectNode(
            eltWidth,
            ZL_NODE_INTERPRET_AS_LE8,
            ZL_NODE_INTERPRET_AS_LE16,
            ZL_NODE_INTERPRET_AS_LE32,
            ZL_NODE_INTERPRET_AS_LE64);
    testNodeOnInput(node, input, eltWidth);
}

FUZZ_F(SerializedTest, FuzzConvertSerialToTokenRoundTrip)
{
    size_t const eltWidth = f.choices("elt_width", { 4, 8 });
    std::string input =
            gen_str(f, "input_data", ShortInputLengthInBytes(eltWidth));
    ZL_NodeID const node = selectNode(
            eltWidth,
            {},
            {},
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN4,
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN8);
    testNodeOnInput(node, input, eltWidth);
}

FUZZ_F(SerializedTest, FuzzHuffmanRoundTrip)
{
    bool const useNode = f.coin("use_node");
    std::string input  = gen_str(f, "input_data", InputLengthInBytes(1));
    reset();
    if (useNode) {
        setLargeCompressBound(8);
        finalizeGraph(
                declareGraph(
                        ZL_NodeID{ ZL_PrivateStandardNodeID_huffman_v2 },
                        { ZL_GRAPH_FSE, ZL_GRAPH_STORE }),
                1);
        testRoundTripCompressionMayFail(input);
    } else {
        finalizeGraph(ZL_GRAPH_HUFFMAN, 1);
        testRoundTrip(input);
    }
}

FUZZ_F(SerializedTest, FuzzFSERoundTrip)
{
    bool const useNode = f.coin("use_node");
    std::string input  = gen_str(f, "input_data", InputLengthInBytes(1));
    reset();
    // Only the graph guarantees that copmression succeeds on every input
    if (useNode) {
        setLargeCompressBound(8);
        finalizeGraph(
                declareGraph(ZL_NodeID{ ZL_PrivateStandardNodeID_fse_v2 }), 1);
        testRoundTripCompressionMayFail(input);
    } else {
        finalizeGraph(ZL_GRAPH_FSE, 1);
        testRoundTrip(input);
    }
}

FUZZ_F(SerializedTest, FuzzZstdRoundTrip)
{
    std::string input = gen_str(f, "input_data", InputLengthInBytes(1));
    testNodeOnInput(ZL_NODE_ZSTD, input);
}

FUZZ_F(SerializedTest, FuzzBitpackRoundTrip)
{
    std::string input = gen_str(f, "input_data", InputLengthInBytes(1));
    testNodeOnInput(ZL_NODE_BITPACK_SERIAL, input);
}

FUZZ_F(SerializedTest, FuzzFlatpackRoundTrip)
{
    std::string input = gen_str(f, "input_data", InputLengthInBytes(1));
    testNodeOnInput(ZL_NODE_FLATPACK, input);
}

FUZZ_F(SerializedTest, FuzzBitunpackRoundTrip)
{
    size_t integerBitWidth = f.i32_range("integer_bit_width", 1, 64);
    std::string input =
            gen_str(f, "input_str", InputLengthInBytes(integerBitWidth));
    ASSERT_LT((input.size() * 8) % integerBitWidth, 8);
    ZL_IntParam intParam  = { ZL_Bitunpack_numBits, (int)integerBitWidth };
    ZL_LocalParams params = { { &intParam, 1 }, { NULL, 0 }, { NULL, 0 } };
    setLargeCompressBound(8);
    testParameterizedNodeOnInput(ZL_NODE_BITUNPACK, params, input);
}

FUZZ_F(SerializedTest, FuzzSplitByStructRoundTrip)
{
    std::string input = gen_str(f, "input_str", InputLengthInBytes(1));
    size_t numFields  = f.usize_range("num_fields", 1, 16);
    std::vector<size_t> fieldSizes;
    size_t structSize = 0;
    fieldSizes.reserve(numFields);
    for (size_t i = 0; i < numFields && structSize < input.size(); ++i) {
        size_t const fieldSize =
                f.usize_range("field_size", 1, input.size() - structSize);
        fieldSizes.push_back(fieldSize);
        structSize += fieldSize;
    }
    if (structSize == 0) {
        fieldSizes.push_back(1);
        structSize += 1;
    }

    reset();
    std::vector<ZL_GraphID> successors(fieldSizes.size(), ZL_GRAPH_STORE);
    auto graph = ZL_Compressor_registerSplitByStructGraph(
            cgraph_, fieldSizes.data(), successors.data(), successors.size());
    finalizeGraph(graph, structSize);
    setLargeCompressBound(8);
    testRoundTrip(input);
}

FUZZ_F(SerializedTest, FuzzSplitNRoundTrip)
{
    std::string input = gen_str(f, "input_str", InputLengthInBytes(1));

    StructuredFDP<HarnessMode>* fPtr = &f;

    reset();
    if (f.u8("split_by_param") >= 128) {
        auto segmentSizes = getSplitNSegments(f, input.size());
        std::vector<ZL_GraphID> successors(segmentSizes.size(), ZL_GRAPH_STORE);
        auto graph = ZL_Compressor_registerSplitGraph(
                cgraph_,
                ZL_Type_serial,
                segmentSizes.data(),
                successors.data(),
                successors.size());
        finalizeGraph(graph, 1);
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
                cgraph_, ZL_Type_serial, parser, &fPtr);
        finalizeGraph(declareGraph(node), 1);
    }
    setLargeCompressBound(8);
    testRoundTrip(input);
}

FUZZ_F(SerializedTest, FuzzDispatchNByTagRoundTrip)
{
    std::string input = gen_str(f, "input_str", InputLengthInBytes(1));

    reset();
    StructuredFDP<HarnessMode>* fPtr = &f;
    auto parser = [](ZL_DispatchState* state, ZL_Input const* in) {
        auto& fdp = **static_cast<StructuredFDP<HarnessMode>* const*>(
                ZL_DispatchState_getOpaquePtr(state));
        size_t const nbElts = ZL_Input_numElts(in);
        auto const numTags  = fdp.u32_range(
                "num_tags",
                std::min<size_t>(nbElts, 1),
                std::min<size_t>(255, std::max<size_t>(nbElts, 10)));

        ZL_DispatchInstructions instructions{};

        if (numTags == 0) {
            ZL_REQUIRE_EQ(nbElts, 0);
            return instructions;
        }

        auto const segmentSizesVec = getSplitNSegments(
                fdp, ZL_Input_numElts(in), /* lastZero */ false);
        std::vector<unsigned> tagsVec;
        tagsVec.reserve(segmentSizesVec.size());
        for (size_t i = 0; i < segmentSizesVec.size(); ++i) {
            tagsVec.push_back(fdp.u32_range("tag", 0, numTags - 1));
        }

        auto segmentSizes = (size_t*)ZL_DispatchState_malloc(
                state, segmentSizesVec.size() * sizeof(segmentSizesVec[0]));
        auto tags = (unsigned*)ZL_DispatchState_malloc(
                state, tagsVec.size() * sizeof(tagsVec[0]));
        if (segmentSizes == nullptr || tags == nullptr) {
            return instructions;
        }
        memcpy(segmentSizes,
               segmentSizesVec.data(),
               segmentSizesVec.size() * sizeof(segmentSizes[0]));
        memcpy(tags, tagsVec.data(), tagsVec.size() * sizeof(tags[0]));
        instructions.segmentSizes = segmentSizes;
        instructions.tags         = tags;
        instructions.nbSegments   = segmentSizesVec.size();
        instructions.nbTags       = numTags;
        return instructions;
    };
    ZL_NodeID node = ZL_Compressor_registerDispatchNode(cgraph_, parser, &fPtr);
    finalizeGraph(declareGraph(node), 1);
    setLargeCompressBound(8);
    testRoundTrip(input);
}

FUZZ_F(SerializedTest, FuzzSetStringSizesRoundTrip)
{
    std::string input = gen_str(f, "input_str", InputLengthInBytes(1));

    StructuredFDP<HarnessMode>* fPtr = &f;

    reset();
    auto parser = [](ZL_SetStringLensState* state, ZL_Input const* in) {
        auto& fdp = **static_cast<StructuredFDP<HarnessMode>* const*>(
                ZL_SetStringLensState_getOpaquePtr(state));
        auto const segments = getSplitNSegments(
                fdp, ZL_Input_numElts(in), /* lastZero */ false);
        auto segmentSizes = (uint32_t*)ZL_SetStringLensState_malloc(
                state, segments.size() * sizeof(uint32_t));
        ZL_SetStringLensInstructions instructions = { segmentSizes, 0 };
        if (segmentSizes == NULL) {
            return instructions;
        }
        for (size_t i = 0; i < segments.size(); ++i) {
            segmentSizes[i] = uint32_t(segments[i]);
        }
        instructions.nbStrings = segments.size();
        return instructions;
    };
    ZL_NodeID node = ZL_Compressor_registerConvertSerialToStringNode(
            cgraph_, parser, &fPtr);
    finalizeGraph(declareGraph(node), 1);
    setLargeCompressBound(8);
    testRoundTrip(input);
}

FUZZ_F(SerializedTest, FuzzConstantRoundTrip)
{
    uint8_t const rptChr = f.u8_range("rptChr", 0, 255);
    size_t const nbRpts  = InputLengthInBytes(1).gen("nbRpts", f);
    const std::string input(nbRpts == 0 ? 1 : nbRpts, rptChr);

    testNodeOnInput(ZL_NODE_CONSTANT_SERIAL, input, 1);
}

} // namespace
} // namespace tests
} // namespace zstrong
