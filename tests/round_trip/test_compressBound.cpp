// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "openzl/codecs/zl_store.h"
#include "openzl/zl_common_types.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_segmenter.h"
#include "openzl/zl_version.h"

namespace {

// ---------------------------------------------------------------------------
// Configurable fixed-chunk-size segmenter
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(facebook-avoid-non-const-global-variables)
static size_t g_fixedChunkSize = ZL_MIN_CHUNK_SIZE;

/// Splits input into fixed-size chunks, routing each to ZL_GRAPH_STORE.
/// The last chunk may be smaller (remainder).
/// This produces worst-case overhead: STORE = no compression, maximum
/// framing cost per byte of input.
ZL_Report fixedChunkStoreSegmenterFn(ZL_Segmenter* sctx)
{
    assert(ZL_Segmenter_numInputs(sctx) == 1);
    const ZL_Input* input = ZL_Segmenter_getInput(sctx, 0);
    assert(input != NULL);

    while (ZL_Input_numElts(input) > 0) {
        size_t remaining = ZL_Input_numElts(input);
        size_t chunkElts =
                (remaining < g_fixedChunkSize) ? remaining : g_fixedChunkSize;

        ZL_Report r = ZL_Segmenter_processChunk(
                sctx, &chunkElts, 1, ZL_GRAPH_STORE, nullptr);
        if (ZL_isError(r))
            return r;

        input = ZL_Segmenter_getInput(sctx, 0);
    }
    return ZL_returnSuccess();
}

static ZL_SegmenterDesc const fixedChunkStoreSegmenter = {
    .name           = "Fixed-Chunk STORE Segmenter",
    .segmenterFn    = fixedChunkStoreSegmenterFn,
    .inputTypeMasks = (const ZL_Type[]){ ZL_Type_serial },
    .numInputs      = 1,
};

/// Registers the segmenter with worst-case parameters:
/// max format version, both checksums enabled.
static ZL_GraphID registerFixedChunkStoreSegmenter(
        ZL_Compressor* compressor) noexcept
{
    ZL_Report r = ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION);
    if (ZL_isError(r))
        abort();

    r = ZL_Compressor_setParameter(
            compressor, ZL_CParam_contentChecksum, ZL_TernaryParam_enable);
    if (ZL_isError(r))
        abort();

    r = ZL_Compressor_setParameter(
            compressor, ZL_CParam_compressedChecksum, ZL_TernaryParam_enable);
    if (ZL_isError(r))
        abort();

    return ZL_Compressor_registerSegmenter(
            compressor, &fixedChunkStoreSegmenter);
}

// ---------------------------------------------------------------------------
// Core round-trip helper
// ---------------------------------------------------------------------------

