// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/pivco-huffman/gpu/pivco_block_index.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "openzl/codecs/pivco_huffman/common_pivco_kernel.h"
#include "openzl/shared/bits.h"

namespace {

ZL_Report parameterError()
{
    return ZL_returnError(ZL_ErrorCode_parameter_invalid);
}

ZL_Report corruptionError()
{
    return ZL_returnError(ZL_ErrorCode_corruption);
}

ZL_Report capacityError()
{
    return ZL_returnError(ZL_ErrorCode_dstCapacity_tooSmall);
}

bool addOverflows(size_t a, size_t b)
{
    return a > SIZE_MAX - b;
}

size_t bitmapBytes(size_t bits)
{
    return (bits + 7) / 8;
}

size_t popcountBitmap(const uint8_t* bitmap, size_t numBits)
{
    const size_t fullBytes = numBits / 8;
    size_t count           = 0;
    for (size_t i = 0; i < fullBytes; ++i) {
        count += static_cast<size_t>(__builtin_popcount(bitmap[i]));
    }

    const size_t tailBits = numBits & 7;
    if (tailBits != 0) {
        const uint8_t mask = static_cast<uint8_t>((1u << tailBits) - 1u);
        count += static_cast<size_t>(__builtin_popcount(
                static_cast<unsigned>(bitmap[fullBytes] & mask)));
    }
    return count;
}

class BitReader {
   public:
    BitReader(const uint8_t* data, size_t size)
            : data_(data), size_(size), bitSize_(size * 8)
    {
    }

    bool popAlignedBits(size_t numBits, const uint8_t** out, size_t* outBytes)
    {
        if (numBits > SIZE_MAX - 7) {
            return false;
        }
        byteAlign();
        if (numBits > bitSize_ || bitPos_ > bitSize_ - numBits) {
            return false;
        }

        *out      = data_ + bitPos_ / 8;
        *outBytes = bitmapBytes(numBits);
        bitPos_ += numBits;
        return true;
    }

    bool read(size_t numBits, size_t* value)
    {
        if (numBits == 0) {
            *value = 0;
            return true;
        }
        if (numBits >= sizeof(size_t) * 8) {
            return false;
        }
        if (numBits > bitSize_ || bitPos_ > bitSize_ - numBits) {
            return false;
        }

        const size_t bytePos     = bitPos_ / 8;
        const size_t bitOff      = bitPos_ & 7;
        size_t word              = 0;
        const size_t bytesToCopy = std::min<size_t>(
                sizeof(word), size_ > bytePos ? size_ - bytePos : 0);
        if (bytesToCopy != 0) {
            std::memcpy(&word, data_ + bytePos, bytesToCopy);
        }

        *value = (word >> bitOff) & ((((size_t)1) << numBits) - 1);
        bitPos_ += numBits;
        return true;
    }

    size_t consumedBytes() const
    {
        return (bitPos_ + 7) / 8;
    }

   private:
    void byteAlign()
    {
        const size_t misalignment = bitPos_ & 7;
        if (misalignment != 0) {
            bitPos_ += 8 - misalignment;
        }
    }

