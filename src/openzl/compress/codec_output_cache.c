// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/codec_output_cache.h"

#include <string.h>

#include "openzl/common/allocation.h"
#include "openzl/common/assertion.h"
#include "openzl/common/map.h"
#include "openzl/common/stream.h"
#include "openzl/compress/cctx.h"
#include "openzl/compress/cgraph.h"
#include "openzl/compress/cnode.h"
#include "openzl/compress/enc_interface.h"
#include "openzl/compress/nodemgr.h"
#include "openzl/dict/dict_constants.h"
#include "openzl/shared/overflow.h"
#include "openzl/shared/xxhash.h"

#define CODEC_CACHE_DEFAULT_MAX_BYTES ((size_t)256 << 20)
#define CODEC_CACHE_MAX_ENTRIES (1u << 24)

typedef struct {
    ZL_Type type;
    size_t eltWidth;
    size_t numElts;
    size_t contentSize;
    const void* content;
    size_t nbIntMetadata;
    const Stream_IntMetadata* intMetadata;
} CodecCache_Input;

typedef struct {
    uint64_t contentHash64;
    ZL_IDType standardNodeID;
    uint32_t formatVersion;
    int32_t compressionLevel;
    int32_t decompressionLevel;
    CodecCache_Input input;
    size_t localParamsSize;
    const void* localParams;
} CodecCache_Key;

static size_t CodecCache_Map_hash(const CodecCache_Key* key);
static bool CodecCache_Map_eq(
        const CodecCache_Key* lhs,
        const CodecCache_Key* rhs);

ZL_DECLARE_CUSTOM_MAP_TYPE(
        CodecCache_Map,
        CodecCache_Key,
        const CodecCache_Result*);

struct ZL_CodecOutputCache_s {
    Arena* cacheArena;
    CodecCache_Map map;
    size_t maxBytes;
    size_t bytesStored;
    bool insertionsEnabled;
    bool statsEnabled;
    size_t hashReuses;
    CodecCache_Stats stats;
    CodecCache_Stats lastCompletedStats;
};

struct CodecCache_Lookup_s {
    ZL_CodecOutputCache* cache;
    CodecCache_Key key;
    const CodecCache_Result* result;
};

static bool CodecCache_equalBytes(const void* lhs, const void* rhs, size_t size)
{
    return size == 0 || memcmp(lhs, rhs, size) == 0;
}

static size_t CodecCache_Map_hash(const CodecCache_Key* key)
{
    XXH3_state_t state;
    XXH3_INITSTATE(&state);
    XXH3_64bits_reset(&state);
    XXH3_64bits_update(&state, &key->contentHash64, sizeof(key->contentHash64));
    XXH3_64bits_update(
            &state, &key->standardNodeID, sizeof(key->standardNodeID));
    XXH3_64bits_update(&state, &key->formatVersion, sizeof(key->formatVersion));
    XXH3_64bits_update(
            &state, &key->compressionLevel, sizeof(key->compressionLevel));
    XXH3_64bits_update(
            &state, &key->decompressionLevel, sizeof(key->decompressionLevel));
    XXH3_64bits_update(
            &state, &key->localParamsSize, sizeof(key->localParamsSize));
    if (key->localParamsSize != 0) {
        XXH3_64bits_update(&state, key->localParams, key->localParamsSize);
    }
    return (size_t)XXH3_64bits_digest(&state);
}

static bool CodecCache_Input_eq(
        const CodecCache_Input* lhs,
        const CodecCache_Input* rhs)
{
    if (lhs->type != rhs->type || lhs->eltWidth != rhs->eltWidth
        || lhs->numElts != rhs->numElts || lhs->contentSize != rhs->contentSize
        || lhs->nbIntMetadata != rhs->nbIntMetadata) {
        return false;
    }
    size_t metadataSize;
    if (ZL_overflowMulST(
                lhs->nbIntMetadata,
                sizeof(Stream_IntMetadata),
                &metadataSize)) {
        return false;
    }
    return CodecCache_equalBytes(lhs->content, rhs->content, lhs->contentSize)
            && CodecCache_equalBytes(
                    lhs->intMetadata, rhs->intMetadata, metadataSize);
}

