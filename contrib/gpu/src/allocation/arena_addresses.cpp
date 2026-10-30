// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/allocation/arena_addresses.hpp"

#include "openzl/common/assertion.h"

namespace openzl::gpu {

ArenaAddresses::ArenaAddresses(
        const AllocationLayout& layout,
        poly::span<std::byte* const> bases_d) noexcept
        : layout_{ &layout }, bases_d_{ bases_d }
{
    ZL_ASSERT_EQ(bases_d.size(), layout.arenas.size());
}

std::byte* ArenaAddresses::addressOf(size_t index) const
{
    const AllocationPlacement& placement = layout_->placements.at(index);
    std::byte* const base_d              = bases_d_[placement.arenaIndex];
    if (base_d == nullptr) {
        ZL_ASSERT_FAIL("An arena with a placement has no base address");
        return nullptr;
    }
    return base_d + placement.offset;
}

} // namespace openzl::gpu
