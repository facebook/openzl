// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/rolz/encode_rolz_kernel.h"

#include "openzl/codecs/common/window.h"
#include "openzl/codecs/rolz/encode_encoder.h"
#include "openzl/codecs/rolz/encode_match_finder.h"
#include "openzl/codecs/rolz/encode_rolz_sequences.h"
#include "openzl/shared/utils.h"

size_t ZS_rolzCompressBound(size_t srcSize)
{
    return ZL_MAX(
            ZS_rolzEncoder->compressBound(srcSize, 0),
            ZS_rolzEncoder->compressBound(0, srcSize / 3));
}

size_t ZS_fastLzCompressBound(size_t srcSize)
{
    return ZL_MAX(
            ZS_fastLzEncoder->compressBound(srcSize, 0),
            ZS_fastLzEncoder->compressBound(0, srcSize / 3));
}

#if 0
static const ZS_MatchFinderParameters mfParams = {
  .rolzEnabled            = false,
  .rolzContextDepth       = 2,
  .rolzContextLog         = 12,
  .rolzRowLog             = 4,
  .rolzMinLength          = 3,
  .rolzPredictMatchLength = true,

  .lzEnabled     = true,
  .lzHashLog     = 19,
  .lzChainLog    = 18,
  .lzMinLength   = 5,
  .lzSearchLog   = 3,
  .lzSearchDelay = 0,

  .repMinLength = 3,
};
#else
// "good" 256K params
static const ZS_MatchFinderParameters mfParams = {
    .rolzEnabled            = true, // Constant for speed for now
    .rolzContextDepth       = 2,    // Constant for speed for now
    .rolzContextLog         = 12,   // Constant for speed for now
    .rolzRowLog             = 4,
    .rolzMinLength          = 3, // Constant for speed for now
    .rolzSearchLog          = 2,
    .rolzPredictMatchLength = true, // Constant for speed for now

    .lzEnabled = true, // Constant for speed for now
    // .lzHashLog     = 19,
    // .lzChainLog    = 18,
    .lzMinLength   = 7, // Constant for speed for now
    .lzSearchLog   = 3,
    .lzSearchDelay = 1, // Constant for speed for now
    .lzTableLog    = 17,
    .lzRowLog      = 4,
    .lzLargeMatch  = false,

    .strategy = ZS_MatchFinderStrategy_greedy,

    .repMinLength = 3, // Constant for speed for now

    // .tableLog  = 15,
    // .rowLog    = 4,
    // .searchLog = 3,
    // .minLength = 5,
};
#endif

static ZS_EncoderParameters ZS_encoderParams(
        ZS_MatchFinderParameters const* src,
        int level)
{
    (void)level;
    ZS_EncoderParameters params = {
        .rolzContextDepth       = src->rolzContextDepth,
        .rolzContextLog         = src->rolzContextLog,
        .rolzRowLog             = src->rolzRowLog,
        .rolzMinLength          = src->rolzMinLength,
        .rolzPredictMatchLength = src->rolzPredictMatchLength,
        .lzMinLength            = src->lzMinLength,
        .repMinLength           = src->repMinLength,
        .fieldSize              = src->fieldSize,
        .fixedOffset            = src->fixedOffset,
        .literalEncoding        = ZS_LiteralEncoding_o1,
        .zstdCompressLiterals   = true,
    };
    return params;
}

ZL_Report
ZS_rolzCompress(void* dst, size_t dstCapacity, void const* src, size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_Report ret;
    // Initialization
    int error = 0;
    ZS_RolzSeqStore seqStore;
    error |= ZS_RolzSeqStore_initBound(&seqStore, srcSize, 3);
    ZS_window window;
    error |= ZS_window_init(&window, ZL_MIN((uint32_t)srcSize, 1u << 23), 8);
    ZS_matchFinder const* mf       = &ZS_lazyMatchFinder;
    ZS_matchFinderCtx* const mfCtx = mf->ctx_create(&window, &mfParams);
    error |= !mfCtx;
    ZS_EncoderParameters eParams = ZS_encoderParams(&mfParams, 1);
    ZS_encoderCtx* const eCtx    = ZS_rolzEncoder->ctx_create(&eParams);
    error |= !eCtx;
    if (error)
        goto err;

    ZS_window_update(&window, (uint8_t const*)src, srcSize);
    mf->parse(mfCtx, &seqStore, (uint8_t const*)src, srcSize);
    size_t const dstSize = ZS_rolzEncoder->compress(
            eCtx, (uint8_t*)dst, dstCapacity, &seqStore);
    if (dstSize == 0)
        goto err;

    ret = ZL_returnValue(dstSize);
    goto out;
err:
    ret = ZL_REPORT_ERROR(allocation);
out:
    ZS_rolzEncoder->ctx_release(eCtx);
    mf->ctx_release(mfCtx);
    ZS_RolzSeqStore_destroy(&seqStore);
    return ret;
}

ZL_Report ZS_fastLzCompress(
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_Report ret;
    // Initialization
    int error = 0;
    ZS_RolzSeqStore seqStore;
    error |= ZS_RolzSeqStore_initBound(&seqStore, srcSize, 3);
    ZS_window window;
    error |= ZS_window_init(&window, ZL_MIN((uint32_t)srcSize, 1u << 23), 8);
    ZS_matchFinder const* mf       = &ZS_doubleFastLzMatchFinder;
    ZS_matchFinderCtx* const mfCtx = mf->ctx_create(&window, &mfParams);
    error |= !mfCtx;
    ZS_EncoderParameters eParams = ZS_encoderParams(&mfParams, 1);
    ZS_encoderCtx* const eCtx    = ZS_fastLzEncoder->ctx_create(&eParams);
    error |= !eCtx;
    if (error)
        goto err;

    ZS_window_update(&window, (uint8_t const*)src, srcSize);
    mf->parse(mfCtx, &seqStore, (uint8_t const*)src, srcSize);
    size_t const dstSize = ZS_fastLzEncoder->compress(
            eCtx, (uint8_t*)dst, dstCapacity, &seqStore);
    if (dstSize == 0)
        goto err;

    ret = ZL_returnValue(dstSize);
    goto out;
err:
    ret = ZL_REPORT_ERROR(GENERIC);
out:
    ZS_fastLzEncoder->ctx_release(eCtx);
    mf->ctx_release(mfCtx);
    ZS_RolzSeqStore_destroy(&seqStore);
    return ret;
}
