// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "tests/utils.h"

#include "openzl/codecs/pivco_huffman/arch/common_pivco_arch.h"
#include "openzl/codecs/pivco_huffman/arch/decode_pivco_arch.h"
#include "openzl/codecs/pivco_huffman/arch/encode_pivco_arch.h"
#include "openzl/codecs/pivco_huffman/common_pivco_kernel.h"
#include "openzl/codecs/pivco_huffman/decode_pivco_kernel.h"
#include "openzl/codecs/pivco_huffman/encode_pivco_kernel.h"

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

struct HuffmanScenario {
    const char* name;
    std::vector<uint8_t> weights;
    std::vector<uint8_t> alphabet;
};

std::vector<uint8_t> makeWeights(
        size_t size,
        std::initializer_list<std::pair<size_t, uint8_t>> entries)
{
    std::vector<uint8_t> weights(size);
    for (auto const& entry : entries) {
        weights[entry.first] = entry.second;
    }
    return weights;
}

std::vector<HuffmanScenario> huffmanScenarios()
{
    return {
        {
                "single",
                makeWeights(43, { { 42, 1 } }),
                { 42 },
        },
        {
                "two_equal",
                makeWeights(2, { { 0, 1 }, { 1, 1 } }),
                { 0, 1 },
        },
        {
                "short_plus_pair",
                makeWeights(3, { { 0, 2 }, { 1, 1 }, { 2, 1 } }),
                { 0, 1, 2 },
        },
        {
                "short_plus_flat4",
                makeWeights(
                        5,
                        { { 0, 3 }, { 1, 1 }, { 2, 1 }, { 3, 1 }, { 4, 1 } }),
                { 0, 1, 2, 3, 4 },
        },
        {
                "two_flat_children",
                makeWeights(
                        6,
                        { { 0, 3 },
                          { 1, 3 },
                          { 2, 2 },
                          { 3, 2 },
                          { 4, 2 },
                          { 5, 2 } }),
                { 0, 1, 2, 3, 4, 5 },
        },
    };
}

std::vector<uint8_t> makeHuffmanData(
        const std::vector<uint8_t>& alphabet,
        size_t size)
{
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = alphabet[((i * 37) + size) % alphabet.size()];
    }
    return data;
}

void expectPivCoRoundTrip(
        const HuffmanScenario& scenario,
        const std::vector<uint8_t>& data,
        const ZL_PivCoHuffmanEncode* encodeKernels,
        const ZL_PivCoHuffmanDecode* decodeKernels,
        size_t blockSize = ZL_PIVCO_DEFAULT_BLOCK_SIZE)
{
    size_t const encodeScratchElements =
            ZL_PivCoHuffmanEncode_scratchElements(data.size(), blockSize);
    size_t const decodeScratchBytes =
            ZL_PivCoHuffmanDecode_scratchBytes(data.size(), blockSize);
    std::vector<uint8_t> encodeScratch(encodeScratchElements);
    std::vector<uint8_t> encoded(
            ZL_PivCoHuffmanEncode_bound(data.size(), blockSize));
    int const tableLog = ZL_PivCoHuffman_computeTableLog(
            scenario.weights.data(), scenario.weights.size());
    ASSERT_GE(tableLog, 0);

    // A constant input encodes to an empty bitstream (encodedSize == 0); only
    // SIZE_MAX signals failure.
    size_t const encodedSize = ZL_PivCoHuffman_encode(
            encoded.data(),
            encoded.size(),
            encodeScratch.data(),
            encodeScratch.size(),
            scenario.weights.data(),
            scenario.weights.size(),
            tableLog,
            data.data(),
            data.size(),
            blockSize,
            encodeKernels);
    ASSERT_NE(encodedSize, SIZE_MAX);
    ASSERT_LE(encodedSize, encoded.size());
    encoded.resize(encodedSize);

    std::vector<uint8_t> decodeScratch(decodeScratchBytes);
    std::vector<uint8_t> decoded(data.size(), 0xCC);
    ASSERT_TRUE(ZL_PivCoHuffman_decode(
            decoded.data(),
            decoded.size(),
            decodeScratch.data(),
            decodeScratch.size(),
            scenario.weights.data(),
            scenario.weights.size(),
            encoded.data(),
            encoded.size(),
            blockSize,
            decodeKernels));
    EXPECT_EQ(decoded, data);
}

