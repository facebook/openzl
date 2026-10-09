// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/safetensors/safetensors_parser.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "custom_parsers/safetensors/safetensors_header.h"
#include "openzl/codecs/zl_ace.h"
#include "openzl/codecs/zl_conversion.h"
#include "openzl/codecs/zl_entropy.h"
#include "openzl/codecs/zl_float_deconstruct.h"
#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_split_by_struct.h"
#include "openzl/codecs/zl_store.h"
#include "openzl/codecs/zl_transformer.h"
#include "openzl/codecs/zl_zstd.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_segmenter.h"
#include "openzl/zl_version.h"

#define ST_CHUNK_SIZE_MAX_PID 1
#define ST_SEGMENT_SIZES_PID 2
#define ST_SEGMENT_TAGS_PID 3

// A float tensor is atypical when an evenly spaced sample of its elements has
// fewer than 1/ST_DISTINCT_RATIO distinct values (masks, constants, ...).
#define ST_SAMPLE_SIZE 4096
#define ST_MIN_ELTS_FOR_SAMPLING 64
#define ST_DISTINCT_RATIO 8

// A chunk cannot hold more than ZL_runtimeNodeLimit() nodes (20000). Measured
// costs: up to ~15 nodes per segment sent to the Transformer, and ~16 nodes per
// MiB of float data. Merging small neighbors and these two caps keep a chunk
// under ~9000 nodes.
#define ST_SMALL_SEGMENT_SIZE ((size_t)16 << 10)
#define ST_MAX_SEGMENTS_PER_CHUNK 512
#define ST_CHUNK_SIZE_LIMIT ((size_t)64 << 20)

typedef enum {
    ST_Successor_header = 0,
    ST_Successor_raw,
    ST_Successor_int8,
    ST_Successor_int16,
    ST_Successor_int32,
    ST_Successor_int64,
    ST_Successor_f16,
    ST_Successor_bf16,
    ST_Successor_f32,
    ST_Successor_f64,
    ST_Successor_atypical16, // low-cardinality 16-bit floats
    ST_Successor_atypical32, // low-cardinality 32-bit floats
    ST_Successor_count,
} ST_Successor;

typedef struct {
    size_t size;
    uint32_t tag;
    uint32_t group; // consecutive segments of a group share a chunk if they fit
    size_t splitAlign; // byte granularity when the segment must be split
} ST_Segment;

typedef struct {
    ZL_Segmenter* sctx;
    ZL_GraphID innerGraph;
    size_t chunkSizeMax;
    size_t* sizes;
    uint32_t* tags;
    size_t capacity;
    size_t nbSegments;
    size_t nbBytes;
} ST_Chunker;

static ST_Successor ST_successorOf(ZL_SafetensorsDtype dtype)
{
    switch (dtype) {
        case ZL_SafetensorsDtype_bool:
        case ZL_SafetensorsDtype_u8:
        case ZL_SafetensorsDtype_i8:
        case ZL_SafetensorsDtype_f8_e4m3:
        case ZL_SafetensorsDtype_f8_e5m2:
            return ST_Successor_int8;
        case ZL_SafetensorsDtype_i16:
        case ZL_SafetensorsDtype_u16:
            return ST_Successor_int16;
        case ZL_SafetensorsDtype_i32:
        case ZL_SafetensorsDtype_u32:
            return ST_Successor_int32;
        case ZL_SafetensorsDtype_i64:
        case ZL_SafetensorsDtype_u64:
            return ST_Successor_int64;
        case ZL_SafetensorsDtype_f16:
            return ST_Successor_f16;
        case ZL_SafetensorsDtype_bf16:
            return ST_Successor_bf16;
        case ZL_SafetensorsDtype_f32:
            return ST_Successor_f32;
        case ZL_SafetensorsDtype_f64:
            return ST_Successor_f64;
        case ZL_SafetensorsDtype_unknown:
        default:
            return ST_Successor_raw;
    }
}

/* ------------------------------------------------------------------------
 * Inner graph: splits a chunk into its segments and routes them by tag.
 * ------------------------------------------------------------------------ */

