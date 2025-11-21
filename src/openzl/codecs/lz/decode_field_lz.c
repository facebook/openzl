// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string.h>

#include "openzl/codecs/common/copy.h"
#include "openzl/codecs/lz/common_field_lz.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_errors.h"

#if ZL_HAS_AVX2

#    include <immintrin.h>
#    include <smmintrin.h>

#    include "openzl/codecs/lz/decode_field_lz_offset_tables.h"

typedef struct {
    __m128i reps;
} ZS_Reps;

static ZS_Reps ZS_Reps_init(uint32_t rep0, uint32_t rep1, uint32_t rep2)
{
    return (ZS_Reps){ _mm_setr_epi32((int)rep0, (int)rep1, (int)rep2, 0) };
}

static void ZS_Reps_save(ZS_Reps r, uint32_t reps[3])
{
    reps[0] = (uint32_t)_mm_extract_epi32(r.reps, 0);
    reps[1] = (uint32_t)_mm_extract_epi32(r.reps, 1);
    reps[2] = (uint32_t)_mm_extract_epi32(r.reps, 2);
}

ZL_FORCE_INLINE __m256i ZS_Reps_loadOffsetShuffle(uint64_t mask)
{
    ZL_ASSERT_LT(mask, 256);
    ZL_ASSERT_EQ(sizeof(ZS_kOffsetShuffle[mask]) * 8, ZL_OFFSET_SHUFFLE_BITS);
#    if ZL_OFFSET_SHUFFLE_BITS == 256
    // Load the shuffle directly
    return _mm256_load_si256(
            (__m256i const*)(void const*)ZS_kOffsetShuffle[mask]);
#    else
    // Unpack the 64-bit shuffle to a 256-bit shuffle
    ZL_ASSERT_EQ(ZL_OFFSET_SHUFFLE_BITS, 64);
    uint64_t const offsetShuffle64 = ZL_readLE64(ZS_kOffsetShuffle[mask]);
    __m256i offsetShuffle = _mm256_set1_epi64x((int64_t)offsetShuffle64);
    char const zero       = (char)0xff;
    // clang-format off
    __m256i const unpack= _mm256_setr_epi8(
        0x00, zero, zero, zero,
        0x01, zero, zero, zero,
        0x02, zero, zero, zero,
        0x03, zero, zero, zero,
        0x04, zero, zero, zero,
        0x05, zero, zero, zero,
        0x06, zero, zero, zero,
        0x07, zero, zero, zero);
    // clang-format on
    return _mm256_shuffle_epi8(offsetShuffle, unpack);
#    endif
}

ZL_FORCE_INLINE size_t ZS_Reps_update4(
        ZS_Reps* reps,
        uint32_t outOffsets[4],
        uint32_t const* inOffsets,
        uint64_t tokens,
        size_t kEltBits)
{
    uint64_t const mask   = _pext_u64(tokens, 0x0003000300030003ULL);
    __m256i const shuffle = ZS_Reps_loadOffsetShuffle(mask);
    __m128i const offs0   = _mm_loadu_si128((__m128i_u const*)inOffsets);
    __m128i const offs1   = _mm_slli_epi32(offs0, (int)kEltBits);
    __m256i const vec     = _mm256_set_m128i(offs1, reps->reps);
    __m256i const ret     = _mm256_permutevar8x32_epi32(vec, shuffle);
    reps->reps            = _mm256_extracti128_si256(ret, 0);
    __m128i const outOffs = _mm256_extracti128_si256(ret, 1);
    _mm_store_si128((__m128i*)(void*)outOffsets, outOffs);
    return ZS_kNumOffsets[mask];
}

#else

typedef struct {
    uint32_t reps[3];
} ZS_Reps;

static ZS_Reps ZS_Reps_init(uint32_t rep0, uint32_t rep1, uint32_t rep2)
{
    return (ZS_Reps){ .reps = { rep0, rep1, rep2 } };
}

static void ZS_Reps_save(ZS_Reps r, uint32_t reps[3])
{
    reps[0] = r.reps[0];
    reps[1] = r.reps[1];
    reps[2] = r.reps[2];
}

