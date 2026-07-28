// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/pivco-huffman/gpu/pivco_block_index.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/codecs/pivco_huffman/common_pivco_kernel.h"
#include "openzl/codecs/pivco_huffman/decode_pivco_kernel.h"
#include "openzl/codecs/pivco_huffman/encode_pivco_kernel.h"
#include "openzl/zl_errors.h"

namespace {

struct Encoded {
    std::vector<uint8_t> bytes;
    size_t size;
};

Encoded encode(
        const std::vector<uint8_t>& weights,
        const std::vector<uint8_t>& data,
        size_t blockSize)
{
    const int tableLog =
            ZL_PivCoHuffman_computeTableLog(weights.data(), weights.size());
    EXPECT_GE(tableLog, 0);
    const size_t bound = ZL_PivCoHuffmanEncode_bound(data.size(), blockSize);
    EXPECT_NE(bound, SIZE_MAX);
    std::vector<uint8_t> encoded(bound);
    std::vector<uint8_t> scratch(
            ZL_PivCoHuffmanEncode_scratchElements(data.size(), blockSize));
    const size_t encodedSize = ZL_PivCoHuffman_encode(
            encoded.data(),
            encoded.size(),
            scratch.data(),
            scratch.size(),
            weights.data(),
            weights.size(),
            tableLog,
            data.data(),
            data.size(),
            blockSize,
            &ZL_PivCoHuffmanEncode_generic);
    EXPECT_NE(encodedSize, SIZE_MAX);
    encoded.resize(encodedSize);
    return Encoded{ std::move(encoded), encodedSize };
}

std::vector<uint8_t> decodeSlice(
        const std::vector<uint8_t>& weights,
        const uint8_t* bitstream,
        size_t bitstreamSize,
        size_t decodedSize)
{
    std::vector<uint8_t> decoded(decodedSize);
    std::vector<uint8_t> scratch(
            ZL_PivCoHuffmanDecode_scratchBytes(decodedSize, decodedSize));
    EXPECT_TRUE(ZL_PivCoHuffman_decode(
            decoded.data(),
            decoded.size(),
            scratch.data(),
            scratch.size(),
            weights.data(),
            weights.size(),
            bitstream,
            bitstreamSize,
            decodedSize,
            &ZL_PivCoHuffmanDecode_generic));
    return decoded;
}

TEST(PivCoBlockIndexTest, EmptyInputReturnsSingleZeroOffset)
{
    uint64_t offset = 123;
    const ZL_Report report =
            pivcoFindBlockOffsets(&offset, 1, nullptr, 0, nullptr, 0, 0, 7);

    ASSERT_FALSE(ZL_isError(report));
    EXPECT_EQ(ZL_validResult(report), 1);
    EXPECT_EQ(offset, 0);
}

TEST(PivCoBlockIndexTest, ConstantInputAllowsRepeatedZeroOffsets)
{
    std::vector<uint8_t> weights(8);
    weights[7] = 1;
    const std::vector<uint8_t> data(20, 7);
    constexpr size_t kBlockSize = 7;

    const Encoded encoded = encode(weights, data, kBlockSize);
    ASSERT_EQ(encoded.size, 0);

    std::vector<uint64_t> offsets(4, 99);
    const ZL_Report report = pivcoFindBlockOffsets(
            offsets.data(),
            offsets.size(),
            weights.data(),
            weights.size(),
            encoded.bytes.data(),
            encoded.size,
            data.size(),
            kBlockSize);

    ASSERT_FALSE(ZL_isError(report));
    EXPECT_EQ(ZL_validResult(report), offsets.size());
    EXPECT_EQ(offsets, (std::vector<uint64_t>{ 0, 0, 0, 0 }));
}

TEST(PivCoBlockIndexTest, OffsetsBoundIndependentlyDecodableSlices)
{
    const std::vector<uint8_t> weights{ 1, 1 };
    const std::vector<uint8_t> data{ 0, 1, 1, 0, 1, 0, 0, 1, 1, 1,
                                     0, 0, 1, 0, 1, 0, 1, 1, 0 };
    constexpr size_t kBlockSize = 7;

    const Encoded encoded = encode(weights, data, kBlockSize);
    std::vector<uint64_t> offsets(4);
    const ZL_Report report = pivcoFindBlockOffsets(
            offsets.data(),
            offsets.size(),
            weights.data(),
            weights.size(),
            encoded.bytes.data(),
            encoded.size,
            data.size(),
            kBlockSize);

    ASSERT_FALSE(ZL_isError(report));
    EXPECT_EQ(offsets.front(), 0);
    EXPECT_EQ(offsets.back(), encoded.size);
    EXPECT_TRUE(std::is_sorted(offsets.begin(), offsets.end()));

    for (size_t block = 0; block + 1 < offsets.size(); ++block) {
        const size_t decodedOffset = block * kBlockSize;
        const size_t decodedSize =
                std::min(kBlockSize, data.size() - decodedOffset);
        const auto decoded = decodeSlice(
                weights,
                encoded.bytes.data() + offsets[block],
                offsets[block + 1] - offsets[block],
                decodedSize);
        EXPECT_EQ(
                decoded,
                std::vector<uint8_t>(
                        data.begin() + decodedOffset,
                        data.begin() + decodedOffset + decodedSize));
    }
}

TEST(PivCoBlockIndexTest, RejectsInvalidWeights)
{
    const std::vector<uint8_t> weights{ 13 };
    uint64_t offsets[2]    = {};
    const ZL_Report report = pivcoFindBlockOffsets(
            offsets, 2, weights.data(), weights.size(), nullptr, 0, 1, 1);

    EXPECT_TRUE(ZL_isError(report));
}

TEST(PivCoBlockIndexTest, RejectsTruncatedBitstream)
{
    const std::vector<uint8_t> weights{ 1, 1 };
    const std::vector<uint8_t> data{ 0, 1, 1, 0, 1, 0, 0 };
    const Encoded encoded = encode(weights, data, data.size());
    ASSERT_GT(encoded.size, 0);

    uint64_t offsets[2]    = {};
    const ZL_Report report = pivcoFindBlockOffsets(
            offsets,
            2,
            weights.data(),
            weights.size(),
            encoded.bytes.data(),
            encoded.size - 1,
            data.size(),
            data.size());

    EXPECT_TRUE(ZL_isError(report));
}

TEST(PivCoBlockIndexTest, RejectsUnconsumedTrailingBytes)
{
    const std::vector<uint8_t> weights{ 1, 1 };
    const std::vector<uint8_t> data{ 0, 1, 1, 0, 1, 0, 0 };
    Encoded encoded = encode(weights, data, data.size());
    encoded.bytes.push_back(0);

    uint64_t offsets[2]    = {};
    const ZL_Report report = pivcoFindBlockOffsets(
            offsets,
            2,
            weights.data(),
            weights.size(),
            encoded.bytes.data(),
            encoded.bytes.size(),
            data.size(),
            data.size());

    EXPECT_TRUE(ZL_isError(report));
}

TEST(PivCoBlockIndexTest, RejectsBitmapCountMismatch)
{
    const std::vector<uint8_t> weights{ 2, 1, 1 };
    const std::vector<uint8_t> data{ 0, 1, 2, 0, 2, 1, 0 };
    Encoded encoded = encode(weights, data, data.size());
    ASSERT_GT(encoded.size, 0);

    encoded.bytes[0] ^= 1;

    uint64_t offsets[2]    = {};
    const ZL_Report report = pivcoFindBlockOffsets(
            offsets,
            2,
            weights.data(),
            weights.size(),
            encoded.bytes.data(),
            encoded.size,
            data.size(),
            data.size());

    EXPECT_TRUE(ZL_isError(report));
}

TEST(PivCoBlockIndexTest, RejectsTooSmallOffsetBuffer)
{
    const std::vector<uint8_t> weights{ 1, 1 };
    const std::vector<uint8_t> data{ 0, 1, 0, 1, 0, 1, 0, 1 };
    const Encoded encoded  = encode(weights, data, 3);
    uint64_t offsets[2]    = {};
    const ZL_Report report = pivcoFindBlockOffsets(
            offsets,
            2,
            weights.data(),
            weights.size(),
            encoded.bytes.data(),
            encoded.size,
            data.size(),
            3);

    EXPECT_TRUE(ZL_isError(report));
}

} // namespace
