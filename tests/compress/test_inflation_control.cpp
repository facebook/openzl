// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <random>
#include <vector>

#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_public_nodes.h"

namespace {

class InflationControlTest : public ::testing::Test {
   protected:
    // Helper to compress data
    size_t compress(
            void* dst,
            size_t dstCapacity,
            void const* src,
            size_t srcSize,
            ZL_GraphID graph)
    {
        ZL_CCtx* cctx = ZL_CCtx_create();
        EXPECT_NE(cctx, nullptr);

        EXPECT_TRUE(!ZL_isError(ZL_CCtx_setParameter(
                cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION)));
        EXPECT_TRUE(!ZL_isError(
                ZL_CCtx_selectStartingGraphID(cctx, nullptr, graph, nullptr)));

        ZL_Report const report =
                ZL_CCtx_compress(cctx, dst, dstCapacity, src, srcSize);
        EXPECT_TRUE(!ZL_isError(report));

        ZL_CCtx_free(cctx);

        return ZL_validResult(report);
    }

    // Helper to decompress data
    size_t
    decompress(void* dst, size_t dstCapacity, void const* src, size_t srcSize)
    {
        ZL_Report const report = ZL_decompress(dst, dstCapacity, src, srcSize);
        EXPECT_TRUE(!ZL_isError(report));
        return ZL_validResult(report);
    }

    // Generate high-entropy (random) data that will inflate when compressed
    std::vector<uint8_t> generateRandomData(size_t size, unsigned seed = 42)
    {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<uint8_t> dist(0, 255);

        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i) {
            data[i] = dist(rng);
        }
        return data;
    }

    // Generate low-entropy (compressible) data
    std::vector<uint8_t> generateCompressibleData(size_t size)
    {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(i % 256);
        }
        return data;
    }
};

// Test that random data triggers anti-inflation mechanism with HUFFMAN
TEST_F(InflationControlTest, RandomDataWithHuffmanMatchesStore)
{
    // Generate highly random data that would inflate with HUFFMAN
    const size_t inputSize = 10000;
    auto input             = generateRandomData(inputSize);

    std::vector<uint8_t> compressedHuffman(ZL_compressBound(inputSize));
    std::vector<uint8_t> compressedStore(ZL_compressBound(inputSize));

    // Compress with HUFFMAN (anti-inflation should trigger → STORE internally)
    size_t const huffmanSize = compress(
            compressedHuffman.data(),
            compressedHuffman.size(),
            input.data(),
            input.size(),
            ZL_GRAPH_HUFFMAN);

    // Compress explicitly with STORE for comparison baseline
    size_t const storeSize = compress(
            compressedStore.data(),
            compressedStore.size(),
            input.data(),
            input.size(),
            ZL_GRAPH_STORE);

    // KEY TEST: If anti-inflation worked, HUFFMAN should have switched to
    // STORE, producing the same (or nearly same) size as explicit STORE. Allow
    // small delta for potential header differences.
    EXPECT_NEAR(huffmanSize, storeSize, 10)
            << "HUFFMAN on random data should have switched to STORE. "
            << "Expected size ≈ " << storeSize << " (explicit STORE), "
            << "but got " << huffmanSize;

    // Verify round-trip works
    std::vector<uint8_t> decompressed(inputSize);
    size_t const dSize = decompress(
            decompressed.data(),
            decompressed.size(),
            compressedHuffman.data(),
            huffmanSize);

    ASSERT_EQ(dSize, inputSize);
    EXPECT_EQ(input, decompressed) << "Round-trip failed";
}

// Test that compressible data is still compressed normally
TEST_F(InflationControlTest, CompressibleDataStillCompresses)
{
    // Generate compressible data
    const size_t inputSize = 10000;
    auto input             = generateCompressibleData(inputSize);

    // Compress with entropy coding (should compress well)
    std::vector<uint8_t> compressed(ZL_compressBound(inputSize));
    size_t const cSize = compress(
            compressed.data(),
            compressed.size(),
            input.data(),
            input.size(),
            ZL_GRAPH_HUFFMAN);

    // Should achieve significant compression
    EXPECT_LT(cSize, inputSize * 0.5)
            << "Expected compression, but got " << cSize << " bytes from "
            << inputSize << " bytes input";

    // Verify round-trip works
    std::vector<uint8_t> decompressed(inputSize);
    size_t const dSize = decompress(
            decompressed.data(), decompressed.size(), compressed.data(), cSize);

    ASSERT_EQ(dSize, inputSize);
    EXPECT_EQ(input, decompressed) << "Round-trip failed";
}

