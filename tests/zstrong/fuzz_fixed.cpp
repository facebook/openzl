// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <limits.h>

#include "openzl/compress/private_nodes.h"
#include "tests/datagen/DataGen.h"
#include "tests/fuzz_utils.h"
#include "tests/zstrong/test_fixed_fixture.h"

namespace openzl {
namespace tests {

FUZZ_F(FixedTest, FuzzInterpretTokenAsLEIntRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
    std::string input = dg.randStringWithQuantizedLength("input_str", eltWidth);
    testNodeOnInput(ZL_NODE_INTERPRET_TOKEN_AS_LE, eltWidth, input);
}

FUZZ_F(FixedTest, FuzzConvertTokenToSerialRoundTrip)
{
    datagen::DataGen dg      = fromFDP(f);
    std::string input        = dg.randString("input_str");
    size_t const maxEltWidth = input.size() == 0 ? INT_MAX : input.size();
    size_t const eltWidth    = dg.usize_range("elt_width", 1, maxEltWidth);
    testNodeOnInput(ZL_NODE_CONVERT_TOKEN_TO_SERIAL, eltWidth, input);
}

FUZZ_F(FixedTest, FuzzHuffRoundtrip)
{
    datagen::DataGen dg   = fromFDP(f);
    auto const useNode    = dg.coin("use_node");
    size_t const eltWidth = dg.usize_range("elt_width", 1, 2);
    std::string input = dg.randStringWithQuantizedLength("input_str", eltWidth);
    reset();
    if (useNode) {
        setLargeCompressBound(8);
        finalizeGraph(
                declareGraph(
                        ZL_NodeID{
                                ZL_PrivateStandardNodeID_huffman_struct_v2 }),
                eltWidth);
        testRoundTripCompressionMayFail(input);
    } else {
        testGraphOnInput(ZL_GRAPH_HUFFMAN, eltWidth, std::move(input));
    }
}

FUZZ_F(FixedTest, FuzzFieldLzRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 2, 4, 8 });
    std::string input = dg.randStringWithQuantizedLength("input_str", eltWidth);
    testNodeOnInput(ZL_NODE_FIELD_LZ, eltWidth, std::move(input));
}

FUZZ_F(FixedTest, FuzzFieldLzFNodeRoundTrip)
{
    datagen::DataGen dg            = fromFDP(f);
    bool const customLiteralsGraph = dg.boolean("custom_literals_graph");
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 2, 4, 8 });
    std::string input = dg.randStringWithQuantizedLength("input_str", eltWidth);
    int const clevel  = dg.i32_range("compression_level", 0, 10);
    int const dlevel  = dg.i32_range("decompression_level", 0, 10);
    reset();
    setLevels(clevel, dlevel);
    ZL_GraphID const graph = customLiteralsGraph
            ? ZL_Compressor_registerFieldLZGraph_withLiteralsGraph(
                      cgraph_, ZL_GRAPH_ZSTD)
            : ZL_Compressor_registerFieldLZGraph(cgraph_);
    testGraphOnInput(graph, eltWidth, input);
}

FUZZ_F(FixedTest, FuzzFieldLzFNodeRoundTripWithOverrideLevels)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 2, 4, 8 });
    std::string input = dg.randStringWithQuantizedLength("input_str", eltWidth);
    int const clevel  = dg.i32_range("compression_level", 0, 10);
    int const dlevel  = dg.i32_range("decompression_level", 0, 10);

    reset();
    setLevels(clevel, dlevel);

    auto makeFieldLZ = [&] {
        if (!dg.coin("should_override", 0.9)) {
            return ZL_Compressor_registerFieldLZGraph(cgraph_);
        }
        int const overrideCLevel =
                dg.i32_range("override_compression_level", 0, 10);
        return ZL_Compressor_registerFieldLZGraph_withLevel(
                cgraph_, overrideCLevel);
    };

    auto fieldLZ1 = makeFieldLZ();
    auto fieldLZ2 = makeFieldLZ();

    std::array<size_t, 2> segmentSizes   = { input.size() / eltWidth / 2, 0 };
    std::array<ZL_GraphID, 2> successors = { fieldLZ1, fieldLZ2 };

    auto graph = ZL_Compressor_registerSplitGraph(
            cgraph_,
            ZL_Type_struct,
            segmentSizes.data(),
            successors.data(),
            successors.size());

    testGraphOnInput(graph, eltWidth, input);
}

FUZZ_F(FixedTest, FuzzTransposeRoundTrip)
{
    datagen::DataGen dg   = fromFDP(f);
    size_t const eltWidth = dg.usize_range("elt_width", 1, 8);
    std::string input = dg.randStringWithQuantizedLength("input_str", eltWidth);
    setFormatVersion(10);
    testNodeOnInput(ZL_NODE_TRANSPOSE_DEPRECATED, eltWidth, std::move(input));
}

