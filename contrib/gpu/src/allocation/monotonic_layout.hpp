// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "contrib/gpu/src/allocation/allocation_layout.hpp"
#include "contrib/gpu/src/common/result.hpp"

namespace openzl::gpu {

/**
 * Places each requirement after the previous requirement of its arena.
 *
 * Each final output gets one destination arena for its data and, if it has
 * string lengths, one for its lengths, as in the core output layout. A region
 * with no bytes has no requirement, so an empty output gets no arena. All
 * workspace regions share one arena, and each starts at a 256-byte boundary at
 * least, like a @c cudaMalloc buffer. The layout does not reuse storage, even
 * when two lifetimes do not
 * overlap, as in core's @c StackArena and
 * @c std::pmr::monotonic_buffer_resource.
 *
 * Core frees its stream arena after each chunk. This workspace holds the
 * intermediate streams of every chunk at the same time, so it can be much
 * larger than core's peak memory.
 *
 * @param requirements Requirements from @c buildDecodeStorageRequirements.
 * This function trusts their alignments and destination order.
 * @returns The layout; @c logicError if a destination chunk size is not a
 * multiple of its alignment, which only a planner bug can cause;
 * @c integerOverflow if an arena size does not fit in @c size_t; or
 * @c allocation if an allocation fails.
 */
Result<AllocationLayout> buildMonotonicAllocationLayout(
        const ExecutionRequirements& requirements);

} // namespace openzl::gpu