static bool CodecCache_Map_eq(
        const CodecCache_Key* lhs,
        const CodecCache_Key* rhs)
{
    if (lhs->contentHash64 != rhs->contentHash64
        || lhs->standardNodeID != rhs->standardNodeID
        || lhs->formatVersion != rhs->formatVersion
        || lhs->compressionLevel != rhs->compressionLevel
        || lhs->decompressionLevel != rhs->decompressionLevel
        || lhs->localParamsSize != rhs->localParamsSize
        || !CodecCache_equalBytes(
                lhs->localParams, rhs->localParams, lhs->localParamsSize)) {
        return false;
    }
    return CodecCache_Input_eq(&lhs->input, &rhs->input);
}

static bool CodecCache_addSize(size_t* total, size_t amount)
{
    size_t result;
    if (ZL_overflowAddST(*total, amount, &result)) {
        return false;
    }
    *total = result;
    return true;
}

static bool CodecCache_addArraySize(size_t* total, size_t count, size_t eltSize)
{
    size_t size;
    return !ZL_overflowMulST(count, eltSize, &size)
            && CodecCache_addSize(total, size);
}

static void CodecCache_updateInputHash(
        XXH3_state_t* state,
        const CodecCache_Input* input)
{
    XXH3_64bits_update(state, &input->type, sizeof(input->type));
    XXH3_64bits_update(state, &input->eltWidth, sizeof(input->eltWidth));
    XXH3_64bits_update(state, &input->numElts, sizeof(input->numElts));
    XXH3_64bits_update(state, &input->contentSize, sizeof(input->contentSize));
    if (input->contentSize != 0) {
        XXH3_64bits_update(state, input->content, input->contentSize);
    }
    XXH3_64bits_update(
            state, &input->nbIntMetadata, sizeof(input->nbIntMetadata));
    if (input->nbIntMetadata != 0) {
        XXH3_64bits_update(
                state,
                input->intMetadata,
                input->nbIntMetadata * sizeof(input->intMetadata[0]));
    }
}

static uint64_t CodecCache_hashInput(const CodecCache_Input* input)
{
    XXH3_state_t state;
    XXH3_INITSTATE(&state);
    XXH3_64bits_reset(&state);
    CodecCache_updateInputHash(&state, input);
    return XXH3_64bits_digest(&state);
}

static bool CodecCache_buildInput(
        CodecCache_Input* cacheInput,
        uint64_t* contentHash,
        ZL_CodecOutputCache* cache,
        Arena* scratchArena,
        const ZL_Data* input)
{
    *cacheInput = (CodecCache_Input){
        .type        = ZL_Data_type(input),
        .eltWidth    = ZL_Data_eltWidth(input),
        .numElts     = ZL_Data_numElts(input),
        .contentSize = ZL_Data_contentSize(input),
        .content     = ZL_Data_rPtr(input),
    };
    cacheInput->nbIntMetadata = STREAM_numIntMetadata(input);
    if (cacheInput->nbIntMetadata != 0) {
        size_t metadataSize;
        if (ZL_overflowMulST(
                    cacheInput->nbIntMetadata,
                    sizeof(Stream_IntMetadata),
                    &metadataSize)) {
            return false;
        }
        Stream_IntMetadata* const metadata =
                ALLOC_Arena_malloc(scratchArena, metadataSize);
        if (metadata == NULL
            || ZL_isError(STREAM_copyIntMetadata(
                    metadata, input, cacheInput->nbIntMetadata))) {
            return false;
        }
        cacheInput->intMetadata = metadata;
    }

    uint64_t memoizedHash;
    if (STREAM_getCodecCacheKeyHash(input, &memoizedHash)) {
        *contentHash = memoizedHash;
        CodecCache_recordHashReuse(cache);
        return true;
    }

    *contentHash = CodecCache_hashInput(cacheInput);
    return true;
}

