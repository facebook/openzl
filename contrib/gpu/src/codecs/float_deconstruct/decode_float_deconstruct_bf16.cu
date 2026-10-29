// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "decode_float_deconstruct_bf16.cuh"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include "contrib/gpu/src/common/cuda_error.cuh"
#include "contrib/gpu/src/common/cuda_launch.cuh"
#include "contrib/gpu/src/common/cuda_raii.cuh"
#include "contrib/gpu/src/common/segment_plan.cuh"

namespace openzl::gpu {

namespace {

constexpr int kThreads = 256;
constexpr int kVec     = 4; // elements per thread step (uchar4 in, ushort4 out)
constexpr int kUnroll  = 2; // independent uchar4 loads/stream in flight (MLP)

// Reassembles one uchar4 of exponent + one uchar4 of signFrac into a ushort4.
__device__ __forceinline__ ushort4 joinVec4(uchar4 e, uchar4 s)
{
    ushort4 out;
    out.x = decodeBf16Elt(e.x, s.x);
    out.y = decodeBf16Elt(e.y, s.y);
    out.z = decodeBf16Elt(e.z, s.z);
    out.w = decodeBf16Elt(e.w, s.w);
    return out;
}

// Whether every address of `c` is aligned for the vector loads and stores of
// decodeChunkVec.
__device__ __forceinline__ bool isVecAligned(const FloatDeconChunk& c)
{
    const uintptr_t inputs = reinterpret_cast<uintptr_t>(c.exponent)
            | reinterpret_cast<uintptr_t>(c.signFrac);
    return inputs % alignof(uchar4) == 0
            && reinterpret_cast<uintptr_t>(c.dst) % alignof(ushort4) == 0;
}

// Vectorized decode of one chunk: the threads [start, start+stride, ...) cover
// the chunk, each handling kVec elements per step via wide transactions (uchar4
// in, ushort4 out) so both input streams and the output move in wide, fully-
// coalesced transactions. The main loop is unrolled by kUnroll so several
// independent loads from each stream are in flight before the stores, hiding
// DRAM latency (memory-level parallelism). A remainder loop and then a scalar
// tail (last nbElts % kVec) finish the chunk. Requires isVecAligned(c).
__device__ __forceinline__ void
decodeChunkVec(const FloatDeconChunk& c, size_t start, size_t stride)
{
    // Misaligned vector loads and stores are UB on CUDA.
    assert(isVecAligned(c));
    const size_t nVec = c.nbElts / kVec;
    size_t v          = start;
    for (; v + (size_t)(kUnroll - 1) * stride < nVec;
         v += (size_t)kUnroll * stride) {
        uchar4 e[kUnroll];
        uchar4 s[kUnroll];
#pragma unroll
        for (int u = 0; u < kUnroll; ++u) {
            const size_t base = (v + (size_t)u * stride) * kVec;
            e[u] = *reinterpret_cast<const uchar4*>(c.exponent + base);
            s[u] = *reinterpret_cast<const uchar4*>(c.signFrac + base);
        }
#pragma unroll
        for (int u = 0; u < kUnroll; ++u) {
            const size_t base = (v + (size_t)u * stride) * kVec;
            *reinterpret_cast<ushort4*>(c.dst + base) = joinVec4(e[u], s[u]);
        }
    }
    for (; v < nVec; v += stride) {
        const size_t base = v * kVec;
        const uchar4 e    = *reinterpret_cast<const uchar4*>(c.exponent + base);
        const uchar4 s    = *reinterpret_cast<const uchar4*>(c.signFrac + base);
        *reinterpret_cast<ushort4*>(c.dst + base) = joinVec4(e, s);
    }
    for (size_t i = nVec * kVec + start; i < c.nbElts; i += stride) {
        c.dst[i] = decodeBf16Elt(c.exponent[i], c.signFrac[i]);
    }
}

// Element-by-element decode of one chunk, for addresses that decodeChunkVec
// cannot use. Consecutive threads touch consecutive elements, so the accesses
// stay coalesced.
__device__ __forceinline__ void
decodeChunkScalar(const FloatDeconChunk& c, size_t start, size_t stride)
{
    for (size_t i = start; i < c.nbElts; i += stride) {
        c.dst[i] = decodeBf16Elt(c.exponent[i], c.signFrac[i]);
    }
}

// The host has split the input into equal-sized segments (each a contiguous
// run of one chunk); one block owns a segment and grid-strides over the segment
// array, so equal segments balance the work. A 1D grid means numSegs has no
// gridDim.y cap.
__global__ void decodeSegmentsKernel(
        const FloatDeconChunk* __restrict__ segments,
        uint32_t numSegs)
{
    for (uint32_t s = blockIdx.x; s < numSegs; s += gridDim.x) {
        // Every thread of the block reads the same segment, so the whole block
        // takes the same branch.
        const FloatDeconChunk segment = segments[s];
        if (isVecAligned(segment)) {
            decodeChunkVec(segment, threadIdx.x, blockDim.x);
        } else {
            decodeChunkScalar(segment, threadIdx.x, blockDim.x);
        }
    }
}

// Throws unless maxSegElts is a positive multiple of kVec, so that every
// segment of a chunk keeps the alignment of the chunk's start.
void validateMaxSegElts(size_t maxSegElts)
{
    if (maxSegElts == 0 || maxSegElts % kVec != 0) {
        throw std::runtime_error(
                "bf16 decode: maxSegElts " + std::to_string(maxSegElts)
                + " must be a positive multiple of " + std::to_string(kVec));
    }
}

// Sizes a 1D grid that fills the GPU (capped at numSegs) and launches the
// decode kernel over the staged segment array.
void launchSegments(
        const FloatDeconChunk* segs_d,
        uint32_t numSegs,
        cudaStream_t stream)
{
    const uint32_t target =
            fillGpuGrid((const void*)decodeSegmentsKernel, kThreads);
    const uint32_t grid = (uint32_t)std::min<size_t>(numSegs, target);
    ZL_CUDA_CHECK(launchKernel(
            decodeSegmentsKernel, grid, kThreads, 0, stream, segs_d, numSegs));
}

} // namespace

// segAlignElts = kVec keeps every segment start at the alignment of its
// chunk's start.
Bf16DecodePlan::Bf16DecodePlan(
        const FloatDeconChunk* chunks_h,
        uint32_t numInBatch,
        size_t maxSegElts)
        : plan_(chunks_h, numInBatch, maxSegElts, kVec)
{
}

void Bf16DecodePlan::launch(cudaStream_t stream) const
{
    if (plan_.numSegs() == 0) {
        return;
    }
    launchSegments(plan_.deviceSegments(), plan_.numSegs(), stream);
}

void bf16DeconDecode(
        const FloatDeconChunk* chunks_h,
        uint32_t numInBatch,
        size_t maxSegElts,
        cudaStream_t stream)
{
    if (numInBatch == 0) {
        return;
    }
    validateMaxSegElts(maxSegElts);
    const std::vector<FloatDeconChunk> segs =
            rechunk(chunks_h, numInBatch, maxSegElts);
    if (segs.empty()) {
        return;
    }
    const uint32_t numSegs = (uint32_t)segs.size();

    // Stream-ordered scratch so the one-shot path stays async and leak-free.
    StreamScratch<FloatDeconChunk> segs_d(numSegs, stream);
    ZL_CUDA_CHECK(cudaMemcpyAsync(
            segs_d.get(),
            segs.data(),
            (size_t)numSegs * sizeof(FloatDeconChunk),
            cudaMemcpyHostToDevice,
            stream));
    launchSegments(segs_d.get(), numSegs, stream);
}

KernelLaunchInfo bf16DeconDecodeLaunchInfo()
{
    return launchInfoFor((const void*)decodeSegmentsKernel, kThreads);
}

} // namespace openzl::gpu
