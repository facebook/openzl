// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <span>

#include "contrib/gpu/src/planning/decode_plan.hpp"

namespace openzl::gpu {

/**
 * Host data needed to plan one prepared device chunk.
 *
 * The decode context contains the frame graph, stream producers, and stored
 * streams. @c transformHeaders_h contains the codec headers used during
 * planning. It must remain valid until planning returns. The decode context
 * and its frame storage must remain valid until the plan finishes executing.
 */
struct PreparedGpuChunkView {
    /// Non-null prepared context with frame, producer, and stream metadata.
    ZL_DCtx* dctx;
    /// Host-staged codec headers indexed by each node's offset and size.
    std::span<const std::byte> transformHeaders_h;
};

/**
 * Builds a decode plan without allocating device buffers.
 *
 * Each prepared context and its frame storage must remain valid until the plan
 * finishes executing because they own stored stream bytes. The plan does not
 * point into the staged transform headers or the caller's output buffers.
 * @p plan changes only after every chunk has been planned successfully.
 *
 * A final @c StreamArena stream becomes a @c Destination so its decoder writes
 * directly to the caller's output. A final @c Stored or @c StreamRef stream
 * keeps its class and must be copied to the caller's output.
 *
 * @param chunks Prepared chunks in batch order.
 * @param plan Destination replaced atomically on success and unchanged on
 * error.
 * @returns Success, or the first validation, allocation, or unsupported-codec
 * error encountered while planning the batch.
 */
ZL_Report planDecode(
        std::span<const PreparedGpuChunkView> chunks,
        DecodePlan& plan);

} // namespace openzl::gpu
