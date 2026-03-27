// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_SPLIT_H
#define ZSTRONG_CODECS_SPLIT_H

#include <stddef.h>

#include "openzl/zl_data.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_nodes.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

// split_byrange
// Input: 1 numeric stream (all widths: 1, 2, 4, 8 bytes)
// Output: variable number of numeric streams, one per detected range segment.
// Automatically detects boundaries where values belong to non-overlapping
// ranges, and splits the input at those boundaries.
// Built on top of splitN: shares the same wire format and decoder.
//
// Limitation: each segment must have at least minSegmentSize elements
// (default 16) for the boundary to be detected. Ranges that alternate
// at finer granularity (e.g. element-level [low, high, low, high])
// will not be split.
//
// Optional parameter:
//   ZL_SPLIT_BYRANGE_MIN_SEGMENT_SIZE_PID (int):
//     Minimum number of elements per segment. A split is rejected if either
//     side would have fewer elements. Higher values reduce false positives
//     from noise; lower values allow detecting shorter segments.
//     Default is width-dependent: larger for narrow types (u8).
#define ZL_NODE_SPLIT_BYRANGE ZL_MAKE_NODE_ID(ZL_StandardNodeID_split_byrange)

/// Optional int param: minimum segment size for split_byrange.
/// If unset, uses a width-dependent default (larger for narrow types).
#define ZL_SPLIT_BYRANGE_MIN_SEGMENT_SIZE_PID 324

// SplitN
// Input: 1 stream, of type either Serial, Numeric of Struct.
// Outputs : a variable nb of output streams, of same type as input,
//           each referencing a portion of the input content, in order.
//           Sizes are provided by ZL_SPLITN_SEGMENTSIZES_PID parameter,
//           which is an array of size_t.
//           (special: if last value == `0`, it means "whatever left")
// Each output receives a counter ID, for optional coordination with downstream
// node(s), which can be consulted via Int Stream Metadata
// ZL_SPLIT_CHANNEL_ID.
//
// Example:
// spliting into 3 segments, the last one being of variable size :
// size_t segmentSizes[3] = { 20, 50, 0 };
// ZL_CopyParam ssp = {
//         .paramId = ZL_SPLITN_SEGMENTSIZES_PID,
//         .paramPtr = segmentSizes,
//         .paramSize = sizeof(segmentSizes)
//     };
// ZL_LocalCopyParams lcp = { &ssp, 1 };
// ZL_LocalParams lParams = { .copyParams = lcp };
// ZL_NodeID nid = ZL_Compressor_registerParameterizedNode(cgraph,
//     &(const ZL_ParameterizedNodeDesc){ .node = ZS2_NODE_SPLITN, .localParams
//     = &lParams });
//
// The whole declaration logic is abstracted behind a public function
// ZL_Compressor_registerSplitNode_withParams(), which achieves the same thing
// as above.
//
#define ZL_SPLIT_CHANNEL_ID 867
ZL_NodeID ZL_Compressor_registerSplitNode_withParams(
        ZL_Compressor* cgraph,
        ZL_Type type,
        const size_t* segmentSizes,
        size_t nbSegments);

/**
 * Run the SplitN node within the context of a dynamic graph,
 * applying runtime-defined @p segmentSizes parameters.
 *
 * @returns the list of Streams created by the Transform
 * or an error if the splitting process fails (invalid segment sizes or input
 * type for example)
 */
ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runSplitNode(
        ZL_Edge* input,
        const size_t* segmentSizes,
        size_t nbSegments);

// SplitN-byExtParser
// Same as SplitN,
// but Split decisions (nb of segments, and size of each segment)
// are decided by an external parsing function
// following the ZL_SplitParserFn() definition,
// provided to the node using generic parameter ZL_SPLITN_PARSINGF_PID .
//
// The external parser returns a ZL_SplitInstructions structure,
// which references an array of size_t @.segmentSizes.
// The array could use a global static memory segment,
// but in such a case, the array's lifetime must outlive CGraph.
// The more recommended pattern is to allocate the array
// using *exclusively* the provided allocator function ZL_SplitState_malloc().
//
// ZL_SplitState_malloc() can also be used to allocate any workspace if needed.
// Any workspace buffer allocated within ZL_SplitState_malloc()
// will be automatically freed by Zstrong after execution of the Node.
// The external parser should not employ its own allocation methods.
//
// The external parser is allowed to fail,
// in which case it shall return { NULL, 0 }.
//
// As the declaration logic can be a bit complex, it's abstracted behind
// public function ZL_Compressor_registerSplitNode_withParser().
//
typedef struct {
    const size_t* segmentSizes;
    size_t nbSegments;
} ZL_SplitInstructions;

typedef struct ZL_SplitState_s ZL_SplitState;
void* ZL_SplitState_malloc(ZL_SplitState* state, size_t size);

/**
 * Provides an opaque pointer that can be useful to provide state to the parser.
 * For example, it can be used by language bindings to allow parsers written in
 * languages other than C.
 *
 * @returns The opaque pointer provided to @fn
 * ZL_Compressor_registerSplitNode_withParser(). WARNING: ZStrong does not
 * manage the lifetime of this pointer, it must outlive the ZL_Compressor.
 */
void const* ZL_SplitState_getOpaquePtr(ZL_SplitState* state);

typedef ZL_SplitInstructions (
        *ZL_SplitParserFn)(ZL_SplitState* state, const ZL_Input* in);

ZL_NodeID ZL_Compressor_registerSplitNode_withParser(
        ZL_Compressor* cgraph,
        ZL_Type type,
        ZL_SplitParserFn f,
        void const* opaque);

/** Split-by-param
 * This operation splits a serialized input
 * into segments, defined by array @segmentSizes[].
 * The nb of segments and their size is static,
 * except for the last segment size, which can receive a size value `0`,
 * meaning "whatever is left in the stream".
 * Each segment is then into its own output,
 * and then sent to the next processing stage defined by @successors[].
 */
ZL_GraphID ZL_Compressor_registerSplitGraph(
        ZL_Compressor* cgraph,
        ZL_Type type,
        const size_t segmentSizes[],
        const ZL_GraphID successors[],
        size_t nbSegments);

#if defined(__cplusplus)
}
#endif

#endif
