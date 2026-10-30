// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "contrib/gpu/src/allocation/arena_addresses.hpp"
#include "openzl/common/assertion.h"

namespace openzl::gpu {
namespace {

TEST(ArenaAddressesTest, AddsEachPlacementOffsetToItsArenaAddress)
{
    // This fails if an address uses another arena's base, or drops the offset.
    const AllocationLayout layout{
        .arenas = {
                { .arenaClass  = ArenaClass::Destination,
                  .finalOutput = 0,
                  .size        = 8 },
                { .arenaClass = ArenaClass::StreamArena, .size = 4 },
        },
        .placements = {
                { .arenaIndex = 0, .offset = 0 },
                { .arenaIndex = 1, .offset = 0 },
                { .arenaIndex = 0, .offset = 5 },
        },
    };
    std::vector<std::byte> output(8);
    std::vector<std::byte> workspace(4);
    const std::vector<std::byte*> bases_d{ output.data(), workspace.data() };

    const ArenaAddresses arenas{ layout, bases_d };

    EXPECT_EQ(arenas.addressOf(0), output.data());
    EXPECT_EQ(arenas.addressOf(1), workspace.data());
    EXPECT_EQ(arenas.addressOf(2), output.data() + 5);
}

TEST(ArenaAddressesTest, GivesNoAddressInAnArenaWithoutABase)
{
    // This fails if addressOf adds the offset to a null base.
    const AllocationLayout layout{
        .arenas     = { { .arenaClass = ArenaClass::StreamArena, .size = 4 } },
        .placements = { { .arenaIndex = 0, .offset = 2 } },
    };
    const std::vector<std::byte*> bases_d{ nullptr };

    const ArenaAddresses arenas{ layout, bases_d };

#if ZL_ENABLE_ASSERT
    EXPECT_DEATH((void)arenas.addressOf(0), "has no base address");
#else
    EXPECT_EQ(arenas.addressOf(0), nullptr);
#endif
}

TEST(ArenaAddressesTest, NeedsOneBaseForEachArena)
{
    // This fails if the constructor accepts fewer bases than arenas, which
    // lets addressOf read past the bases.
#if ZL_ENABLE_ASSERT
    const AllocationLayout layout{
        .arenas = {
                { .arenaClass = ArenaClass::StreamArena, .size = 4 },
                { .arenaClass = ArenaClass::StreamArena, .size = 4 },
        },
    };
    std::vector<std::byte> workspace(4);
    const std::vector<std::byte*> bases_d{ workspace.data() };

    EXPECT_DEATH((void)ArenaAddresses(layout, bases_d), "bases_d.size");
#else
    GTEST_SKIP() << "Builds without ZL_ASSERT do not check the count.";
#endif
}

} // namespace
} // namespace openzl::gpu