ZL_FORCE_INLINE size_t ZS_Reps_update4(
        ZS_Reps* r,
        uint32_t outOffsets[4],
        uint32_t const* inOffsets,
        uint64_t tokens,
        size_t kEltBits)
{
    size_t nbOffsetsRead = 0;
    for (size_t i = 0; i < 4; ++i) {
        uint64_t const ofCode = (tokens >> (16 * i)) & 0x3;
        uint32_t offset;
        if (ofCode == 3) {
            offset     = inOffsets[nbOffsetsRead++] << kEltBits;
            r->reps[2] = r->reps[1];
            r->reps[1] = r->reps[0];
            r->reps[0] = offset;
        } else if (ofCode == 0) {
            offset = r->reps[0];
        } else if (ofCode == 1) {
            offset     = r->reps[1];
            r->reps[1] = r->reps[0];
            r->reps[0] = offset;
        } else {
            ZL_ASSERT_EQ(ofCode, 2);
            offset     = r->reps[2];
            r->reps[2] = r->reps[1];
            r->reps[1] = r->reps[0];
            r->reps[0] = offset;
        }
        outOffsets[i] = offset;
    }
    return nbOffsetsRead;
}

#endif

ZL_FORCE_INLINE ZL_Report ZL_FieldLz_decompress_impl2(
        void* dst,
        size_t dstEltCapacity,
        ZL_FieldLz_InSequences const* src,
        size_t const kEltBits,
        size_t const kShortLLCode,
        size_t const kShortMLCode)
{
    uint32_t const minMatch = kMinMatch(1 << kEltBits);

    uint8_t* const outStart = (uint8_t*)dst;

    uint16_t const* toks = (uint16_t const*)src->tokens;
    uint8_t const* lits  = (uint8_t const*)src->literalElts;
    uint32_t const* offs = src->offsets;
    uint32_t const* ells = src->extraLiteralLengths;
    uint32_t const* emls = src->extraMatchLengths;
    uint8_t* out         = outStart;

    uint16_t const* const toksEnd = toks + src->nbTokens;
    uint8_t const* const litsEnd  = lits + (src->nbLiteralElts << kEltBits);
    uint32_t const* const offsEnd = offs + src->nbOffsets;
    uint32_t const* const ellsEnd = ells + src->nbExtraLiteralLengths;
    uint32_t const* const emlsEnd = emls + src->nbExtraMatchLengths;
    uint8_t* outEnd               = out + (dstEltCapacity << kEltBits);

    ZS_Reps r = ZS_Reps_init(1u << kEltBits, 2u << kEltBits, 4u << kEltBits);
    uint64_t tokens;
    uint32_t ZL_ALIGNED(16) offsets[4];
    size_t idx = 4;

    size_t const kShortLL = kShortLLCode << kEltBits;
    size_t const kShortML = (kShortMLCode + minMatch) << kEltBits;
    size_t const kTokenLL = (kMaxLitLengthCode) << kEltBits;
    size_t const kTokenML = (kMaxMatchLengthCode + minMatch) << kEltBits;
    size_t const kUnroll  = 4;

    ZL_ASSERT(kShortLL % 16 == 0 || kShortLLCode == kMaxLitLengthCode - 1);
    ZL_ASSERT(kShortML % 16 == 0 || kShortMLCode == kMaxMatchLengthCode - 1);

    uint8_t const* const outLimit   = outEnd - kUnroll * (kTokenLL + kTokenML);
    uint8_t const* const litsLimit  = litsEnd - kUnroll * kTokenLL;
    uint16_t const* const toksLimit = toksEnd - kUnroll + 1;
    uint32_t const* const offsLimit = offsEnd - kUnroll + 1;

    if (out < outLimit && lits < litsLimit && toks < toksLimit
        && offs < offsLimit) {
        for (;;) {
            tokens = ZL_readLE64(toks);
            toks += 4;

            offs += ZS_Reps_update4(&r, offsets, offs, tokens, kEltBits);

            if (ZL_UNLIKELY(
                        toks >= toksLimit || offs >= offsLimit
                        || out >= outLimit || lits >= litsLimit)) {
                idx = 0;
                goto end;
            }

            // We want loop unrolling, but this is triggering a compiler bug in
            // alias analysis.
            // See T195077417 & https://github.com/llvm/llvm-project/pull/100130
#if defined(__clang__) && !defined(__APPLE__)
#    pragma clang loop unroll_count(4)
#endif
            for (size_t u = 0; u < kUnroll; ++u) {
                uint64_t const token  = tokens >> (16 * u);
                uint32_t const llCode = (token >> kTokenOFBits) & kTokenLLMask;
                uint32_t const mlCode =
                        (token >> (kTokenOFBits + kTokenLLBits)) & kTokenMLMask;

                uint32_t const llen = llCode << kEltBits;
                uint32_t const mlen = (mlCode + minMatch) << kEltBits;

                // ZL_DLOG(ALWAYS, "LLCode = %u | llen = %u | kShortLL = %zu |
                // kTokenLL = %zu", llCode, llen, kShortLL, kTokenLL);
                // ZL_DLOG(ALWAYS, "MLCode = %u | mlen = %u | kShortML = %zu |
                // kTokenML = %zu", mlCode, mlen, kShortML, kTokenML);

                memcpy(out, lits, kShortLL);
                if (ZL_LIKELY(llCode <= kShortLLCode)) {
                    ZL_ASSERT_LE(llen, kShortLL);
                    out += llen;
                    lits += llen;
                } else {
                    memcpy(out + kShortLL,
                           lits + kShortLL,
                           kTokenLL - kShortLL);
                    out += llen;
                    lits += llen;
                    if (ZL_UNLIKELY(llCode == kMaxLitLengthCode)) {
                        ZL_RET_R_IF_EQ(
                                srcSize_tooSmall,
                                ells,
                                ellsEnd,
                                "Not enough extra literal lengths");
                        uint32_t const extra = *ells++ << kEltBits;
                        if (ZL_UNLIKELY(
                                    out + extra >= outLimit
                                    || lits + extra >= litsLimit)) {
                            // TODO: Do we want to avoid the extra copies?
                            --ells;
                            out -= llen;
                            lits -= llen;
                            idx = u;
                            goto end;
                        }
                        ZS_wildcopy(out, lits, extra, ZS_wo_no_overlap);
                        out += extra;
                        lits += extra;
                    }
                }

                uint32_t const offset = offsets[u];

                ZL_ASSERT_GE(out, outStart);
                if (ZL_UNLIKELY(offset > (out - outStart))) {
                    ZL_DLOG(ERROR,
                            "Corruption: offset too large: %u vs %u",
                            offset,
                            (unsigned)(out - outStart));
                    ZL_RET_R_ERR(GENERIC);
                }
                uint8_t const* match = out - offset;

                if (ZL_LIKELY(offset >= 16)) {
                    for (size_t l = 0; l < kShortML; l += 16) {
                        ZS_copy16(out + l, match + l);
                    }

                    if (ZL_LIKELY(mlCode <= kShortMLCode)) {
                        ZL_ASSERT_LE(mlen, kShortML);
                        out += mlen;
                    } else {
                        for (size_t l = kShortML; l < kTokenML; l += 16) {
                            ZS_copy16(out + l, match + l);
                        }
                        out += mlen;
                        match += mlen;
                        if (ZL_UNLIKELY(mlCode == kMaxMatchLengthCode)) {
                            ZL_RET_R_IF_EQ(
                                    srcSize_tooSmall,
                                    emls,
                                    emlsEnd,
                                    "Not enough extra match lengths");
                            uint32_t const extra = *emls++ << kEltBits;
                            if (ZL_UNLIKELY(out + extra >= outLimit)) {
                                ZL_RET_R_IF_GT(
                                        internalBuffer_tooSmall,
                                        out + extra,
                                        outEnd,
                                        "Match too long");
                                ZS_safecopy(
                                        out,
                                        match,
                                        extra,
                                        ZS_wo_src_before_dst);
                                out += extra;
                                idx = u + 1;
                                goto end;
                            }
                            ZS_wildcopy(
                                    out, match, extra, ZS_wo_src_before_dst);
                            out += extra;
                        }
                    }
                } else {
                    // TODO: Optimize this
                    uint32_t plen = mlen;
                    if (ZL_UNLIKELY(mlCode == kMaxMatchLengthCode)) {
                        ZL_RET_R_IF_EQ(
                                srcSize_tooSmall,
                                emls,
                                emlsEnd,
                                "Not enough extra match lengths");
                        uint32_t const extra = *emls++ << kEltBits;
                        plen += extra;
                        if (ZL_UNLIKELY(out + plen >= outLimit)) {
                            ZL_RET_R_IF_GT(
                                    internalBuffer_tooSmall,
                                    out + plen,
                                    outEnd,
                                    "Match too long");
                            ZS_safecopy(out, match, plen, ZS_wo_src_before_dst);
                            out += plen;
                            idx = u + 1;
                            goto end;
                        }
                    }
                    ZS_wildcopy(out, match, plen, ZS_wo_src_before_dst);
                    out += plen;
                }
            }
        }
    end:
        for (; idx < kUnroll; ++idx) {
            uint64_t const token  = tokens >> (16 * idx);
            uint32_t const llCode = (token >> kTokenOFBits) & kTokenLLMask;
            uint32_t const mlCode =
                    (token >> (kTokenOFBits + kTokenLLBits)) & kTokenMLMask;

            uint32_t llen = llCode << kEltBits;
            uint32_t mlen = (mlCode + minMatch) << kEltBits;

            if (ZL_UNLIKELY(llCode == kMaxLitLengthCode)) {
                ZL_RET_R_IF_EQ(
                        srcSize_tooSmall,
                        ells,
                        ellsEnd,
                        "Not enough extra lit lengths");
                uint32_t const extra = *ells++;
                llen += extra << kEltBits;
            }

            if (ZL_UNLIKELY(mlCode == kMaxMatchLengthCode)) {
                ZL_RET_R_IF_EQ(
                        srcSize_tooSmall,
                        emls,
                        emlsEnd,
                        "Not enough extra match lengths");
                uint32_t const extra = *emls++;
                mlen += extra << kEltBits;
            }

            //> Ensure we have output space
            ZL_RET_R_IF_GT(
                    internalBuffer_tooSmall,
                    out + llen + mlen,
                    outEnd,
                    "Output buffer too small");

            //> Copy literals
            ZL_RET_R_IF_GT(
                    srcSize_tooSmall, lits + llen, litsEnd, "Too few literals");

            ZS_safecopy(out, lits, llen, ZS_wo_no_overlap);
            lits += llen;
            out += llen;

            uint32_t const offset = offsets[idx];

            //> Validate offset
            if (ZL_UNLIKELY(offset > (out - outStart))) {
                ZL_DLOG(ERROR,
                        "Corruption: offset too large: %u vs %u",
                        offset,
                        (unsigned)(out - outStart));
                ZL_RET_R_ERR(GENERIC);
            }

            //> Copy match
            uint8_t const* const match = out - offset;
            ZS_safecopy(out, match, mlen, ZS_wo_src_before_dst);
            out += mlen;
        }
    }

    uint32_t reps[3];
    ZS_Reps_save(r, reps);
    for (; toks < toksEnd; ++toks) {
        uint16_t const token = ZL_read16(&toks[0]);

        uint32_t const ofCode = token & kTokenOFMask;
        uint32_t const llCode = (token >> kTokenOFBits) & kTokenLLMask;
        uint32_t const mlCode =
                (token >> (kTokenOFBits + kTokenLLBits)) & kTokenMLMask;

        //> Decode offset
        uint32_t offset;
        if (ofCode == 3) {
            ZL_RET_R_IF_EQ(srcSize_tooSmall, offs, offsEnd, "Too few offsets");
            offset  = *offs++ << kEltBits;
            reps[2] = reps[1];
            reps[1] = reps[0];
            reps[0] = offset;
        } else if (ofCode == 0) {
            offset = reps[0];
        } else if (ofCode == 1) {
            offset  = reps[1];
            reps[1] = reps[0];
            reps[0] = offset;
        } else {
            ZL_ASSERT_EQ(ofCode, 2);
            offset  = reps[2];
            reps[2] = reps[1];
            reps[1] = reps[0];
            reps[0] = offset;
        }
        // ZL_DLOG(ALWAYS, "OFCode = %u (%u) | LLCode = %u | MLCode = %u",
        // ofCode, offset, llCode, mlCode);

        //> Decode literal length
        uint32_t literalLength = llCode;
        if (ZL_UNLIKELY(llCode == kMaxLitLengthCode)) {
            ZL_RET_R_IF_EQ(
                    srcSize_tooSmall, ells, ellsEnd, "Too few extra llens");
            uint32_t const extra = *ells++;
            literalLength += extra;
        }
        literalLength <<= kEltBits;

        //> Decode match length
        uint32_t matchLength = mlCode + minMatch;
        if (ZL_UNLIKELY(mlCode == kMaxMatchLengthCode)) {
            ZL_RET_R_IF_EQ(
                    srcSize_tooSmall, emls, emlsEnd, "Too few extra mlens");
            uint32_t const extra = *emls++;
            matchLength += extra;
        }
        matchLength <<= kEltBits;

        //> Ensure we have output space
        ZL_RET_R_IF_GT(
                internalBuffer_tooSmall,
                (uint64_t)literalLength + (uint64_t)matchLength,
                (uint64_t)(outEnd - out),
                "Output size too small");

        //> Copy literals
        ZL_RET_R_IF_GT(
                srcSize_tooSmall,
                literalLength,
                (litsEnd - lits),
                "Too few literals");
        ZS_safecopy(out, lits, literalLength, ZS_wo_no_overlap);
        lits += literalLength;
        out += literalLength;

        //> Validate offset
        ZL_RET_R_IF_GT(
                corruption, offset, (out - outStart), "Offset too large");

        //> Copy match
        uint8_t const* const match = out - offset;
        ZS_safecopy(out, match, matchLength, ZS_wo_src_before_dst);
        out += matchLength;
    }
    if (lits != litsEnd) {
        size_t const lastLiterals = (size_t)(litsEnd - lits);
        ZL_RET_R_IF_GT(
                internalBuffer_tooSmall,
                lastLiterals,
                (size_t)(outEnd - out),
                "Output size too small for last lits");
        memcpy(out, lits, lastLiterals);
        lits += lastLiterals;
        out += lastLiterals;
    }

    ZL_RET_R_IF_NE(corruption, offs, offsEnd, "too many offsets");
    ZL_RET_R_IF_NE(corruption, ells, ellsEnd, "too many extra llens");
    ZL_RET_R_IF_NE(corruption, emls, emlsEnd, "too many extra mlens");

    ZL_ASSERT_EQ((out - outStart) % (1 << kEltBits), 0);

    size_t const nbElts = (size_t)(out - outStart) >> kEltBits;
    return ZL_returnValue(nbElts);
}

