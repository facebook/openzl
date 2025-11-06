// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdint.h>
#include "openzl/common/allocation.h"
#include "openzl/common/errors_internal.h" // ZS2_RET_IF*
#include "openzl/common/introspection.h" // WAYPOINT, ZL_CompressIntrospectionHooks
#include "openzl/common/limits.h"        // ZL_runtimeInputLimit
#include "openzl/common/logging.h"       // ZL_LOG
#include "openzl/common/operation_context.h"
#include "openzl/common/stream.h"               // ZL_Data*
#include "openzl/common/wire_format.h"          // ZSTRONG_MAGIC_NUMBER
#include "openzl/compress/cctx.h"               // createCCtx, GraphInfo
#include "openzl/compress/encode_frameheader.h" // EFH_writeFrameHeader
#include "openzl/compress/private_nodes.h"      // ZS2_GRAPH_xxx
#include "openzl/shared/mem.h"                  // writeLE32
#include "openzl/shared/xxhash.h"               // XXH3_64bits
#include "openzl/zl_common_types.h" // ZL_TernaryParam_enable, ZL_TernaryParam_disable
#include "openzl/zl_compress.h" // ZS2_compress_*
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

ZL_CCtx* ZL_CCtx_create(void)
{
    return CCTX_create();
}
void ZL_CCtx_free(ZL_CCtx* cctx)
{
    CCTX_free(cctx);
}

/**
 * Requires: applied parameters set
 */
static ZL_Report writeFrameHeader(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const ZL_Data* inputs[],
        size_t numInputs)
{
    ZL_DLOG(BLOCK, "writeFrameHeader");

    // Check format limitations
    uint32_t formatVersion =
            (uint32_t)CCTX_getAppliedGParam(cctx, ZL_CParam_formatVersion);
    ZL_ASSERT_NE(formatVersion, 0, "Format version should not be 0.");
    ZL_ASSERT(
            ZL_isFormatVersionSupported(formatVersion),
            "Format should already have been validated.");
    ZL_RET_R_IF_GT(
            formatVersion_unsupported,
            numInputs,
            ZL_runtimeInputLimit(formatVersion));

    // Allocate array for description of inputs
    ALLOC_MALLOC_CHECKED(InputDesc, inputDescs, numInputs);
    for (size_t n = 0; n < numInputs; n++) {
        inputDescs[n].byteSize = ZL_Data_contentSize(inputs[n]);
        inputDescs[n].type     = ZL_Data_type(inputs[n]);
        inputDescs[n].numElts  = ZL_Data_numElts(inputs[n]);
    }

    ZL_Comment comment = CCTX_getHeaderComment(cctx);
    // Requested frame properties (checksum)
    ZL_FrameProperties const fprop = {
        .hasContentChecksum =
                CCTX_getAppliedGParam(cctx, ZL_CParam_contentChecksum)
                != ZL_TernaryParam_disable,
        .hasCompressedChecksum =
                CCTX_getAppliedGParam(cctx, ZL_CParam_compressedChecksum)
                != ZL_TernaryParam_disable,
        .hasComment = (comment.size != 0),
    };

    EFH_FrameInfo const fi = {
        .inputDescs = inputDescs,
        .numInputs  = numInputs,
        .fprop      = &fprop,
        .comment    = comment,
    };

    ZL_Report const r =
            EFH_writeFrameHeader(dst, dstCapacity, &fi, formatVersion);

    free(inputDescs);
    return r;
}

static ZL_Report CCTX_compressInputs_withGraphSet_stage2(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const ZL_Data* inputs[],
        size_t nbInputs)
{
    ZL_ASSERT_NN(cctx);
    ZL_ASSERT_NN(inputs);
    ZL_RESULT_DECLARE_SCOPE_REPORT(cctx);
    ZL_DLOG(FRAME,
            "CCTX_compressInputs_withGraphSet_stage2 (%zu inputs) (type[0]:%u)",
            nbInputs,
            ZL_Data_type(inputs[0]));

    // freeze parameters to their final values
    ZL_ERR_IF_ERR(CCTX_setAppliedParameters(cctx));

    // Write frame header
    ZL_TRY_LET(
            size_t,
            fhSize,
            writeFrameHeader(cctx, dst, dstCapacity, inputs, nbInputs));
    ZL_ASSERT_LE(fhSize, dstCapacity);
    size_t frameSize = fhSize;

    // Pass output parameters
    CCTX_setDst(cctx, dst, dstCapacity, frameSize);

    // Pass input(s) to starting Graph, initiating compression
    ZL_TRY_LET(size_t, r, CCTX_startCompression(cctx, inputs, nbInputs));

    ZL_DLOG(FRAME, "Final compressed size: %zu", r);
    return ZL_returnValue(r);
}

