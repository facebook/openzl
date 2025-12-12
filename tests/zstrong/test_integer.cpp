// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <array>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/compress/private_nodes.h"
#include "openzl/zl_opaque_types.h"
#include "tests/zstrong/test_integer_fixture.h"

namespace openzl {
namespace tests {
TEST_F(IntegerTest, ConvertIntToToken1)
{
    testNode(ZL_NODE_CONVERT_NUM_TO_TOKEN, 1);
}

TEST_F(IntegerTest, ConvertIntToToken2)
{
    testNode(ZL_NODE_CONVERT_NUM_TO_TOKEN, 2);
}

TEST_F(IntegerTest, ConvertIntToToken4)
{
    testNode(ZL_NODE_CONVERT_NUM_TO_TOKEN, 4);
}

TEST_F(IntegerTest, ConvertIntToToken8)
{
    testNode(ZL_NODE_CONVERT_NUM_TO_TOKEN, 8);
}

TEST_F(IntegerTest, ConvertIntToSerial1)
{
    testNode(ZL_NODE_CONVERT_NUM_TO_SERIAL, 1);
}

TEST_F(IntegerTest, ConvertIntToSerial2)
{
    testNode(ZL_NODE_CONVERT_NUM_TO_SERIAL, 2);
}

TEST_F(IntegerTest, ConvertIntToSerial4)
{
    testNode(ZL_NODE_CONVERT_NUM_TO_SERIAL, 4);
}

TEST_F(IntegerTest, ConvertIntToSerial8)
{
    testNode(ZL_NODE_CONVERT_NUM_TO_SERIAL, 8);
}

TEST_F(IntegerTest, QuantizeOffsets)
{
    setValueBounds(1);
    testNode(ZL_NODE_QUANTIZE_OFFSETS, 4);
}

TEST_F(IntegerTest, QuantizeLengths)
{
    testNode(ZL_NODE_QUANTIZE_LENGTHS, 4);
}

TEST_F(IntegerTest, Delta8)
{
    testNode(ZL_NODE_DELTA_INT, 1);
}

TEST_F(IntegerTest, Delta16)
{
    testNode(ZL_NODE_DELTA_INT, 2);
}

TEST_F(IntegerTest, Delta32)
{
    testNode(ZL_NODE_DELTA_INT, 4);
}

TEST_F(IntegerTest, Delta64)
{
    testNode(ZL_NODE_DELTA_INT, 8);
}

TEST_F(IntegerTest, Zigzag8)
{
    testNode(ZL_NODE_ZIGZAG, 1);
}

TEST_F(IntegerTest, Zigzag16)
{
    testNode(ZL_NODE_ZIGZAG, 2);
}

TEST_F(IntegerTest, Zigzag32)
{
    testNode(ZL_NODE_ZIGZAG, 4);
}

TEST_F(IntegerTest, Zigzag64)
{
    testNode(ZL_NODE_ZIGZAG, 8);
}

TEST_F(IntegerTest, Bitpack8)
{
    testNode(ZL_NODE_BITPACK_INT, 1);
}

TEST_F(IntegerTest, Bitpack16)
{
    testNode(ZL_NODE_BITPACK_INT, 2);
}

TEST_F(IntegerTest, Bitpack32)
{
    testNode(ZL_NODE_BITPACK_INT, 4);
}

TEST_F(IntegerTest, Float32Deconstruct)
{
    testNode(ZL_NODE_FLOAT32_DECONSTRUCT, 4);
}

TEST_F(IntegerTest, Bfloat16Deconstruct)
{
    testNode(ZL_NODE_BFLOAT16_DECONSTRUCT, 2);
}

TEST_F(IntegerTest, Float16Deconstruct)
{
    testNode(ZL_NODE_FLOAT16_DECONSTRUCT, 2);
}

TEST_F(IntegerTest, IntegerSelector)
{
    testGraph(ZL_GRAPH_NUMERIC, 1);
    testGraph(ZL_GRAPH_NUMERIC, 2);
    testGraph(ZL_GRAPH_NUMERIC, 4);
    testGraph(ZL_GRAPH_NUMERIC, 8);
}

TEST_F(IntegerTest, Range)
{
    testNode(ZL_NODE_RANGE_PACK, 1);
    testNode(ZL_NODE_RANGE_PACK, 2);
    testNode(ZL_NODE_RANGE_PACK, 4);
    testNode(ZL_NODE_RANGE_PACK, 8);
}

TEST_F(IntegerTest, MergeSorted)
{
    auto testMergeSorted = [this](std::vector<uint32_t> const& data) {
        testNodeOnInput(
                ZL_NODE_MERGE_SORTED,
                4,
                std::string_view((char const*)data.data(), data.size() * 4));
    };
    // testMergeSorted({});
    testMergeSorted(std::vector<uint32_t>({ 0, 1, 2, 0, 2, 1, 1, 2 }));
    testMergeSorted(std::vector<uint32_t>({ 0, 1, 2, 10, (uint32_t)-1 }));
    testMergeSorted(std::vector<uint32_t>({ 0, 0, 0, 0, 0, 0 }));
    testMergeSorted(
            std::vector<uint32_t>({ 0,      1,  2,  3,  4,  10, 10, 9,
                                    8,      7,  8,  9,  12, 0,  5,  9,
                                    100000, 15, 18, 25, 0,  13, 5,  18 }));
    for (size_t i = 1; i < 65; ++i) {
        testMergeSorted(std::vector<uint32_t>(i));
        std::vector<uint32_t> data;
        for (size_t r = 1; r <= i; ++r) {
            for (size_t j = 0; j < 10 * r; ++j) {
                data.push_back(uint32_t(j * r));
            }
        }
        testMergeSorted(data);
    }
    // testMergeSorted(std::vector<uint32_t>(65));
}

TEST_F(IntegerTest, SplitN)
{
    reset();
    {
        std::string data;
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_, ZL_Type_numeric, nullptr, 0);
        finalizeGraph(declareGraph(node), 4);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data;
        std::array<size_t, 1> segmentSizes = { 0 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_numeric,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 4);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data                   = "0000";
        std::array<size_t, 1> segmentSizes = { 0 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_numeric,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 2);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data                   = "000011112222333344445555";
        std::array<size_t, 5> segmentSizes = { 0, 2, 1, 1, 2 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_numeric,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 4);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data                   = "000011112222333344445555";
        std::array<size_t, 6> segmentSizes = { 0, 4, 4, 2, 1, 1 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_numeric,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 2);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data                   = "00112233445566778899";
        std::array<size_t, 3> segmentSizes = { 4, 1, 0 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_numeric,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 2);
        testRoundTrip(data);
    }
}

TEST_F(IntegerTest, FSENCount)
{
    auto testInput = [this](std::vector<int16_t> const& input) {
        testNodeOnInput(
                { ZL_PrivateStandardNodeID_fse_ncount },
                2,
                { (char const*)input.data(), input.size() * 2 });
    };
    testInput({ 32 });
    testInput(std::vector<int16_t>(32, 1));
    testInput(std::vector<int16_t>(32, -1));
    testInput({ 1, 2, 3, 4, 5, 6, 7, -1, 1, 2 });
    testInput({ 1000, -1, 20, 3 });

    std::mt19937 gen(0xdeadbeef);
    for (size_t i = 0; i < 100; ++i) {
        std::uniform_int_distribution<unsigned> tableLog{ 5, 12 };
        int16_t remaining = int16_t(1 << tableLog(gen));
        std::vector<int16_t> data;
        for (size_t j = 0; j < 255 && remaining > 0; ++j) {
            std::uniform_int_distribution<int16_t> dist{ -1, remaining };
            data.push_back(dist(gen));
            int const count = data.back() == -1 ? 1 : (int)data.back();
            remaining       = (int16_t)(remaining - count);
        }
        if (remaining > 0) {
            data.push_back(remaining);
        }
        testInput(data);
    }
}

} // namespace tests
} // namespace openzl
