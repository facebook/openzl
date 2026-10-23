// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/allocation/monotonic_layout.hpp"

#include <algorithm>
#include <cstddef>
#include <new>
#include <stdexcept>

#include "openzl/shared/overflow.h"
#include "openzl/shared/utils.h"

namespace openzl::gpu {
namespace {

/**
 * @returns Whether @p requirement belongs in @p arena. A destination arena
 * holds one region of one final output. The workspace arena holds every
 * workspace requirement, whatever its region.
 */
bool isInArena(
        const AllocationArena& arena,
        const StorageRequirement& requirement)
{
    if (arena.arenaClass != requirement.arena) {
        return false;
    }
    if (requirement.arena == ArenaClass::StreamArena) {
        return true;
    }
    if (arena.finalOutput != requirement.finalOutput) {
        return false;
    }
    if (arena.region != requirement.region) {
        return false;
    }
    return true;
}

/**
 * Finds the arena of @p layout that holds @p requirement, and adds that arena
 * if it does not exist yet.
 *
 * @returns The index of the arena in @c layout.arenas.
 */
size_t findOrAddArena(
        AllocationLayout& layout,
        const StorageRequirement& requirement)
{
    for (size_t i = 0; i < layout.arenas.size(); ++i) {
        if (isInArena(layout.arenas.at(i), requirement)) {
            return i;
        }
    }
    AllocationArena newArena{ .arenaClass  = requirement.arena,
                              .finalOutput = requirement.finalOutput,
                              .region      = requirement.arena
                                              == ArenaClass::Destination
                                           ? requirement.region
                                           : StreamRegion{} };
    layout.arenas.push_back(newArena);
    return layout.arenas.size() - 1;
}

/**
 * The alignment at which to place @p requirement. A workspace region starts at
 * a 256-byte boundary at least, as a buffer from @c cudaMalloc or DietGPU's
 * @c StackDeviceMemory does. A kernel can then use 16-byte vector loads, and
 * its warps read whole 128-byte cache lines. Core fixes the destination layout.
 */
size_t placementAlignment(const StorageRequirement& requirement)
{
    constexpr size_t kMinWorkspaceAlignment = 256;
    return requirement.arena == ArenaClass::StreamArena
            ? std::max(requirement.alignment, kMinWorkspaceAlignment)
            : requirement.alignment;
}

Result<AllocationLayout> buildLayout(const ExecutionRequirements& requirements)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    AllocationLayout layout;
    layout.placements.reserve(requirements.requirements.size());

    for (const StorageRequirement& requirement : requirements.requirements) {
        ZL_ASSERT(
                requirement.alignment != 0 && ZL_isPow2(requirement.alignment));
        const size_t arenaIndex = findOrAddArena(layout, requirement);
        AllocationArena& arena  = layout.arenas.at(arenaIndex);

        const size_t alignment = placementAlignment(requirement);
        const size_t padding   = (-arena.size) & (alignment - 1);
        // Core appends the chunks of one output with no gap between them.
        // Each destination size is a multiple of its alignment, so padding
        // here means that a planner computed a wrong size.
        ZL_ERR_IF(
                requirement.arena == ArenaClass::Destination && padding != 0,
                logicError,
                "A destination chunk size is not a multiple of its alignment");

        size_t offset = 0;
        size_t end    = 0;
        ZL_ERR_IF(
                ZL_overflowAddST(arena.size, padding, &offset)
                        || ZL_overflowAddST(offset, requirement.size, &end),
                integerOverflow,
                "Arena size does not fit in size_t");

        layout.placements.push_back(
                {
                        .arenaIndex = arenaIndex,
                        .offset     = offset,
                });
        arena.size      = end;
        arena.alignment = std::max(arena.alignment, alignment);
    }
    return layout;
}

} // namespace

Result<AllocationLayout> buildMonotonicAllocationLayout(
        const ExecutionRequirements& requirements)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    try {
        return buildLayout(requirements);
    } catch (const std::bad_alloc&) {
        ZL_ERR(allocation);
    } catch (const std::length_error&) {
        ZL_ERR(allocation);
    }
}

} // namespace openzl::gpu
