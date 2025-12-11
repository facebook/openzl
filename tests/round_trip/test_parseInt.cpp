// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/common/assertion.h"

#include <gtest/gtest.h>

#include <random>
#include "openzl/codecs/zl_parse_int.h"
#include "openzl/common/debug.h" // ZL_REQUIRE
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "tests/datagen/random_producer/PRNGWrapper.h"
#include "tests/datagen/structures/IntegerStringProducer.h"

static ZL_GraphID registerTryParseIntGraph(ZL_Compressor* compressor) noexcept
{
    return ZL_RES_value(ZL_Compressor_parameterizeTryParseIntGraph(
            compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE));
}

static ZL_GraphID registerParseIntGraph(ZL_Compressor* compressor) noexcept
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor, ZL_NODE_PARSE_INT, ZL_GRAPH_STORE);
}

static size_t decompress(ZL_TypedBuffer* output, std::string const& compressed)
{
    ZL_DCtx* const dctx = ZL_DCtx_create();
    ZL_REQUIRE_NN(dctx);
    ZL_Report const r = ZL_DCtx_decompressTBuffer(
            dctx, output, compressed.data(), compressed.size());
    EXPECT_EQ(ZL_isError(r), 0) << "decompression failed \n";
    if (ZL_isError(r)) {
        std::cout << "decompress Error: "
                  << ZL_DCtx_getErrorContextString(dctx, r) << std::endl;
    }
    ZL_DCtx_free(dctx);
    return ZL_validResult(r);
}

static size_t compress(
        void* dst,
        size_t dstCapacity,
        std::string const& input,
        std::vector<uint32_t> const& fieldSizes,
        ZL_GraphFn const graphFn,
        bool shouldFail)
{
    ZL_CCtx* const cctx = ZL_CCtx_create();
    ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
            cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    ZL_REQUIRE_NN(cctx);

    ZL_Compressor* const compressor = ZL_Compressor_create();
    ZL_GraphID parseInt             = graphFn(compressor);

    ZL_Report const gssr =
            ZL_Compressor_selectStartingGraphID(compressor, parseInt);
    EXPECT_EQ(ZL_isError(gssr), 0) << "selection of starting graphid failed\n";
    ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx, compressor);
    EXPECT_EQ(ZL_isError(rcgr), 0) << "CGraph reference failed\n";

    ZL_TypedRef* const tref = ZL_TypedRef_createString(
            input.data(), input.size(), fieldSizes.data(), fieldSizes.size());
    ZL_REQUIRE_NN(tref);
    ZL_Report const r = ZL_CCtx_compressTypedRef(cctx, dst, dstCapacity, tref);

    if (shouldFail) {
        EXPECT_NE(ZL_isError(r), 0) << "compression succeeded \n";
    } else {
        EXPECT_EQ(ZL_isError(r), 0) << "compression failed \n";
        if (ZL_isError(r)) {
            std::cout << "compress Error: "
                      << ZL_CCtx_getErrorContextString(cctx, r) << std::endl;
        }
    }
    /* Free allocations*/
    ZL_Compressor_free(compressor);
    ZL_TypedRef_free(tref);
    ZL_CCtx_free(cctx);
    if (shouldFail) {
        return 0;
    }
    return ZL_validResult(r);
}

static void testRoundTrip(
        std::vector<std::string> const& data,
        ZL_GraphFn const graphFn)
{
    auto const [input, fieldSizes] =
            openzl::tests::datagen::IntegerStringProducer::flatten(data);
    size_t uncompressedSize =
            input.size() + fieldSizes.size() * sizeof(uint32_t);
    size_t const compressedBound = ZL_compressBound(uncompressedSize);
    std::string compressed(compressedBound, '\0');
    size_t const compressedSize = compress(
            compressed.data(),
            compressedBound,
            input,
            fieldSizes,
            graphFn,
            false);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           uncompressedSize,
           compressedSize);
    compressed.resize(compressedSize);
    ZL_TypedBuffer* output        = ZL_TypedBuffer_create();
    size_t const decompressedSize = decompress(output, compressed);
    EXPECT_EQ(input.size(), decompressedSize)
            << "Number of decompressed bytes does not match input size";
    EXPECT_EQ(
            memcmp(input.data(), ZL_TypedBuffer_rPtr(output), input.size()), 0)
            << "Decompressed content does not match input";
    EXPECT_EQ(
            memcmp(fieldSizes.data(),
                   ZL_TypedBuffer_rStringLens(output),
                   fieldSizes.size() * sizeof(uint32_t)),
            0)
            << "Decompressed field sizes does not match input";
    ZL_TypedBuffer_free(output);
    printf("round-trip success \n");
}