static void compressBoundRoundTrip(size_t inputSize, const char* testLabel)
{
    printf("\n=== CompressBound: %s (inputSize=%zu, chunkSize=%zu) ===\n",
           testLabel,
           inputSize,
           g_fixedChunkSize);

    // Generate synthetic input (repeating byte pattern)
    std::vector<unsigned char> inputData(inputSize);
    for (size_t i = 0; i < inputSize; i++)
        inputData[i] = static_cast<unsigned char>(i & 0xFF);

    // Allocate exactly ZL_compressBound(inputSize)
    size_t const bound = ZL_compressBound(inputSize);
    std::vector<unsigned char> compressed(bound);

    // Create serial typed ref
    ZL_TypedRef* input = ZL_TypedRef_createSerial(inputData.data(), inputSize);
    ASSERT_NE(input, nullptr);

    // Compress
    ZL_CCtx* cctx = ZL_CCtx_create();
    ASSERT_NE(cctx, nullptr);
    ZL_Compressor* compressor = ZL_Compressor_create();
    ASSERT_NE(compressor, nullptr);

    ZL_Report r = ZL_Compressor_initUsingGraphFn(
            compressor, registerFixedChunkStoreSegmenter);
    ASSERT_FALSE(ZL_isError(r)) << "compressor init failed";

    r = ZL_CCtx_refCompressor(cctx, compressor);
    ASSERT_FALSE(ZL_isError(r)) << "compressor ref failed";

    r = ZL_CCtx_compressTypedRef(cctx, compressed.data(), bound, input);
    ASSERT_FALSE(ZL_isError(r))
            << "compression failed: bound " << bound
            << " was insufficient for input size " << inputSize;
    size_t compressedSize = ZL_validResult(r);

    printf("  compressed %zu -> %zu (bound=%zu, overhead=%zu)\n",
           inputSize,
           compressedSize,
           bound,
           compressedSize > inputSize ? compressedSize - inputSize : 0);
    EXPECT_LE(compressedSize, bound);

    ZL_Compressor_free(compressor);
    ZL_CCtx_free(cctx);
    ZL_TypedRef_free(input);

    // Decompress and verify round-trip
    if (inputSize > 0) {
        ZL_TypedBuffer* tbuf = ZL_TypedBuffer_create();
        ASSERT_NE(tbuf, nullptr);
        ZL_DCtx* dctx = ZL_DCtx_create();
        ASSERT_NE(dctx, nullptr);

        r = ZL_DCtx_decompressTBuffer(
                dctx, tbuf, compressed.data(), compressedSize);
        ASSERT_FALSE(ZL_isError(r)) << "decompression failed";
        EXPECT_EQ(ZL_validResult(r), inputSize);
        EXPECT_EQ(
                memcmp(inputData.data(), ZL_TypedBuffer_rPtr(tbuf), inputSize),
                0)
                << "round-trip data mismatch";

        ZL_TypedBuffer_free(tbuf);
        ZL_DCtx_free(dctx);
    }
}

// ---------------------------------------------------------------------------
// Test cases: 32 KB chunk size (the documented minimum)
// ---------------------------------------------------------------------------

TEST(CompressBound, exactMultiple)
{
    g_fixedChunkSize = ZL_MIN_CHUNK_SIZE;
    compressBoundRoundTrip(8 * ZL_MIN_CHUNK_SIZE, "8 x 32KB exact");
}

TEST(CompressBound, tinyRemainder)
{
    g_fixedChunkSize = ZL_MIN_CHUNK_SIZE;
    compressBoundRoundTrip(8 * ZL_MIN_CHUNK_SIZE + 1, "8 x 32KB + 1");
}

TEST(CompressBound, largeRemainder)
{
    g_fixedChunkSize = ZL_MIN_CHUNK_SIZE;
    compressBoundRoundTrip(
            8 * ZL_MIN_CHUNK_SIZE + ZL_MIN_CHUNK_SIZE - 1, "8 x 32KB + 32KB-1");
}

TEST(CompressBound, smallInput)
{
    g_fixedChunkSize = ZL_MIN_CHUNK_SIZE;
    compressBoundRoundTrip(1000, "small 1000 bytes");
}

TEST(CompressBound, singleFullChunk)
{
    g_fixedChunkSize = ZL_MIN_CHUNK_SIZE;
    compressBoundRoundTrip(ZL_MIN_CHUNK_SIZE, "single 32KB chunk");
}

// ---------------------------------------------------------------------------
// Margin validation with larger chunk sizes
// ---------------------------------------------------------------------------

TEST(CompressBound, chunkSize_64KB)
{
    g_fixedChunkSize = 65536;
    compressBoundRoundTrip(8 * 65536, "8 x 64KB exact");
    compressBoundRoundTrip(8 * 65536 + 1, "8 x 64KB + 1");
}

TEST(CompressBound, chunkSize_128KB)
{
    g_fixedChunkSize = 131072;
    compressBoundRoundTrip(8 * 131072, "8 x 128KB exact");
    compressBoundRoundTrip(8 * 131072 + 1, "8 x 128KB + 1");
}

} // namespace
