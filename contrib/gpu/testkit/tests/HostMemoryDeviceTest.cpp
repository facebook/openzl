// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "contrib/gpu/testkit/host_memory_device.h"
#include "openzl/common/wire_format.h"

namespace openzl::gpu::testkit {
namespace {

TEST(HostMemoryDeviceTest, CopiesEveryNonEmptyRegion)
{
    // This fails if a region is skipped or copied from the wrong offset, or if
    // an empty region with null addresses is dereferenced.
    const std::vector<std::byte> source{
        std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 }
    };
    std::vector<std::byte> first(2);
    std::vector<std::byte> second(2);
    const std::vector<DeviceToHostCopy> copies{
        { .dst_h = first, .src_d = source.data() },
        { .dst_h = {}, .src_d = nullptr },
        { .dst_h = second, .src_d = source.data() + 2 },
    };
    const std::unique_ptr<Device> device = makeHostMemoryDevice();
    ASSERT_NE(device, nullptr);

    ZL_Report const result =
            device->defaultStream().copyToHostAndSynchronize(copies);

    ASSERT_FALSE(ZL_isError(result));
    EXPECT_EQ(
            first, (std::vector<std::byte>{ std::byte{ 1 }, std::byte{ 2 } }));
    EXPECT_EQ(
            second, (std::vector<std::byte>{ std::byte{ 3 }, std::byte{ 4 } }));
}

TEST(HostMemoryDeviceTest, RejectsANonEmptyRegionWithANullAddress)
{
    // This fails if a null address reaches the runtime, or if a region is
    // copied before every region is checked.
    const std::vector<std::byte> source(2, std::byte{ 1 });
    const std::vector<std::byte> untouched(2, std::byte{ 0 });
    std::vector<std::byte> destination = untouched;
    const std::vector<DeviceToHostCopy> nullSource{
        { .dst_h = destination, .src_d = source.data() },
        { .dst_h = destination, .src_d = nullptr },
    };
    const std::vector<DeviceToHostCopy> nullDestination{
        { .dst_h = poly::span<std::byte>{ static_cast<std::byte*>(nullptr), 2 },
          .src_d = source.data() },
    };
    const std::unique_ptr<Device> device = makeHostMemoryDevice();
    ASSERT_NE(device, nullptr);

    ZL_Report const nullSourceResult =
            device->defaultStream().copyToHostAndSynchronize(nullSource);
    ZL_Report const nullDestinationResult =
            device->defaultStream().copyToHostAndSynchronize(nullDestination);

    EXPECT_EQ(ZL_errorCode(nullSourceResult), ZL_ErrorCode_parameter_invalid);
    EXPECT_EQ(
            ZL_errorCode(nullDestinationResult),
            ZL_ErrorCode_parameter_invalid);
    EXPECT_EQ(destination, untouched);
}

TEST(HostMemoryDeviceTest, DecodesNothing)
{
    // This fails if the test device reports success for a codec run, even one
    // that a real device decodes.
    CodecRunInfo run;
    run.transform = { trt_standard, ZL_StandardTransformID_float_deconstruct };
    const std::unique_ptr<Device> device = makeHostMemoryDevice();
    ASSERT_NE(device, nullptr);
    std::vector<DecodeBatch> batches;

    ZL_Report const result = device->chooseDecodeBatches(
            poly::span<const CodecRunInfo>{ &run, 1 }, batches);

    EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_temporaryLibraryLimitation);
}

// A caller joins its branches while an exception unwinds, so join must not
// throw.
static_assert(noexcept(std::declval<const Stream&>().join({})));

TEST(HostMemoryDeviceTest, ForksIntoItsOwnStream)
{
    // The test device has no concurrent streams. This fails if a branch is
    // not the parent stream, so that branch work could run out of order.
    const std::unique_ptr<Device> device = makeHostMemoryDevice();
    ASSERT_NE(device, nullptr);
    const Stream stream = device->defaultStream();
    std::vector<Stream> branches;

    ASSERT_FALSE(ZL_isError(stream.fork(3, branches)));

    ASSERT_EQ(branches.size(), 3);
    for (const Stream& branch : branches) {
        EXPECT_EQ(branch.native(), stream.native());
    }
    EXPECT_FALSE(ZL_isError(stream.join(branches)));
}

