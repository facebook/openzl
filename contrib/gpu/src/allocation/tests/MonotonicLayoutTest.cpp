// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "contrib/gpu/src/allocation/monotonic_layout.hpp"

namespace openzl::gpu {
namespace {

StorageRequirement workspace(size_t size, size_t alignment)
{
    return {
        .arena     = ArenaClass::StreamArena,
        .size      = size,
        .alignment = alignment,
    };
}

StorageRequirement destination(
        size_t finalOutput,
        StreamRegion region,
        size_t size,
        size_t alignment = 1)
{
    return {
        .region      = region,
        .arena       = ArenaClass::Destination,
        .finalOutput = finalOutput,
        .size        = size,
        .alignment   = alignment,
    };
}

AllocationArena destinationArena(
        size_t finalOutput,
        StreamRegion region,
        size_t size,
        size_t alignment = 1)
{
    return {
        .arenaClass  = ArenaClass::Destination,
        .finalOutput = finalOutput,
        .region      = region,
        .size        = size,
        .alignment   = alignment,
    };
}

AllocationArena workspaceArena(size_t size, size_t alignment)
{
    return {
        .arenaClass = ArenaClass::StreamArena,
        .size       = size,
        .alignment  = alignment,
    };
}

AllocationLayout layoutOf(std::vector<StorageRequirement> requirements)
{
    Result<AllocationLayout> layout = buildMonotonicAllocationLayout(
            { .requirements = std::move(requirements) });
    EXPECT_FALSE(layout.isError());
    return layout.isError() ? AllocationLayout{} : std::move(layout).value();
}

TEST(MonotonicLayoutTest, PlacesNothingForNoRequirements)
{
    const AllocationLayout layout = layoutOf({});

    EXPECT_TRUE(layout.arenas.empty());
    EXPECT_TRUE(layout.placements.empty());
}

TEST(MonotonicLayoutTest, KeepsDestinationAndWorkspaceInSeparateArenas)
{
    const AllocationLayout layout = layoutOf(
            {
                    destination(0, StreamRegion::Data, 5),
                    workspace(4, 4),
                    destination(0, StreamRegion::Data, 3),
            });

    EXPECT_EQ(
            layout.arenas,
            (std::vector<AllocationArena>{
                    destinationArena(0, StreamRegion::Data, 8),
                    workspaceArena(4, 256),
            }));
    EXPECT_EQ(
            layout.placements,
            (std::vector<AllocationPlacement>{
                    { .arenaIndex = 0, .offset = 0 },
                    { .arenaIndex = 1, .offset = 0 },
                    { .arenaIndex = 0, .offset = 5 },
            }));
}

TEST(MonotonicLayoutTest, GivesEachFinalOutputItsOwnDestinationArena)
{
    const AllocationLayout layout = layoutOf(
            {
                    destination(0, StreamRegion::Data, 6),
                    destination(0, StreamRegion::Data, 2),
                    destination(1, StreamRegion::Data, 4),
            });

    EXPECT_EQ(
            layout.arenas,
            (std::vector<AllocationArena>{
                    destinationArena(0, StreamRegion::Data, 8),
                    destinationArena(1, StreamRegion::Data, 4),
            }));
    EXPECT_EQ(
            layout.placements,
            (std::vector<AllocationPlacement>{
                    { .arenaIndex = 0, .offset = 0 },
                    { .arenaIndex = 0, .offset = 6 },
                    { .arenaIndex = 1, .offset = 0 },
            }));
}

TEST(MonotonicLayoutTest, GivesStringLengthsTheirOwnDestinationArena)
{
    StorageRequirement lengths0 =
            destination(0, StreamRegion::StringLengths, 8);
    lengths0.alignment          = alignof(uint32_t);
    StorageRequirement lengths1 = lengths0;
    lengths1.size               = 4;

    const AllocationLayout layout = layoutOf(
            {
                    destination(0, StreamRegion::Data, 5),
                    lengths0,
                    destination(0, StreamRegion::Data, 3),
                    lengths1,
            });

    EXPECT_EQ(
            layout.arenas,
            (std::vector<AllocationArena>{
                    destinationArena(0, StreamRegion::Data, 8),
                    destinationArena(
                            0,
                            StreamRegion::StringLengths,
                            12,
                            alignof(uint32_t)),
            }));
    EXPECT_EQ(
            layout.placements,
            (std::vector<AllocationPlacement>{
                    { .arenaIndex = 0, .offset = 0 },
                    { .arenaIndex = 1, .offset = 0 },
                    { .arenaIndex = 0, .offset = 5 },
                    { .arenaIndex = 1, .offset = 8 },
            }));
}

TEST(MonotonicLayoutTest, PutsWorkspaceStringLengthsInTheWorkspaceArena)
{
    StorageRequirement lengths = workspace(8, alignof(uint32_t));
    lengths.region             = StreamRegion::StringLengths;

    const AllocationLayout layout = layoutOf({ workspace(3, 1), lengths });

    EXPECT_EQ(
            layout.arenas,
            (std::vector<AllocationArena>{
                    workspaceArena(264, 256),
            }));
    EXPECT_EQ(
            layout.placements,
            (std::vector<AllocationPlacement>{
                    { .arenaIndex = 0, .offset = 0 },
                    { .arenaIndex = 0, .offset = 256 },
            }));
}

TEST(MonotonicLayoutTest, GivesEveryWorkspaceRequirementDistinctStorage)
{
    const AllocationLayout layout =
            layoutOf({ workspace(8, 1), workspace(8, 1) });

    EXPECT_EQ(
            layout.placements,
            (std::vector<AllocationPlacement>{
                    { .arenaIndex = 0, .offset = 0 },
                    { .arenaIndex = 0, .offset = 256 },
            }));
}

TEST(MonotonicLayoutTest, AppendsAlignedDestinationChunksWithoutGaps)
{
    // Two chunks of a numeric output with 4-byte elements. This fails if the
    // second chunk does not start where the first one ends.
    const AllocationLayout layout = layoutOf(
            {
                    destination(0, StreamRegion::Data, 8, 4),
                    destination(0, StreamRegion::Data, 12, 4),
            });

    EXPECT_EQ(
            layout.arenas,
            (std::vector<AllocationArena>{
                    destinationArena(0, StreamRegion::Data, 20, 4),
            }));
    EXPECT_EQ(
            layout.placements,
            (std::vector<AllocationPlacement>{
                    { .arenaIndex = 0, .offset = 0 },
                    { .arenaIndex = 0, .offset = 8 },
            }));
}

TEST(MonotonicLayoutTest, RejectsPaddingThatOverflowsSizeT)
{
    // The second region starts at the next 256-byte boundary, 4 bytes past the
    // end of size_t.
    const Result<AllocationLayout> layout = buildMonotonicAllocationLayout({
            .requirements = {
                    workspace(std::numeric_limits<size_t>::max() - 3, 1),
                    workspace(8, 1),
            },
    });

    ASSERT_TRUE(layout.isError());
    EXPECT_EQ(ZL_errorCode(layout.report()), ZL_ErrorCode_integerOverflow);
}

TEST(MonotonicLayoutTest, RejectsARegionThatOverflowsSizeT)
{
    // The second region starts on a 256-byte boundary with no padding, and its
    // end does not fit in size_t.
    const Result<AllocationLayout> layout = buildMonotonicAllocationLayout({
            .requirements = {
                    workspace(std::numeric_limits<size_t>::max() - 255, 1),
                    workspace(512, 1),
            },
    });

    ASSERT_TRUE(layout.isError());
    EXPECT_EQ(ZL_errorCode(layout.report()), ZL_ErrorCode_integerOverflow);
}

} // namespace
} // namespace openzl::gpu
