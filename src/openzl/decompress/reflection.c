// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/allocation.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/stream.h"
#include "openzl/decompress/dctx2.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_reflection.h"

struct ZL_DataInfo_s {
    size_t index;
    const ZL_Data* stream;
    const ZL_CodecInfo* producer;
    const ZL_CodecInfo* consumer;
};

struct ZL_CodecInfo_s {
    const ZL_ReflectionCtx* rctx;
    size_t index;
    const char* name;
    const void* header;
    size_t headerSize;
    PublicTransformInfo info;
    const ZL_DataInfo** inputStreams;
    size_t nbInputStreams;
    const ZL_DataInfo** outputStreams;
    size_t nbOutputStreams;
    size_t nbVariableOutputs;
};

struct ZL_ReflectionCtx_s {
    bool inputHasBeenSet;
    ZL_DCtx* dctx;
    Arena* arena;
    uint32_t frameFormatVersion;
    size_t frameHeaderSize;
    size_t frameFooterSize;
    size_t totalTransformHeaderSize;
    ZL_DataInfo* streams;
    size_t nbStreams;
    ZL_CodecInfo* transforms;
    size_t nbTransforms;
    const ZL_DataInfo** storedStreams;
    size_t nbStoredStreams;
    const ZL_DataInfo** inputStreams;
    size_t nbInputStreams;
};

ZL_ReflectionCtx* ZL_ReflectionCtx_create(void)
{
    ZL_ReflectionCtx* rctx = ZL_calloc(sizeof(*rctx));
    if (rctx == NULL) {
        return NULL;
    }
    rctx->dctx = ZL_DCtx_create();
    if (rctx->dctx == NULL) {
        ZL_ReflectionCtx_free(rctx);
        return NULL;
    }
    rctx->arena = ALLOC_HeapArena_create();
    if (rctx->arena == NULL) {
        ZL_ReflectionCtx_free(rctx);
        return NULL;
    }
    return rctx;
}

void ZL_ReflectionCtx_free(ZL_ReflectionCtx* rctx)
{
    if (rctx == NULL) {
        return;
    }
    ZL_DCtx_free(rctx->dctx);
    ALLOC_Arena_freeArena(rctx->arena);
    ZL_free(rctx);
}

ZL_DCtx* ZL_ReflectionCtx_getDCtx(ZL_ReflectionCtx* rctx)
{
    ZL_REQUIRE(
            !rctx->inputHasBeenSet, "Must be called before processing input");
    return rctx->dctx;
}

void ZL_ReflectionCtx_registerTypedDecoder(
        ZL_ReflectionCtx* rctx,
        const ZL_TypedDecoderDesc* dtd)
{
    ZL_REQUIRE(
            !rctx->inputHasBeenSet,
            "Must register transform before processing input");
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(rctx->dctx, dtd));
}

void ZL_ReflectionCtx_registerVODecoder(
        ZL_ReflectionCtx* rctx,
        ZL_VODecoderDesc const* dtd)
{
    ZL_REQUIRE(
            !rctx->inputHasBeenSet,
            "Must register transform before processing input");
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerVODecoder(rctx->dctx, dtd));
}

void ZL_ReflectionCtx_registerMIDecoder(
        ZL_ReflectionCtx* rctx,
        ZL_MIDecoderDesc const* dtd)
{
    ZL_REQUIRE(
            !rctx->inputHasBeenSet,
            "Must register transform before processing input");
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerMIDecoder(rctx->dctx, dtd));
}

static ZL_Report
copyStream(ZL_ReflectionCtx* rctx, const ZL_Data** dstPtr, const ZL_Data* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(rctx->dctx);
    ZL_Data* dst = STREAM_createInArena(rctx->arena, ZL_Data_id(src));
    ZL_ERR_IF_ERR(STREAM_copy(dst, src));
    *dstPtr = dst;
    return ZL_returnSuccess();
}

/**
 * Builds the transform graph & fills out all the stream & transform info.
 */