static ZL_Report
ST_innerGraphFn(ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_ERR_IF_NE(nbInputs, 1, graph_invalidNumInputs);

    const ZL_RefParam sizesParam =
            ZL_Graph_getLocalRefParam(graph, ST_SEGMENT_SIZES_PID);
    const ZL_RefParam tagsParam =
            ZL_Graph_getLocalRefParam(graph, ST_SEGMENT_TAGS_PID);
    ZL_ERR_IF_NE(sizesParam.paramId, ST_SEGMENT_SIZES_PID, graph_invalid);
    ZL_ERR_IF_NE(tagsParam.paramId, ST_SEGMENT_TAGS_PID, graph_invalid);
    const size_t* const sizes  = (const size_t*)sizesParam.paramRef;
    const uint32_t* const tags = (const uint32_t*)tagsParam.paramRef;
    const size_t nbSegments    = sizesParam.paramSize / sizeof(size_t);
    ZL_ERR_IF_NE(
            tagsParam.paramSize / sizeof(uint32_t), nbSegments, graph_invalid);

    const ZL_GraphIDList successors = ZL_Graph_getCustomGraphs(graph);
    ZL_ERR_IF_NE(successors.nbGraphIDs, ST_Successor_count, graph_invalid);

    size_t total = 0;
    for (size_t i = 0; i < nbSegments; ++i) {
        ZL_ERR_IF_GE(tags[i], ST_Successor_count, graph_invalid);
        total += sizes[i];
    }
    ZL_ERR_IF_NE(
            total,
            ZL_Input_contentSize(ZL_Edge_getData(inputs[0])),
            graph_invalid);
    if (nbSegments == 0) {
        return ZL_Edge_setDestination(inputs[0], ZL_GRAPH_STORE);
    }

    ZL_TRY_LET(
            ZL_EdgeList,
            streams,
            ZL_Edge_runSplitNode(inputs[0], sizes, nbSegments));
    ZL_ASSERT_EQ(streams.nbEdges, nbSegments);
    for (size_t i = 0; i < nbSegments; ++i) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(
                streams.edges[i], successors.graphids[tags[i]]));
    }
    return ZL_returnSuccess();
}

/* ------------------------------------------------------------------------
 * Segmentation of the file
 * ------------------------------------------------------------------------ */

/// Length of the name prefix ending with its first integer component,
/// e.g. "model.layers.12" in "model.layers.12.mlp.up_proj.weight", or 0.
static bool ST_isSeparator(char c)
{
    return c == '.' || c == '/' || c == '_' || c == ':' || c == '-';
}

static size_t ST_layerKeySize(const char* name, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        const bool atBoundary = i == 0 || ST_isSeparator(name[i - 1]);
        if (!atBoundary || name[i] < '0' || name[i] > '9') {
            continue;
        }
        size_t j = i;
        while (j < size && name[j] >= '0' && name[j] <= '9') {
            ++j;
        }
        if (j == size || ST_isSeparator(name[j])) {
            return j;
        }
    }
    return 0;
}

static bool ST_sameLayer(
        const ZL_SafetensorsTensor* a,
        const ZL_SafetensorsTensor* b)
{
    const size_t ka = ST_layerKeySize(a->name, a->nameSize);
    const size_t kb = ST_layerKeySize(b->name, b->nameSize);
    return ka == kb && memcmp(a->name, b->name, ka) == 0;
}

/// Split granularity: whole rows when they are small enough, else elements.
static size_t ST_splitAlign(const ZL_SafetensorsTensor* t, size_t chunkSizeMax)
{
    const size_t eltSize = ZL_Safetensors_dtypeSize(t->dtype);
    if (t->dtype == ZL_SafetensorsDtype_unknown || t->ndims < 2) {
        return eltSize;
    }
    uint64_t rowBytes = eltSize;
    for (uint32_t d = 1; d < t->ndims; ++d) {
        rowBytes *= t->shape[d];
        if (rowBytes > chunkSizeMax / 2) {
            return eltSize;
        }
    }
    return rowBytes ? (size_t)rowBytes : eltSize;
}

static int ST_compareU32(const void* a, const void* b)
{
    const uint32_t x = *(const uint32_t*)a;
    const uint32_t y = *(const uint32_t*)b;
    return (x > y) - (x < y);
}

