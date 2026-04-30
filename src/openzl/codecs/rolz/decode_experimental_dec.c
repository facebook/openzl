// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdlib.h>
#include <string.h>

#include "openzl/codecs/common/copy.h"
#include "openzl/codecs/common/window.h"
#include "openzl/codecs/entropy/deprecated/common_entropy.h"
#include "openzl/codecs/rolz/common_markov.h"
#include "openzl/codecs/rolz/decode_decoder.h"
#include "openzl/codecs/rolz/decode_rep.h"
#include "openzl/codecs/rolz/decode_rolz_kernel.h"
#include "openzl/common/allocation.h"
#include "openzl/common/cursor.h"
#include "openzl/fse/bitstream.h"
#include "openzl/shared/clustering.h"
#include "openzl/zl_errors.h"

static ZS_decoderCtx* ZS_decoderCtx_create(void)
{
    return (ZS_decoderCtx*)ZL_malloc(sizeof(ZS_decoderCtx));
}

static void ZS_decoderCtx_release(ZS_decoderCtx* ctx)
{
    ZL_free(ctx);
}

static void ZS_decoderCtx_reset(ZS_decoderCtx* ctx)
{
    (void)ctx;
}

#define kMaxNumClusters 256

typedef struct {
    bool o1;
    uint8_t* lits;
    uint8_t* litsEnd;
    size_t numLits;
    size_t litsConsumed;
    size_t numClusters;
    uint8_t* o1LitsByCluster[kMaxNumClusters];
    uint8_t* o1LitsEndByCluster[kMaxNumClusters];
    uint8_t** o1LitsByContext[256];
} ZS_Lits;

static ZL_Report decodeCodes(uint8_t* codes, size_t numSequences, ZL_RC* src);

static ZL_Report decodeLiterals(ZS_Lits* lits, ZL_RC* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (lits->numLits == 0) {
        lits->o1 = false;
        return ZL_returnSuccess();
    }
    ZL_ERR_IF_LT(ZL_RC_avail(src), 1, srcSize_tooSmall);
    lits->o1 = ZL_RC_pop(src);
    // ZL_WC out = ZL_WC_wrap(lits->lits, lits->numLits);
    if (!lits->o1) {
        return decodeCodes(lits->lits, lits->numLits, src);
    }

    ZL_ContextClustering clustering;

    ZL_ERR_IF_ERR(ZL_ContextClustering_decode(&clustering, src));
    ZL_ASSERT_LE(clustering.numClusters, kMaxNumClusters);
    lits->numClusters = clustering.numClusters;

    uint8_t* litPtr       = lits->lits;
    uint8_t* const litEnd = litPtr + lits->numLits;
    for (size_t c = 0; c < clustering.numClusters; ++c) {
        ZL_ERR_IF_LT(ZL_RC_avail(src), 4, srcSize_tooSmall);
        uint32_t const numLits = ZL_RC_popLE32(src);
        ZL_ERR_IF_GT(numLits, (size_t)(litEnd - litPtr), corruption);
        lits->o1LitsByCluster[c]    = litPtr;
        lits->o1LitsEndByCluster[c] = litPtr + numLits;
        ZL_ERR_IF_ERR(decodeCodes(litPtr, numLits, src));
        litPtr += numLits;
        // lits->o1LitsByCluster[c] = ZL_WC_ptr(&out);
        // ZL_TRY_LET_CONST_T(uint64_t, numSplits, ZL_RC_popVarint(src));
        // for (uint64_t i = 0; i <= numSplits; ++i) {
        //   ZL_RC_REQUIRE_HAS(src, 4);
        //   uint32_t const size = ZL_RC_popLE32(src);
        //   ZL_RC_REQUIRE_HAS(src, size);
        //   ZL_RC in = ZL_RC_prefix(src, size);
        //   ZL_RC_advance(src, size);
        //   ZL_REQUIRE_SUCCESS(ZS_fseDecode(&out, &in));
        // }
    }
    // ZL_REQUIRE_EQ(ZL_WC_avail(&out), 0);
    ZL_ERR_IF_NE((uint32_t)(litPtr - lits->lits), lits->numLits, corruption);
    for (uint32_t ctx = 0; ctx < 256; ++ctx) {
        if (ctx > clustering.maxSymbol)
            lits->o1LitsByContext[ctx] = NULL;
        else
            lits->o1LitsByContext[ctx] =
                    &lits->o1LitsByCluster[clustering.contextToCluster[ctx]];
    }
    return ZL_returnSuccess();
}