    const uint8_t* data_;
    size_t size_;
    size_t bitSize_;
    size_t bitPos_{ 0 };
};

bool parseNode(
        const ZL_PivCoHuffmanTree* tree,
        BitReader& reader,
        size_t level,
        size_t firstRank,
        size_t rankEnd,
        size_t count)
{
    if (firstRank >= rankEnd || rankEnd > tree->numRanks) {
        return false;
    }

    if (ZL_PivCoHuffmanTree_rangeIsLeaf(tree, firstRank, rankEnd)) {
        const size_t depth = ZL_PivCoHuffmanTree_leafFlatDepth(tree, firstRank);
        if (depth == 0) {
            return true;
        }
        if (count != 0 && depth > SIZE_MAX / count) {
            return false;
        }
        const uint8_t* bitmap = nullptr;
        size_t bitmapSize     = 0;
        return reader.popAlignedBits(count * depth, &bitmap, &bitmapSize);
    }

    if (level >= tree->numLevels) {
        return false;
    }

    const size_t splitRank =
            ZL_PivCoHuffmanTree_splitRank(tree, level, firstRank, rankEnd);
    const bool lhsIsConstant =
            ZL_PivCoHuffmanTree_rangeIsConstantLeaf(tree, firstRank, splitRank);
    const bool rhsIsConstant =
            ZL_PivCoHuffmanTree_rangeIsConstantLeaf(tree, splitRank, rankEnd);

    const uint8_t* bitmap = nullptr;
    size_t bitmapSize     = 0;
    if (!reader.popAlignedBits(count, &bitmap, &bitmapSize)) {
        return false;
    }
    (void)bitmapSize;

    size_t numOnes = popcountBitmap(bitmap, count);
    if (!(lhsIsConstant && rhsIsConstant)) {
        size_t storedNumOnes = 0;
        if (!reader.read(
                    static_cast<size_t>(ZL_nextPow2(count + 1)),
                    &storedNumOnes)) {
            return false;
        }
        if (storedNumOnes > count || storedNumOnes != numOnes) {
            return false;
        }
        numOnes = storedNumOnes;
    }

    const size_t numZeros = count - numOnes;
    return parseNode(tree, reader, level + 1, firstRank, splitRank, numZeros)
            && parseNode(tree, reader, level + 1, splitRank, rankEnd, numOnes);
}

} // namespace

extern "C" ZL_Report pivcoFindBlockOffsets(
        uint64_t* offsets,
        size_t offsetsCapacity,
        const uint8_t* weights,
        size_t weightsSize,
        const uint8_t* bitstream,
        size_t bitstreamSize,
        size_t decodedSize,
        size_t blockSize)
{
    if (offsets == nullptr) {
        return parameterError();
    }
    if (weightsSize != 0 && weights == nullptr) {
        return parameterError();
    }
    if (bitstreamSize != 0 && bitstream == nullptr) {
        return parameterError();
    }

    if (decodedSize == 0) {
        if (offsetsCapacity < 1) {
            return capacityError();
        }
        if (weightsSize != 0 || bitstreamSize != 0) {
            return corruptionError();
        }
        offsets[0] = 0;
        return ZL_returnValue(1);
    }

    if (blockSize == 0 || blockSize > ZL_PIVCO_MAX_BLOCK_SIZE) {
        return parameterError();
    }
    if (addOverflows(decodedSize, blockSize - 1)) {
        return ZL_returnError(ZL_ErrorCode_integerOverflow);
    }

    const size_t numBlocks = (decodedSize + blockSize - 1) / blockSize;
    if (offsetsCapacity < numBlocks + 1) {
        return capacityError();
    }

    const int tableLog = ZL_PivCoHuffman_computeTableLog(weights, weightsSize);
    if (tableLog < 0) {
        return corruptionError();
    }

    ZL_PivCoHuffmanTree tree;
    ZL_PivCoHuffmanTree_build(&tree, weights, weightsSize, tableLog);

    BitReader reader(bitstream, bitstreamSize);
    offsets[0] = 0;
    for (size_t block = 0; block < numBlocks; ++block) {
        const size_t blockOff = block * blockSize;
        const size_t blockLen = std::min(blockSize, decodedSize - blockOff);
        if (reader.consumedBytes() > UINT64_MAX) {
            return ZL_returnError(ZL_ErrorCode_integerOverflow);
        }
        offsets[block] = static_cast<uint64_t>(reader.consumedBytes());
        if (!parseNode(&tree, reader, 0, 0, tree.numRanks, blockLen)) {
            return corruptionError();
        }
        if (reader.consumedBytes() > UINT64_MAX) {
            return ZL_returnError(ZL_ErrorCode_integerOverflow);
        }
        offsets[block + 1] = static_cast<uint64_t>(reader.consumedBytes());
    }

    if (reader.consumedBytes() != bitstreamSize) {
        return corruptionError();
    }
    return ZL_returnValue(numBlocks + 1);
}
