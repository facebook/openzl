// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMPRESS_CODEC_OUTPUT_CACHE_H
#define OPENZL_COMPRESS_CODEC_OUTPUT_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openzl/common/stream.h" // Stream_IntMetadata
#include "openzl/shared/portability.h"
#include "openzl/zl_codec_output_cache.h"
#include "openzl/zl_data.h"

ZL_BEGIN_C_DECLS

/**
 * Internal cache used by the compression engine to memoize complete codec
 * invocations.
 *
 * CodecCache_lookup() derives the complete identity of an invocation from the
 * encoder context, invoked node, and input stream. Callers cannot manufacture
 * partial cache keys or confuse an encoder-node identity with its wire-format
 * transform ID.
 *
 * CodecCache_store() deep-copies every variable-length result field, so a
 * stored result remains valid independently of the caller's buffers. The cache
 * owns those copies until CodecCache_reset() or CodecCache_free().
 *
 * A hash is used to locate candidates quickly, but a hit is accepted only
 * after exact comparison of every output-affecting key field. Hash collisions
 * therefore cannot turn one codec invocation into another invocation's result.
 * The opaque ZL_CodecOutputCache handle is declared by the public cache header.
 */
typedef struct CodecCache_Lookup_s CodecCache_Lookup;

/**
 * Exact description of one fixed-width output produced by a codec invocation.
 *
 * `ZL_Type_string` is not representable because `content` does not include the
 * stream's separate string-length array. String outputs must not be stored.
 */
typedef struct {
    /** Logical stream type, checked against the output port during replay. */
    ZL_Type type;
    /** Codec output port that produced the stream, including VO ports. */
    int outcomeIndex;
    /** Width in bytes of one logical output element. */
    size_t eltWidth;
    /** Number of logical elements produced. */
    size_t numElts;
    /** Number of bytes readable from `content`. */
    size_t contentSize;
    /**
     * Borrowed output bytes. Must be non-NULL when `contentSize` is nonzero;
     * may be NULL for an empty output. Insertion deep-copies these bytes.
     */
    const void* content;
    /** Number of elements in `intMetadata`. */
    size_t nbIntMetadata;
    /**
     * Borrowed output metadata in stream order. Must be non-NULL when
     * `nbIntMetadata` is nonzero; deep-copied on insertion.
     */
    const Stream_IntMetadata* intMetadata;
    /**
     * Cache-computed digest used when this output becomes a later codec input.
     * CodecCache_store() ignores the caller's value and fills the stored copy;
     * replay stamps it on the reconstructed stream to avoid rehashing.
     */
    uint64_t keyHash64;
} CodecCache_Output;

/** Complete result of one successful codec invocation. */
typedef struct {
    /** Number of output descriptors in `outputs`; zero is valid. */
    size_t nbOutputs;
    /** Borrowed ordered outputs; NULL only when `nbOutputs` is zero. */
    const CodecCache_Output* outputs;
    /** Number of bytes readable from `header`; zero means no header. */
    size_t headerSize;
    /**
     * Borrowed transform-header bytes, deep-copied on insertion. Must be
     * non-NULL when `headerSize` is nonzero and NULL otherwise.
     */
    const void* header;
} CodecCache_Result;

/** Cache activity and memory accounting since creation or the last reset. */
typedef struct CodecCache_Stats {
    /** Lookups that found an exact invocation match. */
    size_t hits;
    /** Cacheable lookups for which no exact invocation matched. */
    size_t misses;
    /** New results successfully stored. */
    size_t inserts;
    /** Insertions skipped because an exact key was already present. */
    size_t duplicateInserts;
    /** Invocations skipped because the transform is registered by the caller.
     */
    size_t customCodecSkips;
    /** Invocations skipped because they have a local reference parameter. */
    size_t refParamSkips;
    /** Invocations skipped because they reference a dictionary. */
    size_t dictSkips;
    /** Invocations skipped because they have a materialized parameter. */
    size_t mparamSkips;
    /** Invocations skipped because they consume or produce string streams. */
    size_t stringSkips;
    /** Invocations skipped because they do not consume exactly one input. */
    size_t nonSingleInputSkips;
    /** Insertions skipped because size accounting overflowed or hit the budget.
     */
    size_t budgetSkips;
    /** Insertions skipped after an allocation failure. */
    size_t allocationFailures;
    /**
     * Budgeted bytes owned by cached invocations. Includes copied descriptors,
     * contents, metadata, parameters, and headers, but excludes map and arena
     * overhead.
     */
    size_t bytesStored;
    /** Total bytes currently held by the cache arena, including overhead. */
    size_t arenaBytes;
} CodecCache_Stats;