static bool ST_isAtypical(
        const uint8_t* data,
        size_t nbElts,
        size_t eltSize,
        uint32_t* sample)
{
    if (nbElts < ST_MIN_ELTS_FOR_SAMPLING) {
        return false;
    }
    const size_t nbSamples = ZL_MIN(nbElts, (size_t)ST_SAMPLE_SIZE);
    const size_t stride    = nbElts / nbSamples;
    for (size_t i = 0; i < nbSamples; ++i) {
        const uint8_t* const elt = data + i * stride * eltSize;
        sample[i] = eltSize == 2 ? ZL_readLE16(elt) : ZL_readLE32(elt);
    }
    qsort(sample, nbSamples, sizeof(sample[0]), ST_compareU32);
    size_t nbDistinct = 1;
    for (size_t i = 1; i < nbSamples; ++i) {
        nbDistinct += sample[i] != sample[i - 1];
    }
    return nbDistinct * ST_DISTINCT_RATIO < nbSamples;
}

/// Float tensors that are not floating-point noise go to the Transformer.
static uint32_t
ST_tagOf(const ZL_SafetensorsTensor* t, const uint8_t* data, uint32_t* sample)
{
    const ST_Successor successor = ST_successorOf(t->dtype);
    if (successor != ST_Successor_bf16 && successor != ST_Successor_f16
        && successor != ST_Successor_f32) {
        return successor;
    }
    const size_t eltSize = ZL_Safetensors_dtypeSize(t->dtype);
    const size_t nbElts  = (size_t)(t->end - t->begin) / eltSize;
    if (!ST_isAtypical(data + t->begin, nbElts, eltSize, sample)) {
        return successor;
    }
    return eltSize == 2 ? ST_Successor_atypical16 : ST_Successor_atypical32;
}

/// Merges consecutive small segments after the header, so that small segments
/// are never adjacent. A merged run keeps its tag when it is shared, and is
/// never split.
static size_t ST_mergeSmallSegments(ST_Segment* segments, size_t nbSegments)
{
    size_t n = 1;
    for (size_t i = 1; i < nbSegments; ++i) {
        ST_Segment* const run = &segments[n - 1];
        if (n > 1 && run->size < ST_SMALL_SEGMENT_SIZE
            && segments[i].size < ST_SMALL_SEGMENT_SIZE) {
            if (run->tag != segments[i].tag) {
                run->tag = ST_Successor_raw;
            }
            run->size += segments[i].size;
            run->splitAlign = run->size;
            continue;
        }
        segments[n++] = segments[i];
    }
    return n;
}

/// Builds the list of segments covering the input: header, tensors, gaps.
/// @returns the number of segments written to @p segments.
static size_t ST_buildSegments(
        const ZL_SafetensorsTensor* tensors,
        size_t nbTensors,
        size_t dataStart,
        size_t inputSize,
        size_t chunkSizeMax,
        const uint8_t* src,
        uint32_t* sample,
        ST_Segment* segments)
{
    size_t n       = 0;
    uint32_t group = 0;
    segments[n++]  = (ST_Segment){ dataStart, ST_Successor_header, group, 1 };
    size_t pos     = dataStart;
    const ZL_SafetensorsTensor* prev = NULL;
    for (size_t i = 0; i < nbTensors; ++i) {
        const ZL_SafetensorsTensor* const t = &tensors[i];
        if (t->begin == t->end) {
            continue;
        }
        const size_t begin = dataStart + (size_t)t->begin;
        if (begin > pos) {
            segments[n++] =
                    (ST_Segment){ begin - pos, ST_Successor_raw, ++group, 1 };
            prev = NULL;
        }
        if (prev == NULL || !ST_sameLayer(prev, t)) {
            ++group;
        }
        segments[n++] = (ST_Segment){ (size_t)(t->end - t->begin),
                                      ST_tagOf(t, src + dataStart, sample),
                                      group,
                                      ST_splitAlign(t, chunkSizeMax) };
        pos           = dataStart + (size_t)t->end;
        prev          = t;
    }
    if (pos < inputSize) {
        segments[n++] =
                (ST_Segment){ inputSize - pos, ST_Successor_raw, ++group, 1 };
    }
    return ST_mergeSmallSegments(segments, n);
}

/* ------------------------------------------------------------------------
 * Chunking
 * ------------------------------------------------------------------------ */