static bool CodecCache_serializeLocalParams(
        void** serialized,
        size_t* serializedSize,
        Arena* scratchArena,
        const ZL_LocalParams* params)
{
    *serialized     = NULL;
    *serializedSize = 0;
    if (params == NULL) {
        return true;
    }

    const ZL_LocalIntParams* const intParams   = &params->intParams;
    const ZL_LocalCopyParams* const copyParams = &params->copyParams;
    if (intParams->nbIntParams == 0 && copyParams->nbCopyParams == 0) {
        return true;
    }
    if (!CodecCache_addSize(serializedSize, sizeof(intParams->nbIntParams))
        || !CodecCache_addSize(serializedSize, sizeof(copyParams->nbCopyParams))
        || !CodecCache_addArraySize(
                serializedSize,
                intParams->nbIntParams,
                sizeof(int) + sizeof(int))) {
        return false;
    }
    for (size_t i = 0; i < copyParams->nbCopyParams; ++i) {
        if (!CodecCache_addSize(serializedSize, sizeof(int) + sizeof(size_t))
            || !CodecCache_addSize(
                    serializedSize, copyParams->copyParams[i].paramSize)) {
            return false;
        }
    }
    *serialized = ALLOC_Arena_malloc(scratchArena, *serializedSize);
    if (*serialized == NULL) {
        return false;
    }
    uint8_t* dst = *serialized;
    memcpy(dst, &intParams->nbIntParams, sizeof(intParams->nbIntParams));
    dst += sizeof(intParams->nbIntParams);
    memcpy(dst, &copyParams->nbCopyParams, sizeof(copyParams->nbCopyParams));
    dst += sizeof(copyParams->nbCopyParams);
    for (size_t i = 0; i < intParams->nbIntParams; ++i) {
        const int id    = intParams->intParams[i].paramId;
        const int value = intParams->intParams[i].paramValue;
        memcpy(dst, &id, sizeof(id));
        dst += sizeof(id);
        memcpy(dst, &value, sizeof(value));
        dst += sizeof(value);
    }
    for (size_t i = 0; i < copyParams->nbCopyParams; ++i) {
        const ZL_CopyParam* const param = &copyParams->copyParams[i];
        memcpy(dst, &param->paramId, sizeof(param->paramId));
        dst += sizeof(param->paramId);
        memcpy(dst, &param->paramSize, sizeof(param->paramSize));
        dst += sizeof(param->paramSize);
        if (param->paramSize != 0) {
            memcpy(dst, param->paramPtr, param->paramSize);
            dst += param->paramSize;
        }
    }
    ZL_ASSERT_EQ((size_t)(dst - (uint8_t*)*serialized), *serializedSize);
    return true;
}

static bool CodecCache_getStandardNodeID(
        ZL_IDType* standardNodeID,
        const ZL_Encoder* encoder,
        ZL_NodeID node)
{
    const ZL_Compressor* const compressor = CCTX_getCGraph(encoder->cctx);
    ZL_ASSERT_NN(compressor);
    ZL_ASSERT_EQ(CGRAPH_getCNode(compressor, node), encoder->cnode);
    while (!NM_isStandardNode(node)) {
        const ZL_NodeID base =
                CNODE_getBaseNodeID(CGRAPH_getCNode(compressor, node));
        if (base.nid == ZL_NODE_ILLEGAL.nid) {
            return false;
        }
        node = base;
    }
    *standardNodeID = node.nid;
    return true;
}