/** Reason an otherwise encountered codec invocation was not cacheable. */
typedef enum {
    /** The transform is registered by the caller rather than OpenZL. */
    CodecCache_SkipReason_customCodec,
    /** A local reference parameter can affect output without entering the key.
     */
    CodecCache_SkipReason_refParam,
    /** A referenced dictionary can affect output without entering the key. */
    CodecCache_SkipReason_dict,
    /** A materialized parameter can affect output without entering the key. */
    CodecCache_SkipReason_mparam,
    /** String streams include length data not represented by this cache key. */
    CodecCache_SkipReason_string,
    /** The cache only represents single-input codec invocations. */
    CodecCache_SkipReason_nonSingleInput,
} CodecCache_SkipReason;

/** Outcome of CodecCache_store(). All non-stored outcomes are non-fatal. */
typedef enum {
    /** The completed invocation has an output the cache cannot represent. */
    CodecCache_InsertResult_notCacheable,
    /** A deep-copied entry was added. */
    CodecCache_InsertResult_inserted,
    /** The key already existed; the original entry was retained. */
    CodecCache_InsertResult_duplicate,
    /** The entry did not fit the configured budget or its size overflowed. */
    CodecCache_InsertResult_budgetExceeded,
    /** The entry could not be copied into cache-owned storage. */
    CodecCache_InsertResult_allocationFailure,
} CodecCache_InsertResult;

/**
 * Creates an empty cache with the exact @p maxBytes entry-payload budget.
 * Returns NULL on allocation failure.
 */
ZL_CodecOutputCache* CodecCache_create(size_t maxBytes);

/** Returns the default entry-payload budget. */
size_t CodecCache_getDefaultMaxBytes(void);

/** Frees the cache and all owned entries. Accepts NULL. */
void CodecCache_free(ZL_CodecOutputCache* cache);

/** Removes all entries and clears all counters. Accepts NULL. */
void CodecCache_reset(ZL_CodecOutputCache* cache);

/**
 * Removes all entries and current counters while retaining the most recently
 * captured completed-run statistics. Accepts NULL.
 */
void CodecCache_resetPreservingCompletedStats(ZL_CodecOutputCache* cache);

/** Enables or disables storing new results without affecting existing hits. */
void CodecCache_setInsertionsEnabled(ZL_CodecOutputCache* cache, bool enabled);

/**
 * Enables or disables statistics collection and clears all statistics.
 * Call only while the cache is not in use.
 */
void CodecCache_setStatsEnabled(ZL_CodecOutputCache* cache, bool enabled);

/**
 * Returns a snapshot of cache counters and memory use, or zeros when
 * statistics collection is disabled. @p cache is non-NULL.
 */
CodecCache_Stats CodecCache_getStats(const ZL_CodecOutputCache* cache);

/** Captures the current statistics as the most recently completed run. */
void CodecCache_captureCompletedStats(ZL_CodecOutputCache* cache);

/** Returns the most recently captured completed-run statistics. */
CodecCache_Stats CodecCache_getLastCompletedStats(
        const ZL_CodecOutputCache* cache);

/** Records that a stream's previously computed cache-key digest was reused. */
void CodecCache_recordHashReuse(ZL_CodecOutputCache* cache);

/** Returns hash reuses recorded since creation or the last reset. */
size_t CodecCache_getHashReuses(const ZL_CodecOutputCache* cache);

/** Increments the skip counter corresponding to @p reason. */
void CodecCache_recordSkip(
        ZL_CodecOutputCache* cache,
        CodecCache_SkipReason reason);

/**
 * Looks up one single-input codec invocation and records a hit or miss.
 *
 * @p node is the node the engine is about to invoke, not its wire transform
 * ID or a caller-derived base node. The cache resolves the stable built-in
 * encoder identity and all other output-affecting key fields internally.
 *
 * Returns NULL when the invocation is not cacheable, temporary key
 * construction fails, or insertion is disabled and no result matches.
 * Otherwise, the returned lookup is allocated in @p encoder's workspace and
 * remains valid for that encoder invocation.
 */
CodecCache_Lookup* CodecCache_lookup(
        ZL_CodecOutputCache* cache,
        ZL_Encoder* encoder,
        ZL_NodeID node,
        const ZL_Data* input);

/**
 * Returns the immutable cache-owned result found by @p lookup, or NULL for a
 * miss. A non-NULL result remains valid until the cache is reset or freed.
 */
const CodecCache_Result* CodecCache_Lookup_getResult(
        const CodecCache_Lookup* lookup);

/**
 * Stores a deep copy of @p result for the invocation represented by @p lookup
 * if it is new and fits the budget. The caller retains ownership of every
 * supplied buffer. Failure to cache is reported by the return value and must
 * not fail compression.
 */
CodecCache_InsertResult CodecCache_store(
        const CodecCache_Lookup* lookup,
        const CodecCache_Result* result);

ZL_END_C_DECLS

#endif // OPENZL_COMPRESS_CODEC_OUTPUT_CACHE_H
