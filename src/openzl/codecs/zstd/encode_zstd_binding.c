// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/zstd/encode_zstd_binding.h"
#include "openzl/codecs/zstd/common_zstd.h"
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

// Approximately log2 of the factor for the allowed memory usage for the most
// expensive zstd parameter configuration
#define ZSTD_MEMORY_USAGE_CAP_LOG_FACTOR 3

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

// A more restrictive bounds check on parameters for running zstd that affect
// how much memory is used, and preventing certain parameters from being
// overriden.

static bool EI_zstd_parameter_valid(ZSTD_cParameter param, int paramValue)
{
    if (param == ZSTD_c_format || param == ZSTD_c_contentSizeFlag) {
        return false;
    }
    if (param == ZSTD_c_windowLog) {
        return paramValue
                <= ZSTD_WINDOWLOG_MAX - ZSTD_MEMORY_USAGE_CAP_LOG_FACTOR;
    }
    if (param == ZSTD_c_hashLog) {
        return paramValue
                <= ZSTD_HASHLOG_MAX - ZSTD_MEMORY_USAGE_CAP_LOG_FACTOR;
    }
    if (param == ZSTD_c_chainLog) {
        return paramValue
                <= ZSTD_CHAINLOG_MAX - ZSTD_MEMORY_USAGE_CAP_LOG_FACTOR;
    }
    if (param == ZSTD_c_ldmHashLog) {
        return paramValue
                <= ZSTD_LDM_HASHLOG_MAX - ZSTD_MEMORY_USAGE_CAP_LOG_FACTOR;
    }
    return true;
}

#define ZL_ERR_IF_ZSTD_ERR(zstdResult)           \
    do {                                         \
        size_t const _zstdResult = (zstdResult); \
        ZL_ERR_IF(                               \
                ZSTD_isError(_zstdResult),       \
                GENERIC,                         \
                "Zstd Error: %s",                \
                ZSTD_getErrorName(_zstdResult)); \
    } while (0)

