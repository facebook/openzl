// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/zstd/encode_zstd_binding.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h" // ZL_PrivateStandardNodeID_zstd
#include "openzl/shared/varint.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_localParams.h"

#ifndef ZSTD_STATIC_LINKING_ONLY
#    define ZSTD_STATIC_LINKING_ONLY
#endif
#include <zstd.h>

/// Determines if we should cut blocks for each element.
/// E.g. if the input is transposed.
static bool EI_zstd_shouldCutBlocks(ZL_Input const* in)
{
    size_t const nbElts        = ZL_Input_numElts(in);
    size_t const eltWidth      = ZL_Input_eltWidth(in);
    size_t const kMaxNbElts    = 8;
    size_t const kMinBlockSize = 1024;
    return nbElts > 0 && eltWidth >= kMinBlockSize && nbElts <= kMaxNbElts;
}

static bool EI_zstd_parameter_valid(ZSTD_cParameter param)
{
    if (param == ZSTD_c_format)
        return false;
    if (param == ZSTD_c_contentSizeFlag)
        return false;
    return true;
}

#define ZL_RET_R_IF_ZSTD_ERR(zstdResult)         \
    do {                                         \
        size_t const _zstdResult = (zstdResult); \
        ZL_RET_R_IF(                             \
                GENERIC,                         \
                ZSTD_isError(_zstdResult),       \
                "Zstd Error: %s",                \
                ZSTD_getErrorName(_zstdResult)); \
    } while (0)

static ZL_Report
EI_zstdWithCCtx(ZL_Encoder* eictx, ZSTD_CCtx* cctx, const ZL_Input* src)
{
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(src);
    ZL_ASSERT(
            ZL_Input_type(src) == ZL_Type_serial
            || ZL_Input_type(src) == ZL_Type_struct);

    bool const blockSplit = EI_zstd_shouldCutBlocks(src);

    size_t const nbElts    = ZL_Input_numElts(src);
    size_t const eltWidth  = ZL_Input_eltWidth(src);
    size_t const srcSize   = nbElts * eltWidth;
    size_t const blockSize = blockSplit ? eltWidth : srcSize;

    // Need to reserve extra space for block splitting for the extra block
    // headers, to ensure the output is guaranteed to be large enough.
    // We also need space to write the element width.
    size_t const outCapacity = ZSTD_compressBound(srcSize)
            + (blockSplit ? nbElts * 3 : 0) + ZL_varintSize((uint64_t)eltWidth);
    ZL_Output* const dst =
            ZL_Encoder_createTypedStream(eictx, 0, outCapacity, 1);
    ZL_RET_R_IF_NULL(allocation, dst);

    uint8_t* const ostart   = (uint8_t*)ZL_Output_ptr(dst);
    size_t const headerSize = ZL_varintEncode((uint64_t)eltWidth, ostart);

    /* Global parameters influence compression parameters */
    ZL_RET_R_IF_ZSTD_ERR(
            ZSTD_CCtx_reset(cctx, ZSTD_reset_session_and_parameters));

    if (ZL_Encoder_getCParam(eictx, ZL_CParam_formatVersion) >= 9) {
        // Skip the zstd magic number for two reasons:
        // 1. We don't need it, Zstrong tells us we are decompressing zstd.
        // 2. It makes fuzzing harder, because the fuzzer can't find the magic.
        ZL_RET_R_IF_ZSTD_ERR(ZSTD_CCtx_setParameter(
                cctx, ZSTD_c_format, ZSTD_f_zstd1_magicless));
    }

    ZL_RET_R_IF_ZSTD_ERR(ZSTD_CCtx_setParameter(
            cctx,
            ZSTD_c_compressionLevel,
            ZL_Encoder_getCParam(eictx, ZL_CParam_compressionLevel)));

    int const decompressionLevel =
            ZL_Encoder_getCParam(eictx, ZL_CParam_decompressionLevel);
    if (decompressionLevel == 1) {
        ZL_RET_R_IF_ZSTD_ERR(ZSTD_CCtx_setParameter(
                cctx, ZSTD_c_literalCompressionMode, ZSTD_lcm_uncompressed));
    }

    /* Local Integer Parameters can be employed to set advanced zstd compression
     * parameters. They can overwrite parameters previously set via global
     * parameters.
     * Some advanced parameters cannot be changed though.
     * See EI_zstd_parameter_valid().
     */

    ZL_LocalIntParams const lips = ZL_Encoder_getLocalIntParams(eictx);
    for (size_t n = 0; n < lips.nbIntParams; n++) {
        ZL_IntParam const ip        = lips.intParams[n];
        ZSTD_cParameter const param = (ZSTD_cParameter)ip.paramId;
        ZL_RET_R_IF_NOT(
                nodeParameter_invalid,
                EI_zstd_parameter_valid(param),
                "zstd parameter %i cannot be modified");
        ZL_RET_R_IF_ZSTD_ERR(
                ZSTD_CCtx_setParameter(cctx, param, ip.paramValue));
    }

    if (blockSize == srcSize) {
        size_t const cSize = ZSTD_compress2(
                cctx,
                ostart + headerSize,
                outCapacity - headerSize,
                ZL_Input_ptr(src),
                srcSize);
        ZL_RET_R_IF_ZSTD_ERR(cSize);
        ZL_RET_R_IF_ERR(ZL_Output_commit(dst, headerSize + cSize));
    } else {
        ZSTD_CCtx_setPledgedSrcSize(cctx, srcSize);

        ZSTD_outBuffer out = { ostart, outCapacity, headerSize };
        ZSTD_inBuffer in   = { ZL_Input_ptr(src), blockSize, 0 };

        for (; in.pos < srcSize; in.size += blockSize) {
            ZL_ASSERT_LE(in.size, srcSize);
            while (in.pos < in.size) {
                ZSTD_EndDirective const flush =
                        in.size == srcSize ? ZSTD_e_end : ZSTD_e_flush;
                size_t const ret = ZSTD_compressStream2(cctx, &out, &in, flush);
                ZL_RET_R_IF_ZSTD_ERR(ret);
            }
        }
        ZL_ASSERT_EQ(in.pos, srcSize);
        ZL_RET_R_IF_ERR(ZL_Output_commit(dst, out.pos));
    }

    return ZL_returnValue(1);
}

void* EIZSTD_createCCtx(void)
{
    return ZSTD_createCCtx();
}
void EIZSTD_freeCCtx(void* state)
{
    (void)ZSTD_freeCCtx(state);
}

ZL_Report EI_zstd(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in    = ins[0];
    ZSTD_CCtx* const cctx = ZL_Encoder_getState(eictx);
    ZL_RET_R_IF_NULL(allocation, cctx);
    return EI_zstdWithCCtx(eictx, cctx, in);
}

ZL_GraphID ZL_Compressor_registerZstdGraph_withLevel(
        ZL_Compressor* cgraph,
        int compressionLevel)
{
    ZL_LocalParams localParams = { .intParams = ZL_INTPARAMS(
                                           {
                                                   ZSTD_c_compressionLevel,
                                                   compressionLevel,
                                           }) };
    ZL_NodeID node_zstd        = ZL_Compressor_cloneNode(
            cgraph, (ZL_NodeID){ ZL_PrivateStandardNodeID_zstd }, &localParams);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_zstd, ZL_GRAPH_STORE);
}