/*
 * Key-completeness audit:
 * - standardNodeID identifies the built-in encoder implementation and output
 *   schema. Parameterized nodes can only change the state covered below.
 * - input includes its type, element width and count, content, and integer
 *   metadata. Multi-input codecs are outside the cache's scope.
 * - localParams includes every integer and the copied bytes of every copy
 *   parameter. Reference parameters, dictionaries, materialized parameters,
 *   custom codecs, and string streams are rejected because their complete
 *   state is unavailable.
 * - Known limitation: state reachable through pointers embedded in copy
 *   parameters is not keyed. Custom tokenize and dispatch nodes use this
 *   pattern and are unsupported until their callback state is represented by
 *   reference parameters or explicit cache eligibility.
 * - formatVersion, compressionLevel, and decompressionLevel are the only
 *   global parameters currently consulted by built-in codecs. Other global
 *   parameters affect graph or frame behavior outside a codec invocation.
 *
 * Input IDs, addresses, and runtime graph position are intentionally not key
 * fields: built-in codecs must treat them as non-semantic. Cached codec state
 * is also not keyed or advanced on replay. The only built-in encoder using it
 * today is zstd, which resets it before every invocation and uses it strictly
 * as reusable workspace.
 *
 * Any new encoder-visible state, or any new dependency of a built-in codec on
 * existing state, must be added to CodecCache_Key or make that invocation
 * uncacheable here.
 */
static bool CodecCache_buildKey(
        CodecCache_Key* key,
        ZL_CodecOutputCache* cache,
        ZL_Encoder* encoder,
        ZL_NodeID node,
        const ZL_Data* input)
{
    ZL_IDType standardNodeID;
    if (!CodecCache_getStandardNodeID(&standardNodeID, encoder, node)) {
        CodecCache_recordSkip(cache, CodecCache_SkipReason_customCodec);
        return false;
    }
    if (CNODE_getDictIndex(encoder->cnode) != ZL_DICT_INDEX_NONE) {
        CodecCache_recordSkip(cache, CodecCache_SkipReason_dict);
        return false;
    }
    if (CNODE_getMParamObj(encoder->cnode) != NULL) {
        CodecCache_recordSkip(cache, CodecCache_SkipReason_mparam);
        return false;
    }
    if (encoder->lparams != NULL
        && encoder->lparams->refParams.nbRefParams != 0) {
        CodecCache_recordSkip(cache, CodecCache_SkipReason_refParam);
        return false;
    }
    if (ZL_Data_type(input) == ZL_Type_string) {
        CodecCache_recordSkip(cache, CodecCache_SkipReason_string);
        return false;
    }

    CodecCache_Input cacheInput;
    uint64_t contentHash;
    if (!CodecCache_buildInput(
                &cacheInput, &contentHash, cache, encoder->wkspArena, input)) {
        return false;
    }
    void* localParams;
    size_t localParamsSize;
    if (!CodecCache_serializeLocalParams(
                &localParams,
                &localParamsSize,
                encoder->wkspArena,
                encoder->lparams)) {
        return false;
    }

    *key = (CodecCache_Key){
        .contentHash64  = contentHash,
        .standardNodeID = standardNodeID,
        .formatVersion  = (uint32_t)ZL_Encoder_getCParam(
                encoder, ZL_CParam_formatVersion),
        .compressionLevel =
                ZL_Encoder_getCParam(encoder, ZL_CParam_compressionLevel),
        .decompressionLevel =
                ZL_Encoder_getCParam(encoder, ZL_CParam_decompressionLevel),
        .input           = cacheInput,
        .localParamsSize = localParamsSize,
        .localParams     = localParams,
    };
    return true;
}

ZL_CodecOutputCache* CodecCache_create(size_t maxBytes)
{
    ZL_CodecOutputCache* const cache = ZL_calloc(sizeof(*cache));
    if (cache == NULL) {
        return NULL;
    }
    cache->cacheArena = ALLOC_HeapArena_create();
    if (cache->cacheArena == NULL) {
        ZL_free(cache);
        return NULL;
    }
    cache->maxBytes          = maxBytes;
    cache->insertionsEnabled = true;
    cache->map               = CodecCache_Map_createInArena(
            cache->cacheArena, CODEC_CACHE_MAX_ENTRIES);
    return cache;
}

