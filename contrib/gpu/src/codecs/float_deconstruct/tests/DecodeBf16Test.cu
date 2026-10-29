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

// Stages the streams at `offsets`, decodes them with bf16DeconDecode, and
// returns each chunk's host output.
std::vector<std::vector<uint16_t>> gpuDecode(
        const std::vector<OwnedHostChunk>& chunks,
        size_t maxSegElts,
        ChunkOffsets offsets = {})
{
    DeviceChunkSet dev(toHostChunks(chunks), offsets);
    bf16DeconDecode(dev.hostChunks().data(), dev.numInBatch(), maxSegElts, 0);
    ZL_CUDA_CHECK(cudaDeviceSynchronize());

    std::vector<std::vector<uint16_t>> outs(dev.numInBatch());
    for (uint32_t c = 0; c < dev.numInBatch(); ++c) {
        outs[c] = dev.download(c);
    }
    return outs;
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

/** Real streams of several single-chunk frames of the given sizes. */
struct Bf16Frames {
    Bf16Frames(const std::vector<size_t>& sizes, unsigned seed)
    {
        for (size_t k = 0; k < sizes.size(); ++k) {
            vals.push_back(makeBf16(sizes[k], (unsigned)(k + seed)));
            frames.push_back(mintBf16Frame(vals.back()));
            streams.push_back(extractBf16Chunk(frames.back()));
            EXPECT_EQ(streams.back().exponent.size(), sizes[k]);
        }
    }

    void expectDecoded(const std::vector<std::vector<uint16_t>>& gpu) const
    {
        ASSERT_EQ(gpu.size(), vals.size());
        for (size_t k = 0; k < vals.size(); ++k) {
            expectMatches(frames[k], gpu[k], vals[k]);
        }
    }

    std::vector<std::vector<uint16_t>> vals;
    std::vector<std::string> frames;
    std::vector<OwnedHostChunk> streams;
};

const std::vector<size_t> kUnevenSizes = { 50000, 1, 200000, 12345, 7 };

// A single chunk whose size is not a multiple of the vector width, with a small
// segment size that forces many segments plus a ragged tail: exercises both the
// vectorized body and the scalar tail.
TEST(DecodeBf16Test, SingleChunkMatchesOpenZLDecoder)
{
    const Bf16Frames frames({ 100002 }, 3);

    frames.expectDecoded(gpuDecode(frames.streams, 4096));
}

// Uneven chunks with a small segment size: the big chunk peels into many
// segments while tiny chunks stay whole, and several sizes are not a multiple
// of the vector width. Checks that segments alias the right output offsets.
TEST(DecodeBf16Test, MultiChunkBatchMatchesOpenZLDecoder)
{
    const Bf16Frames frames(kUnevenSizes, 20);

    frames.expectDecoded(gpuDecode(frames.streams, 4096));
}

// The production default segment size.
TEST(DecodeBf16Test, DefaultSegSizeMatchesOpenZLDecoder)
{
    const Bf16Frames frames({ 300000 }, 4);

    frames.expectDecoded(gpuDecode(frames.streams, kBf16DefaultMaxSegElts));
}

// Inputs stored in a frame start at any offset, and a later chunk's output can
// start at any 2-byte boundary. Every segment here is misaligned for the
// vector loads and stores. This fails if such a segment takes the vector path,
// or if the element path decodes a wrong value.
TEST(DecodeBf16Test, MisalignedChunksMatchOpenZLDecoder)
{
    const Bf16Frames frames(kUnevenSizes, 30);
    const ChunkOffsets offsets{ .exponentBytes = 1,
                                .signFracBytes = 3,
                                .dstElts       = 1 };

    frames.expectDecoded(gpuDecode(frames.streams, 4096, offsets));
}

// Each case misaligns one address and keeps the other two aligned. This fails
// if any one of them is not checked before a segment takes the vector path.
TEST(DecodeBf16Test, EachMisalignedAddressTakesTheElementPath)
{
    const Bf16Frames frames(kUnevenSizes, 40);
    const ChunkOffsets cases[] = {
        { .exponentBytes = 1, .signFracBytes = 0, .dstElts = 0 },
        { .exponentBytes = 0, .signFracBytes = 1, .dstElts = 0 },
        { .exponentBytes = 0, .signFracBytes = 0, .dstElts = 1 },
    };

    for (const ChunkOffsets& offsets : cases) {
        SCOPED_TRACE(
                "exponent +" + std::to_string(offsets.exponentBytes)
                + ", signFrac +" + std::to_string(offsets.signFracBytes)
                + ", dst +" + std::to_string(offsets.dstElts));
        frames.expectDecoded(gpuDecode(frames.streams, 4096, offsets));
    }
}

// This fails if an empty batch launches a kernel or reads its null chunk
// array.
TEST(DecodeBf16Test, EmptyBatchLaunchesNothing)
{
    bf16DeconDecode(nullptr, 0, 4096, 0);
    const Bf16DecodePlan plan(nullptr, 0, 4096);
    plan.launch(0);

    ZL_CUDA_CHECK(cudaDeviceSynchronize());
    EXPECT_EQ(cudaGetLastError(), cudaSuccess);
}

// An empty stream has no storage. This fails if a chunk with no elements stops
// the batch, or shifts the chunks after it.
TEST(DecodeBf16Test, ZeroLengthChunkInABatch)
{
    const Bf16Frames frames({ 1000, 7 }, 50);
    const std::vector<OwnedHostChunk> chunks{ frames.streams[0],
                                              OwnedHostChunk{},
                                              frames.streams[1] };

    const std::vector<std::vector<uint16_t>> gpu = gpuDecode(chunks, 4096);

    ASSERT_EQ(gpu.size(), 3);
    expectMatches(frames.frames[0], gpu[0], frames.vals[0]);
    EXPECT_TRUE(gpu[1].empty());
    expectMatches(frames.frames[1], gpu[2], frames.vals[1]);
}

// A plan keeps its segments on the device, so a caller can launch it again.
// This fails if the second launch skips a chunk or reads stale segments.
TEST(DecodeBf16Test, PlanDecodesAgainOnASecondLaunch)
{
    const Bf16Frames frames(kUnevenSizes, 60);
    const DeviceChunkSet dev(toHostChunks(frames.streams));
    const Bf16DecodePlan plan(dev.hostChunks().data(), dev.numInBatch(), 4096);
    plan.launch(0);
    ZL_CUDA_CHECK(cudaDeviceSynchronize());
    for (const FloatDeconChunk& chunk : dev.hostChunks()) {
        ZL_CUDA_CHECK(
                cudaMemset(chunk.dst, 0xFF, chunk.nbElts * sizeof(uint16_t)));
    }

    plan.launch(0);
    ZL_CUDA_CHECK(cudaDeviceSynchronize());

    std::vector<std::vector<uint16_t>> gpu(dev.numInBatch());
    for (uint32_t c = 0; c < dev.numInBatch(); ++c) {
        gpu[c] = dev.download(c);
    }
    frames.expectDecoded(gpu);
}

} // namespace
} // namespace openzl::gpu