#if 1
#    define ZS_SHORT_LL_CODE 8u
#    define ZS_SHORT_ML_CODE 1u
#else
#    define ZS_SHORT_LL_CODE 4u
#    define ZS_SHORT_ML_CODE 11u
#endif

#define ZL_FIELD_LZ_DECOMPRESS_FN(eltBits, shortLLCode, shortMLCode) \
    ZS2_FieldLz_decompress_##eltBits##_##shortLLCode##_##shortMLCode

#define ZL_GEN_FIELDLZ_DECOMPRESS(eltBits, shortLLCode, shortMLCode)          \
    ZL_FORCE_NOINLINE ZL_Report ZL_FIELD_LZ_DECOMPRESS_FN(                    \
            eltBits, shortLLCode, shortMLCode)(                               \
            void* dst,                                                        \
            size_t dstEltCapacity,                                            \
            ZL_FieldLz_InSequences const* src)                                \
    {                                                                         \
        return ZL_FieldLz_decompress_impl2(                                   \
                dst, dstEltCapacity, src, eltBits, shortLLCode, shortMLCode); \
    }

#define ZL_GEN(X) \
    X(0, 14, 14)  \
    X(1, 8, 6)    \
    X(1, 8, 14)   \
    X(1, 14, 6)   \
    X(1, 14, 14)  \
    X(2, 4, 3)    \
    X(2, 4, 7)    \
    X(2, 4, 11)   \
    X(2, 4, 14)   \
    X(2, 8, 3)    \
    X(2, 8, 7)    \
    X(2, 8, 11)   \
    X(2, 8, 14)   \
    X(2, 14, 3)   \
    X(2, 14, 7)   \
    X(2, 14, 11)  \
    X(2, 14, 14)  \
    X(3, 2, 1)    \
    X(3, 2, 3)    \
    X(3, 2, 5)    \
    X(3, 2, 7)    \
    X(3, 4, 1)    \
    X(3, 4, 3)    \
    X(3, 4, 5)    \
    X(3, 4, 7)    \
    X(3, 6, 1)    \
    X(3, 6, 3)    \
    X(3, 6, 5)    \
    X(3, 6, 7)    \
    X(3, 8, 1)    \
    X(3, 8, 3)    \
    X(3, 8, 5)    \
    X(3, 8, 7)