// Encodes @p data with the generic kernel and returns the encoded bitstream
// (resized to its exact length). Used by the decode-rejection tests, which need
// the raw encoded bytes to corrupt before decoding.
std::vector<uint8_t> encodePivCo(
        const HuffmanScenario& scenario,
        const std::vector<uint8_t>& data,
        size_t blockSize = ZL_PIVCO_DEFAULT_BLOCK_SIZE)
{
    std::vector<uint8_t> encodeScratch(
            ZL_PivCoHuffmanEncode_scratchElements(data.size(), blockSize));
    std::vector<uint8_t> encoded(
            ZL_PivCoHuffmanEncode_bound(data.size(), blockSize));
    int const tableLog = ZL_PivCoHuffman_computeTableLog(
            scenario.weights.data(), scenario.weights.size());
    EXPECT_GE(tableLog, 0);
    size_t const encodedSize = ZL_PivCoHuffman_encode(
            encoded.data(),
            encoded.size(),
            encodeScratch.data(),
            encodeScratch.size(),
            scenario.weights.data(),
            scenario.weights.size(),
            tableLog,
            data.data(),
            data.size(),
            blockSize,
            &ZL_PivCoHuffmanEncode_generic);
    EXPECT_NE(encodedSize, SIZE_MAX);
    encoded.resize(encodedSize == SIZE_MAX ? 0 : encodedSize);
    return encoded;
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

TEST(PivCoHuffmanKernelTest, RoundTripsAcrossKernelImplementations)
{
    auto const encodeArchs = supportedEncodeArchs();
    auto const decodeArchs = supportedDecodeArchs();
    for (auto const& scenario : huffmanScenarios()) {
        for (size_t size : { (size_t)1,
                             (size_t)2,
                             (size_t)3,
                             (size_t)7,
                             (size_t)63,
                             (size_t)128,
                             (size_t)257,
                             (size_t)4096 }) {
            auto const data = makeHuffmanData(scenario.alphabet, size);
            for (auto const& encodeArch : encodeArchs) {
                for (auto const& decodeArch : decodeArchs) {
                    SCOPED_TRACE(
                            std::string(scenario.name)
                            + " size=" + std::to_string(size) + " encode="
                            + encodeArch.name + " decode=" + decodeArch.name);
                    expectPivCoRoundTrip(
                            scenario,
                            data,
                            encodeArch.kernels,
                            decodeArch.kernels);
                }
            }
        }
    }
}

TEST(PivCoHuffmanKernelTest, RoundTripsLoremRepro)
{
    std::vector<uint8_t> const data(
            openzl::tests::kLoremTestInput.begin(),
            openzl::tests::kLoremTestInput.end());
    ASSERT_FALSE(data.empty());

    // Exact HUF weights the binding computes for kLoremTestInput (tableLog 10).
    HuffmanScenario scenario;
    scenario.name    = "lorem";
    scenario.weights = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 5, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 2, 0, 1, 1, 1, 1, 0, 0, 2, 0, 0, 1, 3, 3, 0, 2, 1, 0, 1,
        0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 4, 6, 5, 8, 4, 4, 4,
        7, 2, 0, 7, 6, 7, 6, 5, 5, 7, 7, 7, 7, 5, 0, 2,
    };
    ASSERT_EQ(
            ZL_PivCoHuffman_computeTableLog(
                    scenario.weights.data(), scenario.weights.size()),
            10);

    for (auto const& enc : supportedEncodeArchs()) {
        for (auto const& dec : supportedDecodeArchs()) {
            expectPivCoRoundTrip(scenario, data, enc.kernels, dec.kernels);
        }
    }
}

