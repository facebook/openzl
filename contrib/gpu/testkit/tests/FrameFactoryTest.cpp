// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "openzl/codecs/bitSplit/encode_bitSplit_binding.h"
#include "openzl/dev/contrib/gpu/testkit/frame_factory.h"
#include "openzl/dev/contrib/gpu/testkit/frame_verifier.h"
#include "openzl/openzl.hpp"

// These tests mint real OpenZL frames with hand-picked codec graphs and then
// confirm, via the reflection API, that the frames contain exactly the codecs
// we asked for. Expected wire IDs are reasoned from the OpenZL standard
// transform table, NOT copied from implementation output:
//
//     bitpack_serial    27   (bitpack of a serial/byte stream)
//     bitpack_int       28   (bitpack of a numeric stream)
//     float_deconstruct 33   (split float32 into sign+fraction and exponent)
//
// Float32Deconstruct produces two outputs: sign+fraction (Struct) and
// exponent (Serial). So routing the exponent through Bitpack yields the
// SERIAL variant of bitpack (27), not the numeric one (28).

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

// A structured float ramp: correlated exponents give float_deconstruct
// something real to separate.
std::vector<float> makeFloats(size_t n)
{
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i) {
        v[i] = static_cast<float>(i % 100) * 0.5f + 1.0f;
    }
    return v;
}

constexpr uint32_t kDeltaInt         = 1;
constexpr uint32_t kZigzag           = 3;
constexpr uint32_t kBitpackSerial    = 27;
constexpr uint32_t kBitpackInt       = 28;
constexpr uint32_t kFloatDeconstruct = 33;

constexpr size_t kNumElts = 20000;

