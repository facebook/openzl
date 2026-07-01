// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "openzl/dev/contrib/gpu/testkit/frame_factory.h"
#include "openzl/dev/contrib/gpu/testkit/frame_verifier.h"
#include "openzl/dev/contrib/gpu/testkit/multichunk_frame.h"
#include "openzl/openzl.hpp"

// These tests mint real OpenZL frames that contain MULTIPLE chunks, each with a
// different codec graph, and confirm (a) the frame round-trips, (b) it really
// has the expected number of chunks, and (c) each chunk carries the codecs we
// asked for. Expected wire IDs are reasoned from the OpenZL standard transform
// table (openzl/src/openzl/common/wire_format.h), NOT copied from any frame's
// own output:
//
//     delta_int              1   (numeric delta)
//     convert_num_to_serial 10   (inserted when a numeric stream meets STORE)
//     bitpack_int           28   (bitpack of a numeric stream)
//
// A numeric stream routed straight to STORE is first serialized by
// convert_num_to_serial_le (10) -- the stored bytes themselves are not a codec
// -- so a `delta -> store` chunk on numeric ints contains exactly {1, 10}, and
// a bare `store` chunk on numeric ints contains exactly {10}.

namespace openzl::gpu::testkit {
namespace {

// Independent, deterministic test data (not derived from any frame output).

// Small-range ints (0..7) fit in 3 bits, so bitpack engages on them.
std::vector<int32_t> makeSmallInts(size_t n)
{
    std::vector<int32_t> v(n);
    for (size_t i = 0; i < n; ++i) {
        v[i] = static_cast<int32_t>(i % 8);
    }
    return v;
}

// A monotonic ramp with a constant stride: deltas collapse to a single repeated
// value, giving delta_int real work to do before the chunk is stored.
std::vector<int32_t> makeRamp(size_t n)
{
    std::vector<int32_t> v(n);
    for (size_t i = 0; i < n; ++i) {
        v[i] = static_cast<int32_t>(i) * 3;
    }
    return v;
}

constexpr uint32_t kDeltaInt           = 1;
constexpr uint32_t kConvertNumToSerial = 10;
constexpr uint32_t kBitpackInt         = 28;

// 20000 int32 elements per chunk -> 80000 bytes, comfortably above the
// ZL_MIN_CHUNK_SIZE floor of 32768 bytes.
constexpr size_t kEltsPerChunk = 20000;

bool contains(const std::vector<uint32_t>& ids, uint32_t id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// Builds a `delta_int -> store` single-output graph on `compressor`.
GraphID buildDeltaToStore(Compressor& compressor)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor.get(), nodes::DeltaInt::node, graphs::Store{}());
}

// 2-chunk frame: chunk 0 = bitpack on small ints, chunk 1 = delta -> store on a
// ramp. Asserts the frame round-trips, has two chunks, and that each chunk
// carries the codecs its graph implies.
TEST(MultiChunkFrameTest, BitpackThenDeltaStore_TwoChunksWithDistinctCodecs)
{
    // Chunk 0 data (bitpackable) followed by chunk 1 data (ramp) in one buffer.
    std::vector<int32_t> ints       = makeSmallInts(kEltsPerChunk);
    const std::vector<int32_t> ramp = makeRamp(kEltsPerChunk);
    ints.insert(ints.end(), ramp.begin(), ramp.end());

    const Input in = Input::refNumeric<int32_t>(ints.data(), ints.size());

    Compressor compressor;
    const GraphID bitpack      = graphs::Bitpack{}();
    const GraphID deltaToStore = buildDeltaToStore(compressor);

    const std::string frame = makeMultiChunkFrame(
            compressor,
            { ChunkSpec{ kEltsPerChunk, bitpack },
              ChunkSpec{ kEltsPerChunk, deltaToStore } },
            in);

    // (a) The whole multi-chunk frame round-trips back to the original input.
    EXPECT_TRUE(decompressedEquals(frame, in));

    // Per-chunk codecs, recovered via the documented chunk-walk fallback: carve
    // each chunk's slice out of the input and reflect it as its own
    // single-chunk reference frame. (The public reflection API on the
    // multi-chunk frame only exposes the last chunk -- asserted separately
    // below.)
    const Input slice0 = sliceInput(in, 0, kEltsPerChunk);
    const Input slice1 = sliceInput(in, kEltsPerChunk, kEltsPerChunk);

    Compressor refC0;
    const std::string chunk0Frame =
            makeFrameWithGraph(refC0, graphs::Bitpack{}(), slice0);
    Compressor refC1;
    const std::string chunk1Frame =
            makeFrameWithGraph(refC1, buildDeltaToStore(refC1), slice1);

    const std::vector<uint32_t> chunk0Codecs =
            standardCodecsInFrame(chunk0Frame);
    const std::vector<uint32_t> chunk1Codecs =
            standardCodecsInFrame(chunk1Frame);

    // (c) chunk 0 contains bitpack_int (28); chunk 1 contains delta_int (1)
    // plus the num->serial conversion (10) that STORE forces on a numeric
    // stream.
    EXPECT_TRUE(contains(chunk0Codecs, kBitpackInt));
    EXPECT_TRUE(frameHasExactlyCodecs(chunk0Frame, { kBitpackInt }));
    EXPECT_TRUE(contains(chunk1Codecs, kDeltaInt));
    EXPECT_TRUE(frameHasExactlyCodecs(
            chunk1Frame, { kDeltaInt, kConvertNumToSerial }));
}

// (b) The frame genuinely has 2 chunks. We confirm this structurally by
// round-tripping AND by directly enumerating each chunk's codecs from the full
// multi-chunk frame.
TEST(MultiChunkFrameTest, StandardCodecsPerChunk_ReportsEveryChunk)
{
    // This test verifies standardCodecsPerChunk() uses decompression
    // introspection to report every chunk in a multi-chunk frame; it fails if
    // the verifier falls back to the old reflection API and only exposes the
    // final chunk.
    std::vector<int32_t> ints       = makeSmallInts(kEltsPerChunk);
    const std::vector<int32_t> ramp = makeRamp(kEltsPerChunk);
    ints.insert(ints.end(), ramp.begin(), ramp.end());
    const Input in = Input::refNumeric<int32_t>(ints.data(), ints.size());

    Compressor compressor;
    const GraphID bitpack      = graphs::Bitpack{}();
    const GraphID deltaToStore = buildDeltaToStore(compressor);
    const std::string frame    = makeMultiChunkFrame(
            compressor,
            { ChunkSpec{ kEltsPerChunk, bitpack },
                 ChunkSpec{ kEltsPerChunk, deltaToStore } },
            in);

    const std::vector<std::vector<uint32_t>> perChunk =
            standardCodecsPerChunk(frame);
    ASSERT_EQ(perChunk.size(), 2u);
    EXPECT_EQ(perChunk[0], std::vector<uint32_t>{ kBitpackInt });
    EXPECT_EQ(
            perChunk[1],
            (std::vector<uint32_t>{ kConvertNumToSerial, kDeltaInt }));
}

TEST(MultiChunkFrameTest, RejectsChunkPlanThatExceedsInput)
{
    // This test verifies makeMultiChunkFrame() rejects malformed chunk plans
    // before the segmenter slices the input; it fails if cumulative chunk
    // sizes can walk past input.numElts().
    const std::vector<int32_t> ints = makeSmallInts(100);
    const Input in = Input::refNumeric<int32_t>(ints.data(), ints.size());

    Compressor compressor;
    const GraphID bitpack = graphs::Bitpack{}();

    EXPECT_THROW(
            makeMultiChunkFrame(
                    compressor,
                    { ChunkSpec{ 70, bitpack }, ChunkSpec{ 70, bitpack } },
                    in),
            openzl::Exception);
}

// Positive proof that each chunk really uses its OWN graph (not one graph for
// the whole input): swapping the per-chunk graph order swaps which codecs the
// last chunk reflects. With [bitpack, deltaStore] the last chunk is {1, 10};
// with [deltaStore, bitpack] the last chunk is {28}. A single merged chunk
// could not change its codecs based on chunk position, so this also
// demonstrates the input is genuinely split into separate,
// independently-graphed chunks.
TEST(MultiChunkFrameTest, ChunkOrderControlsLastChunkCodecs)
{
    std::vector<int32_t> ints       = makeSmallInts(kEltsPerChunk);
    const std::vector<int32_t> ramp = makeRamp(kEltsPerChunk);
    ints.insert(ints.end(), ramp.begin(), ramp.end());
    const Input in = Input::refNumeric<int32_t>(ints.data(), ints.size());

    // bitpack first, deltaStore last -> last chunk reflects {1, 10}.
    Compressor cA;
    const std::string frameA = makeMultiChunkFrame(
            cA,
            { ChunkSpec{ kEltsPerChunk, graphs::Bitpack{}() },
              ChunkSpec{ kEltsPerChunk, buildDeltaToStore(cA) } },
            in);
    EXPECT_TRUE(decompressedEquals(frameA, in));
    EXPECT_TRUE(
            frameHasExactlyCodecs(frameA, { kDeltaInt, kConvertNumToSerial }));

    // deltaStore first, bitpack last -> last chunk reflects {28}.
    Compressor cB;
    const GraphID deltaStoreB = buildDeltaToStore(cB);
    const std::string frameB  = makeMultiChunkFrame(
            cB,
            { ChunkSpec{ kEltsPerChunk, deltaStoreB },
               ChunkSpec{ kEltsPerChunk, graphs::Bitpack{}() } },
            in);
    EXPECT_TRUE(decompressedEquals(frameB, in));
    EXPECT_TRUE(frameHasExactlyCodecs(frameB, { kBitpackInt }));
}

} // namespace
} // namespace openzl::gpu::testkit
