// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/dev/contrib/gpu/src/decompress/gpu_decompress.h"

extern "C" ZL_Report
ZL_GPU_decompress(void*, size_t, const void*, size_t, ZL_GPU_Stream)
{
    return ZL_returnError(ZL_ErrorCode_GENERIC); // not implemented yet
}