TEST(PivCoHuffmanKernelTest, RoundTripsLargeAlphabet)
{
    // Valid complete code: single leaves at lengths 4,5,6,7 plus 226 leaves at
    // length 8, giving tableLog == 8 and a large, deep tree.
    HuffmanScenario scenario;
    scenario.name           = "large_alphabet";
    size_t const numSymbols = 230;
    scenario.weights.assign(numSymbols, 1);
    scenario.weights[0] = 5;
    scenario.weights[1] = 4;
    scenario.weights[2] = 3;
    scenario.weights[3] = 2;
    for (size_t s = 0; s < numSymbols; ++s) {
        scenario.alphabet.push_back((uint8_t)s);
    }
    ASSERT_EQ(
            ZL_PivCoHuffman_computeTableLog(
                    scenario.weights.data(), scenario.weights.size()),
            8);

    for (size_t size : { (size_t)1000, (size_t)2758, (size_t)5000 }) {
        auto const data = makeHuffmanData(scenario.alphabet, size);
        for (auto const& enc : supportedEncodeArchs()) {
            for (auto const& dec : supportedDecodeArchs()) {
                expectPivCoRoundTrip(scenario, data, enc.kernels, dec.kernels);
            }
        }
    }
}

TEST(PivCoHuffmanKernelTest, RoundTripsMultipleBlocks)
{
    HuffmanScenario const scenario = huffmanScenarios().back();
    auto const data                = makeHuffmanData(
            scenario.alphabet, ZL_PIVCO_DEFAULT_BLOCK_SIZE + 123);

    expectPivCoRoundTrip(
            scenario,
            data,
            &ZL_PivCoHuffmanEncode_generic,
            &ZL_PivCoHuffmanDecode_generic);
}

TEST(PivCoHuffmanKernelTest, RoundTripsCustomBlockSize)
{
    // A small block size forces many blocks at a non-default size; the encoder
    // and decoder must agree on the block boundaries.
    HuffmanScenario const scenario = huffmanScenarios().back();
    for (size_t blockSize :
         { (size_t)1, (size_t)7, (size_t)64, (size_t)1000 }) {
        auto const data = makeHuffmanData(scenario.alphabet, 4096);
        for (auto const& enc : supportedEncodeArchs()) {
            for (auto const& dec : supportedDecodeArchs()) {
                SCOPED_TRACE(
                        std::string("blockSize=") + std::to_string(blockSize)
                        + " encode=" + enc.name + " decode=" + dec.name);
                expectPivCoRoundTrip(
                        scenario, data, enc.kernels, dec.kernels, blockSize);
            }
        }
    }
}

TEST(PivCoHuffmanKernelTest, BuildsSymbolRanksAndSplitRanks)
{
    {
        auto const weights = makeWeights(3, { { 0, 2 }, { 1, 1 }, { 2, 1 } });
        int const tableLog =
                ZL_PivCoHuffman_computeTableLog(weights.data(), weights.size());
        ASSERT_GE(tableLog, 0);

        ZL_PivCoHuffmanTree tree;
        ZL_PivCoHuffmanTree_build(
                &tree, weights.data(), weights.size(), tableLog);

        EXPECT_EQ((int)tree.symbolToRank[0], 0);
        EXPECT_EQ((int)tree.symbolToRank[1], 1);
        EXPECT_EQ((int)tree.symbolToRank[2], 2);
        EXPECT_EQ(tree.numRanks, 3);
        EXPECT_EQ(ZL_PivCoHuffmanTree_splitRank(&tree, 0, 0, tree.numRanks), 1);
        EXPECT_EQ(ZL_PivCoHuffmanTree_splitRank(&tree, 1, 1, tree.numRanks), 2);
    }

    {
        auto const weights = makeWeights(
                5, { { 0, 3 }, { 1, 1 }, { 2, 1 }, { 3, 1 }, { 4, 1 } });
        int const tableLog =
                ZL_PivCoHuffman_computeTableLog(weights.data(), weights.size());
        ASSERT_GE(tableLog, 0);

        ZL_PivCoHuffmanTree tree;
        ZL_PivCoHuffmanTree_build(
                &tree, weights.data(), weights.size(), tableLog);

        for (uint8_t symbol = 0; symbol < weights.size(); ++symbol) {
            EXPECT_EQ((int)tree.symbolToRank[symbol], (int)symbol);
        }
        EXPECT_TRUE(ZL_PivCoHuffmanTree_rangeIsLeaf(&tree, 1, 5));
        EXPECT_EQ(ZL_PivCoHuffmanTree_leafFlatDepth(&tree, 1), 2);
        EXPECT_EQ(tree.numRanks, 5);
        EXPECT_EQ(ZL_PivCoHuffmanTree_splitRank(&tree, 0, 0, tree.numRanks), 1);
    }
}

