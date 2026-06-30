// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/codecs/pivco_huffman/arch/common_pivco_arch.h"
#include "openzl/codecs/pivco_huffman/arch/decode_pivco_arch.h"
#include "openzl/codecs/pivco_huffman/arch/encode_pivco_arch.h"

namespace {

struct EncodeArch {
    const char* name;
    const ZL_PivCoHuffmanEncode* kernels;
};

struct DecodeArch {
    const char* name;
    const ZL_PivCoHuffmanDecode* kernels;
};

std::vector<EncodeArch> supportedEncodeArchs()
{
    ZL_cpuid_t const cpuid = ZL_cpuid();
    std::vector<EncodeArch> out;
    std::vector<EncodeArch> const archs = {
        { "generic", &ZL_PivCoHuffmanEncode_generic },
    };
    for (auto const& arch : archs) {
        if (arch.kernels->supported(&cpuid)) {
            out.push_back(arch);
        }
    }
    return out;
}

std::vector<DecodeArch> supportedDecodeArchs()
{
    ZL_cpuid_t const cpuid = ZL_cpuid();
    std::vector<DecodeArch> out;
    std::vector<DecodeArch> const archs = {
        { "generic", &ZL_PivCoHuffmanDecode_generic },
    };
    for (auto const& arch : archs) {
        if (arch.kernels->supported(&cpuid)) {
            out.push_back(arch);
        }
    }
    return out;
}

template <typename T>
std::vector<T> firstN(const std::vector<T>& in, size_t n)
{
    return std::vector<T>(in.begin(), in.begin() + n);
}

size_t bitmapBytes(size_t bits)
{
    return (bits + 7) / 8;
}

void setBitmapBit(std::vector<uint8_t>& bitmap, size_t bit)
{
    bitmap[bit / 8] |= (uint8_t)(1u << (bit & 7));
}

enum class TopBitPattern {
    AllZero,
    AllOne,
    Mixed,
};

uint8_t makeRank(size_t i, size_t size, TopBitPattern pattern)
{
    switch (pattern) {
        case TopBitPattern::AllZero:
            return 0x10;
        case TopBitPattern::AllOne:
            return 0x90;
        case TopBitPattern::Mixed:
            return (((i * 37) + size) % 7) < 3 ? 0x90 : 0x10;
    }
    return 0;
}

std::vector<uint8_t> makeRanks(size_t size, TopBitPattern pattern)
{
    std::vector<uint8_t> ranks(size + ZL_PIVCO_HUFFMAN_SLOP, 0xA5);
    for (size_t i = 0; i < size; ++i) {
        ranks[i] = makeRank(i, size, pattern);
    }
    return ranks;
}

std::vector<uint8_t>
makeFlatDepthRanks(size_t size, size_t depth, uint8_t rankBegin)
{
    std::vector<uint8_t> ranks(size + ZL_PIVCO_HUFFMAN_SLOP, 0xA5);
    uint8_t const symbolMask = (uint8_t)((1u << depth) - 1u);
    for (size_t i = 0; i < size; ++i) {
        ranks[i] = (uint8_t)(rankBegin + (uint8_t)(i & symbolMask));
    }
    return ranks;
}

struct PartitionReference {
    std::vector<uint8_t> bitmap;
    std::vector<uint8_t> lhs;
    std::vector<uint8_t> rhs;
};

PartitionReference referencePartition(
        const std::vector<uint8_t>& ranks,
        size_t size,
        uint8_t rightRank)
{
    PartitionReference out;
    out.bitmap.assign(bitmapBytes(size), 0);
    for (size_t i = 0; i < size; ++i) {
        uint8_t const rank = ranks[i];
        if (rank >= rightRank) {
            setBitmapBit(out.bitmap, i);
            out.rhs.push_back(rank);
        } else {
            out.lhs.push_back(rank);
        }
    }
    return out;
}

std::vector<uint8_t> referencePackFlatDepth(
        const std::vector<uint8_t>& ranks,
        size_t size,
        size_t depth,
        uint8_t rankBegin)
{
    std::vector<uint8_t> bitmap(bitmapBytes(size * depth), 0);
    for (size_t i = 0; i < size; ++i) {
        uint8_t const idx = (uint8_t)(ranks[i] - rankBegin);
        for (size_t bit = 0; bit < depth; ++bit) {
            if ((idx & (1u << bit)) != 0) {
                setBitmapBit(bitmap, i * depth + bit);
            }
        }
    }
    return bitmap;
}

std::vector<uint8_t> referenceMergeFlatDepth(
        const std::vector<uint8_t>& bitmap,
        size_t outSize,
        size_t depth,
        const std::vector<uint8_t>& symbols)
{
    std::vector<uint8_t> out(outSize);
    uint8_t const mask = (uint8_t)((1u << depth) - 1);
    for (size_t i = 0; i < outSize; ++i) {
        size_t const bitOffset = i * depth;
        size_t const byte      = bitOffset / 8;
        size_t const shift     = bitOffset & 7;
        uint16_t bits          = bitmap[byte];
        if (byte + 1 < bitmap.size()) {
            bits |= (uint16_t)(bitmap[byte + 1] << 8);
        }
        out[i] = symbols[(bits >> shift) & mask];
    }
    return out;
}

std::vector<size_t> boundarySizes()
{
    return {
        0,  1,  2,  3,  4,   5,   7,   8,   9,   15,  16,  17,  31,  32,
        33, 63, 64, 65, 127, 128, 129, 255, 256, 257, 511, 512, 513,
    };
}

std::vector<bool> makeMergeBits(size_t size, TopBitPattern pattern)
{
    std::vector<bool> bits(size);
    for (size_t i = 0; i < size; ++i) {
        switch (pattern) {
            case TopBitPattern::AllZero:
                bits[i] = false;
                break;
            case TopBitPattern::AllOne:
                bits[i] = true;
                break;
            case TopBitPattern::Mixed:
                bits[i] = (((i * 11) + size) % 5) < 2;
                break;
        }
    }
    return bits;
}

std::vector<uint8_t> bitmapFromBits(
        const std::vector<bool>& bits,
        size_t extraCapacity)
{
    size_t const exactBytes = bitmapBytes(bits.size());
    std::vector<uint8_t> bitmap(exactBytes + extraCapacity, 0);
    for (size_t i = 0; i < bits.size(); ++i) {
        if (bits[i]) {
            setBitmapBit(bitmap, i);
        }
    }
    for (size_t i = exactBytes; i < bitmap.size(); ++i) {
        bitmap[i] = 0xA5;
    }
    return bitmap;
}

struct MergeInputs {
    std::vector<uint8_t> expected;
    std::vector<uint8_t> lhs;
    std::vector<uint8_t> rhs;
    size_t ones = 0;
};

MergeInputs makeMergeInputs(const std::vector<bool>& bits)
{
    MergeInputs out;
    out.expected.reserve(bits.size());
    for (size_t i = 0; i < bits.size(); ++i) {
        if (bits[i]) {
            uint8_t const value =
                    (uint8_t)(0x80u + ((out.rhs.size() * 13) & 0x7F));
            out.rhs.push_back(value);
            out.expected.push_back(value);
            ++out.ones;
        } else {
            uint8_t const value =
                    (uint8_t)(0x10u + ((out.lhs.size() * 17) & 0x6F));
            out.lhs.push_back(value);
            out.expected.push_back(value);
        }
    }
    out.lhs.resize(out.lhs.size() + ZL_PIVCO_HUFFMAN_SLOP, 0xEE);
    out.rhs.resize(out.rhs.size() + ZL_PIVCO_HUFFMAN_SLOP, 0xDD);
    return out;
}

void expectBitmapPrefix(
        const std::vector<uint8_t>& actual,
        const std::vector<uint8_t>& expected)
{
    ASSERT_LE(expected.size(), actual.size());
    EXPECT_EQ(firstN(actual, expected.size()), expected);
}

void expectBytesAfterPrefix(
        const std::vector<uint8_t>& actual,
        size_t prefixSize,
        uint8_t sentinel)
{
    ASSERT_LE(prefixSize, actual.size());
    std::vector<uint8_t> const actualTail(
            actual.begin() + prefixSize, actual.end());
    std::vector<uint8_t> const expectedTail(actualTail.size(), sentinel);
    EXPECT_EQ(actualTail, expectedTail);
}

} // namespace