static ZL_Report fillStreamAndTransformInfo(
        ZL_ReflectionCtx* rctx,
        const char* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(rctx->dctx);
    DFH_Struct const* dfh = DCtx_getFrameHeader(rctx->dctx);
    ZL_ERR_IF_NULL(dfh, GENERIC);
    const size_t nbStreams    = ZL_DCtx_getNumStreams(rctx->dctx);
    const size_t nbTransforms = dfh->nbDTransforms;

    ALLOC_ARENA_CALLOC_CHECKED(ZL_DataInfo, streams, nbStreams, rctx->arena);
    ALLOC_ARENA_CALLOC_CHECKED(
            ZL_CodecInfo, transforms, nbTransforms, rctx->arena);

    for (size_t streamIdx = 0; streamIdx < nbStreams; ++streamIdx) {
        ZL_DataInfo* info = &streams[streamIdx];
        info->index       = streamIdx;
        // Must copy out of the dctx, because the streams may reference the
        // source buffer.
        ZL_ERR_IF_ERR(copyStream(
                rctx,
                &info->stream,
                ZL_DCtx_getConstStream(rctx->dctx, (ZL_IDType)streamIdx)));
        // producer filled by transform
        // consumer filled by transform
    }

    const char* const transformHeaderBuffer =
            (const char*)src + rctx->frameHeaderSize;
    for (size_t transformIdx = 0, streamIdx = 0; transformIdx < nbTransforms;
         ++transformIdx) {
        ZL_CodecInfo* info = &transforms[transformIdx];

        // outputs = input streams of decoder
        // inputs  = regenerated streams of decoder
        const DFH_NodeInfo* node = &VECTOR_AT(dfh->nodes, transformIdx);
        const size_t nbInputs    = node->nbRegens;
        ZL_TRY_LET(
                size_t,
                nbOutputs,
                DCtx_getNbInputStreams(rctx->dctx, (ZL_IDType)transformIdx));

        ALLOC_ARENA_MALLOC_CHECKED(
                const ZL_DataInfo*, inputs, nbInputs, rctx->arena);
        ALLOC_ARENA_MALLOC_CHECKED(
                const ZL_DataInfo*, outputs, nbOutputs, rctx->arena);

        const size_t outputBaseIdx = streamIdx;
        for (size_t i = 0; i < nbOutputs; ++i) {
            ZL_DataInfo* output = &streams[outputBaseIdx + i];
            outputs[i]          = output;
            output->producer    = info;
        }

        const size_t inputBaseIdx = outputBaseIdx + nbOutputs;
        for (size_t i = 0; i < nbInputs; ++i) {
            ZL_DataInfo* input =
                    &streams[inputBaseIdx + node->regenDistances[i]];
            inputs[i]       = input;
            input->consumer = info;
        }

        streamIdx = inputBaseIdx;

        info->rctx       = rctx;
        info->index      = transformIdx;
        info->name       = DCTX_getTrName(rctx->dctx, (ZL_IDType)transformIdx);
        info->info       = node->trpid;
        info->header     = transformHeaderBuffer + node->trhStart;
        info->headerSize = node->trhSize;
        info->inputStreams      = inputs;
        info->nbInputStreams    = nbInputs;
        info->outputStreams     = outputs;
        info->nbOutputStreams   = nbOutputs;
        info->nbVariableOutputs = node->nbVOs;
    }

    rctx->streams      = streams;
    rctx->nbStreams    = nbStreams;
    rctx->transforms   = transforms;
    rctx->nbTransforms = nbTransforms;

    return ZL_returnSuccess();
}

/**
 * Finds the input streams to the compression call.
 * Finds the streams stored in the frame.
 */
static ZL_Report fillExtraStreamInfo(
        ZL_ReflectionCtx* rctx,
        size_t nbInputStreams)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(rctx->dctx);
    const DFH_Struct* dfh        = DCtx_getFrameHeader(rctx->dctx);
    const size_t nbStoredStreams = dfh->nbStoredStreams;

    ALLOC_ARENA_CALLOC_CHECKED(
            const ZL_DataInfo*, storedStreams, nbStoredStreams, rctx->arena);
    ALLOC_ARENA_CALLOC_CHECKED(
            const ZL_DataInfo*, inputStreams, nbInputStreams, rctx->arena);

    // stored streams: streams with consumer == NULL
    // input streams: streams with producer == NULL
    size_t storedStreamIdx = 0;
    size_t inputStreamIdx  = 0;
    for (size_t i = 0; i < rctx->nbStreams; ++i) {
        const ZL_DataInfo* stream = &rctx->streams[i];
        if (stream->producer == NULL) {
            ZL_REQUIRE_GE(i, rctx->nbStreams - nbInputStreams);
            inputStreams[inputStreamIdx++] = stream;
        }
        if (stream->consumer == NULL) {
            storedStreams[storedStreamIdx++] = stream;
        }
    }

    ZL_REQUIRE_EQ(storedStreamIdx, nbStoredStreams);
    ZL_REQUIRE_EQ(inputStreamIdx, nbInputStreams);

    rctx->inputStreams    = inputStreams;
    rctx->nbInputStreams  = nbInputStreams;
    rctx->storedStreams   = storedStreams;
    rctx->nbStoredStreams = nbStoredStreams;

    return ZL_returnSuccess();
}