typedef enum {
    ZS_EntropyDecoder_huf = 0,
    ZS_EntropyDecoder_fse = 1,
} ZS_EntropyDecoder;

static ZL_Report decodeCodes(uint8_t* codes, size_t numSequences, ZL_RC* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZS_Entropy_DecodeParameters params = {
        .allowedTypes = ZS_Entropy_TypeMask_all,
        .tableManager = NULL,
    };
    ZL_ERR_IF_ERR(ZS_Entropy_decode(codes, numSequences, src, 1, &params));
    return ZL_returnSuccess();
}

static ZL_Report
decodeMatchTypes(uint8_t* codes, size_t numSequences, ZL_RC* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_ERR(decodeCodes(codes, numSequences, src));
    for (size_t c = 0; c < numSequences; ++c) {
        ZL_ERR_IF_GE(codes[c], 4, corruption, "Invalid match type!");
    }
    return ZL_returnSuccess();
}

static uint32_t const base[] = {
    0,         1,          2,          3,          4,         5,
    6,         7,          8,          9,          10,        11,
    12,        13,         14,         15,         16,        17,
    18,        19,         20,         21,         22,        23,
    24,        25,         26,         27,         28,        29,
    30,        31,         0x20,       0x40,       0x80,      0x100,
    0x200,     0x400,      0x800,      0x1000,     0x2000,    0x4000,
    0x8000,    0x10000,    0x20000,    0x40000,    0x80000,   0x100000,
    0x200000,  0x400000,   0x800000,   0x1000000,  0x2000000, 0x4000000,
    0x8000000, 0x10000000, 0x20000000, 0x40000000, 0x80000000
};
static uint32_t bits[] = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
                           0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
                           0,  0,  0,  0,  0,  0,  0,  0,  5,  6,  7,  8,
                           9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                           21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
ZL_STATIC_ASSERT(sizeof(base) == sizeof(bits), "Assumption");

