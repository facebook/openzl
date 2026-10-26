// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/device/device.hpp"

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

ZL_Report Device::wrapStream(void* native, std::optional<Stream>& stream)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    if (native != nullptr) {
        ZL_ERR_IF_ERR(doCheckStream(native));
    }
    stream = Stream{ *this, native };
    return ZL_returnSuccess();
}

ZL_Report Device::doCheckStream(void*)
{
    return ZL_returnSuccess();
}

} // namespace openzl::gpu
