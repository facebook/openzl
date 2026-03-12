// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_SEGMENTER_H
#define ZSTRONG_COMPRESS_SEGMENTER_H

#include "openzl/common/allocation.h" // Arena
#include "openzl/compress/rtgraphs.h"
#include "openzl/zl_segmenter.h" // ZL_SegmenterDesc

/**
 * Internal Segmenter Implementation
 *
 * This module provides the internal implementation for segmenter initialization
 * and execution. Segmenters are responsible for chunking input data and
 * forwarding chunks to appropriate processing graphs.
 *
 * Typical usage pattern:
 * 1. Initialize segmenter with SEGM_init()
 * 2. Execute segmentation with SEGM_runSegmenter()
 * 3. Memory cleanup is automatic via arena deallocation
 */

/**
 * @brief Initialize a segmenter instance with the provided configuration.
 *
 * Creates and configures a segmenter context based on the provided descriptor.
 * The segmenter will be ready to process the specified number of inputs using
 * the configured chunking strategy.
 *
 * Memory management: The returned ZL_Segmenter* is allocated on the provided
 * arena and does not need manual deallocation. It will be automatically freed
 * when the arena is deallocated or reset.
 *
 * @param segDesc Segmenter descriptor containing configuration (function, input
 * types, etc.)
 * @param numInputs Number of input streams this segmenter will process
 * @param cctx Compression context providing access to global parameters and
 * state
 * @param rtgm Runtime graph manager, must be reset and initialized for each
 * chunk
 * @param sessionArena Main arena allocator for segmenter context allocation
 * @param chunkArena Dedicated arena for chunk-lifetime allocations during
 * processing
 * @return Initialized segmenter context, or NULL on initialization failure
 *
 * @note The segmenter context remains valid until the arena is deallocated
 * @note Both arena and chunkArena must remain valid for the segmenter's
 * lifetime
 */
ZL_Segmenter* SEGM_init(
        const ZL_SegmenterDesc* segDesc,
        size_t numInputs,
        ZL_CCtx* cctx,
        RTGraph* rtgm,
        Arena* sessionArena,
        Arena* chunkArena);

/**
 * @brief Execute the segmenter function to process all input data.
 *
 * Invokes the segmenter's chunking function to analyze input streams and
 * create chunks for processing. The segmenter will repeatedly call its
 * configured function until all input data has been consumed and processed.
 *
 * The segmenter function is responsible for:
 * - Analyzing input data to determine optimal chunk boundaries
 * - Calling ZL_Segmenter_processChunk() to forward chunks to graphs
 * - Ensuring all input data is consumed before completion
 *
 * This is a blocking operation that completes only when all input data
 * has been successfully segmented and processed.
 *
 * @param segmenter Segmenter context, previously created via SEGM_init()
 * @return ZL_Report indicating success or failure of the segmentation process
 *
 * @note The segmenter must consume all input data; partial consumption results
 * in error
 * @note This function may allocate temporary memory using the chunk arena
 */
ZL_Report SEGM_runSegmenter(ZL_Segmenter* segmenter);

#endif // ZSTRONG_COMPRESS_SEGMENTER_H
