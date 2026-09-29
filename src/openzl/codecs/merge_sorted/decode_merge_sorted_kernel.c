// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/merge_sorted/decode_merge_sorted_kernel.h"

#include "openzl/shared/mem.h"

ZL_FORCE_INLINE bool ZL_MergeSorted_split(
        uint32_t* const* dstStarts,
        uint32_t* const* dstEnds,
        size_t nbDsts,
        char const* bitsets,
        uint32_t const* merged,
        size_t nbUniqueValues,
        size_t kBitsetWidth)
{
    assert(kBitsetWidth <= 8);
    assert(nbDsts <= kBitsetWidth * 8);

    uint32_t* dsts[64];
    memcpy(dsts, dstStarts, sizeof(uint32_t*) * nbDsts);

    for (size_t i = 0; i < nbUniqueValues; ++i) {
        uint64_t const bitset =
                ZL_readN(bitsets + i * kBitsetWidth, kBitsetWidth);
        uint32_t const val = merged[i];
        for (size_t b = 0; b < nbDsts; ++b) {
            if (dsts[b] != dstEnds[b]) {
                // Write outside of the branch so the compiler has a chance to
                // turn the ++dst[b] into a conditional move. We expect this
                // transform to be useful when the lists have many repeated
                // values, so expect that most lists will have most values.
                *dsts[b] = val;
                if (bitset & ((uint64_t)1 << b)) {
                    ++dsts[b];
                }
            }
        }
    }
    for (size_t i = 0; i < nbDsts; ++i) {
        if (dsts[i] != dstEnds[i]) {
            return false;
        }
    }
    return true;
}

bool ZL_MergeSorted_split8x32(
        uint32_t** dsts,
        uint32_t** dstEnds,
        size_t nbDsts,
        uint8_t const* bitsets,
        uint32_t const* merged,
        size_t nbUniqueValues)
{
    return ZL_MergeSorted_split(
            dsts,
            dstEnds,
            nbDsts,
            (char const*)bitsets,
            merged,
            nbUniqueValues,
            1);
}

bool ZL_MergeSorted_split16x32(
        uint32_t** dsts,
        uint32_t** dstEnds,
        size_t nbDsts,
        uint16_t const* bitsets,
        uint32_t const* merged,
        size_t nbUniqueValues)
{
    return ZL_MergeSorted_split(
            dsts,
            dstEnds,
            nbDsts,
            (char const*)bitsets,
            merged,
            nbUniqueValues,
            2);
}

bool ZL_MergeSorted_split32x32(
        uint32_t** dsts,
        uint32_t** dstEnds,
        size_t nbDsts,
        uint32_t const* bitsets,
        uint32_t const* merged,
        size_t nbUniqueValues)
{
    return ZL_MergeSorted_split(
            dsts,
            dstEnds,
            nbDsts,
            (char const*)bitsets,
            merged,
            nbUniqueValues,
            4);
}

bool ZL_MergeSorted_split64x32(
        uint32_t** dsts,
        uint32_t** dstEnds,
        size_t nbDsts,
        uint64_t const* bitsets,
        uint32_t const* merged,
        size_t nbUniqueValues)
{
    return ZL_MergeSorted_split(
            dsts,
            dstEnds,
            nbDsts,
            (char const*)bitsets,
            merged,
            nbUniqueValues,
            8);
}
