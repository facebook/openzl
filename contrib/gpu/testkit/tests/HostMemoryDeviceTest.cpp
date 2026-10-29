// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
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

} // namespace
} // namespace openzl::gpu::testkit
