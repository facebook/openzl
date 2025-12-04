// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdlib.h>
#include <string.h>
#include "openzl/common/speed.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

#define FSE_STATIC_LINKING_ONLY

#include "openzl/codecs/entropy/deprecated/common_entropy.h"
#include "openzl/codecs/rolz/encode_encoder.h"
#include "openzl/common/cursor.h"
#include "openzl/common/limits.h"
#include "openzl/common/vector.h"
#include "openzl/shared/mem.h"

#define kTokenOFBits 2
#define kTokenLLBits 4
#define kTokenMLBits 5

#define kAllowAvx2Huffman true

#define kLitCoder ZS_EntropyEncoder_huf
#define kSeqCoder ZS_EntropyEncoder_fse

#if 0
#    define kMaxNumClusters 16
#    define kClusterMethod ZL_ClusteringMode_greedy
#else
#    define kMaxNumClusters 256
#    define kClusterMethod ZL_ClusteringMode_prune
#endif
#define kDoSplit 1
#define kSplitMethod ZS_SplitMethod_fixed

#define kO1Seqs 0
#define kO1MT kO1Seqs
#define kO1LL kO1Seqs
#define kO1ML kO1Seqs
#define kO1MC kO1Seqs

typedef enum {
    ZS_SplitMethod_fast,
    ZS_SplitMethod_fixed,
    ZS_SplitMethod_best,
    ZS_SplitMethod_none,
} ZS_SplitMethod_e;

typedef struct {
    ZS_encoderCtx base;
    ZS_EncoderParameters params;
} ZS_fastEncoderCtx;

static ZS_encoderCtx* ZS_encoderCtx_create(ZS_EncoderParameters const* params)
{
    ZS_fastEncoderCtx* ctx =
            (ZS_fastEncoderCtx*)malloc(sizeof(ZS_fastEncoderCtx));
    if (ctx == NULL)
        return NULL;
    ctx->params = *params;
    return &ctx->base;
}

static void ZS_encoderCtx_release(ZS_encoderCtx* ctx)
{
    if (ctx == NULL)
        return;
    free(ZL_CONTAINER_OF(ctx, ZS_fastEncoderCtx, base));
}

static void ZS_encoderCtx_reset(ZS_encoderCtx* ctx)
{
    (void)ctx;
}

static size_t ZS_fastEncoder_compressBound(
        size_t numLiterals,
        size_t numSequences)
{
    return 1000 + numLiterals + 16 * numSequences;
}

static void encodeCodes(
        ZL_WC* out,
        uint8_t const* codes,
        size_t numSequences,
        uint32_t maxSymbol,
        char const* name,
        size_t extraCost,
        ZS_Entropy_TypeMask_e negMask,
        bool entropy)
{
    int mask = ZS_Entropy_TypeMask_raw | ZS_Entropy_TypeMask_constant
            | ZS_Entropy_TypeMask_bit | ZS_Entropy_TypeMask_multi;
    if (entropy) {
        mask |= ZS_Entropy_TypeMask_huf;
        mask |= ZS_Entropy_TypeMask_fse;
    }
    mask &= ~negMask;
    ZS_Entropy_EncodeParameters params = {
        .allowedTypes = (ZS_Entropy_TypeMask_e)mask,
        .encodeSpeed =
                ZL_EncodeSpeed_fromBaseline(ZL_EncodeSpeedBaseline_faster),
        .decodeSpeed = ZL_DecodeSpeed_fromBaseline(
                entropy ? ZL_DecodeSpeedBaseline_zstd
                        : ZL_DecodeSpeedBaseline_fastest),
        .precomputedHistogram = NULL,
        .cardinalityEstimate  = maxSymbol + 1,
        .maxValueUpperBound   = maxSymbol,
        .allowAvx2Huffman     = kAllowAvx2Huffman,
        .blockSplits          = NULL,
        .tableManager         = NULL,
    };
    size_t const outAvail = ZL_WC_avail(out);
    ZL_REQUIRE_SUCCESS(ZS_Entropy_encode(out, codes, numSequences, 1, &params));
    if (name) {
        ZL_LOG(TRANSFORM,
               "%s %u: %zu (extra=%zu)",
               name,
               (uint32_t)numSequences,
               outAvail - ZL_WC_avail(out) + extraCost,
               extraCost);
    }
}

