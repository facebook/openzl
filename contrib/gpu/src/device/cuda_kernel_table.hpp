// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "contrib/gpu/src/common/codec_decode_execution.hpp"

namespace openzl::gpu {

/// The CUDA runtime's kernels for each standard codec.
DecodeKernelTable cudaKernelTable();

} // namespace openzl::gpu
