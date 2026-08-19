// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_LZ_H
#define OPENZL_CODECS_LZ_H

#include "openzl/zl_graphs.h"
#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// LZ compresses a serial byte stream using LZ77 matching,
// decomposing it into four output streams.
//
// Input: A serial byte stream.
// Output 1: Literals: A serial stream of unmatched bytes.
// Output 2: Offsets: A numeric stream of match distances.
// Output 3: Literal Lengths: A numeric stream of literal run lengths.
// Output 4: Match Lengths: A numeric stream of match lengths.
#define ZL_NODE_LZ ZL_MAKE_NODE_ID(ZL_StandardNodeID_lz)

/// Standard function graph for LZ compression.
/// Runs ZL_NODE_LZ with presets to offer performance similar to Zstd.
/// In the future, it will be parameterizable to select different tradeoffs
/// (like LZ4) and control the parameters like compression level.
#define ZL_GRAPH_LZ ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_lz)

/// The LZ encoder sets the metadata on this ID to the minimum expected
/// match length emitted by the encoder. There may be edge cases where
/// the match length is shorter, but it will be extremely rare. The
/// current cases are:
/// - The literal length is >= 2^16, and a match length of 0 is emitted.
/// - The match length is >= 2^16 and (ml % 2^16 < min_match_length)
#define ZL_LZ_MIN_MATCH_LENGTH_METADATA_ID 77

typedef enum {
    ZL_LzStrategy_fast = 1,
    ZL_LzStrategy_doubleFast,
} ZL_LzStrategy;

/**
 * Parameters that control the behavior of ZL_NODE_LZ and ZL_GRAPH_LZ.
 *
 * Parameters will be clamped to their min & max values where relevant (see
 * below).
 */
typedef enum {
    /**
     * If set, it overrides the ZL_GCParam_compressionLevel, otherwise
     * it defaults to the global compression level.
     *
     * The compression level controls the default parameters and successor
     * graphs. If compression level has no effect on parameters or successor
     * graphs that are explicitly overridden.
     *
     * @note 0 means use the default value.
     */
    ZL_LzParam_compressionLevel = 1,

    /**
     * The acceleration parameter controls how quickly the match finder
     * skips over the input. Only one in every `acceleration` bytes of
     * the input is checked for matches. The default value is derived
     * from the compression level. If the compression level is >= 0,
     * then it defaults to 1. Otherwise it defaults to -compression_level.
     *
     * @note 0 means use the default value of 1.
     */
    ZL_LzParam_acceleration = 100,

    /**
     * The log2 of the maximum lookback window in LZ match finding.
     * windowSize = 1u << windowLog.
     *
     * The default value is set depending on the compression level and source
     * size, and is capped at the source size.
     *
     * @note 0 means use the default value.
     *
     * @note If the window size is <= 64K, then LZ will use 16-bit offsets
     * rather than 32-bit offsets.
     */
    ZL_LzParam_windowLog = 101,

    /**
     * The ZL_LzStrategy to use for compression.
     *
     * @note 0 means use the default value.
     */
    ZL_LzParam_strategy = 102,

    /**
     * The log2 of the primary hash table in LZ match finding. Higher values use
     * more memory, but potentially improve compression.
     *
     * @note 0 means use the default value.
     */
    ZL_LzParam_hashLog1 = 103,

    /**
     * The log2 of the secondary hash table in LZ match finding. Higher values
     * use more memory, but potentially improve compression.
     *
     * @note 0 means use the default value.
     * @note This will be ignored if the strategy doesn't use the secondary
     * table.
     */
    ZL_LzParam_hashLog2 = 104,

    /**
     * The number of bytes to hash in match finding hash tables. This is
     * effectively the minimum match length that the match finder searches for
     * (though it may find shorter matches by happenstance).
     *
     * @note 0 means use the default value.
     */
    ZL_LzParam_hashLength = 105,

    /**
     * If set, the customGraph at this index is used to compress the literals,
     * rather than sending them to the default graph.
     */
    ZL_LzParam_literalsGraphIdx = 1000,

    /**
     * If set, the customGraph at this index is used to compress the offsets,
     * rather than sending them to the default graph.
     */
    ZL_LzParam_offsetsGraphIdx = 1001,

    /**
     * If set, the customGraph at this index is used to compress the muxed
     * bytes (first output of ZL_NODE_MUX_LENGTHS), rather than sending them to
     * the default graph.
     *
     * @note Not used if ZL_LzParam_muxLengthsGraphIdx is set.
     */
    ZL_LzParam_muxedBytesGraphIdx = 1002,

    /**
     * If set, the customGraph at this index is used to compress the overflow
     * lengths (second output of ZL_NODE_MUX_LENGTHS), rather than sending them
     * to the default graph.
     *
     * @note Not used if ZL_LzParam_muxLengthsGraphIdx is set.
     */
    ZL_LzParam_overflowLengthsGraphIdx = 1003,

    /**
     * If set, the custom graph at this index is used to compress the literal
     * lengths and match lengths, rather than sending them to
     * ZL_NODE_MUX_LENGTHS.
     *
     * @note Must be a multi-input graph that accepts two 16-bit numeric inputs.
     */
    ZL_LzParam_muxLengthsGraphIdx = 1004,
} ZL_LzParam;

#define ZL_LZPARAM_WINDOWLOG_MIN 10
#define ZL_LZPARAM_WINDOWLOG_MAX 28

#define ZL_LZPARAM_ACCELERATION_MIN 1
#define ZL_LZPARAM_ACCELERATION_MAX 10000

#define ZL_LZPARAM_HASHLOG1_MIN 10
#define ZL_LZPARAM_HASHLOG1_MAX 23

#define ZL_LZPARAM_HASHLOG2_MIN 10
#define ZL_LZPARAM_HASHLOG2_MAX 23

#define ZL_LZPARAM_STRATEGY_MIN ((int)ZL_LzStrategy_fast)
#define ZL_LZPARAM_STRATEGY_MAX ((int)ZL_LzStrategy_doubleFast)

#define ZL_LZPARAM_HASHLENGTH_MIN 4
#define ZL_LZPARAM_HASHLENGTH_MAX 7

#define ZL_LZPARAM_COMPRESSIONLEVEL_MIN (-ZL_LZPARAM_ACCELERATION_MAX)
#define ZL_LZPARAM_COMPRESSIONLEVEL_MAX 4

#if defined(__cplusplus)
}
#endif

#endif
