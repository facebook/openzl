// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_DISPATCH_H
#define ZSTRONG_CODECS_DISPATCH_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/zl_graph_api.h"
#include "openzl/zl_nodes.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_stream.h"

#if defined(__cplusplus)
extern "C" {
#endif

// dispatchN_byTag
// Split serialized input into variable-size segments and tag them.
// Segments with same tag are then concatenated into the same outputs.
//
// Splitting instructions (sizes and tags) are provided by an External Parser
// (necessarily).
//
// The External Parser is provided to the transform as a Generic Parameter,
// using ID ZL_DISPATCH_PARSINGFN_PID.
// The declaration logic is abstracted behind
// ZL_Compressor_registerDispatchNode().
//
// The external parser returns a ZL_DispatchInstructions structure,
// which references an array of size_t @.segmentSizes.,
// and an array of unsigned @.tags.
// Both arrays must have the same size == @.nbSegments.
// The values in @.tags must all be < @.nbTags
// Tags values are supposed to form a clean [0-N] range, all values used.
// The external parser is allowed to fail,
// in which case it returns @.segmentSizes == NULL.
//
// These arrays could employ some global static memory segment,
// but note that, in this case, arrays' lifetime must outlive CGraph.
// A more recommended pattern is to allocate the arrays
// using *exclusively* the provided allocator function
// ZL_DispatchState_malloc().
//
// ZL_DispatchState_malloc() can also be used to allocate any workspace if
// needed. Any workspace buffer allocated with ZL_DispatchState_malloc() will be
// automatically freed by Zstrong after execution of the Node. The external
// parser should not employ its own allocation methods.
//
// The transform will create @.nbTags serialized outputs
// and 2 singleton numeric outputs, one for tags, and one for segment sizes.
// At Graph time, 3 outcomes must be defined, one for tags, one for segment
// sizes, and one for concatenated outputs (instantiated once per concat.
// output).
//
// Each concatenate output will also carry a metadata
// ZL_DISPATCH_CHANNEL_ID, which can be used for coordination with
// downstream node if needed.
//
#define ZL_NODE_DISPATCH ZL_MAKE_NODE_ID(ZL_StandardNodeID_dispatchN_byTag)

typedef struct {
    const size_t* segmentSizes;
    const unsigned* tags;
    size_t nbSegments;
    unsigned nbTags;
} ZL_DispatchInstructions;

typedef struct ZL_DispatchState_s ZL_DispatchState;

void* ZL_DispatchState_malloc(ZL_DispatchState* state, size_t size);

/**
 * Provides an opaque pointer that can be useful to provide state to the parser.
 * For example, it can be used by language bindings to allow parsers written in
 * languages other than C.
 *
 * @returns The opaque pointer provided to @fn
 * ZL_Compressor_registerDispatchNode(). WARNING: ZStrong does not manage the
 * lifetime of this pointer, it must outlive the ZL_Compressor.
 */
void const* ZL_DispatchState_getOpaquePtr(ZL_DispatchState const* state);

/**
 * @returns An error from the parser function and places the @p message into
 * Zstrong's error context.
 */
ZL_NODISCARD ZL_DispatchInstructions
ZL_DispatchState_returnError(ZL_DispatchState* state, char const* message);

typedef ZL_DispatchInstructions (
        *ZL_DispatchParserFn)(ZL_DispatchState* state, const ZL_Input* in);

#define ZL_DISPATCH_PARSINGFN_PID 519

ZL_NodeID ZL_Compressor_registerDispatchNode(
        ZL_Compressor* cgraph,
        ZL_DispatchParserFn f,
        void const* opaque);

#define ZL_DISPATCH_INSTRUCTIONS_PID 520

/**
 * Run the DispatchN node in the context of a dynamic graph,
 * following runtime-defined  @p instructions.
 *
 * @returns The list of Streams produced by the Transform,
 * or an error if the operation fails (invalid instruction or input for example)
 */
ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runDispatchNode(
        ZL_Edge* sctx,
        const ZL_DispatchInstructions* instructions);

/// Each concatenated output will also carry a metadata
/// ZL_DISPATCH_CHANNEL_ID, which can optionally be used for coordination
/// with downstream node whenever needed.
#define ZL_DISPATCH_CHANNEL_ID 83

// Dispatch string
// Input: 1 string stream
// Input params: 1 unsigned int <= 256, 1 u8 array
// Output: 1 u8 numeric stream, variable number of string streams
// Outcome: the input stream is dispatched into a number of separate string
// streams.
//          The number of resultant streams is determined by the int param, and
//          the exact dispatch order is determined by the u8 array param. This
//          array is also output as a numeric stream by the transform.
// Refer to the README in transforms/dispatch_string for more details.
#define ZL_DISPATCH_STRING_NUM_OUTPUTS_PID 47
#define ZL_DISPATCH_STRING_INDICES_PID 48

#define ZL_NODE_DISPATCH_STRING \
    ZL_MAKE_NODE_ID(ZL_StandardNodeID_dispatch_string)

/**
 * Convenience function to get the maximum number of dispatches supported by the
 * current encoder version.
 */
size_t ZL_DispatchString_maxDispatches(void);

/**
 * @param nbOutputs - the number of output streams to be generated. Passed as a
 * local param to the transform.
 * @param dispatchIndices - the array of indices to be used for dispatching.
 * Will be passed as a local param to the transform. The lifetime of the array
 * is to be managed by the caller and should outlive the transform execution.
 */
ZL_NodeID ZL_Compressor_registerDispatchStringNode(
        ZL_Compressor* cgraph,
        int nbOutputsParam,
        const uint16_t* dispatchIndicesParam);
/**
 * Run the ZL_NODE_DISPATCH_STRING Node in the context of a Dynamic Graph,
 * applying runtime defined parameters.
 */
ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runDispatchStringNode(
        ZL_Edge* sctx,
        int nbOutputs,
        const uint16_t* indices);

#if defined(__cplusplus)
}
#endif

#endif
