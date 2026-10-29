// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "contrib/gpu/testkit/bytes.h"
#include "contrib/gpu/testkit/frame_verifier.h"
#include "contrib/gpu/testkit/nimble_graph.h"
#include "openzl/common/wire_format.h"
#include "openzl/openzl.hpp"
#include "openzl/zl_version.h"

// The selectors in Nimble's OpenZLCompressor.cpp decide which branch each
// input takes:
//   - range_pack runs when the value range needs fewer bytes than the element.
//   - tokenize runs when the estimated alphabet plus indices are smaller than
//     90% of the input, and the alphabet is under 64 KiB. Both costs use the
//     upper bound of the alphabet estimate, which is at least 20% above the
//     true count. Each index costs ceil(log2(upper bound)) bits.
//   - 1-byte elements go to zstd. Wider elements go to field_lz.

namespace openzl::gpu::testkit {
namespace {

// Deterministic full-width values. The C++ standard fixes the output of
// std::mt19937_64, so the bytes are the same with every standard library.
std::vector<uint64_t> randomValues(size_t count)
{
    std::mt19937_64 rng{ 1 };
    std::vector<uint64_t> values(count);
    for (uint64_t& value : values) {
        value = rng();
    }
    return values;
}

template <typename T>
std::string truncatedRandomBytes(size_t count)
{
    std::vector<T> values;
    values.reserve(count);
    for (const uint64_t value : randomValues(count)) {
        values.push_back(static_cast<T>(value >> 32));
    }
    return toLittleEndianBytes(values);
}

// A block of `blockElts` full-width values repeated until `count` elements.
// Purely random values do not compress, so OpenZL stores the chunk raw and no
// codec runs. Repeats give field_lz and zstd matches.
template <typename T>
std::string repeatedBlockBytes(size_t blockElts, size_t count)
{
    const std::vector<uint64_t> block = randomValues(blockElts);
    std::vector<T> values;
    values.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        values.push_back(static_cast<T>(block[i % blockElts] >> 32));
    }
    return toLittleEndianBytes(values);
}

std::string decompress(const std::string& frame)
{
    openzl::DCtx dctx;
    return dctx.decompressSerial(frame);
}

std::vector<uint32_t> allChunkCodecs(const std::string& frame)
{
    std::vector<uint32_t> codecs;
    for (const std::vector<uint32_t>& chunk : standardCodecsPerChunk(frame)) {
        codecs.insert(codecs.end(), chunk.begin(), chunk.end());
    }
    return codecs;
}

bool contains(const std::vector<uint32_t>& codecs, uint32_t codec)
{
    return std::find(codecs.begin(), codecs.end(), codec) != codecs.end();
}

TEST(NimbleGraphTest, writesRequestedFormatVersion)
{
    const std::string data = truncatedRandomBytes<uint32_t>(4096);
    // The default is 27, so the test asks for two other versions.
    for (const int version : { 25, 26 }) {
        const std::string frame = compressLikeNimble(
                data, NimbleNumericType::U32, { .formatVersion = version });
        EXPECT_EQ(
                ZL_validResult(ZL_getFormatVersionFromFrame(
                        frame.data(), frame.size())),
                static_cast<size_t>(version));
    }
}

TEST(NimbleGraphTest, highCardinalityValuesGoStraightToFieldLz)
{
    // Tokenize runs only when the alphabet estimate stays below 16384 u32
    // values (64 KiB), and range_pack runs when the range fits in fewer
    // bytes. 32768 distinct full-range values avoid both.
    const std::string data  = repeatedBlockBytes<uint32_t>(32768, 131072);
    const std::string frame = compressLikeNimble(data, NimbleNumericType::U32);
    const std::vector<uint32_t> codecs = allChunkCodecs(frame);

    EXPECT_TRUE(
            contains(codecs, ZL_StandardTransformID_convert_serial_to_num_le));
    EXPECT_TRUE(contains(codecs, ZL_StandardTransformID_field_lz));
    EXPECT_FALSE(contains(codecs, ZL_StandardTransformID_range_pack));
    EXPECT_FALSE(contains(codecs, ZL_StandardTransformID_tokenize_numeric))
            << ::testing::PrintToString(codecs);
}

TEST(NimbleGraphTest, narrowRangeUsesRangePack)
{
    // 10-bit spread above a large base: the range fits in 2 bytes, not 4.
    std::vector<uint32_t> values;
    for (const uint64_t value : randomValues(65536)) {
        values.push_back(3'000'000'000U + static_cast<uint32_t>(value >> 54));
    }

    const std::string data  = toLittleEndianBytes(values);
    const std::string frame = compressLikeNimble(data, NimbleNumericType::U32);

    EXPECT_TRUE(
            contains(allChunkCodecs(frame), ZL_StandardTransformID_range_pack));
}

TEST(NimbleGraphTest, lowCardinalityUsesTokenize)
{
    // 16 distinct values spread over the full 32-bit range, so range_pack
    // does not run first.
    const std::vector<uint64_t> alphabet = randomValues(16);
    std::vector<uint32_t> values;
    for (const uint64_t value : randomValues(65536)) {
        values.push_back(static_cast<uint32_t>(alphabet[value >> 60] >> 32));
    }

    const std::string data  = toLittleEndianBytes(values);
    const std::string frame = compressLikeNimble(data, NimbleNumericType::U32);
    const std::vector<uint32_t> codecs = allChunkCodecs(frame);

    EXPECT_TRUE(contains(codecs, ZL_StandardTransformID_tokenize_numeric));
    EXPECT_FALSE(contains(codecs, ZL_StandardTransformID_range_pack));
}

TEST(NimbleGraphTest, singleByteElementsUseZstd)
{
    const std::string data  = repeatedBlockBytes<uint8_t>(1024, 65536);
    const std::string frame = compressLikeNimble(data, NimbleNumericType::U8);
    const std::vector<uint32_t> codecs = allChunkCodecs(frame);

    EXPECT_TRUE(contains(codecs, ZL_StandardTransformID_zstd));
    EXPECT_FALSE(contains(codecs, ZL_StandardTransformID_field_lz));
}

TEST(NimbleGraphTest, splitsInputsLargerThan16MiBIntoChunks)
{
    constexpr size_t kChunkTargetBytes = 16 << 20;
    const std::string data =
            truncatedRandomBytes<uint32_t>(kChunkTargetBytes / 4 + 1024);

    const std::string frame = compressLikeNimble(data, NimbleNumericType::U32);

    EXPECT_EQ(standardCodecsPerChunk(frame).size(), 2U);
    EXPECT_EQ(decompress(frame), data);
}

TEST(NimbleGraphTest, floatGraphTokenizesOnlyWhenTokenizeSavesMore)
{
    // About 2000 distinct values in a 16-bit range: range_pack narrows them to
    // 2 bytes, and 12-bit indices then cost about 79% of the values. The u32
    // graph tokenizes below 90%, and the float graph only below 70%.
    std::vector<uint32_t> values;
    for (const uint64_t value : randomValues(65536)) {
        values.push_back(
                0x3F800000U + 32 * static_cast<uint32_t>((value >> 32) % 2000));
    }
    const std::string data = toLittleEndianBytes(values);

    const std::vector<uint32_t> asU32 =
            allChunkCodecs(compressLikeNimble(data, NimbleNumericType::U32));
    const std::vector<uint32_t> asF32 =
            allChunkCodecs(compressLikeNimble(data, NimbleNumericType::F32));

    EXPECT_TRUE(contains(asU32, ZL_StandardTransformID_range_pack));
    EXPECT_TRUE(contains(asU32, ZL_StandardTransformID_tokenize_numeric));
    EXPECT_TRUE(contains(asF32, ZL_StandardTransformID_range_pack));
    EXPECT_FALSE(contains(asF32, ZL_StandardTransformID_tokenize_numeric));
}

TEST(NimbleGraphTest, floatGraphTokenizesWhenTokenizeSavesEnough)
{
    // 16 distinct values: 4-bit indices cost about 25% of the 2-byte values,
    // below the float graph's 70%.
    std::vector<uint32_t> values;
    for (const uint64_t value : randomValues(65536)) {
        values.push_back(0x3F800000U + 32 * static_cast<uint32_t>(value >> 60));
    }

    const std::vector<uint32_t> codecs = allChunkCodecs(compressLikeNimble(
            toLittleEndianBytes(values), NimbleNumericType::F32));

    EXPECT_TRUE(contains(codecs, ZL_StandardTransformID_tokenize_numeric));
}

} // namespace
} // namespace openzl::gpu::testkit