static ZL_Report decodeSeq(
        uint32_t* values,
        size_t numSequences,
        uint8_t const* types,
        ZL_RC* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (numSequences == 0)
        return ZL_returnSuccess();
    ZL_ERR_IF_LT(
            ZL_RC_avail(src), 4 + 4 * ZS_MARKOV_NUM_STATES, srcSize_tooSmall);
    uint32_t const bitSize = ZL_RC_popLE32(src);
    uint32_t totals[ZS_MARKOV_NUM_STATES];
    for (size_t m = 0; m < ZS_MARKOV_NUM_STATES; ++m) {
        totals[m] = ZL_RC_popLE32(src);
        if (m != 0)
            ZL_ERR_IF_GT(totals[m - 1], totals[m], corruption);
    }
    ZL_ERR_IF_NE(totals[ZS_MARKOV_NUM_STATES - 1], numSequences, GENERIC);
    // uint32_t const fseSize = ZL_RC_popLE32(src);
    ZL_ERR_IF_LT(ZL_RC_avail(src), bitSize, srcSize_tooSmall);

    BIT_DStream_t dstream;
    ZL_ERR_IF(
            ERR_isError(BIT_initDStream(&dstream, ZL_RC_ptr(src), bitSize)),
            corruption,
            "bitstream is corrupt");
    ZL_RC_advance(src, bitSize);

    uint8_t* const codes = ZL_malloc(numSequences);
    ZL_ERR_IF_NULL(codes, allocation);
    for (size_t m = 0; m < ZS_MARKOV_NUM_STATES; ++m) {
        uint32_t offset = m == 0 ? 0 : totals[m - 1];
        ZL_ASSERT_LE(offset, numSequences);
        ZL_ASSERT_GE(totals[m], offset);
        ZL_ASSERT_LE(totals[m], numSequences);
        ZL_Report report = decodeCodes(codes + offset, totals[m] - offset, src);
        if (ZL_isError(report)) {
            ZL_free(codes);
            return report;
        }
    }
    for (size_t m = ZS_MARKOV_NUM_STATES - 1; m-- > 0;) {
        totals[m + 1] = totals[m];
    }
    totals[0] = 0;

    uint32_t state = ZS_MARKOV_RZ_INITIAL_STATE;
    for (size_t s = 0; s < numSequences; ++s) {
        state            = ZS_markov_nextState(state, types[s]);
        size_t const idx = totals[state]++;
        if (idx >= numSequences) {
            ZL_free(codes);
            ZL_ERR(corruption, "Invalid state!");
        }
        uint8_t const code = codes[idx];
        if (code >= ZL_ARRAY_SIZE(base)) {
            ZL_free(codes);
            ZL_ERR(corruption, "Invalid code!");
        }
        values[s] = base[code] + (uint32_t)BIT_readBits(&dstream, bits[code]);
        BIT_reloadDStream(&dstream);
    }
    ZL_free(codes);
    ZL_ERR_IF_NE(totals[ZS_MARKOV_NUM_STATES - 1], numSequences, GENERIC);
    return ZL_returnSuccess();
}

static ZL_Report decodeLitLengths(
        uint32_t* codes,
        size_t numSequences,
        uint8_t const* types,
        ZL_RC* src)
{
    return decodeSeq(codes, numSequences, types, src);
    // if (numSequences == 0)
    //   return;
    // ZL_RC_REQUIRE_HAS(src, 4);
    // uint32_t const cSize = ZL_RC_popLE32(src);
    // ZL_RC in             = ZL_RC_prefix(src, cSize);
    // ZL_RC_REQUIRE_HAS(src, cSize);
    // ZL_RC_advance(src, cSize);
    // ZL_REQUIRE_SUCCESS(ZS_baseBitsDecode(codes, numSequences, &in, base,
    // bits));
}

static ZL_Report decodeMatchLengths(
        uint32_t* codes,
        size_t numSequences,
        uint8_t const* types,
        ZL_RC* src)
{
    return decodeSeq(codes, numSequences, types, src);
}

static ZL_Report decodeMatchCodes(
        uint32_t* codes,
        size_t numSequences,
        uint8_t const* types,
        ZL_RC* src)
{
    return decodeSeq(codes, numSequences, types, src);
}

static uint32_t const kContextDepth       = 2;
static uint32_t const kContextLog         = 12;
static uint32_t const kRolzRowLog         = 4;
static bool const kRolzPredictMatchLength = true;
static uint32_t const kRolzMinLength      = 3;
static uint32_t const kLzMinLength        = 7;
static uint32_t const kRepMinLength       = 3;