size_t CodecCache_getDefaultMaxBytes(void)
{
    return CODEC_CACHE_DEFAULT_MAX_BYTES;
}

void CodecCache_free(ZL_CodecOutputCache* cache)
{
    if (cache == NULL) {
        return;
    }
    ALLOC_Arena_freeArena(cache->cacheArena);
    ZL_free(cache);
}

static void CodecCache_resetCurrent(ZL_CodecOutputCache* cache)
{
    ZL_ASSERT_NN(cache);
    ALLOC_Arena_freeAll(cache->cacheArena);
    cache->map = CodecCache_Map_createInArena(
            cache->cacheArena, CODEC_CACHE_MAX_ENTRIES);
    cache->bytesStored       = 0;
    cache->insertionsEnabled = true;
    if (cache->statsEnabled) {
        cache->hashReuses = 0;
        memset(&cache->stats, 0, sizeof(cache->stats));
    }
}

void CodecCache_setStatsEnabled(ZL_CodecOutputCache* cache, bool enabled)
{
    ZL_ASSERT_NN(cache);
    cache->statsEnabled = enabled;
    cache->hashReuses   = 0;
    memset(&cache->stats, 0, sizeof(cache->stats));
    memset(&cache->lastCompletedStats, 0, sizeof(cache->lastCompletedStats));
}

void CodecCache_reset(ZL_CodecOutputCache* cache)
{
    if (cache == NULL) {
        return;
    }
    CodecCache_resetCurrent(cache);
    if (cache->statsEnabled) {
        memset(&cache->lastCompletedStats,
               0,
               sizeof(cache->lastCompletedStats));
    }
}

void CodecCache_resetPreservingCompletedStats(ZL_CodecOutputCache* cache)
{
    if (cache == NULL) {
        return;
    }
    CodecCache_resetCurrent(cache);
}

void CodecCache_setInsertionsEnabled(ZL_CodecOutputCache* cache, bool enabled)
{
    ZL_ASSERT_NN(cache);
    cache->insertionsEnabled = enabled;
}

void CodecCache_recordHashReuse(ZL_CodecOutputCache* cache)
{
    ZL_ASSERT_NN(cache);
    if (cache->statsEnabled) {
        ++cache->hashReuses;
    }
}

size_t CodecCache_getHashReuses(const ZL_CodecOutputCache* cache)
{
    ZL_ASSERT_NN(cache);
    return cache->statsEnabled ? cache->hashReuses : 0;
}

CodecCache_Stats CodecCache_getStats(const ZL_CodecOutputCache* cache)
{
    ZL_ASSERT_NN(cache);
    if (!cache->statsEnabled) {
        return (CodecCache_Stats){ 0 };
    }
    CodecCache_Stats stats = cache->stats;
    stats.bytesStored      = cache->bytesStored;
    stats.arenaBytes       = ALLOC_Arena_memAllocated(cache->cacheArena);
    return stats;
}

void CodecCache_captureCompletedStats(ZL_CodecOutputCache* cache)
{
    ZL_ASSERT_NN(cache);
    if (cache->statsEnabled) {
        cache->lastCompletedStats = CodecCache_getStats(cache);
    }
}

CodecCache_Stats CodecCache_getLastCompletedStats(
        const ZL_CodecOutputCache* cache)
{
    ZL_ASSERT_NN(cache);
    if (!cache->statsEnabled) {
        return (CodecCache_Stats){ 0 };
    }
    return cache->lastCompletedStats;
}

void CodecCache_recordSkip(
        ZL_CodecOutputCache* cache,
        CodecCache_SkipReason reason)
{
    ZL_ASSERT_NN(cache);
    if (!cache->statsEnabled) {
        return;
    }
    switch (reason) {
        case CodecCache_SkipReason_customCodec:
            ++cache->stats.customCodecSkips;
            break;
        case CodecCache_SkipReason_refParam:
            ++cache->stats.refParamSkips;
            break;
        case CodecCache_SkipReason_dict:
            ++cache->stats.dictSkips;
            break;
        case CodecCache_SkipReason_mparam:
            ++cache->stats.mparamSkips;
            break;
        case CodecCache_SkipReason_string:
            ++cache->stats.stringSkips;
            break;
        case CodecCache_SkipReason_nonSingleInput:
            ++cache->stats.nonSingleInputSkips;
            break;
    }
}

