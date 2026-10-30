// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/allocation/decode_resources.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "openzl/common/errors_internal.h"

namespace openzl::gpu {
namespace {

/// Whether @p arena is the caller's output buffer: the data of the frame's
/// first output.
bool isOutputArena(const AllocationArena& arena)
{
    return arena.arenaClass == ArenaClass::Destination && arena.finalOutput == 0
            && arena.region == StreamRegion::Data;
}

/// The caller's output arena of @p layout, or nullptr if it has none. The
/// layout has at most one.
const AllocationArena* findOutputArena(const AllocationLayout& layout)
{
    const auto found = std::find_if(
            layout.arenas.begin(), layout.arenas.end(), isOutputArena);
    return found == layout.arenas.end() ? nullptr : &*found;
}

} // namespace

size_t outputSizeOf(const AllocationLayout& layout)
{
    const AllocationArena* const output = findOutputArena(layout);
    return output == nullptr ? 0 : output->size;
}

Result<DecodeResources> DecodeResources::create(
        const Stream& stream,
        const AllocationLayout& layout,
        const DecodeBuffers& buffers)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    for (const AllocationArena& arena : layout.arenas) {
        ZL_ERR_IF(
                arena.arenaClass == ArenaClass::Destination
                        && !isOutputArena(arena),
                temporaryLibraryLimitation,
                "Only the data of the frame's first output can use a caller buffer");
    }
    if (const AllocationArena* const output = findOutputArena(layout)) {
        ZL_ERR_IF_GT(
                output->size, buffers.outputCapacity, dstCapacity_tooSmall);
        ZL_ERR_IF_NULL(
                buffers.output_d,
                parameter_invalid,
                "The output buffer must not be null");
        ZL_ERR_IF_NE(
                reinterpret_cast<uintptr_t>(buffers.output_d)
                        % output->alignment,
                0,
                parameter_invalid,
                "The output buffer must be aligned to %zu bytes",
                output->alignment);
    }
    // On an error below, the destructor frees what was allocated.
    DecodeResources resources{ stream, layout };
    for (const AllocationArena& arena : layout.arenas) {
        ZL_ERR_IF_ERR(resources.addArenaAddress(arena, buffers));
    }
    return std::move(resources);
}

DecodeResources::DecodeResources(
        const Stream& stream,
        const AllocationLayout& layout)
        : stream_{ stream }, layout_{ &layout }
{
    // addArenaAddress pushes after the memory exists, so these must not
    // grow then.
    addresses_d_.reserve(layout.arenas.size());
    owned_d_.reserve(layout.arenas.size());
}

DecodeResources::~DecodeResources()
{
    for (void* const ptr_d : owned_d_) {
        stream_.deallocate(ptr_d);
    }
}

ArenaAddresses DecodeResources::addresses() const noexcept
{
    return ArenaAddresses{ *layout_, addresses_d_ };
}

ZL_Report DecodeResources::addArenaAddress(
        const AllocationArena& arena,
        const DecodeBuffers& buffers)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    if (arena.arenaClass == ArenaClass::StreamArena) {
        void* allocated_d = nullptr;
        ZL_ERR_IF_ERR(
                stream_.allocate(arena.size, arena.alignment, allocated_d));
        owned_d_.push_back(allocated_d);
        addresses_d_.push_back(static_cast<std::byte*>(allocated_d));
    } else {
        // create rejects every destination arena but the output arena.
        addresses_d_.push_back(static_cast<std::byte*>(buffers.output_d));
    }
    return ZL_returnSuccess();
}

} // namespace openzl::gpu
