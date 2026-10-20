// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <vector>

#include "contrib/gpu/src/common/result.hpp"
#include "contrib/gpu/src/planning/decode_plan.hpp"

namespace openzl::gpu {

/**
 * One batch of dependency-ready planned codec runs sharing a wire transform.
 *
 * A work group is the coarsest unit over which one codec can be invoked. Work
 * groups are never empty.
 */
struct WorkGroup {
    /// Wire transform shared by every referenced @c PlannedCodecRun.
    PublicTransformInfo transform;
    /// Indices of the @c DecodePlan::codecRuns batched into this work group.
    std::vector<PlannedCodecRunIndex> codecRunIndices;
};

/**
 * Work groups that may execute after every earlier wave has completed.
 *
 * The executor may launch every work group in a wave independently. It must
 * complete the entire wave before launching the next one. Waves are never
 * empty.
 */
struct WorkWave {
    /// Work groups that may execute independently within this wave.
    std::vector<WorkGroup> workGroups;
};

/**
 * Batched execution order derived from a decode plan's dependency graph.
 *
 * Each codec run's wave exceeds every producer codec run it depends on.
 * Aliasing codec runs, whose outputs are all backed by existing storage, are
 * the exception: they launch no work, appear in no wave, and add no dependency
 * depth.
 */
struct WorkSchedule {
    /// Topological execution buckets in dependency order.
    std::vector<WorkWave> waves;
    /// Aliasing codec runs, in decode-plan order.
    std::vector<PlannedCodecRunIndex> aliasingCodecRunIndices;
};

/**
 * Assigns topological waves and batches codec runs into work groups.
 *
 * Reads only each codec run's dependencies, transform, and output stream
 * storage facts. Input streams, codec headers, and other size information are
 * not consulted.
 *
 * @param plan Decode graph whose codec runs precede their dependents.
 * @returns The work schedule, @c corruption if a range or stream index in
 * @p plan is out of bounds or a dependency does not precede its dependent codec
 * run, or @c allocation if an allocation fails.
 */
Result<WorkSchedule> buildDecodeWorkSchedule(const DecodePlan& plan);

} // namespace openzl::gpu