ZL_FORCE_INLINE size_t ZS_execExperimentalSequence2(
        ZS_sequence seq,
        ZS_RolzDTable2* table,
        ZS_window const* window,
        ZS_rep* reps,
        ZS_Lits* lits,
        uint8_t* ostart,
        uint8_t* op,
        uint8_t* oend,
        bool kO1)
{
    ZL_DLOG(SEQ,
            "lpos = %u | mpos = %u | mt = %u | mc = %u | ll = %u | ml = %u",
            (uint32_t)(op - ostart),
            (uint32_t)(op - ostart) + seq.literalLength,
            seq.matchType,
            seq.matchCode,
            seq.literalLength,
            seq.matchLength);
    // Literals
    size_t const numLits = seq.literalLength;
    ZL_ASSERT_LE(op, oend);
    if (numLits > (size_t)(oend - op)
        || numLits > (size_t)(lits->numLits - lits->litsConsumed))
        return 0;
    ZL_ASSERT_EQ(kO1, lits->o1);
    ZL_ASSERT_EQ(kContextDepth, table->contextDepth);
    ZL_ASSERT_GE((size_t)(op - ostart), kContextDepth);
    ZL_ASSERT_NE(op, ostart);
    ZL_ASSERT(kRolzInsertLits);

    if (kO1) {
        uint8_t ctx = op[-1];
        for (size_t l = 0; l < numLits; ++l) {
            uint8_t** litPtr = lits->o1LitsByContext[ctx];
            if (litPtr == NULL)
                return 0;
            op[l] = ctx = **litPtr;
            ++*litPtr;
        }
    } else {
        uint8_t* const oLitEnd       = op + numLits;
        uint8_t const* const iLitEnd = lits->lits + numLits;
        if (ZL_UNLIKELY(
                    oLitEnd > oend - ZS_WILDCOPY_OVERLENGTH
                    || iLitEnd > lits->litsEnd - ZS_WILDCOPY_OVERLENGTH)) {
            memcpy(op, lits->lits, numLits);
        } else {
            ZS_wildcopy(op, lits->lits, (ptrdiff_t)numLits, ZS_wo_no_overlap);
        }
        lits->lits += numLits;
    }
    // Update rolz table
    for (size_t i = 0; i < numLits; ++i, ++op) {
        uint32_t const ctx = ZS_rolz_getContext(op, kContextDepth, kContextLog);
        ZS_RolzDTable2_put2(
                table, ctx, (uint32_t)(op - window->base), 0, kRolzRowLog);
    }
    lits->litsConsumed += numLits;

    uint8_t const* match;
    uint32_t matchLength = seq.matchLength;

    if (seq.matchType == ZS_mt_rolz) {
        uint32_t const ctx = ZS_rolz_getContext(op, kContextDepth, kContextLog);
        ZS_RolzMatch2 const m = ZS_RolzDTable2_get2(
                table,
                ctx,
                seq.matchCode,
                seq.matchLength,
                kRolzRowLog,
                kRolzMinLength,
                kRolzPredictMatchLength);
        match       = window->base + m.index;
        matchLength = m.length;
        ZL_DLOG(SEQ,
                "ctx=%u off=%u mlen-%u",
                ctx,
                (uint32_t)(op - match),
                matchLength);
        if (m.index < window->lowLimit)
            return 0;
        *reps = ZS_rep_update(
                reps, ZS_NO_REP, (uint32_t)(op - match), matchLength);
        ZS_RolzDTable2_put2(
                table,
                ctx,
                (uint32_t)(op - window->base),
                matchLength,
                kRolzRowLog);
        {
            uint8_t* const oMatchEnd = op + matchLength;
            if (ZL_UNLIKELY(oMatchEnd > oend - ZS_WILDCOPY_OVERLENGTH)) {
                if (oMatchEnd > oend)
                    return 0;
                uint8_t* p        = op;
                uint8_t const* mp = match;
                while (p < oMatchEnd)
                    *p++ = *mp++;
            } else {
                ZS_wildcopy(op, match, matchLength, ZS_wo_src_before_dst);
            }
        }
    } else {
        if (seq.matchType == ZS_mt_lz) {
            ZL_ASSERT_GT(seq.matchCode, 0);
            if (seq.matchCode > (size_t)(op - ostart))
                return 0;
            match = op - seq.matchCode;
            matchLength += kLzMinLength;
            *reps = ZS_rep_update(
                    reps, ZS_NO_REP, seq.matchCode, seq.matchLength);
        } else {
            ZL_ASSERT(
                    seq.matchType == ZS_mt_rep0 || seq.matchType == ZS_mt_rep);
            uint32_t const rep = seq.matchCode;
            ZL_ASSERT_LT(rep & 3, 3); // 3 isn't allowed
            uint32_t const prevOff = reps->reps[rep & 3];
            ZL_ASSERT_NE(prevOff, 0);
            uint32_t const offset =
                    rep == 0 ? prevOff : prevOff + (rep >> 2) - REP_SUB;
            *reps = ZS_rep_update(reps, rep, offset, seq.matchLength);
            ZL_DLOG(SEQ, "mpos=%u off=%u", (uint32_t)(op - ostart), offset);
            match = op - offset;
            matchLength += kRepMinLength;
        }
        {
            uint8_t* const oMatchEnd = op + matchLength;
            if (ZL_UNLIKELY(oMatchEnd > oend - ZS_WILDCOPY_OVERLENGTH)) {
                if (oMatchEnd > oend)
                    return 0;
                uint8_t* p        = op;
                uint8_t const* mp = match;
                while (p < oMatchEnd)
                    *p++ = *mp++;
            } else {
                ZS_wildcopy(op, match, matchLength, ZS_wo_src_before_dst);
            }
        }

        uint32_t const ctx0 =
                ZS_rolz_getContext(op, kContextDepth, kContextLog);
        ZS_RolzDTable2_put2(
                table,
                ctx0,
                (uint32_t)(op - window->base),
                matchLength,
                kRolzRowLog);
        // 8% faster with P1 disabled, but also less compression
        if (P1) {
            // Reading from op[1] is a store forward. Instead read from
            // match[0].
            ZL_ASSERT_EQ(kContextDepth, 2);
            uint64_t const mdata = *match;
            uint32_t const ctx1  = ZS_rolz_hashContext(
                    (uint64_t)op[-1] | (mdata << 8),
                    kContextDepth,
                    kContextLog);
            // ZS_rolz_getContext(op+1, kContextDepth, kContextLog);
            ZS_RolzDTable2_put2(
                    table,
                    ctx1,
                    (uint32_t)(op - window->base) + 1,
                    matchLength - 1,
                    kRolzRowLog);
        }
    }
    ZL_DLOG(SEQ, "%u", seq.matchType);
    ZL_ASSERT_GE(matchLength, 2);
    ZL_ASSERT_GE(match, ostart);
    ZL_ASSERT_LT(match, op);

    return numLits + matchLength;
}