FUZZ_F(FixedTest, FuzzTransposeSplitRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 2, 4, 8 });
    std::string input = dg.randStringWithQuantizedLength("input_str", eltWidth);
    ZL_NodeID node;
    setFormatVersion(10);
    if (eltWidth == 2)
        node = ZL_NODE_TRANSPOSE_SPLIT2_DEPRECATED;
    if (eltWidth == 4)
        node = ZL_NODE_TRANSPOSE_SPLIT4_DEPRECATED;
    if (eltWidth == 8)
        node = ZL_NODE_TRANSPOSE_SPLIT8_DEPRECATED;

    testNodeOnInput(node, eltWidth, std::move(input));
}

FUZZ_F(FixedTest, FuzzTransposeVORoundTrip)
{
    datagen::DataGen dg   = fromFDP(f);
    size_t const eltWidth = dg.usize_range("eltWidth", 1, 100);
    const std::string input =
            dg.randStringWithQuantizedLength("input", eltWidth);

    testNodeOnInput(ZL_NODE_TRANSPOSE_SPLIT, eltWidth, input);
}

FUZZ_F(FixedTest, FuzzZstdFixedRoundTrip)
{
    datagen::DataGen dg      = fromFDP(f);
    std::string input        = dg.randStringWithQuantizedLength("input_str", 1);
    size_t const maxEltWidth = input.size() == 0 ? INT_MAX : input.size();
    size_t const eltWidth    = dg.usize_range("elt_width", 1, maxEltWidth);
    setFormatVersion(10); // Last version that supported ZSTD_FIXED
    testNodeOnInput(ZL_NODE_ZSTD_FIXED_DEPRECATED, eltWidth, std::move(input));
}

FUZZ_F(FixedTest, FuzzZstdRoundTripWithOverrideLevels)
{
    datagen::DataGen dg      = fromFDP(f);
    std::string input        = dg.randStringWithQuantizedLength("input_str", 1);
    size_t const maxEltWidth = input.size() == 0 ? INT_MAX : input.size();
    size_t const eltWidth    = dg.usize_range("elt_width", 1, maxEltWidth);
    int const clevel         = dg.i32_range("compression_level", 0, 10);
    int const dlevel         = dg.i32_range("decompression_level", 0, 10);
    int const overrideCLevel =
            dg.i32_range("override_compression_level", 0, 10);

    reset();
    setLevels(clevel, dlevel);
    ZL_GraphID const graph =
            ZL_Compressor_registerZstdGraph_withLevel(cgraph_, overrideCLevel);

    testGraphOnInput(graph, eltWidth, input);
}

FUZZ_F(FixedTest, FuzzTokenizeRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    auto const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
    std::string input = dg.randStringWithQuantizedLength("input_str", eltWidth);
    bool const sorted = dg.boolean("sorted");
    auto const node   = sorted ? ZL_NODE_TOKENIZE_SORTED : ZL_NODE_TOKENIZE;
    testNodeOnInput(node, eltWidth, std::move(input));
}

FUZZ_F(FixedTest, FuzzConstantRoundTrip)
{
    datagen::DataGen dg   = fromFDP(f);
    size_t const eltWidth = dg.usize_range("eltWidth", 1, 100);
    const auto vec =
            dg.randLongVector<uint8_t>("vec", 0, 255, eltWidth, eltWidth);
    const std::string str = std::string(vec.begin(), vec.end());
    const size_t nbElts   = dg.randVal<size_t>("nbElts", 1, 512);

    std::string inputStr;
    for (size_t i = 0; i < nbElts; i++) {
        inputStr += str;
    }

    testNodeOnInput(ZL_NODE_CONSTANT_FIXED, eltWidth, inputStr);
}

FUZZ_F(FixedTest, FuzzSplitNRoundTrip)
{
    datagen::DataGen dg   = fromFDP(f);
    size_t const eltWidth = dg.usize_range("eltWidth", 1, 100);
    std::string input = dg.randStringWithQuantizedLength("input_str", eltWidth);

    StructuredFDP<HarnessMode>* fPtr = &f;

    reset();
    if (dg.u8("split_by_param") >= 128) {
        auto segmentSizes = getSplitNSegments(f, input.size() / eltWidth);
        std::vector<ZL_GraphID> successors(segmentSizes.size(), ZL_GRAPH_STORE);
        auto graph = ZL_Compressor_registerSplitGraph(
                cgraph_,
                ZL_Type_struct,
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
                cgraph_, ZL_Type_struct, parser, &fPtr);
        finalizeGraph(declareGraph(node), eltWidth);
    }
    setLargeCompressBound(8);
    testRoundTrip(input);
}

} // namespace tests
} // namespace openzl
