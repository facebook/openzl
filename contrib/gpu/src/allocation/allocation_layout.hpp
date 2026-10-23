// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "contrib/gpu/src/allocation/storage_requirements.hpp"

namespace openzl::gpu {

/**
 * One buffer. The executor supplies its address when it instantiates the
 * layout.
 *
 * A destination arena is one caller buffer: the data or the string lengths of
 * one final output. All workspace regions share one workspace arena.
 */
struct AllocationArena {
    /// Kind of storage that this arena holds.
    ArenaClass arenaClass{};
    /// Final output whose buffer this arena is, set only for a destination
    /// arena.
    std::optional<size_t> finalOutput;
    /// Region that a destination arena holds. A workspace arena holds both
    /// regions and keeps the default.
    StreamRegion region{};
    /// Number of bytes to allocate.
    size_t size{};
    /// Minimum alignment of the arena's base address.
    size_t alignment{ 1 };

    bool operator==(const AllocationArena& other) const
    {
        return arenaClass == other.arenaClass
                && finalOutput == other.finalOutput && region == other.region
                && size == other.size && alignment == other.alignment;
    }
};

/** Location of one storage requirement within an arena. */
struct AllocationPlacement {
    /// Index of the arena in @c AllocationLayout::arenas.
    size_t arenaIndex{};
    /// Byte offset from the arena's base address.
    size_t offset{};

    bool operator==(const AllocationPlacement& other) const
    {
        return arenaIndex == other.arenaIndex && offset == other.offset;
    }
};

/**
 * Placement of every storage requirement, without addresses.
 *
 * The layout owns no memory and holds no runtime address. Every offset and
 * arena size depends on the exact sizes in one @c ExecutionRequirements, so a
 * cached layout is valid only for decodes with the same requirements.
 */
struct AllocationLayout {
    /// Arenas in the order of their first requirement.
    std::vector<AllocationArena> arenas;
    /// Entry @c i places @c ExecutionRequirements::requirements[i].
    std::vector<AllocationPlacement> placements;
};

} // namespace openzl::gpu