ZL_FORCE_INLINE size_t ZS_execExperimentalSequence(
        ZS_sequence seq,
        ZS_RolzDTable2* table,
        ZS_window const* window,
        ZS_rep* reps,
        ZS_Lits* lits,
        uint8_t* ostart,
        uint8_t* op,
        uint8_t* oend,
        uint32_t lzMinLength,
        uint32_t repMinLength)
{
    ZL_DLOG(SEQ,
            "lpos = %u | mpos = %u | mt = %u | mc = %u | ll = %u | ml = %u",
            (uint32_t)(op - ostart),
            (uint32_t)(op - ostart) + seq.literalLength,
            seq.matchType,
            seq.matchCode,
            seq.literalLength,
            seq.matchLength);
    // Literals
    size_t const numLits = seq.literalLength;
    ZL_ASSERT_LE(op, oend);
    if (numLits > (size_t)(oend - op)
        || numLits > (size_t)(lits->numLits - lits->litsConsumed))
        return 0;
    if (lits->o1) {
        uint8_t ctx = (op == ostart) ? 0 : op[-1];
        for (size_t l = 0; l < numLits; ++l) {
            uint8_t** litPtr = lits->o1LitsByContext[ctx];
            if (litPtr == NULL)
                return 0;
            op[l] = ctx = **litPtr;
            ++*litPtr;
        }
    } else {
        memcpy(op, lits->lits, numLits);
        lits->lits += numLits;
    }
    lits->litsConsumed += numLits;
    // Update rolz table
    if (kRolzInsertLits) {
        size_t const start =
                op - ostart < table->contextDepth ? table->contextDepth : 0;
        op += ZL_MIN(start, numLits);
        for (size_t i = start; i < numLits; ++i, ++op) {
            uint32_t const ctx = ZS_rolz_getContext(
                    op, table->contextDepth, table->contextLog);
            ZS_RolzDTable2_put(table, ctx, (uint32_t)(op - window->base), 0);
        }
    } else
        op += numLits;

    uint8_t const* match;
    uint32_t matchLength = seq.matchLength;
    switch (seq.matchType) {
        case ZS_mt_lz:
            if (seq.matchCode == 0 || seq.matchCode > (size_t)(op - ostart))
                return 0;
            match = op - seq.matchCode;
            matchLength += lzMinLength;
            *reps = ZS_rep_update(
                    reps, ZS_NO_REP, seq.matchCode, seq.matchLength);
            break;
        case ZS_mt_rolz: {
            uint32_t const ctx = ZS_rolz_getContext(
                    op, table->contextDepth, table->contextLog);
            ZS_RolzMatch2 const m = ZS_RolzDTable2_get(
                    table, ctx, seq.matchCode, seq.matchLength);
            match       = window->base + m.index;
            matchLength = m.length;
            ZL_DLOG(SEQ,
                    "ctx=%u off=%u mlen-%u",
                    ctx,
                    (uint32_t)(op - match),
                    matchLength);
            if (m.index < window->lowLimit)
                return 0;
            *reps = ZS_rep_update(
                    reps, ZS_NO_REP, (uint32_t)(op - match), matchLength);
            ZS_RolzDTable2_put(
                    table, ctx, (uint32_t)(op - window->base), matchLength);
            break;
        }
        case ZS_mt_rep0:
        case ZS_mt_rep: {
            uint32_t const rep = seq.matchCode;
            ZL_ASSERT_LT(rep & 3, 3); // 3 isn't allowed
            uint32_t const prevOff = reps->reps[rep & 3];
            ZL_ASSERT_NE(prevOff, 0);
            uint32_t const offset =
                    rep == 0 ? prevOff : prevOff + (rep >> 2) - REP_SUB;
            *reps = ZS_rep_update(reps, rep, offset, seq.matchLength);
            ZL_DLOG(SEQ, "mpos=%u off=%u", (uint32_t)(op - ostart), offset);
            match = op - offset;
            matchLength += repMinLength;
            break;
        }
        case ZS_mt_lits:
            ZL_ASSERT_EQ(seq.matchLength, 0);
            return numLits;
        case ZS_mt_lzn:
        default:
            return 0;
    }
    ZL_DLOG(SEQ, "%u", seq.matchType);
    ZL_ASSERT_GE(matchLength, 2);
    ZL_ASSERT_GE(match, ostart);
    ZL_ASSERT_LT(match, op);

    if (op + matchLength > oend) {
        return 0;
    }
    for (size_t i = 0; i < matchLength; ++i) {
        op[i] = match[i];
    }

    if (seq.matchType != ZS_mt_rolz && op - ostart >= table->contextDepth) {
        uint32_t const ctx0 =
                ZS_rolz_getContext(op, table->contextDepth, table->contextLog);
        ZS_RolzDTable2_put(
                table, ctx0, (uint32_t)(op - window->base), matchLength);
        if (P1) {
            uint32_t const ctx1 = ZS_rolz_getContext(
                    op + 1, table->contextDepth, table->contextLog);
            ZS_RolzDTable2_put(
                    table,
                    ctx1,
                    (uint32_t)(op - window->base) + 1,
                    matchLength - 1);
        }
    }

    return numLits + matchLength;
}