TEST(PivCoHuffmanArchTest, PartitionKernelsMatchReference)
{
    for (auto const& arch : supportedEncodeArchs()) {
        ASSERT_NE(arch.kernels->partitionFull, nullptr) << arch.name;
        ASSERT_NE(arch.kernels->partitionLeft, nullptr) << arch.name;
        ASSERT_NE(arch.kernels->partitionRight, nullptr) << arch.name;
        ASSERT_NE(arch.kernels->partitionNone, nullptr) << arch.name;

        for (TopBitPattern pattern : { TopBitPattern::AllZero,
                                       TopBitPattern::AllOne,
                                       TopBitPattern::Mixed }) {
            for (size_t size : boundarySizes()) {
                SCOPED_TRACE(
                        std::string(arch.name)
                        + " size=" + std::to_string(size));
                uint8_t const rightRank = 0x80;
                auto const ranks        = makeRanks(size, pattern);
                auto const ref     = referencePartition(ranks, size, rightRank);
                size_t const bytes = bitmapBytes(size);

                std::vector<uint8_t> bitmap(
                        bytes + ZL_PIVCO_HUFFMAN_SLOP, 0xCC);
                std::vector<uint8_t> lhs(size + ZL_PIVCO_HUFFMAN_SLOP, 0xEE);
                std::vector<uint8_t> rhs(size + ZL_PIVCO_HUFFMAN_SLOP, 0xDD);
                size_t const ones = arch.kernels->partitionFull(
                        bitmap.data(),
                        lhs.data(),
                        rhs.data(),
                        ranks.data(),
                        size,
                        rightRank);
                EXPECT_EQ(ones, ref.rhs.size());
                expectBitmapPrefix(bitmap, ref.bitmap);
                EXPECT_EQ(firstN(lhs, ref.lhs.size()), ref.lhs);
                EXPECT_EQ(firstN(rhs, ref.rhs.size()), ref.rhs);

                std::fill(bitmap.begin(), bitmap.end(), 0xCC);
                std::fill(lhs.begin(), lhs.end(), 0xEE);
                EXPECT_EQ(
                        arch.kernels->partitionLeft(
                                bitmap.data(),
                                lhs.data(),
                                ranks.data(),
                                size,
                                rightRank),
                        ref.rhs.size());
                expectBitmapPrefix(bitmap, ref.bitmap);
                EXPECT_EQ(firstN(lhs, ref.lhs.size()), ref.lhs);

                std::fill(bitmap.begin(), bitmap.end(), 0xCC);
                std::fill(rhs.begin(), rhs.end(), 0xDD);
                EXPECT_EQ(
                        arch.kernels->partitionRight(
                                bitmap.data(),
                                rhs.data(),
                                ranks.data(),
                                size,
                                rightRank),
                        ref.rhs.size());
                expectBitmapPrefix(bitmap, ref.bitmap);
                EXPECT_EQ(firstN(rhs, ref.rhs.size()), ref.rhs);

                std::fill(bitmap.begin(), bitmap.end(), 0xCC);
                arch.kernels->partitionNone(
                        bitmap.data(), ranks.data(), size, rightRank);
                expectBitmapPrefix(bitmap, ref.bitmap);
            }
        }
    }
}

