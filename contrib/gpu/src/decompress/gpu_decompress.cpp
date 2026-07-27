// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/dev/contrib/gpu/src/decompress/gpu_decompress.h"

#include <span>

namespace openzl::gpu::detail {

struct GPUChunk {
    const void* frameHeader_d;
    size_t frameHeaderSize;
    const void* chunk_d;
    size_t chunkSize;
    size_t chunkHeaderSize;
};

ZL_Report
decompressChunks(void*, size_t, std::span<const GPUChunk>, ZL_GPU_Stream)
{
    return ZL_returnError(ZL_ErrorCode_GENERIC); // not implemented yet
}

} // namespace openzl::gpu::detail

extern "C" ZL_Report
ZL_GPU_decompress(void*, size_t, const void*, size_t, ZL_GPU_Stream)
{
    return ZL_returnError(ZL_ErrorCode_GENERIC); // not implemented yet
}
