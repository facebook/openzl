// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "decode_float_deconstruct_bf16.cuh"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "openzl/dev/contrib/gpu/source/common/cuda_error.cuh"

namespace openzl::gpu {

namespace {

constexpr int kThreads       = 256;
constexpr int kEltsPerThread = 8; // v2 tile = kThreads * kEltsPerThread elts
constexpr int kVec    = 4; // v3 elements per thread (uchar4 in/ushort4 out)
constexpr int kUnroll = 2; // independent uchar4 loads/stream in flight (MLP)

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

// Vectorized decode of one chunk: the threads [start, start+stride, ...) cover
// the chunk, each handling kVec elements per step via wide transactions (uchar4
// in, ushort4 out) so both input streams and the output move in wide, fully-
// coalesced transactions. The main loop is unrolled by kUnroll so several
// independent loads from each stream are in flight before the stores, hiding
// DRAM latency (memory-level parallelism). A remainder loop and then a scalar
// tail (last nbElts % kVec) finish the chunk. Shared by the v3 and unified
// kernels. Requires exponent/signFrac/dst 4-aligned (true for real chunks and
// for unified segments by construction; see bf16DeconDecodeUnified).
__device__ __forceinline__ void
decodeChunkVec(const FloatDeconChunk& c, size_t start, size_t stride)
{
    // The vectorized loads/stores below require these base pointers aligned to
    // their vector types; misalignment is UB on CUDA. Real chunks (cudaMalloc)
    // and unified segments (starts at multiples of maxSegElts % kVec) satisfy
    // it.
    assert(reinterpret_cast<uintptr_t>(c.exponent) % alignof(uchar4) == 0);
    assert(reinterpret_cast<uintptr_t>(c.signFrac) % alignof(uchar4) == 0);
    assert(reinterpret_cast<uintptr_t>(c.dst) % alignof(ushort4) == 0);
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

// v3 decode: same chunk = blockIdx.y mapping as the naive kernel, but
// vectorized via decodeChunkVec. This raises throughput on the single/uniform
// shapes; jagged still starves like the naive kernel, so use v2 there.
__global__ void decodeChunksVecKernel(
        const FloatDeconChunk* __restrict__ chunks,
        uint32_t numInBatch)
{
    const uint32_t b = blockIdx.y;
    if (b >= numInBatch) {
        return;
    }
    const size_t stride = (size_t)gridDim.x * blockDim.x;
    const size_t start  = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
    decodeChunkVec(chunks[b], start, stride);
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

// Unified decode: element-balanced like v2 and vectorized like v3. The host has
// already split the input into equal-sized segments (each a contiguous run of
// one chunk); one block owns a segment and grid-strides over the segment array,
// so equal segments balance the work. A 1D grid means numSegs is not bound by
// the gridDim.y cap the naive/v3 kernels hit.
__global__ void decodeSegmentsKernel(
        const FloatDeconChunk* __restrict__ segments,
        uint32_t numSegs)
{
    for (uint32_t s = blockIdx.x; s < numSegs; s += gridDim.x) {
        decodeChunkVec(segments[s], threadIdx.x, blockDim.x);
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

// Host-side. Peels the first min(n, c.nbElts) elements off c as a new segment
// and advances c past them. Only advances device pointers, so the segment
// aliases the original device buffers, no data is copied.
FloatDeconChunk peel(FloatDeconChunk& c, size_t n)
{
    n                   = std::min(n, c.nbElts);
    FloatDeconChunk seg = c;
    seg.nbElts          = n;
    c.exponent += n;
    c.signFrac += n;
    c.dst += n;
    c.nbElts -= n;
    return seg;
}

// Host-side. Splits each chunk into segments of at most maxSegElts, so all
// segments carry roughly equal work regardless of the input shape.
std::vector<FloatDeconChunk>
rechunk(const FloatDeconChunk* chunks_h, uint32_t numInBatch, size_t maxSegElts)
{
    size_t numSegs = 0;
    for (uint32_t c = 0; c < numInBatch; ++c) {
        const size_t nb = chunks_h[c].nbElts;
        numSegs += (nb + maxSegElts - 1) / maxSegElts;
    }
    if (numSegs > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(
                "bf16 unified decode: segment count " + std::to_string(numSegs)
                + " exceeds uint32_t limit; use a larger maxSegElts");
    }
    std::vector<FloatDeconChunk> out;
    out.reserve(numSegs);
    for (uint32_t c = 0; c < numInBatch; ++c) {
        FloatDeconChunk cur = chunks_h[c];
        while (cur.nbElts > 0) {
            out.push_back(peel(cur, maxSegElts));
        }
    }
    return out;
}

// Stream-ordered device scratch: frees on the same stream at scope exit, so an
// exception between allocation and launch cannot leak it.
template <typename T>
class StreamScratch {
   public:
    StreamScratch(size_t n, cudaStream_t stream) : stream_(stream)
    {
        ZL_CUDA_CHECK(cudaMallocAsync(&p_, n * sizeof(T), stream));
    }
    ~StreamScratch()
    {
        if (p_) {
            cudaFreeAsync(p_, stream_);
        }
    }
    StreamScratch(const StreamScratch&)            = delete;
    StreamScratch& operator=(const StreamScratch&) = delete;

    T* get() const
    {
        return p_;
    }

   private:
    T* p_ = nullptr;
    cudaStream_t stream_;
};

// Throws unless maxSegElts is a positive multiple of kVec (the alignment
// invariant the vectorized loads/stores rely on).
void validateMaxSegElts(size_t maxSegElts)
{
    if (maxSegElts == 0 || maxSegElts % kVec != 0) {
        throw std::runtime_error(
                "bf16 unified decode: maxSegElts " + std::to_string(maxSegElts)
                + " must be a positive multiple of " + std::to_string(kVec));
    }
}

// Sizes a 1D grid that fills the GPU (capped at numSegs) and launches the
// unified kernel over the staged segment array.
void launchSegments(
        const FloatDeconChunk* segs_d,
        uint32_t numSegs,
        cudaStream_t stream)
{
    const uint32_t target = fillGpuGrid((const void*)decodeSegmentsKernel);
    const uint32_t grid   = (uint32_t)std::min<size_t>(numSegs, target);
    decodeSegmentsKernel<<<grid, kThreads, 0, stream>>>(segs_d, numSegs);
    ZL_CUDA_CHECK_LAST();
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
    const uint32_t target = fillGpuGrid((const void*)decodeChunksVecKernel);
    const uint32_t blocksPerChunk = (target + numInBatch - 1) / numInBatch;
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

UnifiedDecodePlan::UnifiedDecodePlan(
        const FloatDeconChunk* chunks_h,
        uint32_t numInBatch,
        size_t maxSegElts)
{
    if (numInBatch == 0) {
        return;
    }
    validateMaxSegElts(maxSegElts);

    // Split on the host into equal-sized segments (device pointers advanced, no
    // copy) and stage the segment descriptors once. Segment starts land at
    // multiples of maxSegElts from cudaMalloc-aligned bases, so with maxSegElts
    // % kVec == 0 every segment start is 4/8-aligned for the vectorized
    // loads/stores.
    const std::vector<FloatDeconChunk> segs =
            rechunk(chunks_h, numInBatch, maxSegElts);
    if (segs.empty()) {
        return;
    }
    numSegs_ = (uint32_t)segs.size();
    segs_d_  = deviceAlloc<FloatDeconChunk>(numSegs_);
    ZL_CUDA_CHECK(cudaMemcpy(
            segs_d_.get(),
            segs.data(),
            (size_t)numSegs_ * sizeof(FloatDeconChunk),
            cudaMemcpyHostToDevice));
}

void UnifiedDecodePlan::launch(cudaStream_t stream) const
{
    if (numSegs_ == 0) {
        return;
    }
    launchSegments(segs_d_.get(), numSegs_, stream);
}

void bf16DeconDecodeUnified(
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

KernelLaunchInfo bf16DeconDecodeUnifiedLaunchInfo()
{
    int maxActiveBlocksPerSM = 0;
    ZL_CUDA_CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(
            &maxActiveBlocksPerSM,
            (const void*)decodeSegmentsKernel,
            kThreads,
            0));
    return { kThreads, maxActiveBlocksPerSM };
}

} // namespace openzl::gpu