static ZL_Report ST_flush(ST_Chunker* ch)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(ch->sctx);
    if (ch->nbBytes == 0) {
        return ZL_returnSuccess();
    }
    const ZL_RefParam refParams[2] = {
        { ST_SEGMENT_SIZES_PID, ch->sizes, ch->nbSegments * sizeof(size_t) },
        { ST_SEGMENT_TAGS_PID, ch->tags, ch->nbSegments * sizeof(uint32_t) },
    };
    const ZL_LocalParams localParams        = { .refParams = { refParams, 2 } };
    const ZL_RuntimeGraphParameters gparams = { .localParams = &localParams };
    ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
            ch->sctx, &ch->nbBytes, 1, ch->innerGraph, &gparams));
    ch->nbSegments = 0;
    ch->nbBytes    = 0;
    return ZL_returnSuccess();
}

/// A chunk holds at most one piece of each segment: pieces of a split segment
/// are flushed one by one, except the last.
static void ST_append(ST_Chunker* ch, size_t size, uint32_t tag)
{
    ZL_ASSERT_LT(ch->nbSegments, ch->capacity);
    ch->sizes[ch->nbSegments] = size;
    ch->tags[ch->nbSegments]  = tag;
    ++ch->nbSegments;
    ch->nbBytes += size;
}

/// Only the last chunk may be smaller than ZL_MIN_CHUNK_SIZE, which
/// ZL_compressBound() relies on.
static ZL_Report ST_flushIfLargeEnough(ST_Chunker* ch)
{
    if (ch->nbBytes < ZL_MIN_CHUNK_SIZE) {
        return ZL_returnSuccess();
    }
    return ST_flush(ch);
}

/// Adds one segment; when it overflows the chunk, it is split on aligned
/// boundaries, its first piece filling the room left in the current chunk.
static ZL_Report ST_addSegment(ST_Chunker* ch, const ST_Segment* seg)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(ch->sctx);
    if (ch->nbSegments >= ST_MAX_SEGMENTS_PER_CHUNK) {
        ZL_ERR_IF_ERR(ST_flush(ch));
    }
    if (ch->nbBytes + seg->size > ch->chunkSizeMax) {
        ZL_ERR_IF_ERR(ST_flushIfLargeEnough(ch));
    }
    size_t remaining = seg->size;
    while (ch->nbBytes + remaining > ch->chunkSizeMax) {
        const size_t room =
                ch->chunkSizeMax - ZL_MIN(ch->nbBytes, ch->chunkSizeMax);
        const size_t align = seg->splitAlign;
        const size_t piece = ZL_MAX(align, room / align * align);
        if (piece >= remaining) {
            break;
        }
        ST_append(ch, piece, seg->tag);
        ZL_ERR_IF_ERR(ST_flush(ch));
        remaining -= piece;
    }
    ST_append(ch, remaining, seg->tag);
    return ZL_returnSuccess();
}

/// Adds the segments of one group, keeping them in a single chunk if possible.
static ZL_Report
ST_addGroup(ST_Chunker* ch, const ST_Segment* segments, size_t nbSegments)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(ch->sctx);
    size_t groupBytes = 0;
    for (size_t i = 0; i < nbSegments; ++i) {
        groupBytes += segments[i].size;
    }
    if (ch->nbBytes + groupBytes > ch->chunkSizeMax) {
        ZL_ERR_IF_ERR(ST_flushIfLargeEnough(ch));
    }
    for (size_t i = 0; i < nbSegments; ++i) {
        ZL_ERR_IF_ERR(ST_addSegment(ch, &segments[i]));
    }
    return ZL_returnSuccess();
}

static ZL_Report
ST_chunkSegments(ST_Chunker* ch, const ST_Segment* segments, size_t nbSegments)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(ch->sctx);
    size_t first = 0;
    while (first < nbSegments) {
        size_t last = first + 1;
        while (last < nbSegments
               && segments[last].group == segments[first].group) {
            ++last;
        }
        ZL_ERR_IF_ERR(ST_addGroup(ch, segments + first, last - first));
        first = last;
    }
    return ST_flush(ch);
}

