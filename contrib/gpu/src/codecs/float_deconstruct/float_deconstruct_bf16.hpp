// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <cstdint>

// Kept free of CUDA headers so that host implementations share the element
// decode with the kernels.
#if defined(__CUDACC__)
#    define ZL_GPU_HOST_DEVICE_INLINE __host__ __device__ __forceinline__
#else
#    define ZL_GPU_HOST_DEVICE_INLINE inline
#endif

namespace openzl::gpu {

// Describes one bf16 float-deconstruct chunk. All pointers are device pointers.
// dst must be 2-byte aligned. bf16DeconDecode uses vector loads and stores when
// exponent and signFrac are 4-byte aligned and dst is 8-byte aligned, and
// element loads and stores otherwise.
struct FloatDeconChunk {
    const uint8_t* exponent;
    const uint8_t* signFrac;
    uint16_t* dst;
    size_t nbElts;

    // Peels the first min(n, nbElts) elements off as a new segment and advances
    // *this past them, advancing device pointers only (no data copy).
    FloatDeconChunk peel(size_t n)
    {
        n                   = n < nbElts ? n : nbElts;
        FloatDeconChunk seg = *this;
        seg.nbElts          = n;
        exponent += n;
        signFrac += n;
        dst += n;
        nbElts -= n;
        return seg;
    }
};

// Canonical single element decode
ZL_GPU_HOST_DEVICE_INLINE uint16_t
decodeBf16Elt(uint8_t exponent, uint8_t signFrac)
{
    const uint16_t sign  = (uint16_t)(signFrac << 15);
    const uint16_t expnt = (uint16_t)(exponent << 7);
    const uint16_t frac  = (uint16_t)(signFrac >> 1);
    return sign | expnt | frac;
}

} // namespace openzl::gpu
