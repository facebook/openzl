// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <optional>

#include "openzl/cpp/poly/Span.hpp"
#include "openzl/zl_errors.h"

namespace openzl::gpu {

/** One device-to-host copy of @c dst_h.size() bytes starting at @c src_d. */
struct DeviceToHostCopy {
    poly::span<std::byte> dst_h;
    const std::byte* src_d;
};

class Device;

/**
 * A native stream of one device's runtime, such as a CUDA stream, and the
 * operations that host code enqueues on it.
 *
 * Only a @c Device creates a stream, so a stream cannot reach a device of
 * another runtime or another GPU. A stream owns neither its device nor its
 * native handle, and it lives for one call.
 *
 * Each operation checks what every runtime relies on, such as non-null copy
 * addresses, before it calls the device's runtime.
 */
class Stream {
   public:
    Device& device() const noexcept
    {
        return *device_;
    }

    /// The runtime's native stream handle, or NULL for the default stream.
    void* native() const noexcept
    {
        return native_;
    }

    /**
     * Copies every region to host memory and waits for the copies to finish.
     *
     * Empty regions are skipped. When this returns, no copy is still running,
     * even on error, so the caller may free every destination.
     *
     * @returns Success, @c parameter_invalid if a non-empty region has a null
     * address, or an error if the runtime fails.
     */
    ZL_Report copyToHostAndSynchronize(
            poly::span<const DeviceToHostCopy> copies) const;

   private:
    friend class Device;

    Stream(Device& device, void* native) noexcept
            : device_{ &device }, native_{ native }
    {
    }

    Device* device_;
    void* native_;
};

/**
 * The device-runtime operations that the host decode code needs.
 *
 * The host code calls no device runtime directly. Each runtime implements the
 * protected @c do* methods, so supporting another runtime does not change the
 * host code. Host code enqueues work through a @c Stream of the device.
 */
class Device {
   public:
    virtual ~Device() = default;

    /// The runtime's default stream, which always belongs to this device.
    Stream defaultStream() noexcept
    {
        return Stream{ *this, nullptr };
    }

    /**
     * Wraps @p native, a stream of this device's runtime, in @p stream.
     *
     * @param native The runtime's native stream handle, or NULL for the
     * default stream.
     * @param stream Receives the stream on success, and is unchanged on error.
     * @returns Success, or @c parameter_invalid if the runtime reports that
     * @p native belongs to another device.
     */
    ZL_Report wrapStream(void* native, std::optional<Stream>& stream);

   protected:
    /**
     * Checks that @p native, a non-null native stream, belongs to this device.
     * The default accepts every stream, for runtimes that cannot tell.
     */
    virtual ZL_Report doCheckStream(void* native);

    // The runtime's operations. The @c Stream method of the same name checks
    // the arguments and then calls it.
    virtual ZL_Report doCopyToHostAndSynchronize(
            poly::span<const DeviceToHostCopy> copies,
            void* native) = 0;

   private:
    friend class Stream;
};

} // namespace openzl::gpu
