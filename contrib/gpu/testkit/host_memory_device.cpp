// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/testkit/host_memory_device.h"

#include <cstring>
#include <memory>
#include <new>

namespace openzl::gpu::testkit {
namespace {

class HostMemoryDevice final : public Device {
   protected:
    ZL_Report doCopyToHostAndSynchronize(
            poly::span<const DeviceToHostCopy> copies,
            void*) override
    {
        for (const DeviceToHostCopy& copy : copies) {
            if (!copy.dst_h.empty()) {
                std::memcpy(copy.dst_h.data(), copy.src_d, copy.dst_h.size());
            }
        }
        return ZL_returnSuccess();
    }
};

} // namespace

std::unique_ptr<Device> makeHostMemoryDevice()
{
    return std::unique_ptr<Device>{ new (std::nothrow) HostMemoryDevice{} };
}

} // namespace openzl::gpu::testkit
