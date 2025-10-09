// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/lz/common_field_lz.h"
#include "openzl/codecs/lz/encode_field_lz_sequences.h"
#include "openzl/codecs/lz/encode_match_finder.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_errors.h"

size_t ZL_FieldLz_maxNbSequences(size_t nbElts, size_t eltWidth)
{
    size_t const minMatch = kMinMatch(eltWidth);
    return nbElts / minMatch + 1;
}

static bool resolveLevel(
        ZS_MatchFinderParameters* params,
        size_t nbElts,
        size_t eltWidth,
        int level)
{
    if (level <= 0)
        level = 3;
    if (level >= 5)
        level = 5;
    bool greedy = level > 3;
    memset(params, 0, sizeof(*params));
    params->lzLargeMatch = true;
    if (level == 1) {
        params->lzTableLog   = 18;
        params->lzLargeMatch = false;
    }
    if (level == 2) {
        params->lzTableLog = 18;
    }
    if (level == 3) {
        params->lzTableLog = 19;
    }
    if (level == 4) {
        params->lzTableLog = 20;
    }
    if (level == 5) {
        params->lzTableLog = 22;
    }
    unsigned const srcLog = (unsigned)ZL_highbit32((unsigned)nbElts + 1) + 1;
    if (srcLog < params->lzTableLog)
        params->lzTableLog = srcLog;
    if (params->lzTableLog < 10)
        params->lzTableLog = 10;

    params->fieldSize = (uint32_t)eltWidth;
    return greedy;
}

static ZL_Report writeOutSequences(
        ZL_FieldLz_OutSequences* dst,
        ZS_seqStore const* seqStore,
        size_t eltWidth)
{
    int const eltBits = ZL_highbit32((uint32_t)eltWidth);
    //> Copy literals
    {
        size_t const litsSize =
                (size_t)(seqStore->lits.ptr - seqStore->lits.start);
        ZL_ASSERT_EQ(litsSize % eltWidth, 0);
        size_t const nbLits = litsSize >> eltBits;
        ZL_RET_R_IF_GT(
                internalBuffer_tooSmall, nbLits, dst->literalEltsCapacity);
        memcpy(dst->literalElts, seqStore->lits.start, litsSize);
        dst->nbLiteralElts = nbLits;
    }

    //> Write sequences
    {
        size_t const nbSeqs =
                (size_t)(seqStore->seqs.ptr - seqStore->seqs.start);
        ZS_sequence const* seqs = seqStore->seqs.start;
        uint32_t const minMatch = (uint32_t)kMinMatch(eltWidth);

        dst->nbTokens              = 0;
        dst->nbOffsets             = 0;
        dst->nbExtraLiteralLengths = 0;
        dst->nbExtraMatchLengths   = 0;

        for (size_t s = 0; s < nbSeqs; ++s) {
            uint16_t token = 0;
            if (seqs[s].matchType == ZS_mt_rep) {
                ZL_ASSERT_LT(seqs[s].matchCode, 3);
                token |= (uint16_t)seqs[s].matchCode;
            } else if (seqs[s].matchType == ZS_mt_lz) {
                uint32_t const offset = seqs[s].matchCode;
                token |= 3;
                ZL_RET_R_IF_GE(
                        internalBuffer_tooSmall,
                        dst->nbOffsets,
                        dst->sequencesCapacity);
                // Offset has already been reduced by eltBits.
                // TODO(terrelln): Clean up the eltBits reduction logic.
                dst->offsets[dst->nbOffsets++] = offset;
            } else {
                ZL_ASSERT_FAIL("Bad match type!");
            }

            ZL_ASSERT_EQ(seqs[s].literalLength % eltWidth, 0);
            ZL_ASSERT_EQ(seqs[s].matchLength % eltWidth, 0);
            ZL_ASSERT_GE(seqs[s].matchLength >> eltBits, minMatch);

            uint32_t literalLengthCode = seqs[s].literalLength >> eltBits;
            uint32_t matchLengthCode =
                    (seqs[s].matchLength >> eltBits) - minMatch;

            if (literalLengthCode < kMaxLitLengthCode) {
                token |= (uint16_t)(literalLengthCode << kTokenOFBits);
            } else {
                token |= (uint16_t)(kMaxLitLengthCode << kTokenOFBits);
                ZL_RET_R_IF_GE(
                        internalBuffer_tooSmall,
                        dst->nbExtraLiteralLengths,
                        dst->sequencesCapacity);
                dst->extraLiteralLengths[dst->nbExtraLiteralLengths++] =
                        literalLengthCode - kMaxLitLengthCode;
            }

            if (matchLengthCode < kMaxMatchLengthCode) {
                token |= (uint16_t)(matchLengthCode
                                    << (kTokenOFBits + kTokenLLBits));
            } else {
                token |= (uint16_t)(kMaxMatchLengthCode
                                    << (kTokenOFBits + kTokenLLBits));
                ZL_RET_R_IF_GE(
                        internalBuffer_tooSmall,
                        dst->nbExtraMatchLengths,
                        dst->sequencesCapacity);
                dst->extraMatchLengths[dst->nbExtraMatchLengths++] =
                        matchLengthCode - kMaxMatchLengthCode;
            }
            ZL_RET_R_IF_GE(
                    internalBuffer_tooSmall,
                    dst->nbTokens,
                    dst->sequencesCapacity);
            dst->tokens[dst->nbTokens++] = token;
        }
    }
    return ZL_returnSuccess();
}

ZL_Report ZL_FieldLz_compress(
        ZL_FieldLz_OutSequences* dst,
        void const* src,
        size_t nbElts,
        size_t eltWidth,
        int level,
        ZL_FieldLz_Allocator alloc)
{
    if (!ZL_isPow2(eltWidth)) {
        ZL_LOG(ERROR, "eltWidth %u is not a power of 2", (unsigned)eltWidth);
        ZL_RET_R_ERR(compressionParameter_invalid);
    }

    ZL_Report ret;
    size_t const srcSize = nbElts * eltWidth;
    ZS_MatchFinderParameters params;
    bool const greedy = resolveLevel(&params, nbElts, eltWidth, level);
    params.alloc      = alloc;

    ZS_seqStore seqStore;
    ZS_window window;
    {
        int error = ZS_seqStore_initBound(
                &seqStore, srcSize, ZL_MAX(eltWidth, 4), alloc);
        error |=
                ZS_window_init(&window, ZL_MIN((uint32_t)srcSize, 1u << 23), 8);
        ZL_RET_R_IF(allocation, error);
    }
    ZS_matchFinder const* matchFinder =
            greedy ? &ZS_greedyTokenLzMatchFinder : &ZS_tokenLzMatchFinder;
    ZS_matchFinderCtx* mfCtx = matchFinder->ctx_create(&window, &params);
    ZL_RET_R_IF_NULL(allocation, mfCtx);

    ZS_window_update(&window, (uint8_t const*)src, srcSize);
    // TODO(terrelln): We can write directly to the output streams
    matchFinder->parse(mfCtx, &seqStore, (uint8_t const*)src, srcSize);

    ret = writeOutSequences(dst, &seqStore, eltWidth);

    return ret;
}
