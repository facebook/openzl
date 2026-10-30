// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "contrib/gpu/src/allocation/decode_resources.hpp"

namespace openzl::gpu {
namespace {

/// A device over host memory that counts its allocations and frees.
class CountingDevice final : public Device {
   public:
    CountingDevice() : Device(DecodeKernelTable{}) {}

    size_t allocations = 0;
    size_t frees       = 0;

   protected:
    ZL_Report doCopyToHostAndSynchronize(
            poly::span<const DeviceToHostCopy>,
            void*) override
    {
        return ZL_returnSuccess();
    }

    ZL_Report doAllocate(size_t size, size_t alignment, void*, void*& ptr_d)
            override
    {
        ++allocations;
        ptr_d = std::aligned_alloc(
                alignment, (size + alignment - 1) / alignment * alignment);
        return ZL_returnSuccess();
    }

    void doDeallocate(void* ptr_d, void*) noexcept override
    {
        ++frees;
        std::free(ptr_d);
    }

    ZL_Report doCopyDeviceToDevice(poly::span<const DeviceToDeviceCopy>, void*)
            override
    {
        return ZL_returnSuccess();
    }
};

/// One output arena of 8 bytes and two workspace arenas, with a requirement in
/// each and a second one in the output.
AllocationLayout makeLayout(size_t outputAlignment = 1)
{
    return {
        .arenas = {
                { .arenaClass  = ArenaClass::Destination,
                  .finalOutput = 0,
                  .size        = 8,
                  .alignment   = outputAlignment },
                { .arenaClass = ArenaClass::StreamArena,
                  .size       = 64,
                  .alignment  = 256 },
                { .arenaClass = ArenaClass::StreamArena,
                  .size       = 32,
                  .alignment  = 256 },
        },
        .placements = {
                { .arenaIndex = 0, .offset = 0 },
                { .arenaIndex = 1, .offset = 0 },
                { .arenaIndex = 2, .offset = 0 },
                { .arenaIndex = 0, .offset = 4 },
        },
    };
}

template <typename T>
T valueOf(Result<T> result)
{
    EXPECT_FALSE(result.isError());
    return std::move(result).value();
}

TEST(DecodeResourcesTest, PlacesTheOutputInTheCallerBufferAndAllocatesWorkspace)
{
    // This fails if the output does not use the caller's buffer, or if two
    // workspace arenas share memory.
    CountingDevice device;
    const AllocationLayout layout = makeLayout();
    std::vector<std::byte> output(8);

    const DecodeResources resources =
            valueOf(DecodeResources::create(
                    device.defaultStream(),
                    layout,
                    { .output_d       = output.data(),
                      .outputCapacity = output.size() }));

    const ArenaAddresses addresses = resources.addresses();
    EXPECT_EQ(addresses.addressOf(0), output.data());
    EXPECT_EQ(addresses.addressOf(3), output.data() + 4);
    EXPECT_NE(addresses.addressOf(1), nullptr);
    EXPECT_NE(addresses.addressOf(2), nullptr);
    EXPECT_NE(addresses.addressOf(1), addresses.addressOf(2));
    EXPECT_EQ(device.allocations, 2);
}

TEST(DecodeResourcesTest, FreesEachAllocationOnceAfterAMove)
{
    // This fails if a moved-from object also frees the memory, or if an
    // allocation is never freed.
    CountingDevice device;
    const AllocationLayout layout = makeLayout();
    std::vector<std::byte> output(8);
    {
        std::optional<DecodeResources> moved;
        {
            DecodeResources resources =
                    valueOf(DecodeResources::create(
                            device.defaultStream(),
                            layout,
                            { .output_d       = output.data(),
                              .outputCapacity = output.size() }));
            moved.emplace(std::move(resources));
        }
        EXPECT_EQ(device.frees, 0);
    }

    EXPECT_EQ(device.allocations, 2);
    EXPECT_EQ(device.frees, 2);
}

TEST(DecodeResourcesTest, KeepsItsAddressesAcrossAMove)
{
    // ASan fails this if a view still points into the moved-from object, as it
    // would if the addresses used inline storage.
    CountingDevice device;
    const AllocationLayout layout = makeLayout();
    std::vector<std::byte> output(8);
    auto source = std::make_unique<DecodeResources>(
            valueOf(DecodeResources::create(
                    device.defaultStream(),
                    layout,
                    { .output_d       = output.data(),
                      .outputCapacity = output.size() })));
    const ArenaAddresses view = source->addresses();

    const DecodeResources moved{ std::move(*source) };
    source.reset();

    const ArenaAddresses expected = moved.addresses();
    for (size_t i = 0; i < layout.placements.size(); ++i) {
        EXPECT_EQ(view.addressOf(i), expected.addressOf(i))
                << "placement " << i;
    }
}

TEST(DecodeResourcesTest, RejectsAnOutputThatIsTooSmallBeforeAllocating)
{
    CountingDevice device;
    const AllocationLayout layout = makeLayout();
    std::vector<std::byte> output(7);

    const Result<DecodeResources> resources = DecodeResources::create(
            device.defaultStream(),
            layout,
            { .output_d = output.data(), .outputCapacity = output.size() });

    EXPECT_EQ(
            ZL_errorCode(resources.report()),
            ZL_ErrorCode_dstCapacity_tooSmall);
    EXPECT_EQ(device.allocations, 0);
}

TEST(DecodeResourcesTest, RejectsAMisalignedOutputBeforeAllocating)
{
    CountingDevice device;
    const AllocationLayout layout = makeLayout(4);
    std::vector<std::byte> output(16);
    std::byte* const misaligned = output.data() + 1;

    const Result<DecodeResources> resources = DecodeResources::create(
            device.defaultStream(),
            layout,
            { .output_d = misaligned, .outputCapacity = 8 });

    EXPECT_EQ(ZL_errorCode(resources.report()), ZL_ErrorCode_parameter_invalid);
    EXPECT_EQ(device.allocations, 0);
}

TEST(DecodeResourcesTest, RejectsANullOutputBeforeAllocating)
{
    // A null address passes the alignment check. This fails if create accepts
    // it and a kernel later writes through it.
    CountingDevice device;
    const AllocationLayout layout = makeLayout();

    const Result<DecodeResources> resources = DecodeResources::create(
            device.defaultStream(),
            layout,
            { .output_d = nullptr, .outputCapacity = 8 });

    EXPECT_EQ(ZL_errorCode(resources.report()), ZL_ErrorCode_parameter_invalid);
    EXPECT_EQ(device.allocations, 0);
}

TEST(DecodeResourcesTest, RejectsADestinationThatTheCallerCannotProvide)
{
    // DecodeBuffers holds only the data of the frame's first output. Without
    // this check, another destination arena gets a null address, and the error
    // appears later, when a kernel uses that address.
    CountingDevice device;
    AllocationLayout layout = makeLayout();
    layout.arenas.push_back(
            { .arenaClass  = ArenaClass::Destination,
              .finalOutput = 1,
              .size        = 4 });
    std::vector<std::byte> output(8);

    const Result<DecodeResources> resources = DecodeResources::create(
            device.defaultStream(),
            layout,
            { .output_d = output.data(), .outputCapacity = output.size() });

    EXPECT_EQ(
            ZL_errorCode(resources.report()),
            ZL_ErrorCode_temporaryLibraryLimitation);
    EXPECT_EQ(device.allocations, 0);
}

} // namespace
} // namespace openzl::gpu
