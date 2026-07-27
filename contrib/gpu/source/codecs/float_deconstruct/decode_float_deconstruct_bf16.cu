// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "decode_float_deconstruct_bf16.cuh"

#include <stdexcept>
#include <string>

#include "openzl/dev/contrib/gpu/source/common/cuda_error.cuh"

namespace openzl::gpu {

namespace {

constexpr int kThreads       = 256;
constexpr int kEltsPerThread = 8; // v2 tile = kThreads * kEltsPerThread elts
constexpr int kVec = 4; // v3 elements per thread (uchar4 in/ushort4 out)

// chunk = blockIdx.y; each block's threads grid-stride over that chunk along x.
// One chunk is just numInBatch == 1, so this handles single and multi chunk.
// gridDim.y caps numInBatch at 65535.
__global__ void decodeChunksKernel(
        const FloatDeconChunk* __restrict__ chunks,
        uint32_t numInBatch)
{
    const uint32_t b = blockIdx.y;
    if (b >= numInBatch) {
        return;
    }
    const FloatDeconChunk c = chunks[b];
    const size_t stride     = (size_t)gridDim.x * blockDim.x;
    for (size_t i = (size_t)blockIdx.x * blockDim.x + threadIdx.x; i < c.nbElts;
         i += stride) {
        c.dst[i] = decodeBf16Elt(c.exponent[i], c.signFrac[i]);
    }
}

// v3 decode: same chunk = blockIdx.y mapping as the naive kernel, but each
// thread processes kVec elements with vectorized transactions (uchar4 in,
// ushort4 out) instead of one byte at a time, so both input streams and the
// output move in wide, fully-coalesced transactions. A scalar tail handles the
// last nbElts % kVec elements. This raises throughput on the single/uniform
// shapes; jagged still starves like the naive kernel, so use v2 there.
__global__ void decodeChunksVecKernel(
        const FloatDeconChunk* __restrict__ chunks,
        uint32_t numInBatch)
{
    const uint32_t b = blockIdx.y;
    if (b >= numInBatch) {
        return;
    }
    const FloatDeconChunk c = chunks[b];
    const size_t nVec       = c.nbElts / kVec;
    const size_t stride     = (size_t)gridDim.x * blockDim.x;
    const size_t start      = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
    for (size_t v = start; v < nVec; v += stride) {
        const size_t base = v * kVec;
        const uchar4 e    = *reinterpret_cast<const uchar4*>(c.exponent + base);
        const uchar4 s    = *reinterpret_cast<const uchar4*>(c.signFrac + base);
        ushort4 out;
        out.x                                     = decodeBf16Elt(e.x, s.x);
        out.y                                     = decodeBf16Elt(e.y, s.y);
        out.z                                     = decodeBf16Elt(e.z, s.z);
        out.w                                     = decodeBf16Elt(e.w, s.w);
        *reinterpret_cast<ushort4*>(c.dst + base) = out;
    }
    // Scalar tail: the last nbElts % kVec elements.
    for (size_t i = nVec * kVec + start; i < c.nbElts; i += stride) {
        c.dst[i] = decodeBf16Elt(c.exponent[i], c.signFrac[i]);
    }
}

// Largest c in [0, numInBatch) with offsets[c] <= g (upper_bound - 1).
__device__ __forceinline__ uint32_t
findChunk(const size_t* __restrict__ offsets, uint32_t numInBatch, size_t g)
{
    uint32_t lo = 0;
    uint32_t hi = numInBatch;
    while (lo + 1 < hi) {
        const uint32_t mid = (lo + hi) >> 1;
        if (offsets[mid] <= g) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

// Exclusive prefix sum of per-chunk element counts into offsets[0..numInBatch].
// Single thread: numInBatch is small metadata, negligible next to the decode.
__global__ void computeOffsetsKernel(
        const FloatDeconChunk* __restrict__ chunks,
        uint32_t numInBatch,
        size_t* __restrict__ offsets)
{
    if (blockIdx.x != 0 || threadIdx.x != 0) {
        return;
    }
    size_t acc = 0;
    for (uint32_t c = 0; c < numInBatch; ++c) {
        offsets[c] = acc;
        acc += chunks[c].nbElts;
    }
    offsets[numInBatch] = acc;
}

// v2 decode. Why it beats the naive kernel on jagged input: the naive kernel
// pins one chunk to each blockIdx.y row, so a dominant chunk is stuck on a
// fixed block count while tiny chunks waste theirs. Here we flatten all chunks
// into one element space (offsets = exclusive prefix sum of nbElts) and hand
// fixed-size tiles of that space to blocks, grid-strided. Work is distributed
// by total element count, not by chunk count, so a huge chunk automatically
// gets proportionally many tiles and tiny chunks get few. Each tile does ONE
// binary search over offsets for its first chunk; threads then advance the
// chunk index linearly as they cross a boundary inside the tile. Real chunks
// (>= ZL_MIN_CHUNK_SIZE) dwarf a tile, so a tile spans at most one boundary and
// the search is O(log numInBatch) once per tile, negligible next to the memory
// traffic.
__global__ void decodeTiledKernel(
        const FloatDeconChunk* __restrict__ chunks,
        const size_t* __restrict__ offsets,
        uint32_t numInBatch)
{
    const size_t total    = offsets[numInBatch];
    const size_t tile     = (size_t)blockDim.x * kEltsPerThread;
    const size_t tileStep = (size_t)gridDim.x * tile;
    for (size_t tileStart = (size_t)blockIdx.x * tile; tileStart < total;
         tileStart += tileStep) {
        const size_t tileEnd = min(tileStart + tile, total);
        const uint32_t base  = findChunk(offsets, numInBatch, tileStart);
        for (size_t g = tileStart + threadIdx.x; g < tileEnd; g += blockDim.x) {
            uint32_t c = base;
            while (c + 1 < numInBatch && g >= offsets[c + 1]) {
                ++c;
            }
            const FloatDeconChunk d = chunks[c];
            const size_t i          = g - offsets[c];
            d.dst[i] = decodeBf16Elt(d.exponent[i], d.signFrac[i]);
        }
    }
}

// 1D grid size that fills the GPU for a given kernel at kThreads.
uint32_t fillGpuGrid(const void* kernel)
{
    int maxBlocksPerSM = 0;
    ZL_CUDA_CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(
            &maxBlocksPerSM, kernel, kThreads, 0));
    int dev    = 0;
    int numSMs = 0;
    ZL_CUDA_CHECK(cudaGetDevice(&dev));
    ZL_CUDA_CHECK(cudaDeviceGetAttribute(
            &numSMs, cudaDevAttrMultiProcessorCount, dev));
    const uint32_t grid = (uint32_t)maxBlocksPerSM * (uint32_t)numSMs;
    return grid == 0 ? 1 : grid;
}

} // namespace

void bf16DeconDecode(
        uint32_t numInBatch,
        const FloatDeconChunk* chunks_d,
        cudaStream_t stream)
{
    if (numInBatch == 0) {
        return;
    }
    if (numInBatch > kMaxNumInBatch) {
        throw std::runtime_error(
                "bf16DeconDecode: numInBatch " + std::to_string(numInBatch)
                + " exceeds kMaxNumInBatch " + std::to_string(kMaxNumInBatch)
                + " (gridDim.y limit); split the batch across calls");
    }
    // Oversubscribe x so blocksPerChunk * numInBatch roughly fills the GPU;
    // grid-stride in x then covers any per-chunk size.
    const uint32_t target = fillGpuGrid((const void*)decodeChunksKernel);
    const uint32_t blocksPerChunk = (target + numInBatch - 1) / numInBatch;
    const dim3 grid(blocksPerChunk, numInBatch);
    decodeChunksKernel<<<grid, kThreads, 0, stream>>>(chunks_d, numInBatch);
    ZL_CUDA_CHECK_LAST();
}

KernelLaunchInfo bf16DeconDecodeLaunchInfo()
{
    int maxActiveBlocksPerSM = 0;
    ZL_CUDA_CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(
            &maxActiveBlocksPerSM,
            (const void*)decodeChunksKernel,
            kThreads,
            0));
    return { kThreads, maxActiveBlocksPerSM };
}

void bf16DeconDecodeV2(
        uint32_t numInBatch,
        const FloatDeconChunk* chunks_d,
        cudaStream_t stream)
{
    if (numInBatch == 0) {
        return;
    }
    // Stream-ordered scratch for the flat-space offsets prefix sum.
    size_t* offsets = nullptr;
    ZL_CUDA_CHECK(cudaMallocAsync(
            &offsets, (size_t)(numInBatch + 1) * sizeof(size_t), stream));
    computeOffsetsKernel<<<1, 1, 0, stream>>>(chunks_d, numInBatch, offsets);
    ZL_CUDA_CHECK_LAST();

    const uint32_t grid = fillGpuGrid((const void*)decodeTiledKernel);
    decodeTiledKernel<<<grid, kThreads, 0, stream>>>(
            chunks_d, offsets, numInBatch);
    ZL_CUDA_CHECK_LAST();
    ZL_CUDA_CHECK(cudaFreeAsync(offsets, stream));
}

KernelLaunchInfo bf16DeconDecodeV2LaunchInfo()
{
    int maxActiveBlocksPerSM = 0;
    ZL_CUDA_CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(
            &maxActiveBlocksPerSM,
            (const void*)decodeTiledKernel,
            kThreads,
            0));
    return { kThreads, maxActiveBlocksPerSM };
}

