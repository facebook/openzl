// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_ZL_CODEC_OUTPUT_CACHE_H
#define OPENZL_ZL_CODEC_OUTPUT_CACHE_H

#include <stddef.h>

#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Memoizes deterministic standard-codec invocations.
 *
 * A cache is disabled until attached to a compression context with
 * ZL_CCtx_setCodecOutputCache(). Cache hits reproduce the same codec outputs
 * and transform header without invoking the codec again. Custom codecs,
 * dictionary-backed codecs, codecs with reference or materialized parameters,
 * codecs consuming or producing string streams, and codecs that do not take
 * exactly one input are not cached. Non-single-input invocations run normally
 * without a cache lookup or insertion.
 *
 * Copy parameters are keyed by their copied bytes only. State reachable
 * through pointers embedded in those bytes is not part of the key.
 * Callback-backed parameterized standard nodes, including custom tokenize and
 * dispatch nodes, are therefore unsupported.
 *
 * The cache is mutable and single-writer. It may be reused by multiple
 * compression contexts sequentially, but must not be used concurrently.
 */
typedef struct ZL_CodecOutputCache_s ZL_CodecOutputCache;

/** Creates an empty cache with the default 256 MiB entry-payload budget. */
ZL_CodecOutputCache* ZL_CodecOutputCache_create(void);

/**
 * Creates an empty cache with the specified entry-payload budget.
 * Zero creates a live cache that cannot store entries; cacheable invocations
 * still perform hashing and lookups when it is attached. Reaching @p maxBytes
 * skips new entries; it does not fail compression.
 */
ZL_CodecOutputCache* ZL_CodecOutputCache_createWithBudget(size_t maxBytes);

/** Frees a cache and all stored entries. Accepts NULL. */
void ZL_CodecOutputCache_free(ZL_CodecOutputCache* cache);

/** Drops all cached results. Accepts NULL. */
void ZL_CodecOutputCache_reset(ZL_CodecOutputCache* cache);

/**
 * Sets the entry-payload budget for the private cache used automatically by
 * tryGraph. Automatic caching is disabled by default. A positive value enables
 * it with the specified budget; zero disables it.
 *
 * Changing the budget drops the existing private cache. Call this function
 * only between compressions. The setting persists until changed or the
 * context is freed; ZL_CParam_stickyParameters and ZL_CCtx_resetParameters()
 * do not affect it. A caller-attached cache is unaffected and remains active.
 */
ZL_Report ZL_CCtx_setTryGraphCacheBudget(ZL_CCtx* cctx, size_t maxBytes);

/**
 * Attaches a borrowed cache to @p cctx. Passing NULL detaches it; automatic
 * tryGraph caching remains controlled by ZL_CCtx_setTryGraphCacheBudget(). The
 * cache must outlive every compression that uses the context.
 */
ZL_Report ZL_CCtx_setCodecOutputCache(
        ZL_CCtx* cctx,
        ZL_CodecOutputCache* cache);

#if defined(__cplusplus)
}
#endif

#endif // OPENZL_ZL_CODEC_OUTPUT_CACHE_H