/// Records why the input is not parsed, retrievable via ZL_CCtx_getWarnings().
static ZL_Report ST_warnFallback(ZL_Segmenter* sctx, const char* reason)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    ZL_ERR(graph_parser_unhandledInput,
           "Input not parsed as safetensors (%s): compressed as generic data",
           reason);
}

typedef struct {
    ZL_SafetensorsTensor* tensors;
    ST_Segment* segments;
    uint32_t* sample;
    size_t* sizes;
    uint32_t* tags;
} ST_Buffers;

/// header + one gap before each tensor + trailing gap
static size_t ST_maxSegments(size_t nbTensors)
{
    return 2 * nbTensors + 2;
}

/// @returns false if an allocation failed, so that the caller can fall back.
static bool
ST_allocBuffers(ZL_Segmenter* sctx, size_t nbTensors, ST_Buffers* buffers)
{
    const size_t maxSegments = ST_maxSegments(nbTensors);
    buffers->tensors         = ZL_Segmenter_getScratchSpace(
            sctx, ZL_MAX(nbTensors, 1) * sizeof(*buffers->tensors));
    buffers->segments = ZL_Segmenter_getScratchSpace(
            sctx, maxSegments * sizeof(*buffers->segments));
    buffers->sample = ZL_Segmenter_getScratchSpace(
            sctx, ST_SAMPLE_SIZE * sizeof(*buffers->sample));
    buffers->sizes = ZL_Segmenter_getScratchSpace(
            sctx, maxSegments * sizeof(*buffers->sizes));
    buffers->tags = ZL_Segmenter_getScratchSpace(
            sctx, maxSegments * sizeof(*buffers->tags));
    return buffers->tensors && buffers->segments && buffers->sample
            && buffers->sizes && buffers->tags;
}

/// Compresses the input in plain chunks with the generic graph.
static ZL_Report ST_fallback(ZL_Segmenter* sctx, size_t inputSize, size_t chunk)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    size_t pos = 0;
    while (pos < inputSize) {
        size_t size = ZL_MIN(chunk, inputSize - pos);
        ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
                sctx, &size, 1, ZL_GRAPH_COMPRESS_GENERIC, NULL));
        pos += size;
    }
    return ZL_returnSuccess();
}

static size_t ST_chunkSizeMax(ZL_Segmenter* sctx)
{
    const ZL_IntParam param =
            ZL_Segmenter_getLocalIntParam(sctx, ST_CHUNK_SIZE_MAX_PID);
    if (param.paramId != ST_CHUNK_SIZE_MAX_PID || param.paramValue <= 0) {
        return ZL_SAFETENSORS_DEFAULT_CHUNK_SIZE;
    }
    // Room for a chunk to be filled to ZL_MIN_CHUNK_SIZE before row-aligned
    // splitting closes it.
    return ZL_MIN(
            ZL_MAX((size_t)param.paramValue, 2 * (size_t)ZL_MIN_CHUNK_SIZE),
            ST_CHUNK_SIZE_LIMIT);
}