ZL_CodecOutputCache* ZL_CodecOutputCache_create(void)
{
    return CodecCache_create(CodecCache_getDefaultMaxBytes());
}

ZL_CodecOutputCache* ZL_CodecOutputCache_createWithBudget(size_t maxBytes)
{
    return CodecCache_create(maxBytes);
}

void ZL_CodecOutputCache_free(ZL_CodecOutputCache* cache)
{
    CodecCache_free(cache);
}

void ZL_CodecOutputCache_reset(ZL_CodecOutputCache* cache)
{
    CodecCache_reset(cache);
}

CodecCache_Lookup* CodecCache_lookup(
        ZL_CodecOutputCache* cache,
        ZL_Encoder* encoder,
        ZL_NodeID node,
        const ZL_Data* input)
{
    ZL_ASSERT_NN(cache);
    ZL_ASSERT_NN(encoder);
    CodecCache_Lookup* const lookup =
            ALLOC_Arena_malloc(encoder->wkspArena, sizeof(*lookup));
    if (lookup == NULL
        || !CodecCache_buildKey(&lookup->key, cache, encoder, node, input)) {
        return NULL;
    }
    lookup->cache = cache;
    const CodecCache_Map_Entry* const found =
            CodecCache_Map_find(&cache->map, &lookup->key);
    if (found == NULL) {
        if (cache->statsEnabled) {
            ++cache->stats.misses;
        }
        if (!cache->insertionsEnabled) {
            return NULL;
        }
        lookup->result = NULL;
    } else {
        if (cache->statsEnabled) {
            ++cache->stats.hits;
        }
        lookup->result = found->val;
    }
    return lookup;
}

const CodecCache_Result* CodecCache_Lookup_getResult(
        const CodecCache_Lookup* lookup)
{
    ZL_ASSERT_NN(lookup);
    return lookup->result;
}

static bool CodecCache_storedSize(
        const CodecCache_Key* key,
        const CodecCache_Result* result,
        size_t* storedSize)
{
    size_t size = 0;
    if (!CodecCache_addSize(&size, key->localParamsSize)
        || !CodecCache_addSize(&size, sizeof(key->input))
        || !CodecCache_addArraySize(
                &size, result->nbOutputs, sizeof(result->outputs[0]))
        || !CodecCache_addSize(&size, result->headerSize)
        || !CodecCache_addSize(&size, key->input.contentSize)
        || !CodecCache_addArraySize(
                &size, key->input.nbIntMetadata, sizeof(Stream_IntMetadata))) {
        return false;
    }
    for (size_t i = 0; i < result->nbOutputs; ++i) {
        if (!CodecCache_addSize(&size, result->outputs[i].contentSize)
            || !CodecCache_addArraySize(
                    &size,
                    result->outputs[i].nbIntMetadata,
                    sizeof(Stream_IntMetadata))) {
            return false;
        }
    }
    *storedSize = size;
    return true;
}

static void*
CodecCache_copyBytes(Arena* cacheArena, const void* src, size_t size)
{
    if (size == 0) {
        return NULL;
    }
    ZL_ASSERT_NN(src);
    void* const dst = ALLOC_Arena_malloc(cacheArena, size);
    if (dst != NULL) {
        memcpy(dst, src, size);
    }
    return dst;
}

static void CodecCache_freeCopy(Arena* cacheArena, const void* copy)
{
    /* Stored descriptors expose their cache-owned buffers as read-only. */
    void* allocation;
    memcpy(&allocation, &copy, sizeof(allocation));
    ALLOC_Arena_free(cacheArena, allocation);
}