static ZL_Report ZS_experimentalDecoder_decompress(
        ZS_decoderCtx* ctx,
        uint8_t* const dst,
        size_t const capacity,
        uint8_t const* const src,
        size_t size)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    (void)ctx;
    uint8_t* const ostart = dst;
    uint8_t* op           = ostart;
    uint8_t* const oend   = ostart + capacity;

    ZL_RC in = ZL_RC_wrap(src, size);

    // Bounds checks
    ZL_ERR_IF_LT(ZL_RC_avail(&in), 15, corruption);
    uint32_t const rolzContextDepth   = ZL_RC_pop(&in);
    uint32_t const rolzContextLog     = ZL_RC_pop(&in);
    uint32_t const rolzRowLog         = ZL_RC_pop(&in);
    uint32_t const rolzMinLength      = ZL_RC_pop(&in);
    bool const rolzPredictMatchLength = ZL_RC_pop(&in);
    uint32_t const lzMinLength        = ZL_RC_pop(&in);
    uint32_t const repMinLength       = ZL_RC_pop(&in);
    uint32_t const numLiterals        = ZL_RC_popLE32(&in);
    uint32_t const numSequences       = ZL_RC_popLE32(&in);

    switch (rolzContextDepth) {
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 12:
        case 16:
            break;
        default:
            ZL_ERR(node_invalid_input,
                   "Invalid contextDepth %u",
                   rolzContextDepth);
    }
    ZL_ERR_IF_EQ(
            rolzContextLog,
            0,
            node_invalid_input,
            "contextLog must be greater than 0");
    ZL_ERR_IF_EQ(
            rolzRowLog, 0, node_invalid_input, "rowLog must be greater than 0");
    ZL_ERR_IF_GT(
            rolzContextLog + rolzRowLog,
            27,
            node_invalid_input,
            "contextLog + rowLog exceeds maximum");
    ZL_ERR_IF_GE(numSequences, (1 << 30), GENERIC, "too many sequences");
    ZL_ERR_IF_GE(numLiterals, (1 << 30), GENERIC, "too many literals");

    ZS_window window;
    ZS_RolzDTable2 rolz;
    ZS_rep reps = ZS_initialReps;
    ZL_ERR_IF(ZS_window_init(&window, (uint32_t)(oend - ostart), 8), GENERIC);
    ZL_ERR_IF(
            ZL_isError(ZS_RolzDTable2_init(
                    &rolz,
                    rolzContextDepth,
                    rolzContextLog,
                    rolzRowLog,
                    rolzMinLength,
                    rolzPredictMatchLength)),
            GENERIC);

    ZS_Lits lits;
    uint8_t* litsBuffer = ZL_malloc(numLiterals + ZS_WILDCOPY_OVERLENGTH);

    uint8_t* mts  = ZL_malloc(numSequences);
    uint32_t* lls = ZL_malloc(numSequences * sizeof(uint32_t));
    uint32_t* mls = ZL_malloc(numSequences * sizeof(uint32_t));
    uint32_t* mcs = ZL_malloc(numSequences * sizeof(uint32_t));

    if (litsBuffer == NULL || mts == NULL || lls == NULL || mls == NULL
        || mcs == NULL)
        goto _error;

    lits.lits         = litsBuffer;
    lits.litsEnd      = litsBuffer + numLiterals;
    lits.numLits      = numLiterals;
    lits.litsConsumed = 0;

    if (ZL_isError(decodeLiterals(&lits, &in)))
        goto _error;

    if (ZL_isError(decodeMatchTypes(mts, numSequences, &in)))
        goto _error;
    if (ZL_isError(decodeLitLengths(lls, numSequences, mts, &in)))
        goto _error;
    if (ZL_isError(decodeMatchLengths(mls, numSequences, mts, &in)))
        goto _error;
    if (ZL_isError(decodeMatchCodes(mcs, numSequences, mts, &in)))
        goto _error;

    ZS_window_update(&window, ostart, (size_t)(oend - ostart));
    size_t i;
    for (i = 0; i < numSequences && op - ostart < rolz.contextDepth; ++i) {
        ZS_sequence seq;
        seq.literalLength    = lls[i];
        seq.matchCode        = mcs[i];
        seq.matchLength      = mls[i];
        seq.matchType        = mts[i];
        size_t const seqSize = ZS_execExperimentalSequence(
                seq,
                &rolz,
                &window,
                &reps,
                &lits,
                ostart,
                op,
                oend,
                lzMinLength,
                repMinLength);
        if (seqSize == 0)
            goto _error;
        op += seqSize;
    }
    if (rolz.contextDepth == kContextDepth && rolz.contextLog == kContextLog
        && rolz.rowLog == kRolzRowLog
        && rolz.predictMatchLength == kRolzPredictMatchLength
        && rolz.minLength == kRolzMinLength && lzMinLength == kLzMinLength
        && repMinLength == kRepMinLength) {
        if (lits.o1) {
            for (; i < numSequences; ++i) {
                ZS_sequence seq;
                seq.literalLength    = lls[i];
                seq.matchCode        = mcs[i];
                seq.matchLength      = mls[i];
                seq.matchType        = mts[i];
                size_t const seqSize = ZS_execExperimentalSequence2(
                        seq,
                        &rolz,
                        &window,
                        &reps,
                        &lits,
                        ostart,
                        op,
                        oend,
                        true);
                if (seqSize == 0)
                    goto _error;
                op += seqSize;
            }
        } else {
            for (; i < numSequences; ++i) {
                ZS_sequence seq;
                seq.literalLength    = lls[i];
                seq.matchCode        = mcs[i];
                seq.matchLength      = mls[i];
                seq.matchType        = mts[i];
                size_t const seqSize = ZS_execExperimentalSequence2(
                        seq,
                        &rolz,
                        &window,
                        &reps,
                        &lits,
                        ostart,
                        op,
                        oend,
                        false);
                if (seqSize == 0)
                    goto _error;
                op += seqSize;
            }
        }
    }
    ZL_ASSERT(op <= oend);

    ZL_free(mts);
    mts = NULL;
    ZL_free(lls);
    lls = NULL;
    ZL_free(mls);
    mls = NULL;
    ZL_free(mcs);
    mcs = NULL;

    size_t const lastLiterals = lits.numLits - lits.litsConsumed;
    uint8_t* const litsEnd    = lits.lits + lits.numLits;
    if (lastLiterals > (size_t)(oend - op))
        goto _error;
    if (lits.o1) {
        uint8_t context = (op == ostart) ? 0 : op[-1];
        for (size_t l = 0; l < lastLiterals; ++l) {
            uint8_t** litPtr = lits.o1LitsByContext[context];
            if (litPtr == NULL)
                goto _error;
            if (*litPtr >= litsEnd)
                goto _error;
            op[l] = context = **litPtr;
            ++*litPtr;
        }
    } else {
        memcpy(op, lits.lits, lastLiterals);
        lits.lits += lastLiterals;
    }
    op += lastLiterals;

    ZS_RolzDTable2_destroy(&rolz);
    ZL_free(litsBuffer);

    return ZL_returnValue((size_t)(op - ostart));

_error:
    ZL_free(mts);
    ZL_free(lls);
    ZL_free(mls);
    ZL_free(mcs);
    ZL_free(litsBuffer);
    ZS_RolzDTable2_destroy(&rolz);

    ZL_ERR(GENERIC);
}

const ZS_decoder ZS_experimentalDecoder = {
    .name        = "experimental",
    .ctx_create  = ZS_decoderCtx_create,
    .ctx_release = ZS_decoderCtx_release,
    .ctx_reset   = ZS_decoderCtx_reset,
    .decompress  = ZS_experimentalDecoder_decompress,
};