static void encodeCodes16(
        ZL_WC* out,
        uint16_t const* codes,
        size_t numSequences,
        uint32_t maxSymbol,
        char const* name,
        size_t extraCost,
        ZS_Entropy_TypeMask_e negMask,
        bool entropy)
{
    int mask = ZS_Entropy_TypeMask_raw | ZS_Entropy_TypeMask_constant
            | ZS_Entropy_TypeMask_bit | ZS_Entropy_TypeMask_multi;
    if (entropy) {
        mask |= ZS_Entropy_TypeMask_huf;
    }
    mask &= ~negMask;
    ZS_Entropy_EncodeParameters params = {
        .allowedTypes = (ZS_Entropy_TypeMask_e)mask,
        .encodeSpeed =
                ZL_EncodeSpeed_fromBaseline(ZL_EncodeSpeedBaseline_faster),
        .decodeSpeed = ZL_DecodeSpeed_fromBaseline(
                entropy ? ZL_DecodeSpeedBaseline_zstd
                        : ZL_DecodeSpeedBaseline_fastest),
        .precomputedHistogram = NULL,
        .cardinalityEstimate  = maxSymbol + 1,
        .maxValueUpperBound   = maxSymbol,
        .allowAvx2Huffman     = kAllowAvx2Huffman,
        .blockSplits          = NULL,
        .tableManager         = NULL,
    };
    size_t const outAvail = ZL_WC_avail(out);
    ZL_REQUIRE_SUCCESS(ZS_Entropy_encode(out, codes, numSequences, 2, &params));
    if (name) {
        ZL_LOG(TRANSFORM,
               "%s %u: %zu (extra=%zu)",
               name,
               (uint32_t)numSequences,
               outAvail - ZL_WC_avail(out) + extraCost,
               extraCost);
    }
}

// encodeOffsets function appears to be unused but kept for potential future use
static ZL_UNUSED_ATTR void encodeOffsets(
        ZL_WC* out,
        uint8_t const* offsets,
        size_t numOffsets,
        char const* name)
{
    int mask                           = ZS_Entropy_TypeMask_raw;
    ZS_Entropy_EncodeParameters params = {
        .allowedTypes = (ZS_Entropy_TypeMask_e)mask,
        .encodeSpeed =
                ZL_EncodeSpeed_fromBaseline(ZL_EncodeSpeedBaseline_faster),
        .decodeSpeed =
                ZL_DecodeSpeed_fromBaseline(ZL_DecodeSpeedBaseline_fastest),
        .precomputedHistogram = NULL,
        .cardinalityEstimate  = 0,
        .maxValueUpperBound   = 0,
        .allowAvx2Huffman     = kAllowAvx2Huffman,
        .blockSplits          = NULL,
        .tableManager         = NULL,
    };
    size_t const outAvail = ZL_WC_avail(out);
    ZL_REQUIRE_SUCCESS(ZS_Entropy_encode(out, offsets, numOffsets, 2, &params));
    if (name) {
        ZL_LOG(TRANSFORM,
               "%s %u: %zu",
               name,
               (uint32_t)numOffsets,
               outAvail - ZL_WC_avail(out));
    }
}

static void
encodeLiterals(ZL_WC* out, uint8_t const* lits, size_t numLits, bool entropy)
{
    encodeCodes(
            out,
            lits,
            numLits,
            255,
            "lits",
            0,
            ZS_Entropy_TypeMask_fse,
            entropy);
}

