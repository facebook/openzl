// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/device/device.hpp"

#include <utility>

#include "contrib/gpu/src/device/decode_dispatch.hpp"
#include "openzl/common/errors_internal.h"

namespace openzl::gpu {

ZL_Report Stream::copyToHostAndSynchronize(
        poly::span<const DeviceToHostCopy> copies) const
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    for (const DeviceToHostCopy& copy : copies) {
        if (copy.dst_h.empty()) {
            continue;
        }
        ZL_ERR_IF_NULL(copy.dst_h.data(), parameter_invalid);
        ZL_ERR_IF_NULL(copy.src_d, parameter_invalid);
    }
    return device().doCopyToHostAndSynchronize(copies, native_);
}

ZL_Report Stream::decodeBatch(
        const DecodeKernel& kernel,
        poly::span<const BoundCodecRun> runs) const
{
    return device().doDecodeBatch(kernel, runs, native_);
}

Device::Device(DecodeKernelTable decodeKernels) noexcept
        : decodeKernels_{ std::move(decodeKernels) }
{
}

ZL_Report Device::wrapStream(void* native, std::optional<Stream>& stream)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    if (native != nullptr) {
        ZL_ERR_IF_ERR(doCheckStream(native));
    }
    stream = Stream{ *this, native };
    return ZL_returnSuccess();
}

ZL_Report Device::chooseDecodeBatches(
        poly::span<const CodecRunInfo> runs,
        std::vector<DecodeBatch>& batches) const
{
    return openzl::gpu::chooseDecodeBatches(decodeKernels_, runs, batches);
}

ZL_Report Device::doCheckStream(void*)
{
    return ZL_returnSuccess();
}

ZL_Report Device::doDecodeBatch(
        const DecodeKernel& kernel,
        poly::span<const BoundCodecRun> runs,
        void* native)
{
    return decodeBatchWith(kernel, runs, native);
}

} // namespace openzl::gpu
