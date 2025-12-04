// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/entropy/decode_huffman_kernel.h"

#include <stddef.h> // size_t

#include "openzl/codecs/entropy/common_huffman_kernel.h"
#include "openzl/codecs/entropy/deprecated/common_entropy.h"
#include "openzl/fse/bitstream.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_errors.h"

bool ZS_largeHuffmanValidWeights(
        const uint8_t* weights,
        size_t nbWeights,
        int tableLog)
{
    if (tableLog < 0 || tableLog > ZS_kLargeHuffmanMaxNbBits) {
        return false;
    }
    bool weightTooLarge = false;
    uint64_t sum        = 0;
    for (size_t i = 0; i < nbWeights; ++i) {
        int const w = weights[i];
        weightTooLarge |= (w > tableLog);
        sum += (((uint64_t)1) << (w & 63)) >> 1;
    }
    // Guaranteed that there are at least 2 non-zero weights
    // by the fact that we're adding (1 << weight) >> 1, weights are <= the
    // tableLog, that the total is 1 << tableLog.
    return sum == ((uint64_t)1 << tableLog) && !weightTooLarge;
}

void ZS_largeHuffmanBuildDTable(
        ZS_Huf16DElt* dtable,
        const uint8_t* weights,
        size_t nbWeights,
        int tableLog)
{
    ZL_ASSERT(ZS_largeHuffmanValidWeights(weights, nbWeights, tableLog));

    int const maxSymbolValue1                    = (int)nbWeights;
    int rankStart[ZS_kLargeHuffmanMaxNbBits + 1] = { 0 };
    for (int s = 0; s < maxSymbolValue1; ++s) {
        ZL_ASSERT_LT(weights[s], ZL_ARRAY_SIZE(rankStart));
        ++rankStart[weights[s]];
    }
    rankStart[0] = 0;
    for (int r = 1, nextRankStart = 0; r < tableLog + 1; ++r) {
        int const current = nextRankStart;
        nextRankStart += rankStart[r] << (r - 1);
        rankStart[r] = current;
        ZL_ASSERT_LE(nextRankStart, (1 << tableLog));
    }

    for (int s = 0; s < maxSymbolValue1; ++s) {
        int const weight = weights[s];
        int const length = (1 << weight) >> 1;
        int const start  = rankStart[weight];
        ZL_ASSERT_LE(start + length, (1 << tableLog));
        ZS_Huf16DElt const elt = {
            .nbBits = (uint16_t)(tableLog + 1 - weight),
            .symbol = (uint16_t)s,
        };
        rankStart[weight] += length;
        for (int i = 0; i < length; ++i) {
            dtable[start + i] = elt;
        }
    }
}

ZS_Huf16DElt* ZS_largeHuffmanCreateDTable(ZL_RC* src, int* tableLogPtr)
{
    uint8_t* weights     = NULL;
    ZS_Huf16DElt* dtable = NULL;
    if (ZL_RC_avail(src) < 3) {
        goto _error;
    }
    int const tableLog = (int)ZL_RC_pop(src);
    if (tableLog > ZS_kLargeHuffmanMaxNbBits || tableLog == 0) {
        goto _error;
    }
    uint16_t const maxSymbolValue = ZL_RC_popCE16(src);
    int const maxSymbolValue1     = (int)maxSymbolValue + 1;
    weights = (uint8_t*)malloc((size_t)(maxSymbolValue + 1));
    if (weights == NULL) {
        goto _error;
    }
    {
        ZL_Report const ret = ZS_Entropy_decodeDefault(
                weights, (size_t)maxSymbolValue1, src, 1);
        if (ZL_isError(ret) || ZL_validResult(ret) != (size_t)maxSymbolValue1) {
            goto _error;
        }
    }

    if (!ZS_largeHuffmanValidWeights(
                weights, (size_t)maxSymbolValue1, tableLog)) {
        goto _error;
    }

    dtable = (ZS_Huf16DElt*)malloc(sizeof(ZS_Huf16DElt) << tableLog);
    if (dtable == NULL) {
        goto _error;
    }

    ZS_largeHuffmanBuildDTable(
            dtable, weights, (size_t)maxSymbolValue1, tableLog);

    *tableLogPtr = tableLog;

    free(weights);
    return dtable;

_error:
    free(weights);
    free(dtable);
    return NULL;
}

