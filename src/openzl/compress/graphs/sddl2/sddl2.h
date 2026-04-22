// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_GRAPHS_SDDL_V2_H
#define OPENZL_GRAPHS_SDDL_V2_H

#include "openzl/codecs/zl_sddl2.h"
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_segmenter.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SDDL2_REPLAY_SEGMENTS_PARAM 7687
#define SDDL2_REPLAY_START_STREAM_ID_PARAM 7688

/**
 * Private chunk-replay descriptor passed from `SDDL2_segment()` to the
 * `!zl.private.sddl2_chunk` helper graph.
 *
 * Replay operates on chunk-local byte slices that have already been cut out of
 * the original input. It only needs each slice size to re-split the chunk
 * input and the corresponding type metadata to replay conversion and routing.
 */
typedef struct {
    size_t size_bytes;
    SDDL2_Type type;
} SDDL2_ReplaySegment;

#define SEGM_SDDL2_DESC                                            \
    {                                                              \
        .name                = "!zl.sddl2",                        \
        .segmenterFn         = SDDL2_segment,                      \
        .inputTypeMasks      = &(const ZL_Type){ ZL_Type_serial }, \
        .numInputs           = 1,                                  \
        .lastInputIsVariable = false,                              \
    }

/**
 * Private SDDL2 chunk replay entrypoint backing `!zl.private.sddl2_chunk`.
 *
 * The graph expects a slice of precomputed `SDDL2_ReplaySegment` values
 * through `SDDL2_REPLAY_SEGMENTS_PARAM`, and may also receive an initial
 * clustering stream id through `SDDL2_REPLAY_START_STREAM_ID_PARAM`.
 */
ZL_Report SDDL2_replayChunk(ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
        ZL_NOEXCEPT_FUNC_PTR;

/**
 * Public standard SDDL2 segmenter entrypoint.
 *
 * This is the implementation behind the standard graph `!zl.sddl2`. It runs
 * the SDDL2 VM once over the whole input, determines chunk boundaries on the
 * emitted segment list, and dispatches chunk-local work to the private
 * `!zl.private.sddl2_chunk` helper graph.
 */
ZL_Report SDDL2_segment(ZL_Segmenter* sctx) ZL_NOEXCEPT_FUNC_PTR;

#if defined(__cplusplus)
}
#endif

#endif // OPENZL_GRAPHS_SDDL_V2_H
