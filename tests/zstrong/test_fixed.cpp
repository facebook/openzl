// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <array>
#include <numeric>
#include <unordered_map>

#include "openzl/compress/private_nodes.h" // ZS2_NODE_*
#include "openzl/shared/mem.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"
#include "tests/zstrong/test_fixed_fixture.h"

namespace openzl {
namespace tests {
TEST_F(FixedTest, InterpretTokenAsLEInt1)
{
    testNode(ZL_NODE_INTERPRET_TOKEN_AS_LE, 1);
}

TEST_F(FixedTest, InterpretTokenAsLEInt2)
{
    testNode(ZL_NODE_INTERPRET_TOKEN_AS_LE, 2);
}

TEST_F(FixedTest, InterpretTokenAsLEInt4)
{
    testNode(ZL_NODE_INTERPRET_TOKEN_AS_LE, 4);
}

TEST_F(FixedTest, InterpretTokenAsLEInt8)
{
    testNode(ZL_NODE_INTERPRET_TOKEN_AS_LE, 8);
}

TEST_F(FixedTest, ConvertTokenToSerial1)
{
    testNode(ZL_NODE_CONVERT_TOKEN_TO_SERIAL, 1);
}

TEST_F(FixedTest, ConvertTokenToSerial2)
{
    testNode(ZL_NODE_CONVERT_TOKEN_TO_SERIAL, 2);
}

TEST_F(FixedTest, ConvertTokenToSerial3)
{
    testNode(ZL_NODE_CONVERT_TOKEN_TO_SERIAL, 3);
}

TEST_F(FixedTest, ConvertTokenToSerial7)
{
    testNode(ZL_NODE_CONVERT_TOKEN_TO_SERIAL, 7);
}

TEST_F(FixedTest, ConvertTokenToSerial500)
{
    testNode(ZL_NODE_CONVERT_TOKEN_TO_SERIAL, 500);
}

TEST_F(FixedTest, HuffmanGraph)
{
    reset();
    auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph_, ZL_NODE_INTERPRET_AS_LE8, ZL_GRAPH_HUFFMAN);
    testGraph(graph, 1);
}

TEST_F(FixedTest, HuffmanNode2)
{
    reset();
    finalizeGraph(
            declareGraph({ ZL_PrivateStandardNodeID_huffman_struct_v2 }), 2);
    setAlphabetMask("\xff\x03");
    testRoundTrip(generatedData(50000, 2));
    testRoundTrip(generatedData(50000, 10));
    testRoundTrip(generatedData(50000, 100));
    testRoundTrip(generatedData(50000, 1000));
}

TEST_F(FixedTest, HuffmanGraph2)
{
    reset();
    auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph_, ZL_NODE_INTERPRET_AS_LE16, ZL_GRAPH_HUFFMAN);
    testGraph(graph, 2);
    setAlphabetMask("\xff\x03");
    test();
}

TEST_F(FixedTest, Zstd)
{
    setFormatVersion(10); // Last version that supported ZSTD_FIXED
    for (size_t eltWidth = 2; eltWidth <= 8; ++eltWidth) {
        testNode(ZL_NODE_ZSTD_FIXED_DEPRECATED, eltWidth);
    }
}

TEST_F(FixedTest, ZstdTransposed)
{
    setFormatVersion(10); // Last version that supported ZSTD_FIXED
    for (size_t eltWidth = 2; eltWidth <= 8; ++eltWidth) {
        testPipeNodes(
                ZL_NODE_TRANSPOSE_DEPRECATED,
                ZL_NODE_ZSTD_FIXED_DEPRECATED,
                eltWidth);
    }
}

TEST_F(FixedTest, FieldLz2)
{
    testNode(ZL_NODE_FIELD_LZ, 2);
}

TEST_F(FixedTest, FieldLz4)
{
    testNode(ZL_NODE_FIELD_LZ, 4);
}

TEST_F(FixedTest, FieldLz8)
{
    testNode(ZL_NODE_FIELD_LZ, 8);
}

TEST_F(FixedTest, FieldLzGraph1)
{
    reset();
    setLevels(1, 1);
    testGraph(ZL_Compressor_registerFieldLZGraph(cgraph_), 1);
    testGraph(
            ZL_Compressor_registerFieldLZGraph_withLiteralsGraph(
                    cgraph_, ZL_GRAPH_ZSTD),
            1);
}