TEST(PivCoHuffmanKernelTest, DecodeEmptyOutputRequiresEmptyPayload)
{
    HuffmanScenario const scenario = huffmanScenarios()[1];
    uint8_t const payload          = 0xA5;

    EXPECT_TRUE(ZL_PivCoHuffman_decode(
            nullptr,
            0,
            nullptr,
            0,
            scenario.weights.data(),
            scenario.weights.size(),
            nullptr,
            0,
            ZL_PIVCO_DEFAULT_BLOCK_SIZE,
            &ZL_PivCoHuffmanDecode_generic));
    EXPECT_FALSE(ZL_PivCoHuffman_decode(
            nullptr,
            0,
            nullptr,
            0,
            scenario.weights.data(),
            scenario.weights.size(),
            &payload,
            1,
            ZL_PIVCO_DEFAULT_BLOCK_SIZE,
            &ZL_PivCoHuffmanDecode_generic));

    // An empty output has no blocks, so the block size is irrelevant: the
    // binding decodes an empty input with the block size defaulted to
    // decodedSize == 0 (the encoder omits it for a single block), and an
    // out-of-range block size is equally harmless when there is nothing to
    // decode. Both must still succeed on an empty payload.
    EXPECT_TRUE(ZL_PivCoHuffman_decode(
            nullptr,
            0,
            nullptr,
            0,
            scenario.weights.data(),
            scenario.weights.size(),
            nullptr,
            0,
            0,
            &ZL_PivCoHuffmanDecode_generic));
    EXPECT_TRUE(ZL_PivCoHuffman_decode(
            nullptr,
            0,
            nullptr,
            0,
            scenario.weights.data(),
            scenario.weights.size(),
            nullptr,
            0,
            ZL_PIVCO_MAX_BLOCK_SIZE + 1,
            &ZL_PivCoHuffmanDecode_generic));
}

TEST(PivCoHuffmanKernelTest, DecodeRejectsTruncatedBitstream)
{
    HuffmanScenario const scenario     = huffmanScenarios()[2];
    auto const data                    = makeHuffmanData(scenario.alphabet, 32);
    size_t const blockSize             = ZL_PIVCO_DEFAULT_BLOCK_SIZE;
    std::vector<uint8_t> const encoded = encodePivCo(scenario, data, blockSize);
    ASSERT_FALSE(encoded.empty());

    std::vector<uint8_t> decodeScratch(
            ZL_PivCoHuffmanDecode_scratchBytes(data.size(), blockSize));
    std::vector<uint8_t> decoded(data.size());
    EXPECT_FALSE(ZL_PivCoHuffman_decode(
            decoded.data(),
            decoded.size(),
            decodeScratch.data(),
            decodeScratch.size(),
            scenario.weights.data(),
            scenario.weights.size(),
            encoded.data(),
            encoded.size() - 1,
            blockSize,
            &ZL_PivCoHuffmanDecode_generic));
}

TEST(PivCoHuffmanKernelTest, DecodeRejectsCorruptNodeCount)
{
    HuffmanScenario const scenario  = huffmanScenarios()[2];
    std::vector<uint8_t> const data = { 0, 1 };
    size_t const blockSize          = ZL_PIVCO_DEFAULT_BLOCK_SIZE;
    std::vector<uint8_t> encoded    = encodePivCo(scenario, data, blockSize);
    ASSERT_FALSE(encoded.empty());

    encoded[0] = (uint8_t)((encoded[0] & 0x03u) | 0x0Cu);

    std::vector<uint8_t> decodeScratch(
            ZL_PivCoHuffmanDecode_scratchBytes(data.size(), blockSize));
    std::vector<uint8_t> decoded(data.size());
    EXPECT_FALSE(ZL_PivCoHuffman_decode(
            decoded.data(),
            decoded.size(),
            decodeScratch.data(),
            decodeScratch.size(),
            scenario.weights.data(),
            scenario.weights.size(),
            encoded.data(),
            encoded.size(),
            blockSize,
            &ZL_PivCoHuffmanDecode_generic));
}