ZL_FORCE_INLINE uint16_t ZS_largeHuffmanDecodeSymbol(
        BIT_DStream_t* dstream,
        ZS_Huf16DElt const* dtable,
        int const tableLog)
{
    size_t const val = BIT_lookBitsFast(
            dstream, (unsigned)tableLog); /* note : dtLog >= 1 */
    uint16_t const symbol = dtable[val].symbol;
    BIT_skipBits(dstream, dtable[val].nbBits);
    return symbol;
}

ZL_FORCE_INLINE void ZS_largeHuffmanDecode_body(
        uint16_t* dst,
        size_t dstSize,
        BIT_DStream_t* dstream,
        ZS_Huf16DElt const* dtable,
        int tableLog,
        int const kUnroll)
{
    uint16_t* const dstEnd = dst + dstSize;
    uint16_t* ptr          = dst;
    if (dstSize >= (size_t)kUnroll) {
        while ((BIT_reloadDStream(dstream) == BIT_DStream_unfinished)
               & (ptr + kUnroll < dstEnd)) {
            for (int u = 0; u < kUnroll; ++u) {
                *ptr++ = ZS_largeHuffmanDecodeSymbol(dstream, dtable, tableLog);
            }
        }
    }
    BIT_reloadDStream(dstream);
    while (ptr < dstEnd) {
        *ptr++ = ZS_largeHuffmanDecodeSymbol(dstream, dtable, tableLog);
    }
    ZL_ASSERT_EQ(ptr, dstEnd);
}

