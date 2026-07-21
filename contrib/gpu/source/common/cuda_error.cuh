// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <stdexcept>
#include <string>

#include <cuda_runtime.h>

// Shared CUDA error checks for GPU host code, following the OpenZL error-macro
// conventions (ZL_ prefix, uppercase). Throw-based so it works in the
// benchmark's main and reports a failure under gtest. ZL_CUDA_CHECK wraps a
// CUDA call; ZL_CUDA_CHECK_LAST checks cudaGetLastError after a kernel launch.

namespace openzl::gpu {

inline void cudaCheck(cudaError_t err, const char* file, int line)
{
    if (err != cudaSuccess) {
        throw std::runtime_error(
                std::string("CUDA error ") + file + ":" + std::to_string(line)
                + ": " + cudaGetErrorString(err));
    }
}

} // namespace openzl::gpu

#define ZL_CUDA_CHECK(expr) ::openzl::gpu::cudaCheck((expr), __FILE__, __LINE__)
#define ZL_CUDA_CHECK_LAST() \
    ::openzl::gpu::cudaCheck(cudaGetLastError(), __FILE__, __LINE__)