TEST(HostMemoryDeviceTest, RejectsMoreBranchesThanAVectorHolds)
{
    // This fails if fork asks the runtime for branches before it checks the
    // count, or throws for a count that no vector can hold.
    const std::unique_ptr<Device> device = makeHostMemoryDevice();
    ASSERT_NE(device, nullptr);
    const Stream stream = device->defaultStream();
    std::vector<Stream> branches;

    const ZL_Report result = stream.fork(SIZE_MAX, branches);

    EXPECT_EQ(ZL_errorCode(result), ZL_ErrorCode_parameter_invalid);
    EXPECT_TRUE(branches.empty());
}

TEST(HostMemoryDeviceTest, AllocatesAndCopiesHostMemory)
{
    // This fails if an allocation ignores the alignment or a copy moves the
    // wrong bytes.
    const std::unique_ptr<Device> device = makeHostMemoryDevice();
    ASSERT_NE(device, nullptr);
    const Stream stream = device->defaultStream();
    void* source_d      = nullptr;
    void* destination_d = nullptr;
    ASSERT_FALSE(ZL_isError(stream.allocate(5, 16, source_d)));
    ASSERT_FALSE(ZL_isError(stream.allocate(5, 16, destination_d)));
    EXPECT_EQ(reinterpret_cast<uintptr_t>(source_d) % 16, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(destination_d) % 16, 0);
    const std::vector<std::byte> expected{ std::byte{ 1 },
                                           std::byte{ 2 },
                                           std::byte{ 3 },
                                           std::byte{ 4 },
                                           std::byte{ 5 } };
    std::memcpy(source_d, expected.data(), expected.size());
    const DeviceToDeviceCopy copy{
        .dst_d = static_cast<std::byte*>(destination_d),
        .src_d = static_cast<const std::byte*>(source_d),
        .size  = expected.size(),
    };

    ASSERT_FALSE(ZL_isError(stream.copyDeviceToDevice(
            poly::span<const DeviceToDeviceCopy>{ &copy, 1 })));

    const auto* const actual = static_cast<const std::byte*>(destination_d);
    EXPECT_EQ(
            std::vector<std::byte>(actual, actual + expected.size()), expected);
    stream.deallocate(source_d);
    stream.deallocate(destination_d);
}

TEST(HostMemoryDeviceTest, RejectsBadAllocationAndCopyArguments)
{
    // The checks belong to Stream, so every runtime gets them. This fails if
    // a bad alignment reaches the runtime, which divides by it, if a failed
    // allocation changes its output, or if a null copy address reaches the
    // runtime.
    const std::unique_ptr<Device> device = makeHostMemoryDevice();
    ASSERT_NE(device, nullptr);
    const Stream stream = device->defaultStream();
    int sentinel        = 0;
    void* ptr_d         = &sentinel;
    std::byte byte{ 1 };
    const DeviceToDeviceCopy nullSource{ .dst_d = &byte,
                                         .src_d = nullptr,
                                         .size  = 1 };
    const DeviceToDeviceCopy nullDestination{ .dst_d = nullptr,
                                              .src_d = &byte,
                                              .size  = 1 };

    for (const size_t alignment : { size_t{ 0 }, size_t{ 24 } }) {
        EXPECT_EQ(
                ZL_errorCode(stream.allocate(64, alignment, ptr_d)),
                ZL_ErrorCode_parameter_invalid)
                << "alignment " << alignment;
    }
    EXPECT_EQ(
            ZL_errorCode(stream.copyDeviceToDevice(
                    poly::span<const DeviceToDeviceCopy>{ &nullSource, 1 })),
            ZL_ErrorCode_parameter_invalid);
    EXPECT_EQ(
            ZL_errorCode(stream.copyDeviceToDevice(
                    poly::span<const DeviceToDeviceCopy>{ &nullDestination,
                                                          1 })),
            ZL_ErrorCode_parameter_invalid);

    EXPECT_EQ(ptr_d, &sentinel);
}

} // namespace
} // namespace openzl::gpu::testkit
