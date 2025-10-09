// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdio>
#include <random>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "custom_transforms/json_extract/decode_json_extract.h"
#include "custom_transforms/json_extract/encode_json_extract.h"
#include "custom_transforms/json_extract/tests/json_extract_test_data.h"
#include "tools/zstrong_cpp.h"

using namespace ::testing;

namespace zstrong::tests {

namespace {
std::string compressJson(std::string_view data)
{
    CGraph cgraph;
    auto node = ZL_Compressor_registerJsonExtract(cgraph.get(), 0);
    std::vector<ZL_GraphID> store(4, ZL_GRAPH_STORE);
    ZL_GraphID graph = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph.get(), node, store.data(), store.size());
    cgraph.unwrap(ZL_Compressor_selectStartingGraphID(cgraph.get(), graph));
    CCtx cctx;
    return compress(cctx, data, cgraph);
}

std::string decompressJson(std::string_view compressed)
{
    DCtx dctx;
    dctx.unwrap(ZL_DCtx_registerJsonExtract(dctx.get(), 0));
    return decompress(dctx, compressed);
}

void testRoundTripJson(std::string_view data)
{
    auto compressed   = compressJson(data);
    auto decompressed = decompressJson(compressed);
    EXPECT_EQ(decompressed, data);
}

TEST(TestJsonExtract, Basic)
{
    testRoundTripJson("");
    testRoundTripJson("{}");
    testRoundTripJson("[]");
    testRoundTripJson("5");
    testRoundTripJson("-5");
    testRoundTripJson("5.0");
    testRoundTripJson("5.0e-5");
    testRoundTripJson("5.0e5");
    testRoundTripJson("5.0E-5");
    testRoundTripJson("5.0E5");

    testRoundTripJson(" ");
    testRoundTripJson(" {} ");
    testRoundTripJson(" [] ");
    testRoundTripJson(" 5 ");
    testRoundTripJson(" -5 ");
    testRoundTripJson(" 5.0 ");
    testRoundTripJson(" 5.0e-5 ");
    testRoundTripJson(" 5.0e5 ");
    testRoundTripJson(" 5.0E-5 ");
    testRoundTripJson(" 5.0E5 ");

    testRoundTripJson(
            "{\"hello\": \"world\", \"0\": 0, \"1\": -0, \"2\": [0, -1, 5, 5.0E5, 0.05e-5, \"hello\", {}, {\"a\": [0, 1]}, true, false, null]}");

    testRoundTripJson("{]\"hello: 0.\"worlde-5:, [\"");
}

TEST(TestJsonExtract, SmallRandomData)
{
    std::mt19937 gen(0xdeadbeef);
    std::uniform_int_distribution<char> dist;
    for (size_t i = 0; i < 1000; ++i) {
        std::string data;
        data.reserve(100);
        for (size_t j = 0; j < 100; ++j) {
            data.push_back(dist(gen));
        }
        testRoundTripJson(data);
    }
}

TEST(TestJsonExtract, LargeRandomData)
{
    std::mt19937 gen(0xdeadbeef);
    std::uniform_int_distribution<char> charDist;
    std::uniform_int_distribution<size_t> sizeDist(0, 65536);
    for (size_t i = 0; i < 100; ++i) {
        auto const size = sizeDist(gen);
        std::string data;
        data.reserve(size);
        for (size_t j = 0; j < size; ++j) {
            data.push_back(charDist(gen));
        }
        testRoundTripJson(data);
    }
}

TEST(TestJsonExtract, JsonLikeData)
{
    std::mt19937 gen(0xdeadbeef);
    std::uniform_int_distribution<size_t> sizeDist(0, 65536);
    for (size_t i = 0; i < 100; ++i) {
        auto const data = genJsonLikeData(gen, sizeDist(gen));
        testRoundTripJson(data);
    }
}

} // namespace
} // namespace zstrong::tests