static bool encodeSequences(
        ZL_WC* out,
        ZS_sequence const* seqs,
        size_t numSeqs,
        bool entropy)
{
    uint16_t* const tokens = (uint16_t*)malloc(numSeqs * sizeof(uint16_t));
    uint8_t* const offsets = (uint8_t*)malloc(numSeqs * 3 + 1);
    // Might want tighter bounds if ever used in future
    VECTOR(uint8_t) extra = VECTOR_EMPTY(ZL_CONTAINER_SIZE_LIMIT);
    ZL_REQUIRE_NN(tokens);
    ZL_REQUIRE_NN(offsets);
    size_t offsetsSize = 0;

    // 1 bit - rep | off
    // 3 bit - llen
    // 4 bit - mlen

#define ZS_ENCODE_EXTRA(length)                                              \
    do {                                                                     \
        uint32_t _length              = (length);                            \
        uint8_t const _extraFirstByte = (uint8_t)ZL_MIN(255, _length);       \
        if (!VECTOR_PUSHBACK(extra, _extraFirstByte)) {                      \
            goto _error;                                                     \
        }                                                                    \
        if (_length < 255)                                                   \
            break;                                                           \
        _length -= 255;                                                      \
        uint32_t const _kMaxLength = (1u << 16) - 1;                         \
        for (;;) {                                                           \
            uint32_t const _extra   = ZL_MIN(_length, _kMaxLength);          \
            size_t const _extraSize = VECTOR_SIZE(extra);                    \
            if (VECTOR_RESIZE_UNINITIALIZED(extra, _extraSize + 2)           \
                != _extraSize + 2) {                                         \
                goto _error;                                                 \
            }                                                                \
            ZL_writeLE16(VECTOR_DATA(extra) + _extraSize, (uint16_t)_extra); \
            if (_extra < _kMaxLength)                                        \
                break;                                                       \
            _length -= _extra;                                               \
        }                                                                    \
    } while (0)

    for (size_t i = 0; i < numSeqs; ++i) {
        ZL_ASSERT(seqs[i].literalLength != 0 || seqs[i].matchLength != 0);
        uint16_t token = 0;
        if (seqs[i].matchType == ZS_mt_rep0) {
            token |= 0;
        } else if (seqs[i].matchType == ZS_mt_lz) {
            uint32_t const offset = seqs[i].matchCode;
            if (offset < 256) {
                token |= 1;
                offsets[offsetsSize++] = (uint8_t)offset;
            } else if (offset < (1u << 16)) {
                token |= 2;
                ZL_writeLE16(offsets + offsetsSize, (uint16_t)offset);
                offsetsSize += 2;
            } else {
                token |= 3;
                ZL_ASSERT_LT(offset, 1u << 24);
                ZL_writeLE32(offsets + offsetsSize, (uint32_t)offset);
                offsetsSize += 3;
            }
        } else {
            ZL_REQUIRE_FAIL("Not supported!");
        }
        uint32_t const kMaxLitLength   = (1u << kTokenLLBits) - 1;
        uint32_t const kMaxMatchLength = (1u << kTokenMLBits) - 1;
        if (ZL_LIKELY(
                    seqs[i].literalLength <= kMaxLitLength
                    && seqs[i].matchLength <= kMaxMatchLength)) {
            token |= (uint16_t)(seqs[i].literalLength << kTokenOFBits);
            token |= (uint16_t)(seqs[i].matchLength
                                << (kTokenOFBits + kTokenLLBits));
        } else {
            ZS_ENCODE_EXTRA(seqs[i].literalLength);
            ZS_ENCODE_EXTRA(seqs[i].matchLength);
        }
        tokens[i] = token;
    }

#undef ZS_ENCODE_EXTRA

    encodeCodes16(
            out,
            tokens,
            numSeqs,
            (1u << (kTokenOFBits + kTokenLLBits + kTokenMLBits)) - 1,
            "tokens",
            VECTOR_SIZE(extra),
            ZS_Entropy_TypeMask_fse,
            entropy);
    ZL_WC_REQUIRE_HAS(out, 1);

    ZL_LOG(TRANSFORM, "Writing %u offset bytes", (unsigned)offsetsSize);
    ZL_REQUIRE_GE(ZL_WC_avail(out), ZL_varintSize(offsetsSize));
    ZL_WC_pushVarint(out, offsetsSize);
    ZL_REQUIRE_GE(ZL_WC_avail(out), offsetsSize);
    memcpy(ZL_WC_ptr(out), offsets, offsetsSize);
    ZL_WC_advance(out, offsetsSize);

    ZL_DLOG(TRANSFORM, "Writing %u extras", (unsigned)VECTOR_SIZE(extra));
    // TODO: Fix this - extra is canonical endian only right now...
    ZL_REQUIRE_GE(ZL_WC_avail(out), ZL_varintSize(VECTOR_SIZE(extra)));
    ZL_WC_pushVarint(out, VECTOR_SIZE(extra));
    ZL_REQUIRE_GE(ZL_WC_avail(out), VECTOR_SIZE(extra));
    if (VECTOR_SIZE(extra)) {
        memcpy(ZL_WC_ptr(out), VECTOR_DATA(extra), VECTOR_SIZE(extra));
    }
    ZL_WC_advance(out, VECTOR_SIZE(extra));

    bool ret = true;

_out:
    free(tokens);
    free(offsets);
    VECTOR_DESTROY(extra);
    return ret;

_error:
    ret = false;
    goto _out;

    // TODO: Add LA huf to AVX2 HUF and try 1024 symbols:
    // 1 bit - rep | off
    // 4 bit - llen
    // 5 bit - mlen

    // Extras encoded:
    // Rice-golomb
    // 32-bits raw

    // Offsets encoded:
    // Raw - 16 bits (enforced at MF)
    // Entropy base/bits
}

static size_t ZS_fastEncoder_compress(
        ZS_encoderCtx* base,
        uint8_t* dst,
        size_t capacity,
        ZS_RolzSeqStore const* seqStore)
{
    (void)base;
    uint32_t const numLiterals =
            (uint32_t)(seqStore->lits.ptr - seqStore->lits.start);
    uint32_t const numSequences =
            (uint32_t)(seqStore->seqs.ptr - seqStore->seqs.start);
    ZL_ASSERT_GE(
            capacity, ZS_fastEncoder_compressBound(numLiterals, numSequences));
    if (capacity < ZS_fastEncoder_compressBound(numLiterals, numSequences)) {
        return 0;
    }
    ZL_WC out = ZL_WC_wrap(dst, capacity);

    // TODO: Support this...
    bool const entropy = true;

    // Write literals
    encodeLiterals(&out, seqStore->lits.start, numLiterals, entropy);

    if (!encodeSequences(&out, seqStore->seqs.start, numSequences, entropy)) {
        return 0;
    }

    return (size_t)(ZL_WC_ptr(&out) - dst);
}

const ZS_encoder ZS_fastEncoder = {
    .name          = "fast",
    .ctx_create    = ZS_encoderCtx_create,
    .ctx_release   = ZS_encoderCtx_release,
    .ctx_reset     = ZS_encoderCtx_reset,
    .compressBound = ZS_fastEncoder_compressBound,
    .compress      = ZS_fastEncoder_compress,
};