TEST_F(FixedTest, FieldLzGraph2)
{
    reset();
    setLevels(1, 1);
    testGraph(ZL_Compressor_registerFieldLZGraph(cgraph_), 2);
    testGraph(
            ZL_Compressor_registerFieldLZGraph_withLiteralsGraph(
                    cgraph_, ZL_GRAPH_ZSTD),
            2);

    // Our selectors have different code for versions <= 10
    setFormatVersion(10);
    reset();
    setLevels(1, 1);
    testGraph(ZL_Compressor_registerFieldLZGraph(cgraph_), 2);
    testGraph(
            ZL_Compressor_registerFieldLZGraph_withLiteralsGraph(
                    cgraph_, ZL_GRAPH_ZSTD),
            2);
}

TEST_F(FixedTest, FieldLzGraph4)
{
    reset();
    setLevels(1, 1);
    testGraph(ZL_Compressor_registerFieldLZGraph(cgraph_), 4);
    testGraph(
            ZL_Compressor_registerFieldLZGraph_withLiteralsGraph(
                    cgraph_, ZL_GRAPH_ZSTD),
            4);

    // Our selectors have different code for versions <= 10
    setFormatVersion(10);
    reset();
    setLevels(1, 1);
    testGraph(ZL_Compressor_registerFieldLZGraph(cgraph_), 4);
    testGraph(
            ZL_Compressor_registerFieldLZGraph_withLiteralsGraph(
                    cgraph_, ZL_GRAPH_ZSTD),
            4);
}

TEST_F(FixedTest, FieldLzGraph8)
{
    reset();
    setLevels(1, 1);
    testGraph(ZL_Compressor_registerFieldLZGraph(cgraph_), 8);
    testGraph(
            ZL_Compressor_registerFieldLZGraph_withLiteralsGraph(
                    cgraph_, ZL_GRAPH_ZSTD),
            8);

    // Our selectors have different code for versions <= 10
    setFormatVersion(10);
    reset();
    setLevels(1, 1);
    testGraph(ZL_Compressor_registerFieldLZGraph(cgraph_), 8);
    testGraph(
            ZL_Compressor_registerFieldLZGraph_withLiteralsGraph(
                    cgraph_, ZL_GRAPH_ZSTD),
            8);
}

TEST_F(FixedTest, FieldLzGraphWithCompressionLevelOverride)
{
    for (int level = 1; level <= 5; ++level) {
        reset();
        setLevels(1, 1);
        testGraph(
                ZL_Compressor_registerFieldLZGraph_withLevel(cgraph_, level),
                1);
    }
}

TEST_F(FixedTest, ZstdGraphWithCompressionLevelOverride)
{
    for (int level = 1; level <= 19; ++level) {
        reset();
        setLevels(1, 1);
        testGraph(ZL_Compressor_registerZstdGraph_withLevel(cgraph_, level), 1);
    }
}

TEST_F(FixedTest, FieldLzGraphWithMultipleCompressionLevelOverrides)
{
    reset();
    setLevels(1, 1);
    for (int l1 = 1; l1 <= 6; ++l1) {
        for (int l2 = 1; l2 <= 6; ++l2) {
            auto graph1 =
                    ZL_Compressor_registerFieldLZGraph_withLevel(cgraph_, l1);
            auto graph2 =
                    ZL_Compressor_registerFieldLZGraph_withLevel(cgraph_, l2);
            const size_t segmentSizes[3] = { 1000, 1000, 0 };
            const ZL_GraphID graphs[3] = { ZL_GRAPH_FIELD_LZ, graph1, graph2 };
            auto graph                 = ZL_Compressor_registerSplitGraph(
                    cgraph_, ZL_Type_struct, segmentSizes, graphs, 3);
            finalizeGraph(graph, 2);
            testRoundTrip(generatedData(3000, 100));
        }
    }
}

TEST_F(FixedTest, TransposeSplit2)
{
    setFormatVersion(10); // Last version that supported TRANSPOSE_SPLITN
    testNode(ZL_NODE_TRANSPOSE_SPLIT2_DEPRECATED, 2);
}

TEST_F(FixedTest, TransposeSplit4)
{
    setFormatVersion(10); // Last version that supported TRANSPOSE_SPLITN
    testNode(ZL_NODE_TRANSPOSE_SPLIT4_DEPRECATED, 4);
}

