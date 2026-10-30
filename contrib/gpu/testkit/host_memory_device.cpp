// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/testkit/host_memory_device.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include "openzl/common/errors_internal.h"

namespace openzl::gpu::testkit {
namespace {

class HostMemoryDevice final : public Device {
   public:
    HostMemoryDevice() : Device(DecodeKernelTable{}) {}

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

    ZL_Report doAllocate(size_t size, size_t alignment, void*, void*& ptr_d)
            override
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
        if (size == 0) {
            ptr_d = nullptr;
            return ZL_returnSuccess();
        }
        // std::aligned_alloc needs a size that is a multiple of the alignment.
        size_t const roundedSize =
                (size + alignment - 1) / alignment * alignment;
        ZL_ERR_IF_LT(roundedSize, size, allocation);
        void* const allocation = std::aligned_alloc(alignment, roundedSize);
        ZL_ERR_IF_NULL(allocation, allocation);
        ptr_d = allocation;
        return ZL_returnSuccess();
    }

    void doDeallocate(void* ptr_d, void*) noexcept override
    {
        std::free(ptr_d);
    }

    ZL_Report doCopyDeviceToDevice(
            poly::span<const DeviceToDeviceCopy> copies,
            void*) override
    {
        for (const DeviceToDeviceCopy& copy : copies) {
            if (copy.size != 0) {
                std::memcpy(copy.dst_d, copy.src_d, copy.size);
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