ZL_FORCE_INLINE ZL_Report ZS_largeHuffmanDecodeX4_body(
        uint16_t* dsts[5],
        BIT_DStream_t dstreams[4],
        ZS_Huf16DElt const* dtable,
        int tableLog,
        int const kUnroll)
{
    uint16_t* ptrs[4];
    memcpy(ptrs, dsts, sizeof(ptrs));
    if ((size_t)(dsts[4] - dsts[3]) >= (size_t)kUnroll) {
        uint16_t* const limit = dsts[4] - kUnroll;
        int endSignal         = 1;
        for (; endSignal & (ptrs[3] < limit);) {
            for (int i = 0; i < 4; ++i) {
                for (int u = 0; u < kUnroll; ++u) {
                    *ptrs[i]++ = ZS_largeHuffmanDecodeSymbol(
                            &dstreams[i], dtable, tableLog);
                }
                endSignal &=
                        (BIT_reloadDStream(&dstreams[i])
                         == BIT_DStream_unfinished);
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (ptrs[i] > dsts[i + 1]) {
            ZL_RET_R_ERR(GENERIC);
        }
        ZS_largeHuffmanDecode_body(
                ptrs[i],
                (size_t)(dsts[i + 1] - ptrs[i]),
                &dstreams[i],
                dtable,
                tableLog,
                kUnroll);
    }
    return ZL_returnValue((size_t)(dsts[4] - dsts[0]));
}

ZL_Report ZS_largeHuffmanDecodeUsingDTableX4(
        uint16_t* dst,
        size_t capacity,
        ZL_RC* src,
        ZS_Huf16DElt const* dtable,
        int tableLog)
{
    uint16_t* dsts[5];
    BIT_DStream_t dstreams[4];
    uint16_t* dstEnd = dst;
    for (int i = 0; i < 4; ++i) {
        dsts[i] = dstEnd;
        if (ZL_RC_avail(src) < 8) {
            ZL_RET_R_ERR(GENERIC);
        }
        uint32_t const dstSize = ZL_RC_popCE32(src);
        uint32_t const srcSize = ZL_RC_popCE32(src);
        if (ZL_RC_avail(src) < srcSize
            || capacity < (size_t)(dstSize + (dstEnd - dst))) {
            ZL_RET_R_ERR(GENERIC);
        }
        if (ERR_isError(
                    BIT_initDStream(&dstreams[i], ZL_RC_ptr(src), srcSize))) {
            ZL_RET_R_ERR(GENERIC);
        }
        ZL_RC_advance(src, srcSize);
        dstEnd += dstSize;
    }
    dsts[4] = dstEnd;

    if (ZL_64bits()) {
        if (tableLog <= 14) {
            return ZS_largeHuffmanDecodeX4_body(
                    dsts, dstreams, dtable, tableLog, 4);
        } else if (tableLog <= 18) {
            return ZS_largeHuffmanDecodeX4_body(
                    dsts, dstreams, dtable, tableLog, 3);
        } else {
            return ZS_largeHuffmanDecodeX4_body(
                    dsts, dstreams, dtable, tableLog, 2);
        }
    } else {
        if (tableLog <= 14) {
            return ZS_largeHuffmanDecodeX4_body(
                    dsts, dstreams, dtable, tableLog, 2);
        } else {
            return ZS_largeHuffmanDecodeX4_body(
                    dsts, dstreams, dtable, tableLog, 1);
        }
    }
}

ZL_Report ZS_largeHuffmanDecodeUsingDTable(
        uint16_t* dst,
        size_t capacity,
        ZL_RC* src,
        ZS_Huf16DElt const* dtable,
        int tableLog)
{
    if (ZL_RC_avail(src) < 8) {
        ZL_RET_R_ERR(GENERIC);
    }
    uint32_t const dstSize = ZL_RC_popCE32(src);
    uint32_t const srcSize = ZL_RC_popCE32(src);
    if (ZL_RC_avail(src) < srcSize || capacity < dstSize) {
        ZL_RET_R_ERR(GENERIC);
    }
    BIT_DStream_t dstream;
    if (ERR_isError(BIT_initDStream(&dstream, ZL_RC_ptr(src), srcSize))) {
        ZL_RET_R_ERR(GENERIC);
    }

    ZL_RC_advance(src, srcSize);

    if (ZL_64bits()) {
        if (tableLog <= 14) {
            ZS_largeHuffmanDecode_body(
                    dst, dstSize, &dstream, dtable, tableLog, 4);
        } else if (tableLog <= 18) {
            ZS_largeHuffmanDecode_body(
                    dst, dstSize, &dstream, dtable, tableLog, 3);
        } else {
            ZS_largeHuffmanDecode_body(
                    dst, dstSize, &dstream, dtable, tableLog, 2);
        }
    } else {
        if (tableLog <= 14) {
            ZS_largeHuffmanDecode_body(
                    dst, dstSize, &dstream, dtable, tableLog, 2);
        } else {
            ZS_largeHuffmanDecode_body(
                    dst, dstSize, &dstream, dtable, tableLog, 1);
        }
    }

    return ZL_returnValue(dstSize);
}

ZL_Report ZS_largeHuffmanDecode(uint16_t* dst, size_t capacity, ZL_RC* src)
{
    if (ZL_RC_avail(src) < 1) {
        ZL_RET_R_ERR(GENERIC);
    }
    ZS_HufTransformPrefix_e const header =
            (ZS_HufTransformPrefix_e)ZL_RC_pop(src);
    if (header == ZS_HufTransformPrefix_constant) {
        ZL_TRY_LET_CONST_T(uint64_t, nelts, ZL_RC_popVarint(src));
        if (ZL_RC_avail(src) < sizeof(uint16_t)) {
            ZL_RET_R_ERR(GENERIC);
        }
        uint16_t const value = ZL_RC_popCE16(src);
        if (capacity < nelts) {
            ZL_RET_R_ERR(GENERIC);
        }
        for (size_t i = 0; i < nelts; ++i) {
            dst[i] = value;
        }
        return ZL_returnValue((size_t)nelts);
    }
    if (header == ZS_HufTransformPrefix_lit) {
        ZL_TRY_LET_CONST_T(uint64_t, nelts, ZL_RC_popVarint(src));
        if (capacity < nelts) {
            ZL_RET_R_ERR(GENERIC);
        }
        if (ZL_RC_avail(src) < nelts * sizeof(uint16_t)) {
            ZL_RET_R_ERR(GENERIC);
        }
        for (size_t i = 0; i < nelts; ++i) {
            dst[i] = ZL_RC_popCE16(src);
        }
        return ZL_returnValue((size_t)nelts);
    }
    if (header != ZS_HufTransformPrefix_huf) {
        ZL_RET_R_ERR(GENERIC);
    }

    int tableLog;
    ZS_Huf16DElt* const dtable = ZS_largeHuffmanCreateDTable(src, &tableLog);
    if (dtable == NULL) {
        ZL_RET_R_ERR(GENERIC);
    }

    // Decompress
    if (ZL_RC_avail(src) < 1) {
        free(dtable);
        ZL_RET_R_ERR(GENERIC);
    }

    bool const x4 = (bool)ZL_RC_pop(src);
    ZL_Report dstSize;
    if (x4) {
        dstSize = ZS_largeHuffmanDecodeUsingDTableX4(
                dst, capacity, src, dtable, tableLog);
    } else {
        dstSize = ZS_largeHuffmanDecodeUsingDTable(
                dst, capacity, src, dtable, tableLog);
    }

    free(dtable);

    return dstSize;
}