/* requirement: cctx->graph is set */
ZL_Report CCTX_compressInputs_withGraphSet(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const ZL_Data* inputs[],
        size_t nbInputs)
{
    // @note all compression entry points converge here.
    ZL_DLOG(FRAME, "CCTX_compressInputs_withGraphSet");

    ZL_Report const r = CCTX_compressInputs_withGraphSet_stage2(
            cctx, dst, dstCapacity, inputs, nbInputs);

    // ensure that Arena memory is always reclaimed at the end,
    // even in cases of errors.
    CCTX_clean(cctx);
    if (!CCTX_getAppliedGParam(cctx, ZL_CParam_stickyParameters)) {
        // If cctx parameters are not explicitly sticky, reset them
        ZL_RET_R_IF_ERR(ZL_CCtx_resetParameters(cctx));
    }

    return r;
}

static ZL_Report CCTX_compressSerial_withGraphSet(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize)
{
    ZL_Data* stream = STREAM_create(ZL_DATA_ID_INPUTSTREAM);
    ZL_RET_R_IF_NULL(allocation, stream);
    ZL_Report ret =
            STREAM_refConstBuffer(stream, src, ZL_Type_serial, 1, srcSize);
    if (!ZL_isError(ret)) {
        const ZL_Data* constStream = stream;
        ret                        = CCTX_compressInputs_withGraphSet(
                cctx, dst, dstCapacity, &constStream, 1);
    }
    STREAM_free(stream);
    return ret;
}

static ZL_Report ZL_CCtx_compress_usingCGraph(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        const ZL_Compressor* cgraph)
{
    ZL_RET_R_IF_ERR(ZL_CCtx_refCompressor(cctx, cgraph));
    return CCTX_compressSerial_withGraphSet(
            cctx, dst, dstCapacity, src, srcSize);
}

static ZL_Report ZL_CCtx_compress_usingGraph2Desc(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_Graph2Desc gfDesc)
{
    ZL_LOG(FRAME, "ZL_CCtx_compress_usingGraph2Desc (srcSize=%zu)", srcSize);
    ZL_ASSERT_NN(cctx);
    ZL_RET_R_IF_ERR(CCTX_setLocalCGraph_usingGraph2Desc(cctx, gfDesc));
    return CCTX_compressSerial_withGraphSet(
            cctx, dst, dstCapacity, src, srcSize);
}

typedef struct {
    ZL_GraphFn f;
} ZL_Graph_s;

static ZL_GraphID useGraphF(ZL_Compressor* cgraph, const void* gfs)
{
    ZL_GraphFn const f = ((const ZL_Graph_s*)gfs)->f;
    return f(cgraph);
}

ZL_Report ZL_compress_usingGraphFn(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_GraphFn graphFunction)
{
    ZL_LOG(FRAME, "ZL_compress_usingGraphFn");
    ZL_CCtx* const cctx = ZL_CCtx_create();
    ZL_RET_R_IF_NULL(allocation, cctx);

    ZL_Graph2Desc const g2d = { useGraphF, &(ZL_Graph_s){ graphFunction } };
    ZL_Report r             = ZL_CCtx_compress_usingGraph2Desc(
            cctx, dst, dstCapacity, src, srcSize, g2d);

    // Clear the info pointer because it points into the cctx
    ZL_RES_clearInfo(r);
    ZL_CCtx_free(cctx);
    return r;
}

ZL_Report ZL_compress_usingCompressor(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        const ZL_Compressor* compressor)
{
    ZL_CCtx* const cctx = CCTX_create();
    ZL_RET_R_IF_NULL(allocation, cctx);

    ZL_Report r = ZL_CCtx_compress_usingCGraph(
            cctx, dst, dstCapacity, src, srcSize, compressor);
    // Clear the info pointer because it points into the cctx
    ZL_RES_clearInfo(r);
    CCTX_free(cctx);
    return r;
}

static ZL_GraphID selectGraph(ZL_Compressor* cgraph, const void* param)
{
    ZL_ASSERT_NN(param);
    (void)cgraph;
    return *(const ZL_GraphID*)param;
}