// Test anti-inflation with ZSTD
TEST_F(InflationControlTest, RandomDataWithZSTDMatchesStore)
{
    const size_t inputSize = 8192;
    auto input             = generateRandomData(inputSize);

    std::vector<uint8_t> compressedZstd(ZL_compressBound(inputSize));
    std::vector<uint8_t> compressedStore(ZL_compressBound(inputSize));

    // Compress with ZSTD (anti-inflation should trigger → STORE internally)
    size_t const zstdSize = compress(
            compressedZstd.data(),
            compressedZstd.size(),
            input.data(),
            input.size(),
            ZL_GRAPH_ZSTD);

    // Compress explicitly with STORE for comparison baseline
    size_t const storeSize = compress(
            compressedStore.data(),
            compressedStore.size(),
            input.data(),
            input.size(),
            ZL_GRAPH_STORE);

    // KEY TEST: If anti-inflation worked, ZSTD should have switched to STORE
    EXPECT_NEAR(zstdSize, storeSize, 10)
            << "ZSTD on random data should have switched to STORE. "
            << "Expected size ≈ " << storeSize << " (explicit STORE), "
            << "but got " << zstdSize;

    // Verify round-trip
    std::vector<uint8_t> decompressed(inputSize);
    size_t const dSize = decompress(
            decompressed.data(),
            decompressed.size(),
            compressedZstd.data(),
            zstdSize);

    ASSERT_EQ(dSize, inputSize);
    EXPECT_EQ(input, decompressed);
}

// Test anti-inflation with FSE
TEST_F(InflationControlTest, RandomDataWithFSEMatchesStore)
{
    const size_t inputSize = 5000;
    auto input             = generateRandomData(inputSize, 123);

    std::vector<uint8_t> compressedFse(ZL_compressBound(inputSize));
    std::vector<uint8_t> compressedStore(ZL_compressBound(inputSize));

    // Compress with FSE (anti-inflation should trigger → STORE internally)
    size_t const fseSize = compress(
            compressedFse.data(),
            compressedFse.size(),
            input.data(),
            input.size(),
            ZL_GRAPH_FSE);

    // Compress explicitly with STORE for comparison baseline
    size_t const storeSize = compress(
            compressedStore.data(),
            compressedStore.size(),
            input.data(),
            input.size(),
            ZL_GRAPH_STORE);

    // KEY TEST: If anti-inflation worked, FSE should have switched to STORE
    EXPECT_NEAR(fseSize, storeSize, 10)
            << "FSE on random data should have switched to STORE. "
            << "Expected size ≈ " << storeSize << " (explicit STORE), "
            << "but got " << fseSize;

    // Verify round-trip
    std::vector<uint8_t> decompressed(inputSize);
    size_t const dSize = decompress(
            decompressed.data(),
            decompressed.size(),
            compressedFse.data(),
            fseSize);

    ASSERT_EQ(dSize, inputSize);
    EXPECT_EQ(input, decompressed);
}

// Test with very small data (edge case)
TEST_F(InflationControlTest, SmallRandomDataMatchesStore)
{
    const size_t inputSize = 100;
    auto input             = generateRandomData(inputSize, 999);

    std::vector<uint8_t> compressedHuffman(ZL_compressBound(inputSize));
    std::vector<uint8_t> compressedStore(ZL_compressBound(inputSize));

    // Compress with HUFFMAN (anti-inflation should trigger → STORE internally)
    size_t const huffmanSize = compress(
            compressedHuffman.data(),
            compressedHuffman.size(),
            input.data(),
            input.size(),
            ZL_GRAPH_HUFFMAN);

    // Compress explicitly with STORE for comparison baseline
    size_t const storeSize = compress(
            compressedStore.data(),
            compressedStore.size(),
            input.data(),
            input.size(),
            ZL_GRAPH_STORE);

    // KEY TEST: Even with small data, anti-inflation should prevent excessive
    // overhead by switching to STORE when HUFFMAN would inflate
    EXPECT_NEAR(huffmanSize, storeSize, 10)
            << "HUFFMAN on small random data should have switched to STORE. "
            << "Expected size ≈ " << storeSize << " (explicit STORE), "
            << "but got " << huffmanSize;

    // Verify round-trip
    std::vector<uint8_t> decompressed(inputSize);
    size_t const dSize = decompress(
            decompressed.data(),
            decompressed.size(),
            compressedHuffman.data(),
            huffmanSize);

    ASSERT_EQ(dSize, inputSize);
    EXPECT_EQ(input, decompressed);
}

} // namespace