void bf16DeconDecodeVec(
        uint32_t numInBatch,
        const FloatDeconChunk* chunks_d,
        cudaStream_t stream)
{
    if (numInBatch == 0) {
        return;
    }
    if (numInBatch > kMaxNumInBatch) {
        throw std::runtime_error(
                "bf16DeconDecodeVec: numInBatch " + std::to_string(numInBatch)
                + " exceeds kMaxNumInBatch " + std::to_string(kMaxNumInBatch)
                + " (gridDim.y limit); split the batch across calls");
    }
    const uint32_t target   = fillGpuGrid((const void*)decodeChunksVecKernel);
    uint32_t blocksPerChunk = (target + numInBatch - 1) / numInBatch;
    if (blocksPerChunk == 0) {
        blocksPerChunk = 1;
    }
    const dim3 grid(blocksPerChunk, numInBatch);
    decodeChunksVecKernel<<<grid, kThreads, 0, stream>>>(chunks_d, numInBatch);
    ZL_CUDA_CHECK_LAST();
}

KernelLaunchInfo bf16DeconDecodeVecLaunchInfo()
{
    int maxActiveBlocksPerSM = 0;
    ZL_CUDA_CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(
            &maxActiveBlocksPerSM,
            (const void*)decodeChunksVecKernel,
            kThreads,
            0));
    return { kThreads, maxActiveBlocksPerSM };
}

} // namespace openzl::gpu
