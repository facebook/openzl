// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_SEGMENTERS_H
#define ZSTRONG_CODECS_SEGMENTERS_H

#include "openzl/codecs/zl_illegal.h" // ZL_GRAPH_ILLEGAL
#include "openzl/zl_errors.h"         // ZL_RESULT_OF
#include "openzl/zl_graphs.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_segmenter.h"

#include <stddef.h> // size_t

#if defined(__cplusplus)
extern "C" {
#endif

/** Pass as successorGraph to use the built-in default pipeline
 *  (interpret serial as numeric + compress). */
#define ZL_SEGMENTER_DEFAULT_SUCCESSOR ZL_GRAPH_ILLEGAL

// Numeric segmenter (numeric input)
// Input : 1 stream of numeric data
// Result : chunks the numeric input using default chunk size
// and compresses each chunk independently with default
// ZL_GRAPH_NUMERIC_COMPRESS
#define ZL_SEGMENT_NUMERIC ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_segment_numeric)

// Serial-input numeric segmenters
// Input : 1 stream of serial data, interpreted as N-bit numeric elements
// Result : chunks the serial input using default chunk size,
// then converts each chunk to numeric of specified width
// and compresses using a default successor graph.
// Can be overridden via ZL_Compressor_buildNumFromSerialSegmenter.
#define ZL_SEGMENT_NUM8_FROM_SERIAL \
    ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_segment_num8_from_serial)
#define ZL_SEGMENT_NUM16_FROM_SERIAL \
    ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_segment_num16_from_serial)
#define ZL_SEGMENT_NUM32_FROM_SERIAL \
    ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_segment_num32_from_serial)
#define ZL_SEGMENT_NUM64_FROM_SERIAL \
    ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_segment_num64_from_serial)

/**
 * @brief Register a serial-numeric segmenter.
 *
 * Creates a parameterized segmenter that accepts serial input, chunks it
 * by @p chunkByteSize (aligned to @p eltByteWidth), and forwards each chunk
 * to @p successorGraph.
 *
 * @param compressor The compressor to register with
 * @param eltByteWidth Element width in bytes (1, 2, 4, or 8)
 * @param chunkByteSize Maximum chunk size in bytes (will be aligned down
 *        to element width). Pass 0 to use the built-in default
 *        (ZL_DEFAULT_SEGMENTER_CHUNK_BYTE_SIZE). Otherwise must be in
 *        [ZL_MIN_CHUNK_SIZE, INT_MAX]; smaller positive values are rejected
 *        because ZL_compressBound() assumes chunks of at least
 *        ZL_MIN_CHUNK_SIZE bytes.
 * @param successorGraph The graph to process each chunk, or
 *        ZL_SEGMENTER_DEFAULT_SUCCESSOR to use the built-in default
 *        interpret+compress pipeline
 * @return The registered segmenter graph ID, or ZL_GRAPH_ILLEGAL on error
 */
ZL_GraphID ZL_Compressor_buildNumFromSerialSegmenter(
        ZL_Compressor* compressor,
        size_t eltByteWidth,
        size_t chunkByteSize,
        ZL_GraphID successorGraph);

/**
 * Same as ZL_Compressor_buildNumFromSerialSegmenter(), but returns a
 * ZL_RESULT_OF(ZL_GraphID) for richer error reporting.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildNumFromSerialSegmenter2(
        ZL_Compressor* compressor,
        size_t eltByteWidth,
        size_t chunkByteSize,
        ZL_GraphID successorGraph);

#if defined(__cplusplus)
}
#endif

#endif // ZSTRONG_CODECS_SEGMENTERS_H
