// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "contrib/gpu/src/common/codec_decode_execution.hpp"
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

    /**
     * Enqueues the decode of @p runs with @p kernel, and returns without
     * waiting for the device.
     *
     * @p kernel comes from this device's @c chooseDecodeBatches, and @p runs
     * are the runs of that batch with addresses. Host memory that @p runs
     * references may be reused when this returns. Device buffers must stay
     * valid until the work on this stream completes.
     *
     * @returns Success, or a kernel's error, after which earlier work may still
     * be running, so the caller must synchronize this stream before freeing its
     * buffers. A kernel's @c temporaryLibraryLimitation becomes
     * @c transform_executionFailure.
     */
    ZL_Report decodeBatch(
            const DecodeKernel& kernel,
            poly::span<const BoundCodecRun> runs) const;

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
 * protected @c do* methods and passes its kernel table to the constructor, so
 * supporting another runtime does not change the host code. Host code enqueues
 * work through a @c Stream of the device.
 */
class Device {
   public:
    Device(const Device&)            = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&)                 = delete;
    Device& operator=(Device&&)      = delete;
    virtual ~Device()                = default;

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

    /**
     * Chooses the first kernel in this device's table that accepts each run,
     * and groups the runs that choose the same kernel into one batch, as the
     * free function @c chooseDecodeBatches does.
     *
     * Reads no device memory, so binding can choose every kernel before any
     * memory is allocated. The chosen kernels live as long as this device.
     *
     * @param batches Receives the batches on success, and is unchanged on
     * error.
     * @returns Success, or @c temporaryLibraryLimitation if no kernel accepts a
     * run, so the caller can decode on the CPU instead.
     * @throws std::bad_alloc if an allocation fails.
     */
    ZL_Report chooseDecodeBatches(
            poly::span<const CodecRunInfo> runs,
            std::vector<DecodeBatch>& batches) const;

   protected:
    /// @p decodeKernels lists this runtime's kernels for each codec. It does
    /// not change after construction.
    explicit Device(DecodeKernelTable decodeKernels) noexcept;

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

    // Runs the kernel. A runtime can override it to add work around each batch.
    virtual ZL_Report doDecodeBatch(
            const DecodeKernel& kernel,
            poly::span<const BoundCodecRun> runs,
            void* native);

   private:
    friend class Stream;

    DecodeKernelTable decodeKernels_;
};

} // namespace openzl::gpu