TEST_F(FixedTest, TransposeSplit8)
{
    setFormatVersion(10); // Last version that supported TRANSPOSE_SPLITN
    testNode(ZL_NODE_TRANSPOSE_SPLIT8_DEPRECATED, 8);
}

TEST_F(FixedTest, TransposeSplit)
{
    for (size_t i = 1; i < 10; i++) {
        testNode(ZL_NODE_TRANSPOSE_SPLIT, i);
    }
}

TEST_F(FixedTest, TransposeSplitGraph)
{
    reset();
    testGraph(
            ZL_Compressor_registerTransposeSplitGraph(cgraph_, ZL_GRAPH_STORE),
            4);

    // Selector has different code for versions <= 10
    setFormatVersion(10);
    reset();
    testGraph(
            ZL_Compressor_registerTransposeSplitGraph(cgraph_, ZL_GRAPH_STORE),
            4);
}

TEST_F(FixedTest, Tokenize1)
{
    reset();
    testGraph(
            ZL_Compressor_registerTokenizeGraph(
                    cgraph_,
                    ZL_Type_struct,
                    false,
                    ZL_GRAPH_STORE,
                    ZL_GRAPH_STORE),
            1);
}

TEST_F(FixedTest, Tokenize2)
{
    reset();
    testGraph(
            ZL_Compressor_registerTokenizeGraph(
                    cgraph_,
                    ZL_Type_struct,
                    false,
                    ZL_GRAPH_STORE,
                    ZL_GRAPH_STORE),
            2);
}

TEST_F(FixedTest, Tokenize4)
{
    reset();
    testGraph(
            ZL_Compressor_registerTokenizeGraph(
                    cgraph_,
                    ZL_Type_struct,
                    false,
                    ZL_GRAPH_STORE,
                    ZL_GRAPH_STORE),
            4);
}

TEST_F(FixedTest, Tokenize8)
{
    reset();
    testGraph(
            ZL_Compressor_registerTokenizeGraph(
                    cgraph_,
                    ZL_Type_struct,
                    false,
                    ZL_GRAPH_STORE,
                    ZL_GRAPH_STORE),
            8);
}

TEST_F(FixedTest, TokenizeSorted1)
{
    reset();
    testGraph(
            ZL_Compressor_registerTokenizeGraph(
                    cgraph_,
                    ZL_Type_numeric,
                    true,
                    ZL_GRAPH_STORE,
                    ZL_GRAPH_STORE),
            1);
}

TEST_F(FixedTest, TokenizeSorted2)
{
    reset();
    testGraph(
            ZL_Compressor_registerTokenizeGraph(
                    cgraph_,
                    ZL_Type_numeric,
                    true,
                    ZL_GRAPH_STORE,
                    ZL_GRAPH_STORE),
            2);
}

TEST_F(FixedTest, TokenizeSorted4)
{
    reset();
    testGraph(
            ZL_Compressor_registerTokenizeGraph(
                    cgraph_,
                    ZL_Type_numeric,
                    true,
                    ZL_GRAPH_STORE,
                    ZL_GRAPH_STORE),
            4);
}

TEST_F(FixedTest, TokenizeSorted8)
{
    reset();
    testGraph(
            ZL_Compressor_registerTokenizeGraph(
                    cgraph_,
                    ZL_Type_numeric,
                    true,
                    ZL_GRAPH_STORE,
                    ZL_GRAPH_STORE),
            8);
}

TEST_F(FixedTest, CustomTokenize4)
{
    int x         = 42;
    auto tokenize = [](ZL_CustomTokenizeState* ctx,
                       const ZL_Input* input) -> ZL_Report {
        EXPECT_EQ(
                *(const int*)ZL_CustomTokenizeState_getOpaquePtr(ctx), int(42));
        EXPECT_EQ(ZL_Input_eltWidth(input), size_t(4));

        std::unordered_map<uint32_t, uint32_t> valueToIndex;
        uint32_t const* src  = (uint32_t const*)ZL_Input_ptr(input);
        size_t const srcSize = ZL_Input_numElts(input);

        uint32_t* indices =
                (uint32_t*)ZL_CustomTokenizeState_createIndexOutput(ctx, 4);
        if (indices == NULL) {
            return ZL_REPORT_ERROR(allocation);
        }

        for (size_t i = 0; i < srcSize; ++i) {
            auto [it, _] = valueToIndex.emplace(
                    ZL_read32(&src[i]), valueToIndex.size());
            indices[i] = it->second;
        }

        uint32_t* alphabet =
                (uint32_t*)ZL_CustomTokenizeState_createAlphabetOutput(
                        ctx, valueToIndex.size());
        if (alphabet == NULL) {
            return ZL_REPORT_ERROR(allocation);
        }

        for (auto [value, index] : valueToIndex) {
            alphabet[index] = value;
        }

        return ZL_returnSuccess();
    };

    reset();
    testGraph(
            ZL_Compressor_registerCustomTokenizeGraph(
                    cgraph_,
                    ZL_Type_struct,
                    tokenize,
                    &x,
                    ZL_GRAPH_STORE,
                    ZL_GRAPH_STORE),
            4);
}

