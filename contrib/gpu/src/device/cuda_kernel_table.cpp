// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/device/cuda_kernel_table.hpp"

#include "contrib/gpu/src/codecs/float_deconstruct/decode_float_deconstruct_cuda.hpp"
#include "openzl/common/wire_format.h"

namespace openzl::gpu {
namespace {

void setKernels(
        std::vector<DecodeKernel>& list,
        poly::span<const DecodeKernel> kernels)
{
    list.assign(kernels.begin(), kernels.end());
}

} // namespace

DecodeKernelTable cudaKernelTable()
{
    DecodeKernelTable table{};
    setKernels(
            table[ZL_StandardTransformID_float_deconstruct],
            floatDeconstructCudaKernels());
    return table;
}

} // namespace openzl::gpu
