// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "contrib/gpu/testkit/frame_factory.h"
#include "contrib/gpu/testkit/frame_verifier.h"
#include "openzl/codecs/zl_split.h"
#include "openzl/codecs/zl_tokenize.h"
#include "openzl/openzl.hpp"

// Expected signatures come from the standard transform table, not from
// implementation output:
//
//     delta_int               1
//     zigzag                  3
//     convert_num_to_serial  10   (STORE takes serial streams)
//     zstd                   22
//     bitpack_int            28
//     tokenize_numeric       37   (sorted numeric tokenize)
//     splitn                 40   (split of a serial stream)

namespace openzl::gpu::testkit {
namespace {

constexpr size_t kNumElts = 20000;

std::vector<int32_t> makeSmallInts()
{
    std::vector<int32_t> v(kNumElts);
    for (size_t i = 0; i < kNumElts; ++i) {
        v[i] = static_cast<int32_t>(i % 8);
    }
    return v;
}

std::string makeText()
{
    std::string text;
    while (text.size() < kNumElts) {
        text += "the quick brown fox jumps over the lazy dog. ";
    }
    return text.substr(0, kNumElts);
}

std::string compressSplit(
        const std::string& text,
        const std::vector<size_t>& segmentSizes,
        const std::vector<GraphID>& successors)
{
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    const GraphID split = ZL_Compressor_registerSplitGraph(
            compressor.get(),
            ZL_Type_serial,
            segmentSizes.data(),
            successors.data(),
            segmentSizes.size());
    return makeFrameWithGraph(
            compressor, split, Input::refSerial(text.data(), text.size()));
}

std::string compressStructSplit(
        const std::vector<size_t>& segmentSizes,
        const std::vector<GraphID>& successors)
{
    const std::string text = makeText();
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    const GraphID split = ZL_Compressor_registerSplitGraph(
            compressor.get(),
            ZL_Type_struct,
            segmentSizes.data(),
            successors.data(),
            segmentSizes.size());
    return makeFrameWithGraph(
            compressor,
            split,
            Input::refStruct(text.data(), 4, text.size() / 4));
}

std::string compressTokenized(GraphID alphabetGraph, GraphID indicesGraph)
{
    const std::vector<int32_t> values = makeSmallInts();
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    const GraphID tokenize = ZL_Compressor_registerTokenizeGraph(
            compressor.get(),
            ZL_Type_numeric,
            /*sort=*/true,
            alphabetGraph,
            indicesGraph);
    return makeFrameWithGraph(
            compressor,
            tokenize,
            Input::refNumeric<int32_t>(values.data(), values.size()));
}

TEST(FrameVerifierTest, signatureNamesEachCodecAndTheStreamItRegenerates)
{
    const std::vector<int32_t> ints = makeSmallInts();
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

    const std::string frame = makeFrameWithGraph(
            compressor,
            graphs::Bitpack{}(),
            Input::refNumeric<int32_t>(ints.data(), ints.size()));

    EXPECT_EQ(decodeGraphSignature(frame), "28:n4(b)");
}

TEST(FrameVerifierTest, signatureNestsEachCodecUnderTheCodecThatReadsIt)
{
    const std::vector<int32_t> ints = makeSmallInts();
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

    const std::string frame = makeFrameChainToStore(
            compressor,
            { ZL_NODE_ZIGZAG, ZL_NODE_DELTA_INT },
            Input::refNumeric<int32_t>(ints.data(), ints.size()));

    EXPECT_EQ(decodeGraphSignature(frame), "3:n4(1:n4(10:n4(b)))");
}

TEST(FrameVerifierTest, signatureOfAStoredStreamIsItsType)
{
    const std::string text = makeText();
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

    const std::string frame = makeFrameWithGraph(
            compressor,
            ZL_GRAPH_STORE,
            Input::refSerial(text.data(), text.size()));

    EXPECT_EQ(decodeGraphSignature(frame), "b");
}

TEST(FrameVerifierTest, signatureCountsSplitBlocksAndShapeDoesNot)
{
    const std::string text  = makeText();
    const std::string three = compressSplit(
            text,
            { 100, 100, 0 },
            { ZL_GRAPH_STORE, ZL_GRAPH_STORE, ZL_GRAPH_STORE });
    const std::string two =
            compressSplit(text, { 100, 0 }, { ZL_GRAPH_STORE, ZL_GRAPH_STORE });

    EXPECT_EQ(decodeGraphSignature(three), "40:b(b,b,b)");
    EXPECT_EQ(decodeGraphSignature(two), "40:b(b,b)");
    EXPECT_EQ(decodeGraphShape(three), "40:b(b)*");
    EXPECT_EQ(decodeGraphShape(two), "40:b(b)*");
}

TEST(FrameVerifierTest, shapeDropsStoredBlocksNextToCodedBlocks)
{
    const std::string text = makeText();

    const std::string frame = compressSplit(
            text,
            { 10000, 100, 0 },
            { ZL_GRAPH_ZSTD, ZL_GRAPH_STORE, ZL_GRAPH_ZSTD });

    EXPECT_EQ(decodeGraphSignature(frame), "40:b(22:b(b),b,22:b(b))");
    EXPECT_EQ(decodeGraphShape(frame), "40:b(22:b(b))*");
}

TEST(FrameVerifierTest, shapeDropsStoredStructBlocksNextToCodedBlocks)
{
    // STORE takes serial streams, so a stored struct block goes through
    // convert_struct_to_serial and still counts as stored.
    const std::string mixed = compressStructSplit(
            { 2500, 25, 0 }, { ZL_GRAPH_ZSTD, ZL_GRAPH_STORE, ZL_GRAPH_ZSTD });
    const std::string coded =
            compressStructSplit({ 2500, 0 }, { ZL_GRAPH_ZSTD, ZL_GRAPH_ZSTD });

    EXPECT_NE(decodeGraphSignature(mixed), decodeGraphSignature(coded));
    EXPECT_EQ(decodeGraphShape(mixed), decodeGraphShape(coded));
}

TEST(FrameVerifierTest, signatureListsOutputsInSlotOrder)
{
    // Tokenize writes the alphabet (8 int32 values) to output 0 and the
    // 1-byte indices to output 1.
    EXPECT_EQ(
            decodeGraphSignature(
                    compressTokenized(ZL_GRAPH_STORE, ZL_GRAPH_ZSTD)),
            "37:n4(10:n4(b),10:n1(22:b(b)))");
    EXPECT_EQ(
            decodeGraphSignature(
                    compressTokenized(ZL_GRAPH_ZSTD, ZL_GRAPH_STORE)),
            "37:n4(10:n4(22:b(b)),10:n1(b))");
}

TEST(FrameVerifierTest, signatureRejectsBytesThatAreNotAFrame)
{
    EXPECT_THROW(decodeGraphSignature("not a frame"), std::runtime_error);
}

} // namespace
} // namespace openzl::gpu::testkit