bool contains(const std::vector<uint32_t>& ids, uint32_t id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// Bitpack of a small-int numeric stream -> the frame must round-trip and the
// only standard codec must be bitpack_int (28).
TEST(FrameFactoryTest, BitpackOnSmallIntsYieldsBitpackInt)
{
    const std::vector<int32_t> ints = makeSmallInts(kNumElts);
    const Input in = Input::refNumeric<int32_t>(ints.data(), ints.size());

    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    const GraphID bitpack = graphs::Bitpack{}();

    EXPECT_TRUE(roundTrips(compressor, bitpack, in));

    const std::string frame = makeFrameWithGraph(compressor, bitpack, in);
    EXPECT_TRUE(frameHasExactlyCodecs(frame, { kBitpackInt }));
}

// float_deconstruct routed to the generic backend -> the deconstruct codec
// (33) must be present and the frame must round-trip. The generic backend may
// add further codecs, so we assert presence, not exact set.
TEST(FrameFactoryTest, FloatDeconstructToGenericRetainsCodec33)
{
    const std::vector<float> floats = makeFloats(kNumElts);
    const Input in = Input::refNumeric<float>(floats.data(), floats.size());

    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    const GraphID graph = nodes::Float32Deconstruct{}(
            compressor, ZL_GRAPH_COMPRESS_GENERIC, ZL_GRAPH_COMPRESS_GENERIC);

    EXPECT_TRUE(roundTrips(compressor, graph, in));

    const std::string frame = makeFrameWithGraph(compressor, graph, in);
    EXPECT_TRUE(contains(standardCodecsInFrame(frame), kFloatDeconstruct));
}

TEST(FrameFactoryTest, BitSplitCanExpandBeyondGuardedCompressBound)
{
    // This test verifies makeFrameWithGraph() can mint valid expanding
    // fixtures after disabling StoreOnExpansion; it fails if the helper uses
    // the guarded ZL_compressBound() premise from the string-returning CCtx
    // overload.
    std::vector<uint32_t> ints(kNumElts);
    for (size_t i = 0; i < ints.size(); ++i) {
        ints[i] = static_cast<uint32_t>(i);
    }
    const Input in = Input::refNumeric<uint32_t>(ints.data(), ints.size());

    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    const uint8_t bitWidths[] = { 4, 4, 4, 4, 4, 4, 4, 4 };
    const NodeID bitSplitNode = ZL_Compressor_registerBitSplitNode(
            compressor.get(),
            bitWidths,
            sizeof(bitWidths) / sizeof(*bitWidths));
    ASSERT_NE(bitSplitNode.nid, ZL_NODE_ILLEGAL.nid);
    const GraphID graph =
            compressor.buildStaticGraph(bitSplitNode, { graphs::Store{}() });
    const std::string frame = makeFrameWithGraph(compressor, graph, in);

    EXPECT_TRUE(decompressedEquals(frame, in));
}

// float_deconstruct -> {generic (sign+frac, Struct), bitpack (exponent,
// Serial)} -> both deconstruct (33) and bitpack_serial (27) must be present;
// frame round-trips. Exercises the convenience successor builder.
TEST(FrameFactoryTest, FloatDeconstructWithBitpackExponentHasBoth)
{
    const std::vector<float> floats = makeFloats(kNumElts);
    const Input in = Input::refNumeric<float>(floats.data(), floats.size());

    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    const std::string frame = makeFrameNodeWithSuccessors(
            compressor,
            nodes::Float32Deconstruct::node,
            { ZL_GRAPH_COMPRESS_GENERIC, graphs::Bitpack{}() },
            in);

    const std::vector<uint32_t> codecs = standardCodecsInFrame(frame);
    EXPECT_TRUE(contains(codecs, kFloatDeconstruct));
    EXPECT_TRUE(contains(codecs, kBitpackSerial));
}

// KEY EXPERIMENT: float_deconstruct -> {store, store} with the store-backup
// defeated (StoreOnExpansion=disable + MinStreamSize=-1, applied by the
// factory). Question: does codec 33 survive when both outputs just go to STORE?
//
// FINDING (asserted below): codec 33 SURVIVES -- disabling the backup is enough
// to keep the structural transform from being collapsed into a single raw
// STORE. But the frame is NOT a single-codec frame: float_deconstruct's
// sign+fraction output is Struct-typed, and OpenZL must insert a
// convert_struct_to_serial (id 6) codec to serialize it before STORE. So the
// frame contains exactly {6, 33}, not {33} alone.
//
// Consequence for GPU isolation testing: a node -> {store, store} graph does
// NOT, in general, mint a pure single-codec frame; any non-serial output drags
// in a conversion codec. The float_deconstruct codec itself is preserved, which
// is the part that matters for fixture generation.
//
// (Independent reasoning for the expected IDs: 33 = float_deconstruct, the node
// we asked for; 6 = convert_struct_to_serial, forced by the Struct-typed
// sign+fraction output meeting STORE. Both are read from the standard transform
// table, not from this test's own output.)
constexpr uint32_t kConvertStructToSerial = 6;

TEST(FrameFactoryTest, KeyExperiment_FloatDeconstructToStoreRetainsCodec33)
{
    const std::vector<float> floats = makeFloats(kNumElts);
    const Input in = Input::refNumeric<float>(floats.data(), floats.size());

    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    const std::string frame = makeFrameNodeToStore(
            compressor, nodes::Float32Deconstruct::node, in);

    // The frame must still be a valid, decodable frame.
    EXPECT_TRUE(decompressedEquals(frame, in));

    // The core finding: float_deconstruct (33) is NOT backed up away.
    EXPECT_TRUE(contains(standardCodecsInFrame(frame), kFloatDeconstruct));

    // The full truth: the store-everything frame is {6, 33}, not pure {33}, --
    // a convert_struct_to_serial is inserted for the Struct-typed output.
    EXPECT_TRUE(frameHasExactlyCodecs(
            frame, { kConvertStructToSerial, kFloatDeconstruct }));
}

// Chain builder: zigzag -> delta -> store on signed ints. Both single-output
// numeric transforms must appear in the frame (zigzag = 3, delta_int = 1) and
// the frame must round-trip. (A num->serial conversion is also inserted before
// STORE, so we assert presence of the chain codecs rather than an exact set.)
TEST(FrameFactoryTest, ChainZigzagDeltaToStoreRetainsBothCodecs)
{
    // Signed values with sign changes so zigzag/delta have real work to do.
    std::vector<int32_t> ints(kNumElts);
    for (size_t i = 0; i < ints.size(); ++i) {
        ints[i] = static_cast<int32_t>((i % 16)) - 8;
    }
    const Input in = Input::refNumeric<int32_t>(ints.data(), ints.size());

    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    const std::string frame = makeFrameChainToStore(
            compressor, { nodes::Zigzag::node, nodes::DeltaInt::node }, in);

    EXPECT_TRUE(decompressedEquals(frame, in));

    const std::vector<uint32_t> codecs = standardCodecsInFrame(frame);
    EXPECT_TRUE(contains(codecs, kZigzag));
    EXPECT_TRUE(contains(codecs, kDeltaInt));
}

} // namespace
} // namespace openzl::gpu::testkit