TEST_F(FixedTest, ConstantSelector)
{
    const std::vector<size_t> sizes = { 1, 10, 100, 1000, 10000, 50000 };
    size_t const maxEltWidth        = 64;
    for (size_t eltWidth = 1; eltWidth <= maxEltWidth; ++eltWidth) {
        std::string repeatedElt(eltWidth, 'a');
        std::iota(repeatedElt.begin(), repeatedElt.end(), 0);
        for (size_t size : sizes) {
            std::string inputStr;
            for (size_t i = 0; i < size; ++i) {
                inputStr += repeatedElt;
            }
            reset();
            setStreamInType(ZL_Type_struct);
            testGraphOnInput(ZL_GRAPH_CONSTANT, eltWidth, inputStr);
        }
    }
}

TEST_F(FixedTest, Constant)
{
    const std::vector<size_t> sizes = { 1, 10, 100, 1000, 10000, 50000 };
    size_t const maxEltWidth        = 64;
    for (size_t eltWidth = 1; eltWidth <= maxEltWidth; ++eltWidth) {
        std::string repeatedElt(eltWidth, 'a');
        std::iota(repeatedElt.begin(), repeatedElt.end(), 0);
        for (size_t size : sizes) {
            std::string inputStr;
            for (size_t i = 0; i < size; ++i) {
                inputStr += repeatedElt;
            }
            testNodeOnInput(ZL_NODE_CONSTANT_FIXED, eltWidth, inputStr);
        }
    }
}

TEST_F(FixedTest, ConstantZeroRanges)
{
    const std::vector<size_t> sizes     = { 1, 10, 100, 1000, 10000, 50000 };
    const std::vector<size_t> eltWidths = { 1, 2, 4, 8, 16, 32, 64 };
    for (size_t eltWidth : eltWidths) {
        for (size_t size : sizes) {
            std::string inputStr(size * eltWidth, '\0');
            reset();
            setStreamInType(ZL_Type_struct);
            testGraphOnInput(ZL_GRAPH_CONSTANT, eltWidth, inputStr);
        }
    }
}

TEST_F(FixedTest, ConstantSingleBytePatterns)
{
    const std::vector<size_t> sizes     = { 1, 10, 100, 1000, 10000 };
    const std::vector<size_t> eltWidths = { 2, 4, 8, 16 };
    const std::vector<char> patterns    = { '\x00', '\xFF', '\x55', '\xAA' };
    for (char pattern : patterns) {
        for (size_t eltWidth : eltWidths) {
            for (size_t size : sizes) {
                std::string inputStr(size * eltWidth, pattern);
                reset();
                setStreamInType(ZL_Type_struct);
                testGraphOnInput(ZL_GRAPH_CONSTANT, eltWidth, inputStr);
            }
        }
    }
}

TEST_F(FixedTest, SplitN)
{
    reset();
    {
        std::string data;
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_, ZL_Type_struct, nullptr, 0);
        finalizeGraph(declareGraph(node), 3);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data;
        std::array<size_t, 1> segmentSizes = { 0 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_struct,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 500);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data                   = "000000000";
        std::array<size_t, 1> segmentSizes = { 0 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_struct,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 3);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data                   = "000001111122222333334444455555";
        std::array<size_t, 5> segmentSizes = { 0, 2, 1, 1, 2 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_struct,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 5);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data = "000000111111222222333333444444555555";
        std::array<size_t, 6> segmentSizes = { 0, 4, 4, 2, 1, 1 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_struct,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 3);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data                   = "00112233445566778899";
        std::array<size_t, 3> segmentSizes = { 4, 1, 0 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_struct,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 2);
        testRoundTrip(data);
    }
}

} // namespace tests
} // namespace openzl
