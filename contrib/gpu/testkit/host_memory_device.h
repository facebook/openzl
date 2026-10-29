// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>

#include "contrib/gpu/src/device/device.hpp"

namespace openzl::gpu::testkit {

/**
 * Creates a test device whose memory is host memory.
 *
 * CPU tests use it to run the host code that reads device memory, such as
 * chunk discovery and header staging, without a GPU. "Device" addresses are
 * host addresses, copies are synchronous, and streams are ignored. @c Stream
 * rejects a copy with a null address, so tests catch callers that break the
 * contract.
 *
 * It decodes nothing: its kernel table is empty, so
 * @c Device::chooseDecodeBatches returns @c temporaryLibraryLimitation for
 * every run. Tests that check decoded bytes run on a GPU and compare with
 * core.
 *
 * @returns The device, or nullptr if allocation fails.
 */
std::unique_ptr<Device> makeHostMemoryDevice();

} // namespace openzl::gpu::testkit