static ZL_Report ST_segmenterFn(ZL_Segmenter* sctx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    ZL_ERR_IF_NE(ZL_Segmenter_numInputs(sctx), 1, node_invalid_input);
    const ZL_Input* const input = ZL_Segmenter_getInput(sctx, 0);
    const size_t inputSize      = ZL_Input_contentSize(input);
    const size_t chunkSizeMax   = ST_chunkSizeMax(sctx);
    if (inputSize == 0) {
        size_t size = 0;
        return ZL_Segmenter_processChunk(sctx, &size, 1, ZL_GRAPH_STORE, NULL);
    }
    const unsigned formatVersion =
            (unsigned)ZL_Segmenter_getCParam(sctx, ZL_CParam_formatVersion);
    if (formatVersion < ZL_CHUNK_VERSION_MIN) {
        // A single chunk cannot hold one stream per tensor of a large model.
        ZL_RES_convertToWarning(
                sctx, ST_warnFallback(sctx, "format version without chunks"));
        return ST_fallback(sctx, inputSize, SIZE_MAX);
    }

    // Counting first bounds the allocations by the number of tensors, and lets
    // an invalid header fall back before anything is allocated.
    const void* const src = ZL_Input_ptr(input);
    size_t nbTensors, dataStart;
    ST_Buffers buffers = { 0 };
    const char* reason = ZL_Safetensors_parseHeader(
            src, inputSize, NULL, 0, &nbTensors, &dataStart);
    if (reason == NULL && !ST_allocBuffers(sctx, nbTensors, &buffers)) {
        reason = "not enough memory";
    }
    if (reason == NULL) {
        reason = ZL_Safetensors_parseHeader(
                src,
                inputSize,
                buffers.tensors,
                nbTensors,
                &nbTensors,
                &dataStart);
    }
    if (reason != NULL) {
        ZL_RES_convertToWarning(sctx, ST_warnFallback(sctx, reason));
        return ST_fallback(sctx, inputSize, chunkSizeMax);
    }

    const size_t nbSegments = ST_buildSegments(
            buffers.tensors,
            nbTensors,
            dataStart,
            inputSize,
            chunkSizeMax,
            (const uint8_t*)src,
            buffers.sample,
            buffers.segments);

    const ZL_GraphIDList customGraphs = ZL_Segmenter_getCustomGraphs(sctx);
    ZL_ERR_IF_NE(customGraphs.nbGraphIDs, 1, graph_invalid);
    ST_Chunker chunker = {
        .sctx         = sctx,
        .innerGraph   = customGraphs.graphids[0],
        .chunkSizeMax = chunkSizeMax,
        .sizes        = buffers.sizes,
        .tags         = buffers.tags,
        .capacity     = ST_maxSegments(nbTensors),
    };
    return ST_chunkSegments(&chunker, buffers.segments, nbSegments);
}

/* ------------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------------ */

/// Trainable successor for numeric data of width @p interpretNode.
static ZL_GraphID ST_numericSuccessor(
        ZL_Compressor* compressor,
        ZL_NodeID interpretNode,
        ZL_GraphID defaultGraph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor,
            interpretNode,
            ZL_Compressor_buildACEGraphWithDefault(compressor, defaultGraph));
}

static ZL_GraphID ST_floatSuccessor(
        ZL_Compressor* compressor,
        ZL_NodeID interpretNode,
        ZL_NodeID deconstructNode)
{
    const ZL_GraphID deconstruct = ZL_Compressor_registerStaticGraph_fromNode(
            compressor,
            deconstructNode,
            ZL_GRAPHLIST(ZL_GRAPH_STORE, ZL_GRAPH_FSE));
    return ST_numericSuccessor(compressor, interpretNode, deconstruct);
}

/// float16 keeps 2 mantissa bits next to sign and exponent in its high byte,
/// so it is split by byte. The low byte is noise in native float16 weights,
/// where FSE falls back to raw storage at no cost, but weights converted from
/// bfloat16 leave its 3 lowest bits at zero.
static ZL_GraphID ST_f16Successor(ZL_Compressor* compressor)
{
    const size_t fieldSizes[2]     = { 1, 1 };
    const ZL_GraphID byteGraphs[2] = { ZL_GRAPH_FSE, ZL_GRAPH_FSE };
    return ZL_Compressor_buildACEGraphWithDefault(
            compressor,
            ZL_Compressor_registerSplitByStructGraph(
                    compressor, fieldSizes, byteGraphs, 2));
}

