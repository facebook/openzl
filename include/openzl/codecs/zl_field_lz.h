// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_FIELD_LZ_H
#define ZSTRONG_CODECS_FIELD_LZ_H

#include "openzl/zl_graphs.h"
#include "openzl/zl_nodes.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Field LZ compresses a fixed size field stream using LZ compression that only
// matches entire fields.
// NOTE: This node is for advanced users. You probably want to use either
// ZL_Compressor_registerFieldLZGraph() or
// ZS2_createGraph_fieldLzWithLiteralsGraph() to get a pre-built Field LZ graph.
//
// Input: A fixed size stream of width 1, 2, 4, or 8.
// Output 1: Literals: A fixed size stream of the same width as the input.
// Output 2: Tokens: A fixed size stream of width 2 with 10-bit values.
// Output 3: Offsets: A numeric stream of non-zero u32 offsets.
// Output 4: Extra Literal Lengths: A numeric stream of u32 lengths.
// Output 5: Extra Match Lengths: A numeric stream of u32 lengths.
#define ZL_NODE_FIELD_LZ ZL_MAKE_NODE_ID(ZL_StandardNodeID_field_lz)

/**
 * Create the field lz graph with the default backends.
 * Field LZ compresses a fixed size field stream using LZ compression
 * that only matches entire fields.
 *
 * Input: A fixed size stream of width 1, 2, 4, or 8.
 */
#define ZL_GRAPH_FIELD_LZ ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_field_lz)

/// DEPRECATED: Use ZL_GRAPH_FIELD_LZ instead.
/// @returns ZL_GRAPH_FIELD_LZ
ZL_GraphID ZL_Compressor_registerFieldLZGraph(ZL_Compressor* cgraph);

/// @returns ZL_GRAPH_FIELD_LZ with overridden compression level
ZL_GraphID ZL_Compressor_registerFieldLZGraph_withLevel(
        ZL_Compressor* cgraph,
        int compressionLevel);

/**
 * Creates a Field LZ graph with a custom literals compressor.
 *
 * Field LZ compresses a fixed size field stream using LZ compression
 * that only matches entire fields.
 *
 * Input: A fixed size stream of width 1, 2, 4, or 8.
 *
 * @param literalsGraph a graph which takes a fixed size field of the
 * same width as the input. All fields that we can't find matches for
 * are passed to the literals stream.
 */
ZL_GraphID ZL_Compressor_registerFieldLZGraph_withLiteralsGraph(
        ZL_Compressor* cgraph,
        ZL_GraphID literalsGraph);

/// Set this integer parameter to override the compression level
#define ZL_FIELD_LZ_COMPRESSION_LEVEL_OVERRIDE_PID 181

/// Set this integer paramter to override the literals graph
/// It should be the index of the literals graph in the customGraphs list
#define ZL_FIELD_LZ_LITERALS_GRAPH_OVERRIDE_INDEX_PID 0

#define ZL_FIELD_LZ_TOKENS_GRAPH_OVERRIDE_INDEX_PID 1
#define ZL_FIELD_LZ_OFFSETS_GRAPH_OVERRIDE_INDEX_PID 2
#define ZL_FIELD_LZ_EXTRA_LITERAL_LENGTHS_GRAPH_OVERRIDE_INDEX_PID 3
#define ZL_FIELD_LZ_EXTRA_MATCH_LENGTHS_GRAPH_OVERRIDE_INDEX_PID 4

#if defined(__cplusplus)
}
#endif

#endif
