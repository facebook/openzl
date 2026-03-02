// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/zstd/decode_zstd_binding.h"
#include "openzl/common/debug.h"
#include "openzl/decompress/dictx.h"
#include "openzl/shared/varint.h"

#ifndef ZSTD_STATIC_LINKING_ONLY
#    define ZSTD_STATIC_LINKING_ONLY // ZSTD_getFrameHeader_advanced
#endif
#include <zstd.h> // ZSTD_createDCtx, ZSTD_freeDCtx

void* DIZSTD_createDCtx(void)
{
    return ZSTD_createDCtx();
}
void DIZSTD_freeDCtx(void* state)
{
    ZSTD_freeDCtx(state);
}

static bool useMagicless(ZL_Decoder const* dictx)
{
    return DI_getFrameFormatVersion(dictx) >= 9;
}

static ZL_RESULT_OF(uint64_t) getFrameContentSize(
        ZL_Decoder const* dictx,
        void const* src,
        size_t srcSize)
{
    ZSTD_format_e const format =
            useMagicless(dictx) ? ZSTD_f_zstd1_magicless : ZSTD_f_zstd1;
    ZSTD_frameHeader frameHeader;
    size_t const ret =
            ZSTD_getFrameHeader_advanced(&frameHeader, src, srcSize, format);
    ZL_RET_T_IF(
            uint64_t,
            corruption,
            ZSTD_isError(ret),
            "Unable to read zstd frame header: %s",
            ZSTD_getErrorName(ret));
    ZL_RET_T_IF_NE(
            uint64_t, srcSize_tooSmall, ret, 0, "Incomplete frame header");
    ZL_RET_T_IF_EQ(
            uint64_t,
            corruption,
            frameHeader.frameContentSize,
            ZSTD_CONTENTSIZE_ERROR,
            "content size is error (reject to be safe)");
    ZL_RET_T_IF_EQ(
            uint64_t,
            corruption,
            frameHeader.frameContentSize,
            ZSTD_CONTENTSIZE_UNKNOWN,
            "content size not present");
    return ZL_RESULT_WRAP_VALUE(uint64_t, frameHeader.frameContentSize);
}

ZL_Report DI_zstd(ZL_Decoder* dictx, ZL_Input const* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);
    ZL_Input const* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_ASSERT_EQ(ZL_Input_eltWidth(in), 1);

    uint8_t const* src          = (uint8_t const*)ZL_Input_ptr(in);
    uint8_t const* const srcEnd = src + ZL_Input_numElts(in);

    ZL_TRY_LET_T(uint64_t, dstEltWidth, ZL_varintDecode(&src, srcEnd));
    ZL_ERR_IF_EQ(dstEltWidth, 0, corruption);

    size_t const srcSize = (size_t)(srcEnd - src);
    ZL_TRY_LET_T(uint64_t, dstSize, getFrameContentSize(dictx, src, srcSize));
    ZL_ERR_IF_NE(
            dstSize % dstEltWidth,
            0,
            corruption,
            "content size not multiple of element width");
    size_t const dstNbElts = dstSize / dstEltWidth;

    ZL_Output* const out =
            ZL_Decoder_create1OutStream(dictx, dstNbElts, dstEltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    ZSTD_DCtx* const dctx = ZL_Decoder_getState(dictx);
    ZL_ERR_IF_NULL(
            dctx,
            allocation,
            "Zstandard decompression state allocation failed");
    ZL_ERR_IF(
            ZSTD_isError(
                    ZSTD_DCtx_reset(dctx, ZSTD_reset_session_and_parameters)),
            logicError);
    if (DI_getFrameFormatVersion(dictx) >= 9) {
        // See encoder_zstd.c for details
        if (ZSTD_isError(ZSTD_DCtx_setParameter(
                    dctx, ZSTD_d_format, ZSTD_f_zstd1_magicless))) {
            ZL_ERR(logicError, "Zstd unable to set parameter!");
        }
    }
    size_t const dSize = ZSTD_decompressDCtx(
            dctx, ZL_Output_ptr(out), dstSize, src, srcSize);
    ZL_ERR_IF(
            ZSTD_isError(dSize),
            corruption,
            "Zstd decompression failed: %s",
            ZSTD_getErrorName(dSize));
    ZL_ERR_IF_NE(dSize, dstSize, corruption, "bad destination size");

    ZL_ERR_IF_ERR(ZL_Output_commit(out, dstNbElts));

    return ZL_returnValue(1);
}