TEST(PivCoHuffmanArchTest, PartitionFullSupportsDocumentedInputAliasing)
{
    for (auto const& arch : supportedEncodeArchs()) {
        for (TopBitPattern pattern : { TopBitPattern::AllZero,
                                       TopBitPattern::AllOne,
                                       TopBitPattern::Mixed }) {
            for (size_t size : boundarySizes()) {
                SCOPED_TRACE(
                        std::string(arch.name)
                        + " size=" + std::to_string(size));
                uint8_t const rightRank = 0x80;
                auto const ranks        = makeRanks(size, pattern);
                auto const ref     = referencePartition(ranks, size, rightRank);
                size_t const bytes = bitmapBytes(size);

                std::vector<uint8_t> bitmap(
                        bytes + ZL_PIVCO_HUFFMAN_SLOP, 0xCC);
                std::vector<uint8_t> lhsAlias = ranks;
                std::vector<uint8_t> rhs(size + ZL_PIVCO_HUFFMAN_SLOP, 0xDD);
                EXPECT_EQ(
                        arch.kernels->partitionFull(
                                bitmap.data(),
                                lhsAlias.data(),
                                rhs.data(),
                                lhsAlias.data(),
                                size,
                                rightRank),
                        ref.rhs.size());
                expectBitmapPrefix(bitmap, ref.bitmap);
                EXPECT_EQ(firstN(lhsAlias, ref.lhs.size()), ref.lhs);
                EXPECT_EQ(firstN(rhs, ref.rhs.size()), ref.rhs);

                std::fill(bitmap.begin(), bitmap.end(), 0xCC);
                std::vector<uint8_t> lhs(size + ZL_PIVCO_HUFFMAN_SLOP, 0xEE);
                std::vector<uint8_t> rhsAlias = ranks;
                EXPECT_EQ(
                        arch.kernels->partitionFull(
                                bitmap.data(),
                                lhs.data(),
                                rhsAlias.data(),
                                rhsAlias.data(),
                                size,
                                rightRank),
                        ref.rhs.size());
                expectBitmapPrefix(bitmap, ref.bitmap);
                EXPECT_EQ(firstN(lhs, ref.lhs.size()), ref.lhs);
                EXPECT_EQ(firstN(rhsAlias, ref.rhs.size()), ref.rhs);
            }
        }
    }
}