static ZL_Report
EI_zstdWithCCtx(ZL_Encoder* eictx, ZSTD_CCtx* cctx, const ZL_Input* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
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
    ZL_ERR_IF_NULL(dst, allocation);

    uint8_t* const ostart   = (uint8_t*)ZL_Output_ptr(dst);
    size_t const headerSize = ZL_varintEncode((uint64_t)eltWidth, ostart);

    /* Global parameters influence compression parameters */
    ZL_ERR_IF_ZSTD_ERR(
            ZSTD_CCtx_reset(cctx, ZSTD_reset_session_and_parameters));

    if (ZL_Encoder_getCParam(eictx, ZL_CParam_formatVersion) >= 9) {
        // Skip the zstd magic number for two reasons:
        // 1. We don't need it, Zstrong tells us we are decompressing zstd.
        // 2. It makes fuzzing harder, because the fuzzer can't find the magic.
        ZL_ERR_IF_ZSTD_ERR(ZSTD_CCtx_setParameter(
                cctx, ZSTD_c_format, ZSTD_f_zstd1_magicless));
    }

    ZL_ERR_IF_ZSTD_ERR(ZSTD_CCtx_setParameter(
            cctx,
            ZSTD_c_compressionLevel,
            ZL_Encoder_getCParam(eictx, ZL_CParam_compressionLevel)));

    int const decompressionLevel =
            ZL_Encoder_getCParam(eictx, ZL_CParam_decompressionLevel);
    if (decompressionLevel == 1) {
        ZL_ERR_IF_ZSTD_ERR(ZSTD_CCtx_setParameter(
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
        ZL_ERR_IF_NOT(
                EI_zstd_parameter_valid(param, ip.paramValue),
                nodeParameter_invalid,
                "zstd parameter %i cannot be modified");
        ZL_ERR_IF_ZSTD_ERR(ZSTD_CCtx_setParameter(cctx, param, ip.paramValue));
    }
    if (blockSize == srcSize) {
        // dict only used when not cutting blocks; the subsequent
        // ZSTD_CCtx_reset in this function's next invocation will unbind it.
        const ZSTD_CDict* cdict =
                (const ZSTD_CDict*)ZL_Encoder_getMaterializedDict(eictx);
        if (cdict != NULL) {
            ZL_ERR_IF_ZSTD_ERR(ZSTD_CCtx_refCDict(cctx, cdict));
        }
        size_t const cSize = ZSTD_compress2(
                cctx,
                ostart + headerSize,
                outCapacity - headerSize,
                ZL_Input_ptr(src),
                srcSize);
        ZL_ERR_IF_ZSTD_ERR(cSize);
        ZL_ERR_IF_ERR(ZL_Output_commit(dst, headerSize + cSize));
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
                ZL_ERR_IF_ZSTD_ERR(ret);
            }
        }
        ZL_ASSERT_EQ(in.pos, srcSize);
        ZL_ERR_IF_ERR(ZL_Output_commit(dst, out.pos));
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
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in    = ins[0];
    ZSTD_CCtx* const cctx = ZL_Encoder_getState(eictx);
    ZL_ERR_IF_NULL(cctx, allocation);
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
    ZL_NodeID node_zstd        = ZL_Compressor_registerParameterizedNode(
            cgraph,
            &(const ZL_ParameterizedNodeDesc){
                           .node        = (ZL_NodeID){ ZL_PrivateStandardNodeID_zstd },
                           .localParams = &localParams,
            });
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_zstd, ZL_GRAPH_STORE);
}

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildTrainableZstdGraph(ZL_Compressor* cgraph)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, cgraph);
    ZL_NodeID node_zstd = ZL_Compressor_registerParameterizedNode(
            cgraph,
            &(const ZL_ParameterizedNodeDesc){
                    .name = "zl.trainable.zstd",
                    .node = (ZL_NodeID){ ZL_PrivateStandardNodeID_zstd },
            });
    ZL_ERR_IF_NOT(
            ZL_NodeID_isValid(node_zstd),
            node_invalid,
            "Failed to build zl.trainable.zstd node");
    ZL_GraphID const graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_zstd, ZL_GRAPH_STORE);
    ZL_ERR_IF_NOT(
            ZL_GraphID_isValid(graph),
            graph_invalid,
            "Failed to build trainable zstd graph");
    return ZL_WRAP_VALUE(graph);
}

ZL_RESULT_OF(ZL_VoidPtr)
EIZSTD_materializeCDict(
        ZL_Materializer* matCtx,
        const void* src,
        size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, NULL);
    ZL_TrainedZstdContentParsed parsed;
    ZL_ERR_IF_NOT(
            ZL_TrainedZstdContent_parse(src, srcSize, &parsed),
            dict_materialization,
            "Failed to parse trained zstd dict content");
    ZSTD_customMem const customMem = {
        .customAlloc = ZL_Zstd_materializerAlloc,
        .customFree  = ZL_Zstd_materializerFree,
        .opaque      = matCtx,
    };
    ZSTD_CDict* cdict = ZSTD_createCDict_advanced(
            parsed.rawDict,
            parsed.rawDictSize,
            ZSTD_dlm_byCopy,
            ZSTD_dct_auto,
            ZSTD_getCParams(parsed.clevel, 0, parsed.rawDictSize),
            customMem);
    ZL_ERR_IF_NULL(cdict, dict_materialization, "ZSTD_createCDict failed");
    return ZL_RESULT_WRAP_VALUE(ZL_VoidPtr, (ZL_VoidPtr)cdict);
}

void EIZSTD_dematerializeCDict(ZL_Materializer* matCtx, void* materialized)
{
    (void)matCtx;
    if (materialized != NULL) {
        ZSTD_freeCDict((ZSTD_CDict*)materialized);
    }
}