/**
 * Fills info about the frame like the header & footer size.
 */
static ZL_Report
fillFrameInfo(ZL_ReflectionCtx* rctx, const void* src, size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(rctx->dctx);
    DFH_Struct const* dfh = DCtx_getFrameHeader(rctx->dctx);
    ZL_TRY_LET(size_t, frameHeaderSize, ZL_getHeaderSize(src, srcSize));
    rctx->frameFormatVersion       = dfh->formatVersion;
    rctx->frameHeaderSize          = frameHeaderSize;
    rctx->totalTransformHeaderSize = dfh->totalTHSize;
    rctx->frameFooterSize          = 0;
    if (FrameInfo_hasCompressedChecksum(dfh->frameinfo)) {
        rctx->frameFooterSize += 4;
    }
    if (FrameInfo_hasContentChecksum(dfh->frameinfo)) {
        rctx->frameFooterSize += 4;
    }
    return ZL_returnSuccess();
}

static ZL_Report ZL_ReflectionCtx_setCompressedFrame_impl(
        ZL_ReflectionCtx* rctx,
        const ZL_FrameInfo* fi,
        const void* src,
        size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(rctx->dctx);
    // Create the output stream storage in our arena.
    // We reference these streams, so they must live for the lifetime of the
    // ZL_ReflectionCtx.
    ZL_TRY_LET(size_t, nbOutputs, ZL_FrameInfo_getNumOutputs(fi));
    ALLOC_ARENA_CALLOC_CHECKED(
            ZL_TypedBuffer*, outputs, nbOutputs, rctx->arena);
    for (size_t i = 0; i < nbOutputs; ++i) {
        outputs[i] = ZL_codemodDataAsOutput(
                STREAM_createInArena(rctx->arena, ZL_DATA_ID_INPUTSTREAM));
    }

    // Run decompression
    DCTX_preserveStreams(rctx->dctx);
    ZL_ERR_IF_ERR(ZL_DCtx_decompressMultiTBuffer(
            rctx->dctx, outputs, nbOutputs, src, srcSize));

    ZL_ERR_IF_ERR(fillFrameInfo(rctx, src, srcSize));
    ZL_ERR_IF_ERR(fillStreamAndTransformInfo(rctx, src));
    ZL_ERR_IF_ERR(fillExtraStreamInfo(rctx, nbOutputs));

    return ZL_returnSuccess();
}

ZL_Report ZL_ReflectionCtx_setCompressedFrame(
        ZL_ReflectionCtx* rctx,
        void const* src,
        size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(rctx->dctx);
    ZL_REQUIRE(
            !rctx->inputHasBeenSet,
            "Each reflection ctx can only be used for one input");
    rctx->inputHasBeenSet = true;

    ZL_FrameInfo* fi = ZL_FrameInfo_create(src, srcSize);
    ZL_ERR_IF_NULL(fi, allocation);
    ZL_Report report =
            ZL_ReflectionCtx_setCompressedFrame_impl(rctx, fi, src, srcSize);
    ZL_FrameInfo_free(fi);
    return report;
}

static void validate(const ZL_ReflectionCtx* rctx)
{
    ZL_REQUIRE(
            rctx->inputHasBeenSet,
            "Must call ZL_ReflectionCtx_setCompressedFrame() first");
}

uint32_t ZL_ReflectionCtx_getFrameFormatVersion(ZL_ReflectionCtx const* rctx)
{
    validate(rctx);
    return rctx->frameFormatVersion;
}

size_t ZL_ReflectionCtx_getNumInputs(ZL_ReflectionCtx const* rctx)
{
    validate(rctx);
    return rctx->nbInputStreams;
}

ZL_DataInfo const* ZL_ReflectionCtx_getInput(
        ZL_ReflectionCtx const* rctx,
        size_t index)
{
    validate(rctx);
    ZL_REQUIRE_LT(index, rctx->nbInputStreams);
    return rctx->inputStreams[index];
}

size_t ZL_ReflectionCtx_getNumStoredOutputs_lastChunk(
        ZL_ReflectionCtx const* rctx)
{
    validate(rctx);
    return rctx->nbStoredStreams;
}

ZL_DataInfo const* ZL_ReflectionCtx_getStoredOutput_lastChunk(
        ZL_ReflectionCtx const* rctx,
        size_t index)
{
    validate(rctx);
    ZL_REQUIRE_LT(index, rctx->nbStoredStreams);
    return rctx->storedStreams[index];
}