static void ST_buildSuccessors(
        ZL_Compressor* compressor,
        ZL_GraphID* successors)
{
    successors[ST_Successor_header] =
            ZL_Compressor_buildACEGraphWithDefault(compressor, ZL_GRAPH_ZSTD);
    successors[ST_Successor_raw] = ZL_Compressor_buildACEGraphWithDefault(
            compressor, ZL_GRAPH_COMPRESS_GENERIC);
    // Same default as raw, but kept apart so that training specializes typed
    // byte tensors (masks, quantized weights) separately from unknown bytes.
    successors[ST_Successor_int8] = ZL_Compressor_buildACEGraphWithDefault(
            compressor, ZL_GRAPH_COMPRESS_GENERIC);
    successors[ST_Successor_int16] = ST_numericSuccessor(
            compressor, ZL_NODE_INTERPRET_AS_LE16, ZL_GRAPH_COMPRESS_GENERIC);
    successors[ST_Successor_int32] = ST_numericSuccessor(
            compressor, ZL_NODE_INTERPRET_AS_LE32, ZL_GRAPH_COMPRESS_GENERIC);
    successors[ST_Successor_int64] = ST_numericSuccessor(
            compressor, ZL_NODE_INTERPRET_AS_LE64, ZL_GRAPH_COMPRESS_GENERIC);
    successors[ST_Successor_f16]  = ST_f16Successor(compressor);
    successors[ST_Successor_bf16] = ST_floatSuccessor(
            compressor,
            ZL_NODE_INTERPRET_AS_LE16,
            ZL_NODE_BFLOAT16_DECONSTRUCT);
    successors[ST_Successor_f32] = ST_floatSuccessor(
            compressor, ZL_NODE_INTERPRET_AS_LE32, ZL_NODE_FLOAT32_DECONSTRUCT);
    successors[ST_Successor_f64] = ST_numericSuccessor(
            compressor, ZL_NODE_INTERPRET_AS_LE64, ZL_GRAPH_COMPRESS_GENERIC);
    successors[ST_Successor_atypical16] = ST_numericSuccessor(
            compressor,
            ZL_NODE_INTERPRET_AS_LE16,
            ZL_GRAPH_TRANSFORMER_NUMERIC);
    successors[ST_Successor_atypical32] = ST_numericSuccessor(
            compressor,
            ZL_NODE_INTERPRET_AS_LE32,
            ZL_GRAPH_TRANSFORMER_NUMERIC);
}

static ZL_GraphID ST_innerGraph(ZL_Compressor* compressor)
{
    ZL_GraphID base = ZL_Compressor_getGraph(compressor, "Safetensors Parser");
    if (base.gid == ZL_GRAPH_ILLEGAL.gid) {
        const ZL_Type inputType         = ZL_Type_serial;
        const ZL_FunctionGraphDesc desc = {
            .name           = "!Safetensors Parser",
            .graph_f        = ST_innerGraphFn,
            .inputTypeMasks = &inputType,
            .nbInputs       = 1,
        };
        base = ZL_Compressor_registerFunctionGraph(compressor, &desc);
    }
    ZL_GraphID successors[ST_Successor_count];
    ST_buildSuccessors(compressor, successors);
    const ZL_ParameterizedGraphDesc desc = {
        .graph          = base,
        .customGraphs   = successors,
        .nbCustomGraphs = ST_Successor_count,
    };
    return ZL_Compressor_registerParameterizedGraph(compressor, &desc);
}

ZL_RESULT_OF(ZL_GraphID)
ZL_Safetensors_registerGraph(ZL_Compressor* compressor, size_t chunkSizeMax)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor);
    ZL_ERR_IF_EQ(chunkSizeMax, 0, parameter_invalid);
    const ZL_GraphID inner = ST_innerGraph(compressor);
    ZL_ERR_IF_EQ(inner.gid, ZL_GRAPH_ILLEGAL.gid, graph_invalid);

    ZL_GraphID base =
            ZL_Compressor_getGraph(compressor, "Safetensors Segmenter");
    if (base.gid == ZL_GRAPH_ILLEGAL.gid) {
        const ZL_Type inputType     = ZL_Type_serial;
        const ZL_SegmenterDesc desc = {
            .name           = "!Safetensors Segmenter",
            .segmenterFn    = ST_segmenterFn,
            .inputTypeMasks = &inputType,
            .numInputs      = 1,
        };
        base = ZL_Compressor_registerSegmenter(compressor, &desc);
    }
    const ZL_IntParam intParam = {
        .paramId    = ST_CHUNK_SIZE_MAX_PID,
        .paramValue = (int)ZL_MIN(chunkSizeMax, (size_t)INT32_MAX),
    };
    const ZL_LocalParams localParams     = { .intParams = { &intParam, 1 } };
    const ZL_ParameterizedGraphDesc desc = {
        .graph          = base,
        .localParams    = &localParams,
        .customGraphs   = &inner,
        .nbCustomGraphs = 1,
    };
    const ZL_GraphID graph =
            ZL_Compressor_registerParameterizedGraph(compressor, &desc);
    ZL_ERR_IF_EQ(graph.gid, ZL_GRAPH_ILLEGAL.gid, graph_invalid);
    return ZL_RESULT_WRAP_VALUE(ZL_GraphID, graph);
}
