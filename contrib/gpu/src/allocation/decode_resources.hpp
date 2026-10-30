// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <vector>

#include "contrib/gpu/src/allocation/allocation_layout.hpp"
#include "contrib/gpu/src/allocation/arena_addresses.hpp"
#include "contrib/gpu/src/common/result.hpp"
#include "contrib/gpu/src/device/device.hpp"

namespace openzl::gpu {

/** The device buffers that the caller supplies. */
struct DecodeBuffers {
    /// Device buffer for the data of the frame's first output.
    void* output_d = nullptr;
    /// Bytes at @c output_d.
    size_t outputCapacity = 0;
};

/// Bytes that the caller's output buffer must hold: the size that @p layout
/// gives the data of the frame's first output.
size_t outputSizeOf(const AllocationLayout& layout);

/**
 * The device memory of an allocation layout: one address for each arena.
 *
 * - The arena for the data of the frame's first output is the caller's output
 *   buffer. This object does not own it.
 * - Each workspace arena gets new memory from @c Stream::allocate, so the
 *   stream's device chooses where the memory comes from.
 *
 * The destructor frees the new memory in stream order, so the free waits for
 * the work enqueued before it.
 */
class DecodeResources {
   public:
    /**
     * Checks @p buffers against @p layout, then allocates every workspace
     * arena.
     *
     * @param layout It must outlive the result.
     * @returns The resources; before any allocation, @c dstCapacity_tooSmall,
     * @c parameter_invalid if the output buffer is null or not aligned as the
     * layout requires, or @c temporaryLibraryLimitation if the layout needs a
     * caller buffer that @c DecodeBuffers cannot supply; or the error of the
     * device's allocator.
     */
    static Result<DecodeResources> create(
            const Stream& stream,
            const AllocationLayout& layout,
            const DecodeBuffers& buffers);

    DecodeResources(DecodeResources&&) noexcept        = default;
    DecodeResources& operator=(DecodeResources&&)      = delete;
    DecodeResources(const DecodeResources&)            = delete;
    DecodeResources& operator=(const DecodeResources&) = delete;
    ~DecodeResources();

    /**
     * One address for each arena of the layout.
     *
     * The view stays valid until the owner of this memory is destroyed. A move
     * changes the owner, not the addresses.
     */
    ArenaAddresses addresses() const noexcept;

   private:
    DecodeResources(const Stream& stream, const AllocationLayout& layout);

    /**
     * Appends the address of @p arena to @c addresses_d_: the caller's output
     * buffer, or new memory. @c create calls this once for each arena, in
     * layout order, so index i holds the address of arena i.
     */
    ZL_Report addArenaAddress(
            const AllocationArena& arena,
            const DecodeBuffers& buffers);

    Stream stream_;
    const AllocationLayout* layout_;
    /// One address for each arena of the layout, in the same order.
    std::vector<std::byte*> addresses_d_;
    /// The memory that this object allocated, and frees.
    std::vector<void*> owned_d_;
};

} // namespace openzl::gpu