ZL_GEN(ZL_GEN_FIELDLZ_DECOMPRESS)

typedef ZL_Report (
        *ZL_FieldLz_DecompressFn)(void*, size_t, ZL_FieldLz_InSequences const*);

typedef struct {
    unsigned eltBits;
    unsigned shortLLCode;
    unsigned shortMLCode;
    ZL_FieldLz_DecompressFn decompressFn;
} ZL_FieldLz_Decompress;

#define ZL_GEN_FIELDLZ_DECOMPRESS_FNS(eltBits, shortLLCode, shortMLCode) \
    { eltBits,                                                           \
      shortLLCode,                                                       \
      shortMLCode,                                                       \
      ZL_FIELD_LZ_DECOMPRESS_FN(eltBits, shortLLCode, shortMLCode) },

static ZL_FieldLz_Decompress const ZL_kDecompressors[] = { ZL_GEN(
        ZL_GEN_FIELDLZ_DECOMPRESS_FNS) };

static ZL_FieldLz_DecompressFn
ZL_selectDecompressor(unsigned eltBits, uint16_t const* tokens, size_t nbTokens)
{
    (void)tokens, (void)nbTokens;
    (void)ZL_kDecompressors;

    unsigned shortLLCode;
    unsigned shortMLCode;

    if (eltBits == 0) {
        return ZL_FIELD_LZ_DECOMPRESS_FN(0, 14, 14);
    }

    if ((0)) {
        if (eltBits == 1) {
            shortLLCode = shortMLCode = 14;
        }
        if (eltBits == 2) {
            shortLLCode = 4;
            shortMLCode = 7;
        }
        if (eltBits == 3) {
            shortLLCode = 8;
            shortMLCode = 1;
        }
    } else {
        uint32_t llHist[kTokenLLMask + 1] = { 0 };
        uint32_t mlHist[kTokenMLMask + 1] = { 0 };
        size_t const sampledTokens        = nbTokens / 16;
        size_t const sampledTokensOff0    = sampledTokens;
        size_t const sampledTokensOff1    = nbTokens - sampledTokens;
        for (size_t t = 0; t < sampledTokens; ++t) {
            size_t const token = ZL_read16(&tokens[sampledTokensOff0 + t]);
            ++llHist[(token >> kTokenOFBits) & kTokenLLMask];
            ++mlHist[(token >> (kTokenOFBits + kTokenLLBits)) & kTokenMLMask];
        }
        for (size_t t = 0; t < sampledTokens; ++t) {
            size_t const token = ZL_read16(&tokens[sampledTokensOff1 + t]);
            ++llHist[(token >> kTokenOFBits) & kTokenLLMask];
            ++mlHist[(token >> (kTokenOFBits + kTokenLLBits)) & kTokenMLMask];
        }
        size_t const total = sampledTokens * 2;
        size_t const p50   = total / 2;
        size_t const p90   = total - total / 8 + total / 32;
        size_t const p95   = total - total / 32 - total / 64;
        {
            size_t acc  = 0;
            shortLLCode = kMaxLitLengthCode;
            for (unsigned llCode = 0; llCode <= kTokenLLMask; ++llCode) {
                acc += llHist[llCode];
                if (acc >= p50) {
                    if (2 * llCode < shortLLCode) {
                        shortLLCode = 2 * llCode;
                    }
                }
                if (acc < p90) {
                    if (llCode > shortLLCode) {
                        shortLLCode = llCode;
                    }
                }
                if (acc >= p95) {
                    if (llCode < shortLLCode) {
                        shortLLCode = llCode;
                        break;
                    }
                }
            }
        }
        {
            size_t acc  = 0;
            shortMLCode = kMaxMatchLengthCode;
            for (unsigned mlCode = 0; mlCode <= kTokenMLMask; ++mlCode) {
                acc += mlHist[mlCode];
                if (acc >= p50) {
                    if (2 * mlCode < shortMLCode) {
                        shortMLCode = 2 * mlCode;
                    }
                }
                if (acc < p90) {
                    if (mlCode > shortMLCode) {
                        shortMLCode = mlCode;
                    }
                }
                if (acc >= p95) {
                    if (mlCode < shortMLCode) {
                        shortMLCode = mlCode;
                        break;
                    }
                }
            }
        }
    }

    ZL_FieldLz_Decompress const* best = NULL;
    int const nbDecompressors =
            sizeof(ZL_kDecompressors) / sizeof(ZL_kDecompressors[0]);
    for (int i = nbDecompressors - 1; i >= 0; --i) {
        ZL_FieldLz_Decompress const* curr = &ZL_kDecompressors[i];

        //> Find decompressors with the right eltBits
        if (curr->eltBits > eltBits)
            continue;
        if (curr->eltBits < eltBits)
            break;

        //> Set best to the max LL / ML decompressor
        if (best == NULL) {
            best = curr;
            continue;
        }

        //> If we're at a shorter LL code than we want, stop we already
        //> have the best.
        if (curr->shortLLCode < ZL_MIN(best->shortLLCode, shortLLCode))
            break;

        //> If the ML code is too short, continue
        if (curr->shortMLCode < shortMLCode)
            continue;

        if (curr->shortLLCode <= best->shortLLCode
            && curr->shortMLCode <= best->shortMLCode)
            best = curr;
    }

    if (best) {
        ZL_DLOG(BLOCK,
                "Selected decompress(eltBits=%u, shortLLCode=%u, shortMLCode=%u)",
                best->eltBits,
                best->shortLLCode,
                best->shortMLCode);
        return best->decompressFn;
    }
    ZL_DLOG(BLOCK, "Selected decompress(generic)");
    return NULL;
}