static void CodecCache_freeInput(
        Arena* cacheArena,
        const CodecCache_Input* input)
{
    CodecCache_freeCopy(cacheArena, input->content);
    CodecCache_freeCopy(cacheArena, input->intMetadata);
}

static bool CodecCache_copyInput(
        Arena* cacheArena,
        const CodecCache_Input* src,
        CodecCache_Input* dst)
{
    *dst             = *src;
    dst->content     = NULL;
    dst->intMetadata = NULL;
    dst->content =
            CodecCache_copyBytes(cacheArena, src->content, src->contentSize);
    if (src->contentSize != 0 && dst->content == NULL) {
        return false;
    }
    size_t metadataSize;
    if (ZL_overflowMulST(
                src->nbIntMetadata,
                sizeof(Stream_IntMetadata),
                &metadataSize)) {
        return false;
    }
    dst->intMetadata =
            CodecCache_copyBytes(cacheArena, src->intMetadata, metadataSize);
    if (metadataSize != 0 && dst->intMetadata == NULL) {
        return false;
    }
    return true;
}

static void CodecCache_freeOutputs(
        Arena* cacheArena,
        const CodecCache_Output* outputs,
        size_t nbOutputs)
{
    if (outputs == NULL) {
        return;
    }
    for (size_t i = 0; i < nbOutputs; ++i) {
        CodecCache_freeCopy(cacheArena, outputs[i].content);
        CodecCache_freeCopy(cacheArena, outputs[i].intMetadata);
    }
    CodecCache_freeCopy(cacheArena, outputs);
}

static bool CodecCache_copyOutputs(
        Arena* cacheArena,
        const CodecCache_Result* src,
        CodecCache_Result* dst)
{
    if (src->nbOutputs == 0) {
        dst->outputs = NULL;
        return true;
    }
    size_t outputsSize;
    if (ZL_overflowMulST(
                src->nbOutputs, sizeof(src->outputs[0]), &outputsSize)) {
        return false;
    }
    CodecCache_Output* const outputs =
            ALLOC_Arena_malloc(cacheArena, outputsSize);
    if (outputs == NULL) {
        return false;
    }
    for (size_t i = 0; i < src->nbOutputs; ++i) {
        outputs[i]             = src->outputs[i];
        outputs[i].content     = NULL;
        outputs[i].intMetadata = NULL;
        outputs[i].content     = CodecCache_copyBytes(
                cacheArena,
                src->outputs[i].content,
                src->outputs[i].contentSize);
        if (src->outputs[i].contentSize != 0 && outputs[i].content == NULL) {
            CodecCache_freeOutputs(cacheArena, outputs, i + 1);
            return false;
        }
        size_t metadataSize;
        if (ZL_overflowMulST(
                    src->outputs[i].nbIntMetadata,
                    sizeof(Stream_IntMetadata),
                    &metadataSize)) {
            CodecCache_freeOutputs(cacheArena, outputs, i + 1);
            return false;
        }
        outputs[i].intMetadata = CodecCache_copyBytes(
                cacheArena, src->outputs[i].intMetadata, metadataSize);
        if (metadataSize != 0 && outputs[i].intMetadata == NULL) {
            CodecCache_freeOutputs(cacheArena, outputs, i + 1);
            return false;
        }
        const CodecCache_Input outputAsInput = {
            .type          = outputs[i].type,
            .eltWidth      = outputs[i].eltWidth,
            .numElts       = outputs[i].numElts,
            .contentSize   = outputs[i].contentSize,
            .content       = outputs[i].content,
            .nbIntMetadata = outputs[i].nbIntMetadata,
            .intMetadata   = outputs[i].intMetadata,
        };
        outputs[i].keyHash64 = CodecCache_hashInput(&outputAsInput);
    }
    dst->outputs = outputs;
    return true;
}