static void testCompressFail(
        std::vector<std::string> const& data,
        ZL_GraphFn const graphFn)
{
    auto const [input, fieldSizes] =
            openzl::tests::datagen::IntegerStringProducer::flatten(data);
    size_t uncompressedSize =
            input.size() + fieldSizes.size() * sizeof(uint32_t);
    size_t const compressedBound = ZL_compressBound(uncompressedSize);
    std::string compressed(compressedBound, '\0');
    compress(
            compressed.data(),
            compressedBound,
            input,
            fieldSizes,
            graphFn,
            true);
}

TEST(ParseIntTest, Basic)
{
    testRoundTrip({ "0", "1", "100", "200" }, registerParseIntGraph);
    testRoundTrip({ "-1", "-5", "-10" }, registerParseIntGraph);
    testRoundTrip(
            { "9223372036854775807", "-9223372036854775808" },
            registerParseIntGraph);
    testRoundTrip(
            {
                    "1",
                    "10",
                    "100",
                    "1000",
                    "10000",
                    "100000",
                    "1000000",
                    "10000000",
                    "100000000",
                    "1000000000",
                    "10000000000",
                    "100000000000",
                    "1000000000000",
                    "10000000000000",
                    "100000000000000",
                    "1000000000000000",
                    "10000000000000000",
                    "100000000000000000",
                    "1000000000000000000",
            },
            registerParseIntGraph);
    testRoundTrip(
            {
                    "-1",
                    "-10",
                    "-100",
                    "-1000",
                    "-10000",
                    "-100000",
                    "-1000000",
                    "-10000000",
                    "-100000000",
                    "-1000000000",
                    "-10000000000",
                    "-100000000000",
                    "-1000000000000",
                    "-10000000000000",
                    "-100000000000000",
                    "-1000000000000000",
                    "-10000000000000000",
                    "-100000000000000000",
            },
            registerParseIntGraph);
    testRoundTrip(
            { "0",
              "9",
              "99",
              "999",
              "9999",
              "99999",
              "999999",
              "9999999",
              "99999999",
              "999999999",
              "9999999999",
              "99999999999",
              "999999999999",
              "9999999999999",
              "99999999999999",
              "999999999999999",
              "9999999999999999",
              "99999999999999999",
              "999999999999999999" },
            registerParseIntGraph);
    testRoundTrip(
            { "-9",
              "-99",
              "-999",
              "-9999",
              "-99999",
              "-999999",
              "-9999999",
              "-99999999",
              "-999999999",
              "-9999999999",
              "-99999999999",
              "-999999999999",
              "-9999999999999",
              "-99999999999999",
              "-999999999999999",
              "-9999999999999999",
              "-99999999999999999" },
            registerParseIntGraph);
}

TEST(ParseIntTest, GeneratedRandom)
{
    auto rw = std::make_shared<openzl::tests::datagen::PRNGWrapper>(
            std::make_shared<std::mt19937>());
    auto gen = openzl::tests::datagen::IntegerStringProducer(rw);
    for (size_t trials = 0; trials < 1000; ++trials) {
        auto data = gen("data");
        testRoundTrip(data, registerParseIntGraph);
    }
}

TEST(ParseIntTest, FailCases)
{
    testCompressFail({ "100000000000000000000" }, registerParseIntGraph);
    testCompressFail({ "-100000000000000000000" }, registerParseIntGraph);
    testCompressFail({ "01" }, registerParseIntGraph);
    testCompressFail({ "a" }, registerParseIntGraph);
    testCompressFail({ "--1" }, registerParseIntGraph);
    testCompressFail({ "+1" }, registerParseIntGraph);
    testCompressFail({ "-0" }, registerParseIntGraph);
}

TEST(ParseIntTest, TryParseIntAllInputs)
{
    // All inputs should succeed regardless of whether it is int or not
    testRoundTrip(
            { "A",
              "2",
              "-0001",
              "0.02",
              "5",
              "11",
              "",
              "100000000000000000000000000" },
            registerTryParseIntGraph);
    // Fully valid parse
    testRoundTrip({ "1", "2", "3" }, registerTryParseIntGraph);
    // Fully invalid parse
    testRoundTrip({ "01", "-02", "003" }, registerTryParseIntGraph);
}