ZL_FORCE_NOINLINE ZL_Report ZL_FieldLz_decompress_generic(
        void* dst,
        size_t dstEltCapacity,
        ZL_FieldLz_InSequences const* src,
        size_t eltBits)
{
    size_t const shortLLCode = (size_t)32 >> eltBits;
    size_t const shortMLCode = (size_t)32 >> eltBits;
    return ZL_FieldLz_decompress_impl2(
            dst, dstEltCapacity, src, eltBits, shortLLCode, shortMLCode);
}

// tokenStats is only used in debug builds for logging token statistics.
// To avoid unused function warnings in release builds, we conditionally compile
// it.
#ifndef NDEBUG
static void tokenStats(uint16_t const* tokens, size_t nbTokens)
{
    uint32_t llHist[kTokenLLMask + 1] = { 0 };
    uint32_t mlHist[kTokenMLMask + 1] = { 0 };
    for (size_t t = 0; t < nbTokens; ++t) {
        size_t const token = ZL_read16(&tokens[t]);
        ++llHist[(token >> kTokenOFBits) & kTokenLLMask];
        ++mlHist[(token >> (kTokenOFBits + kTokenLLBits)) & kTokenMLMask];
    }
    double llAcc = 0;
    double mlAcc = 0;
    for (size_t i = 0; i < 16; ++i) {
        llAcc += llHist[i];
        mlAcc += mlHist[i];
        double const llPct = 100 * llAcc / (double)nbTokens;
        double const mlPct = 100 * mlAcc / (double)nbTokens;
        ZL_DLOG(V5, "%2zu: ll=%.1f | ml=%.1f", i, llPct, mlPct);
    }
}
#endif

ZL_Report ZS2_FieldLz_decompress(
        void* dst,
        size_t dstEltCapacity,
        size_t eltWidth,
        ZL_FieldLz_InSequences const* src)
{
    if (src->nbTokens == 0) {
        ZL_RET_R_IF_GT(
                internalBuffer_tooSmall, src->nbLiteralElts, dstEltCapacity);
        memcpy(dst, src->literalElts, src->nbLiteralElts * eltWidth);
        return ZL_returnValue(src->nbLiteralElts);
    }
    if (!ZL_isPow2(eltWidth)) {
        ZL_LOG(ERROR, "eltWidth %u is not a power of 2", (unsigned)eltWidth);
        ZL_RET_R_ERR(compressionParameter_invalid);
    }
#ifndef NDEBUG
    tokenStats(src->tokens, src->nbTokens);
#endif
    uint32_t const eltBits = (uint32_t)ZL_highbit32((uint32_t)eltWidth);
    ZL_FieldLz_DecompressFn const decompress =
            ZL_selectDecompressor(eltBits, src->tokens, src->nbTokens);
    if (decompress != NULL)
        return decompress(dst, dstEltCapacity, src);
    else
        return ZL_FieldLz_decompress_generic(dst, dstEltCapacity, src, eltBits);
}