TEST(PivCoHuffmanArchTest, FlatDepthPackAndMergeMatchReference)
{
    auto const sizes = boundarySizes();
    for (size_t depth = 1; depth <= 8; ++depth) {
        std::vector<uint8_t> symbols((size_t)1 << depth);
        for (size_t i = 0; i < symbols.size(); ++i) {
            symbols[i] = (uint8_t)((i * 37 + depth * 11) & 0xFF);
        }

        for (size_t size : sizes) {
            uint8_t const rankBegin = depth == 8 ? 0 : 17;
            auto const ranks = makeFlatDepthRanks(size, depth, rankBegin);
            auto const packed =
                    referencePackFlatDepth(ranks, size, depth, rankBegin);
            auto const merged =
                    referenceMergeFlatDepth(packed, size, depth, symbols);

            for (auto const& arch : supportedEncodeArchs()) {
                ASSERT_NE(arch.kernels->packFlatDepth, nullptr) << arch.name;
                SCOPED_TRACE(
                        std::string("pack ") + arch.name
                        + " depth=" + std::to_string(depth)
                        + " size=" + std::to_string(size));
                size_t const guardOffset =
                        packed.size() + ZL_PIVCO_HUFFMAN_SLOP;
                std::vector<uint8_t> bitmap(
                        guardOffset + ZL_PIVCO_HUFFMAN_SLOP, 0xCC);
                arch.kernels->packFlatDepth(
                        bitmap.data(), depth, ranks.data(), size, rankBegin);
                expectBitmapPrefix(bitmap, packed);
                expectBytesAfterPrefix(bitmap, guardOffset, 0xCC);
            }

            for (auto const& arch : supportedDecodeArchs()) {
                ASSERT_NE(arch.kernels->mergeFlatDepth, nullptr) << arch.name;
                SCOPED_TRACE(
                        std::string("merge ") + arch.name
                        + " depth=" + std::to_string(depth)
                        + " size=" + std::to_string(size));
                std::vector<uint8_t> out(size + ZL_PIVCO_HUFFMAN_SLOP, 0xCC);
                arch.kernels->mergeFlatDepth(
                        out.data(),
                        size,
                        out.size(),
                        packed.data(),
                        packed.size(),
                        depth,
                        symbols.data());
                EXPECT_EQ(firstN(out, size), merged);
            }
        }
    }
}

