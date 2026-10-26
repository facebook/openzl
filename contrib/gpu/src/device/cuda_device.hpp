// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>

#include "contrib/gpu/src/device/device.hpp"

namespace openzl::gpu {

/**
 * Creates a device that runs on the CUDA device that is current on the calling
 * thread when each operation starts.
 *
 * The caller makes the right device current and passes streams that belong to
 * it. Creation does not access the CUDA runtime, and operations never change
 * the current device.
 *
 * The CUDA runtime and driver must be version 12.8 or newer.
 *
 * @returns The device, or nullptr if allocation fails.
 */
std::unique_ptr<Device> makeCurrentCudaDevice();

} // namespace openzl::gpu
