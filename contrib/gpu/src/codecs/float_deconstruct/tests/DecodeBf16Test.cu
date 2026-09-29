// Copyright (c) Meta Platforms, Inc. and affiliates.

// Differential correctness test for the bf16 float_deconstruct GPU decode.
//
// Mints real OpenZL bf16 float_deconstruct frames (via contrib/gpu/testkit,
// which drives the actual encoder), reflects out the codec's two encoded
// streams (the decoder's inputs), feeds them to bf16DeconDecode on the GPU, and
// checks that the result reproduces the original values (and that OpenZL's own
// decoder round-trips the frame, via testkit::decompressedEquals).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "openzl/cpp/codecs/FloatDeconstruct.hpp"
#include "openzl/openzl.hpp"
#include "openzl/zl_reflection.h"

#include "contrib/gpu/src/codecs/float_deconstruct/decode_float_deconstruct_bf16.cuh"
#include "contrib/gpu/src/codecs/float_deconstruct/gpu_chunk_harness.cuh"
#include "contrib/gpu/src/common/cuda_error.cuh"
#include "contrib/gpu/testkit/frame_factory.h"

namespace openzl::gpu {
namespace {

// float_deconstruct standard codec wire id (from the OpenZL transform table).
constexpr uint32_t kFloatDeconstructCodecId = 33;

std::vector<uint16_t> makeBf16(size_t n, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFF);
    std::vector<uint16_t> v(n);
    for (size_t i = 0; i < n; ++i) {
        v[i] = (uint16_t)dist(rng);
    }
    return v;
}

std::string mintBf16Frame(const std::vector<uint16_t>& vals)
{
    Input in = Input::refNumeric<uint16_t>(vals.data(), vals.size());
    Compressor c;
    c.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    return testkit::makeFrameNodeToStore(
            c, nodes::BFloat16Deconstruct::node, in);
}

struct ReflectionCtxDeleter {
    void operator()(ZL_ReflectionCtx* rctx) const noexcept
    {
        ZL_ReflectionCtx_free(rctx);
    }
};
using ReflectionCtxPtr =
        std::unique_ptr<ZL_ReflectionCtx, ReflectionCtxDeleter>;

// Reflect a bf16 float_deconstruct frame and copy out codec 33's outputs (the
// decoder's inputs): struct = signFrac, serial = exponent.
OwnedHostChunk extractBf16Chunk(const std::string& frame)
{
    ReflectionCtxPtr rctx{ ZL_ReflectionCtx_create() };
    EXPECT_NE(rctx.get(), nullptr);
    ZL_Report r = ZL_ReflectionCtx_setCompressedFrame(
            rctx.get(), frame.data(), frame.size());
    EXPECT_FALSE(ZL_isError(r));

    OwnedHostChunk out;
    const size_t numCodecs =
            ZL_ReflectionCtx_getNumCodecs_lastChunk(rctx.get());
    for (size_t i = 0; i < numCodecs; ++i) {
        const ZL_CodecInfo* codec =
                ZL_ReflectionCtx_getCodec_lastChunk(rctx.get(), i);
        if (!ZL_CodecInfo_isStandardCodec(codec)
            || ZL_CodecInfo_getCodecID(codec) != kFloatDeconstructCodecId) {
            continue;
        }
        const size_t numOut = ZL_CodecInfo_getNumOutputs(codec);
        for (size_t o = 0; o < numOut; ++o) {
            const ZL_DataInfo* si = ZL_CodecInfo_getOutput(codec, o);
            const ZL_Type type    = ZL_DataInfo_getType(si);
            const size_t bytes    = ZL_DataInfo_getContentSize(si);
            const uint8_t* const ptr =
                    (const uint8_t*)ZL_DataInfo_getDataPtr(si);
            // Classify by type, not index (reflection reverses encoder order).
            if (type == ZL_Type_struct) {
                out.signFrac.assign(ptr, ptr + bytes);
                EXPECT_EQ(
                        ZL_DataInfo_getEltWidth(si),
                        (size_t)1);                           // bf16 signFrac
                EXPECT_EQ(ZL_DataInfo_getNumElts(si), bytes); // 1 byte/elt
            } else if (type == ZL_Type_serial) {
                out.exponent.assign(ptr, ptr + bytes);
            }
        }
        break;
    }
    return out;
}

// Stages the streams, runs `launch` (which selects the decode kernel), and
// writes the per-chunk host outputs to `outs`.
template <typename LaunchFn>
void gpuDecode(
        const std::vector<OwnedHostChunk>& chunks,
        std::vector<std::vector<uint16_t>>& outs,
        LaunchFn&& launch)
{
    DeviceChunkSet dev(toHostChunks(chunks));

    launch(dev);
    ZL_CUDA_CHECK(cudaDeviceSynchronize());

    outs.resize(dev.numInBatch());
    for (uint32_t c = 0; c < dev.numInBatch(); ++c) {
        outs[c] = dev.download(c);
    }
}

void gpuDecodeNaive(
        const std::vector<OwnedHostChunk>& chunks,
        std::vector<std::vector<uint16_t>>& outs)
{
    gpuDecode(chunks, outs, [](DeviceChunkSet& dev) {
        bf16DeconDecode(dev.numInBatch(), dev.deviceChunks(), 0);
    });
}

void gpuDecodeUnified(
        const std::vector<OwnedHostChunk>& chunks,
        std::vector<std::vector<uint16_t>>& outs,
        size_t maxSegElts)
{
    gpuDecode(chunks, outs, [maxSegElts](DeviceChunkSet& dev) {
        bf16DeconDecodeUnified(
                dev.hostChunks().data(), dev.numInBatch(), maxSegElts, 0);
    });
}

// The GPU decode of the real OpenZL-encoded streams must reproduce the original
// values; and OpenZL's own decoder must round-trip the frame too.
void expectMatches(
        const std::string& frame,
        const std::vector<uint16_t>& gpu,
        const std::vector<uint16_t>& vals)
{
    ASSERT_EQ(gpu.size(), vals.size());
    EXPECT_EQ(
            0,
            std::memcmp(
                    gpu.data(), vals.data(), vals.size() * sizeof(uint16_t)));
    EXPECT_TRUE(
            testkit::decompressedEquals(
                    frame,
                    Input::refNumeric<uint16_t>(vals.data(), vals.size())));
}

TEST(DecodeBf16Test, SingleChunkMatchesOpenZLDecoder)
{
    const std::vector<uint16_t> vals = makeBf16(100000, 1);
    const std::string frame          = mintBf16Frame(vals);

    const OwnedHostChunk s = extractBf16Chunk(frame);
    ASSERT_EQ(s.exponent.size(), vals.size());
    ASSERT_EQ(s.signFrac.size(), vals.size());

    std::vector<std::vector<uint16_t>> gpu;
    gpuDecodeNaive({ s }, gpu);
    expectMatches(frame, gpu[0], vals);
}

// Exercises the multi-chunk (batched) path with real, unevenly-sized streams by
// minting several single-chunk frames and decoding them in one kernel call.
TEST(DecodeBf16Test, MultiChunkBatchMatchesOpenZLDecoder)
{
    const std::vector<size_t> sizes = { 50000, 1, 200000, 12345, 7 };
    std::vector<std::vector<uint16_t>> vals;
    std::vector<std::string> frames;
    std::vector<OwnedHostChunk> streams;
    for (size_t k = 0; k < sizes.size(); ++k) {
        vals.push_back(makeBf16(sizes[k], (unsigned)(k + 10)));
        frames.push_back(mintBf16Frame(vals.back()));
        streams.push_back(extractBf16Chunk(frames.back()));
        ASSERT_EQ(streams.back().exponent.size(), sizes[k]);
    }
    std::vector<std::vector<uint16_t>> gpu;
    gpuDecodeNaive(streams, gpu);
    for (size_t k = 0; k < sizes.size(); ++k) {
        expectMatches(frames[k], gpu[k], vals[k]);
    }
}

// Unified decode of a single chunk whose size is not a multiple of the vector
// width, with a small segment size that forces many segments plus a ragged
// tail: exercises both the vectorized body and the scalar tail.
TEST(DecodeBf16Test, UnifiedSingleChunkMatchesOpenZLDecoder)
{
    const std::vector<uint16_t> vals = makeBf16(100002, 3);
    const std::string frame          = mintBf16Frame(vals);

    const OwnedHostChunk s = extractBf16Chunk(frame);
    ASSERT_EQ(s.exponent.size(), vals.size());
    ASSERT_EQ(s.signFrac.size(), vals.size());

    std::vector<std::vector<uint16_t>> gpu;
    gpuDecodeUnified({ s }, gpu, 4096);
    expectMatches(frame, gpu[0], vals);
}

// Unified decode over uneven chunks with a small segment size: the big chunk
// peels into many segments while tiny chunks stay whole, and several sizes are
// not a multiple of the vector width. Checks segments alias the right output
// offsets.
TEST(DecodeBf16Test, UnifiedMultiChunkMatchesOpenZLDecoder)
{
    const std::vector<size_t> sizes = { 50000, 1, 200000, 12345, 7 };
    std::vector<std::vector<uint16_t>> vals;
    std::vector<std::string> frames;
    std::vector<OwnedHostChunk> streams;
    for (size_t k = 0; k < sizes.size(); ++k) {
        vals.push_back(makeBf16(sizes[k], (unsigned)(k + 20)));
        frames.push_back(mintBf16Frame(vals.back()));
        streams.push_back(extractBf16Chunk(frames.back()));
        ASSERT_EQ(streams.back().exponent.size(), sizes[k]);
    }
    std::vector<std::vector<uint16_t>> gpu;
    gpuDecodeUnified(streams, gpu, 4096);
    for (size_t k = 0; k < sizes.size(); ++k) {
        expectMatches(frames[k], gpu[k], vals[k]);
    }
}

// Unified decode with the production default segment size.
TEST(DecodeBf16Test, UnifiedDefaultSegSizeMatchesOpenZLDecoder)
{
    const std::vector<uint16_t> vals = makeBf16(300000, 4);
    const std::string frame          = mintBf16Frame(vals);

    const OwnedHostChunk s = extractBf16Chunk(frame);
    ASSERT_EQ(s.exponent.size(), vals.size());

    std::vector<std::vector<uint16_t>> gpu;
    gpuDecodeUnified({ s }, gpu, kUnifiedDefaultMaxSegElts);
    expectMatches(frame, gpu[0], vals);
}

} // namespace
} // namespace openzl::gpu