static ZL_Report ZL_CCtx_compress_usingGraphID(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_GraphID gid)
{
    return ZL_CCtx_compress_usingGraph2Desc(
            cctx,
            dst,
            dstCapacity,
            src,
            srcSize,
            (ZL_Graph2Desc){ selectGraph, &gid });
}

ZL_TypedRef* ZL_TypedRef_createString(
        const void* strBuffer,
        size_t bufferSize,
        const uint32_t* strLens,
        size_t nbStrings)
{
    ZL_Data* const stream = STREAM_create(ZL_DATA_ID_INPUTSTREAM);
    if (stream == NULL)
        return NULL;
    ZL_Report const ret = STREAM_refConstExtString(
            stream, strBuffer, bufferSize, strLens, nbStrings);
    if (ZL_isError(ret)) {
        STREAM_free(stream);
        return NULL;
    }
    // Note: currently, ZL_TypedRef == ZL_Data
    return ZL_codemodMutDataAsInput(stream);
}

static ZL_TypedRef*
ZL_refGeneric(ZL_Type type, size_t fieldWidth, size_t nbFields, const void* src)
{
    ZL_Data* const stream = STREAM_create(ZL_DATA_ID_INPUTSTREAM);
    if (stream == NULL)
        return NULL;
    ZL_Report ret =
            STREAM_refConstBuffer(stream, src, type, fieldWidth, nbFields);
    if (ZL_isError(ret)) {
        STREAM_free(stream);
        return NULL;
    }
    // Note: currently, ZL_TypedRef == ZL_Data
    return ZL_codemodMutDataAsInput(stream);
}

ZL_TypedRef* ZL_TypedRef_createSerial(const void* src, size_t srcSize)
{
    return ZL_refGeneric(ZL_Type_serial, 1, srcSize, src);
}

ZL_TypedRef* ZL_TypedRef_createStruct(
        const void* start,
        size_t structWidth,
        size_t nbStructs)
{
    return ZL_refGeneric(ZL_Type_struct, structWidth, nbStructs, start);
}

ZL_TypedRef*
ZL_TypedRef_createNumeric(const void* start, size_t numWidth, size_t nbNums)
{
    return ZL_refGeneric(ZL_Type_numeric, numWidth, nbNums, start);
}

void ZL_TypedRef_free(ZL_TypedRef* tbuf)
{
    // Note: currently, ZL_TypedRef == ZL_Data
    STREAM_free(ZL_codemodMutInputAsData(tbuf));
}

ZL_Report ZL_CCtx_compressMultiTypedRef(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const ZL_TypedRef* inputs[],
        size_t nbInputs)
{
    WAYPOINT(
            on_ZL_CCtx_compressMultiTypedRef_start,
            cctx,
            dst,
            dstCapacity,
            inputs,
            nbInputs);
    ZL_RET_R_IF_NULL(compressionParameter_invalid, inputs);
    // this works directly because ZL_TypedRef == ZL_Data
    // In the future, if these types diverge, a conversion operation will be
    // required
    ZL_RET_R_IF_NOT(compressionParameter_invalid, CCTX_isGraphSet(cctx));
    const ZL_Report rep = CCTX_compressInputs_withGraphSet(
            cctx, dst, dstCapacity, ZL_codemodInputsAsDatas(inputs), nbInputs);
    WAYPOINT(on_ZL_CCtx_compressMultiTypedRef_end, cctx, rep);
    return rep;
}

ZL_Report ZL_CCtx_compressTypedRef(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const ZL_TypedRef* input)
{
    return ZL_CCtx_compressMultiTypedRef(cctx, dst, dstCapacity, &input, 1);
}

ZL_Report ZL_CCtx_compress(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize)
{
    ZL_OC_startOperation(
            ZL_CCtx_getOperationContext(cctx), ZL_Operation_compress);
    if (CCTX_isGraphSet(cctx))
        return CCTX_compressSerial_withGraphSet(
                cctx, dst, dstCapacity, src, srcSize);
    // No graph set => use default
    return ZL_CCtx_compress_usingGraphID(
            cctx, dst, dstCapacity, src, srcSize, ZL_GRAPH_SERIAL_COMPRESS);
}

ZL_Report
ZL_CCtx_addHeaderComment(ZL_CCtx* cctx, const void* comment, size_t commentSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(cctx);
    ZL_ERR_IF_GT(
            commentSize,
            ZL_MAX_HEADER_COMMENT_SIZE_LIMIT,
            parameter_invalid,
            "Max header comment size limit exceeded");
    return CCTX_setHeaderComment(cctx, comment, commentSize);
}