TEST(PivCoHuffmanArchTest, MergeKernelsMatchReference)
{
    for (auto const& arch : supportedDecodeArchs()) {
        ASSERT_NE(arch.kernels->mergeVectorVector, nullptr) << arch.name;
        ASSERT_NE(arch.kernels->mergeConstantVector, nullptr) << arch.name;
        ASSERT_NE(arch.kernels->mergeVectorConstant, nullptr) << arch.name;

        for (TopBitPattern pattern : { TopBitPattern::AllZero,
                                       TopBitPattern::AllOne,
                                       TopBitPattern::Mixed }) {
            for (size_t size : boundarySizes()) {
                auto const bits   = makeMergeBits(size, pattern);
                auto const inputs = makeMergeInputs(bits);

                // The expected outputs depend only on `bits`/`inputs`, so build
                // them once here rather than inside the capacity loops below.
                uint8_t const lhsConstant = 0x3C;
                uint8_t const rhsConstant = 0xC3;
                std::vector<uint8_t> expectedConstantVector(size);
                std::vector<uint8_t> expectedVectorConstant(size);
                for (size_t i = 0, lhsPos = 0, rhsPos = 0; i < size; ++i) {
                    expectedConstantVector[i] =
                            bits[i] ? inputs.rhs[rhsPos++] : lhsConstant;
                    expectedVectorConstant[i] =
                            bits[i] ? rhsConstant : inputs.lhs[lhsPos++];
                }

                for (size_t outExtra :
                     { (size_t)0, (size_t)ZL_PIVCO_HUFFMAN_SLOP }) {
                    for (size_t bitmapExtra :
                         { (size_t)0, (size_t)ZL_PIVCO_HUFFMAN_SLOP }) {
                        SCOPED_TRACE(
                                std::string(arch.name)
                                + " size=" + std::to_string(size) + " outExtra="
                                + std::to_string(outExtra) + " bitmapExtra="
                                + std::to_string(bitmapExtra));
                        auto const bitmap = bitmapFromBits(bits, bitmapExtra);

                        std::vector<uint8_t> out(size + outExtra, 0xCC);
                        EXPECT_EQ(
                                arch.kernels->mergeVectorVector(
                                        out.data(),
                                        out.size(),
                                        bitmap.data(),
                                        bitmap.size(),
                                        inputs.lhs.data(),
                                        inputs.lhs.size()
                                                - ZL_PIVCO_HUFFMAN_SLOP,
                                        inputs.rhs.data(),
                                        inputs.rhs.size()
                                                - ZL_PIVCO_HUFFMAN_SLOP),
                                inputs.ones);
                        EXPECT_EQ(firstN(out, size), inputs.expected);

                        std::fill(out.begin(), out.end(), 0xCC);
                        EXPECT_EQ(
                                arch.kernels->mergeConstantVector(
                                        out.data(),
                                        out.size(),
                                        bitmap.data(),
                                        bitmap.size(),
                                        lhsConstant,
                                        inputs.lhs.size()
                                                - ZL_PIVCO_HUFFMAN_SLOP,
                                        inputs.rhs.data(),
                                        inputs.rhs.size()
                                                - ZL_PIVCO_HUFFMAN_SLOP),
                                inputs.ones);
                        EXPECT_EQ(firstN(out, size), expectedConstantVector);

                        std::fill(out.begin(), out.end(), 0xCC);
                        EXPECT_EQ(
                                arch.kernels->mergeVectorConstant(
                                        out.data(),
                                        out.size(),
                                        bitmap.data(),
                                        bitmap.size(),
                                        inputs.lhs.data(),
                                        inputs.lhs.size()
                                                - ZL_PIVCO_HUFFMAN_SLOP,
                                        rhsConstant,
                                        inputs.rhs.size()
                                                - ZL_PIVCO_HUFFMAN_SLOP),
                                inputs.ones);
                        EXPECT_EQ(firstN(out, size), expectedVectorConstant);
                    }
                }
            }
        }
    }
}
