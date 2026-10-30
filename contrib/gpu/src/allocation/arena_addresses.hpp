// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>

#include "contrib/gpu/src/allocation/allocation_layout.hpp"
#include "openzl/cpp/poly/Span.hpp"

namespace openzl::gpu {

/**
 * A layout with one device address for each of its arenas.
 *
 * The layout holds no runtime address, so the layout can be used again with
 * other buffers. This view pairs the layout with one set of addresses. It owns
 * neither the layout nor the addresses, which must outlive it.
 */
class ArenaAddresses {
   public:
    /**
     * @param bases_d One address for each arena of @p layout, in the same
     * order. Each must meet its arena's alignment and capacity. An arena that
     * no requirement is placed in can have a null address.
     */
    ArenaAddresses(
            const AllocationLayout& layout,
            poly::span<std::byte* const> bases_d) noexcept;

    /// The address that the layout gives requirement @p index: the base
    /// address of its arena plus its offset.
    std::byte* addressOf(size_t index) const;

   private:
    const AllocationLayout* layout_;
    poly::span<std::byte* const> bases_d_;
};

} // namespace openzl::gpu