static void CodecCache_freeStoredEntry(
        Arena* cacheArena,
        CodecCache_Key* key,
        CodecCache_Result* result)
{
    CodecCache_freeCopy(cacheArena, key->localParams);
    CodecCache_freeInput(cacheArena, &key->input);
    if (result != NULL) {
        CodecCache_freeOutputs(cacheArena, result->outputs, result->nbOutputs);
        CodecCache_freeCopy(cacheArena, result->header);
        ALLOC_Arena_free(cacheArena, result);
    }
}

CodecCache_InsertResult CodecCache_store(
        const CodecCache_Lookup* lookup,
        const CodecCache_Result* result)
{
    ZL_ASSERT_NN(lookup);
    ZL_ASSERT_NN(result);
    ZL_CodecOutputCache* const cache = lookup->cache;
    const CodecCache_Key* const key  = &lookup->key;

    if (!cache->insertionsEnabled) {
        return CodecCache_InsertResult_notCacheable;
    }

    for (size_t i = 0; i < result->nbOutputs; ++i) {
        if (result->outputs[i].type == ZL_Type_string) {
            CodecCache_recordSkip(cache, CodecCache_SkipReason_string);
            return CodecCache_InsertResult_notCacheable;
        }
    }

    if (CodecCache_Map_find(&cache->map, key) != NULL) {
        if (cache->statsEnabled) {
            ++cache->stats.duplicateInserts;
        }
        return CodecCache_InsertResult_duplicate;
    }

    size_t storedSize;
    size_t newBytesStored;
    if (!CodecCache_storedSize(key, result, &storedSize)
        || ZL_overflowAddST(cache->bytesStored, storedSize, &newBytesStored)
        || newBytesStored > cache->maxBytes) {
        if (cache->statsEnabled) {
            ++cache->stats.budgetSkips;
        }
        return CodecCache_InsertResult_budgetExceeded;
    }

    CodecCache_Key storedKey        = *key;
    storedKey.localParams           = NULL;
    storedKey.input.content         = NULL;
    storedKey.input.intMetadata     = NULL;
    CodecCache_Result* storedResult = NULL;

    storedKey.localParams = CodecCache_copyBytes(
            cache->cacheArena, key->localParams, key->localParamsSize);
    if ((key->localParamsSize != 0 && storedKey.localParams == NULL)
        || !CodecCache_copyInput(
                cache->cacheArena, &key->input, &storedKey.input)) {
        goto allocationFailure;
    }

    storedResult = ALLOC_Arena_malloc(cache->cacheArena, sizeof(*storedResult));
    if (storedResult == NULL) {
        goto allocationFailure;
    }
    *storedResult         = *result;
    storedResult->outputs = NULL;
    storedResult->header  = NULL;
    if (!CodecCache_copyOutputs(cache->cacheArena, result, storedResult)) {
        goto allocationFailure;
    }
    storedResult->header = CodecCache_copyBytes(
            cache->cacheArena, result->header, result->headerSize);
    if (result->headerSize != 0 && storedResult->header == NULL) {
        goto allocationFailure;
    }

    const CodecCache_Map_Entry mapEntry = {
        .key = storedKey,
        .val = storedResult,
    };
    const CodecCache_Map_Insert inserted =
            CodecCache_Map_insert(&cache->map, &mapEntry);
    if (inserted.badAlloc) {
        goto allocationFailure;
    }
    if (!inserted.inserted) {
        CodecCache_freeStoredEntry(cache->cacheArena, &storedKey, storedResult);
        if (cache->statsEnabled) {
            ++cache->stats.duplicateInserts;
        }
        return CodecCache_InsertResult_duplicate;
    }
    cache->bytesStored = newBytesStored;
    if (cache->statsEnabled) {
        ++cache->stats.inserts;
    }
    return CodecCache_InsertResult_inserted;

allocationFailure:
    CodecCache_freeStoredEntry(cache->cacheArena, &storedKey, storedResult);
    if (cache->statsEnabled) {
        ++cache->stats.allocationFailures;
    }
    return CodecCache_InsertResult_allocationFailure;
}