size_t ZL_ReflectionCtx_getNumStreams_lastChunk(ZL_ReflectionCtx const* rctx)
{
    validate(rctx);
    return rctx->nbStreams;
}

const ZL_DataInfo* ZL_ReflectionCtx_getStream_lastChunk(
        ZL_ReflectionCtx const* rctx,
        size_t index)
{
    validate(rctx);
    ZL_REQUIRE_LT(index, rctx->nbStreams);
    return &rctx->streams[index];
}

size_t ZL_ReflectionCtx_getNumCodecs_lastChunk(ZL_ReflectionCtx const* rctx)
{
    validate(rctx);
    return rctx->nbTransforms;
}

const ZL_CodecInfo* ZL_ReflectionCtx_getCodec_lastChunk(
        ZL_ReflectionCtx const* rctx,
        size_t index)
{
    validate(rctx);
    ZL_REQUIRE_LT(index, rctx->nbTransforms);
    return &rctx->transforms[index];
}

size_t ZL_ReflectionCtx_getFrameHeaderSize(ZL_ReflectionCtx const* rctx)
{
    validate(rctx);
    return rctx->frameHeaderSize;
}

size_t ZL_ReflectionCtx_getFrameFooterSize(ZL_ReflectionCtx const* rctx)
{
    validate(rctx);
    return rctx->frameFooterSize;
}

size_t ZL_ReflectionCtx_getTotalTransformHeaderSize_lastChunk(
        ZL_ReflectionCtx const* rctx)
{
    return rctx->totalTransformHeaderSize;
}

ZL_Type ZL_DataInfo_getType(ZL_DataInfo const* si)
{
    return ZL_Data_type(si->stream);
}

size_t ZL_DataInfo_getNumElts(ZL_DataInfo const* si)
{
    return ZL_Data_numElts(si->stream);
}

size_t ZL_DataInfo_getContentSize(ZL_DataInfo const* si)
{
    return ZL_Data_contentSize(si->stream);
}

size_t ZL_DataInfo_getEltWidth(ZL_DataInfo const* si)
{
    return ZL_Data_eltWidth(si->stream);
}

void const* ZL_DataInfo_getDataPtr(ZL_DataInfo const* si)
{
    return ZL_Data_rPtr(si->stream);
}

uint32_t const* ZL_DataInfo_getLengthsPtr(ZL_DataInfo const* si)
{
    return ZL_Data_rStringLens(si->stream);
}

ZL_CodecInfo const* ZL_DataInfo_getProducerCodec(ZL_DataInfo const* si)
{
    return si->producer;
}

ZL_CodecInfo const* ZL_DataInfo_getConsumerCodec(ZL_DataInfo const* si)
{
    return si->consumer;
}

size_t ZL_DataInfo_getIndex(ZL_DataInfo const* si)
{
    return si->index;
}

char const* ZL_CodecInfo_getName(ZL_CodecInfo const* ti)
{
    return ti->name;
}

ZL_IDType ZL_CodecInfo_getCodecID(ZL_CodecInfo const* ti)
{
    return ti->info.trid;
}

bool ZL_CodecInfo_isStandardCodec(ZL_CodecInfo const* ti)
{
    return ti->info.trt == trt_standard;
}

bool ZL_CodecInfo_isCustomCodec(ZL_CodecInfo const* ti)
{
    return !ZL_CodecInfo_isStandardCodec(ti);
}

size_t ZL_CodecInfo_getNumInputs(ZL_CodecInfo const* ti)
{
    return ti->nbInputStreams;
}

void const* ZL_CodecInfo_getHeaderPtr(ZL_CodecInfo const* ti)
{
    return ti->header;
}

size_t ZL_CodecInfo_getHeaderSize(ZL_CodecInfo const* ti)
{
    return ti->headerSize;
}

ZL_DataInfo const* ZL_CodecInfo_getInput(ZL_CodecInfo const* ti, size_t index)
{
    ZL_REQUIRE_LT(index, ti->nbInputStreams);
    return ti->inputStreams[index];
}

size_t ZL_CodecInfo_getNumOutputs(ZL_CodecInfo const* ti)
{
    return ti->nbOutputStreams;
}

ZL_DataInfo const* ZL_CodecInfo_getOutput(ZL_CodecInfo const* ti, size_t index)
{
    ZL_REQUIRE_LT(index, ti->nbOutputStreams);
    return ti->outputStreams[index];
}

size_t ZL_CodecInfo_getNumVariableOutputs(ZL_CodecInfo const* ti)
{
    return ti->nbVariableOutputs;
}

size_t ZL_CodecInfo_getIndex(ZL_CodecInfo const* ti)
{
    return ti->index;
}
