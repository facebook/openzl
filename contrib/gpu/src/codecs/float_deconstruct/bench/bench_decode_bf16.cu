// Copyright (c) Meta Platforms, Inc. and affiliates.

// Benchmark for the bf16 float_deconstruct GPU decode kernel.
//
// Uses the kernel-agnostic driver in bench/gpu_bench.cuh: for each workload
// shape we stage the chunks on the device (DeviceChunkSet), then benchmark a
// list of KernelCase variants (currently just the one production kernel) and
// report latency, throughput, %peak, and occupancy. The `cases` list is the
// extension point for comparing future kernel versions against each other. Each
// case verifies itself before timing, and a single-threaded CPU baseline is
// printed for reference. A device-to-device copy (PeakBandwidthCase) is run
// once up front to sanity-check the reported %peak.
//
// Decode is branchless, so input *values* do not affect timing (only sizes) --
// synthetic random bytes are sufficient here.

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "contrib/gpu/src/bench/gpu_bench.cuh"
#include "contrib/gpu/src/codecs/float_deconstruct/decode_float_deconstruct_bf16.cuh"
#include "contrib/gpu/src/codecs/float_deconstruct/gpu_chunk_harness.cuh"
#include "contrib/gpu/src/common/cuda_error.cuh"

// Forward-declared instead of including its header, which pulls in SIMD
// intrinsics nvcc cannot compile; linked from //openzl/dev:zstronglib.
extern "C" void FLTDECON_bfloat16_deconstruct_decode(
        uint16_t* dst16,
        const uint8_t* exponent,
        const uint8_t* signFrac,
        size_t nbElts);

using openzl::gpu::DeviceChunkSet;
using openzl::gpu::OwnedHostChunk;
using openzl::gpu::toHostChunks;
using openzl::gpu::bench::KernelCase;
using openzl::gpu::bench::PeakBandwidthCase;
using openzl::gpu::bench::printResults;
using openzl::gpu::bench::runKernelBench;
using openzl::gpu::bench::Workload;

namespace {

constexpr unsigned kRngSeed        = 1234;
constexpr size_t kBytesPerElt      = 4;
constexpr size_t kMinBf16ChunkElts = 16384;
constexpr size_t kSanityCopyBytes  = 256ull * 1024 * 1024;
constexpr size_t kBatchedSweepElts = 32ull * 1024 * 1024;

// Host-side random source bytes for one set of chunks; kept for the CPU
// baseline and for verification.
struct HostData {
    size_t totalElts = 0;
    std::vector<OwnedHostChunk> chunks;
};

HostData makeHostData(const std::vector<size_t>& sizes, std::mt19937& rng)
{
    HostData d;
    d.chunks.resize(sizes.size());
    std::uniform_int_distribution<int> byteDist(0, 255);
    for (size_t c = 0; c < sizes.size(); ++c) {
        const size_t nb = sizes[c];
        d.totalElts += nb;
        d.chunks[c].exponent.resize(nb);
        d.chunks[c].signFrac.resize(nb);
        for (size_t i = 0; i < nb; ++i) {
            d.chunks[c].exponent[i] = (uint8_t)byteDist(rng);
            d.chunks[c].signFrac[i] = (uint8_t)byteDist(rng);
        }
    }
    return d;
}

// Single-threaded CPU reference decode, timed with the monotonic clock.
double cpuDecodeMs(const HostData& hd)
{
    std::vector<std::vector<uint16_t>> out(hd.chunks.size());
    for (size_t c = 0; c < hd.chunks.size(); ++c) {
        out[c].resize(hd.chunks[c].exponent.size());
    }
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (size_t c = 0; c < hd.chunks.size(); ++c) {
        FLTDECON_bfloat16_deconstruct_decode(
                out[c].data(),
                hd.chunks[c].exponent.data(),
                hd.chunks[c].signFrac.data(),
                hd.chunks[c].exponent.size());
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) * 1e-6;
}

// Copies each chunk's dst back and checks it against the scalar golden.
// Returns the number of mismatching elements (0 == correct).
size_t countMismatches(const HostData& hd, const DeviceChunkSet& dev)
{
    size_t bad = 0;
    for (uint32_t c = 0; c < dev.numInBatch(); ++c) {
        const OwnedHostChunk& chunk     = hd.chunks[c];
        const size_t nb                 = chunk.exponent.size();
        const std::vector<uint16_t> out = dev.download(c);
        std::vector<uint16_t> want(nb);
        FLTDECON_bfloat16_deconstruct_decode(
                want.data(), chunk.exponent.data(), chunk.signFrac.data(), nb);
        for (size_t i = 0; i < nb; ++i) {
            bad += (out[i] != want[i]);
        }
    }
    return bad;
}

// One decode-kernel variant over an already-staged DeviceChunkSet. `launch`
// selects the kernel version; verify() compares the device output against the
// scalar golden. No setup() override: all variants share the one DeviceChunkSet
// fixture staged once per shape, so a case has no private device state to set
// up (unlike PeakBandwidthCase, which owns its buffers).
class Bf16DecodeCase : public KernelCase {
   public:
    Bf16DecodeCase(
            std::string name,
            std::function<void(cudaStream_t)> launch,
            openzl::gpu::KernelLaunchInfo info,
            const HostData& hd,
            const DeviceChunkSet& dev)
            : name_(std::move(name)),
              launch_(std::move(launch)),
              info_(info),
              hd_(hd),
              dev_(dev)
    {
    }

    std::string name() const override
    {
        return name_;
    }
    void launch(cudaStream_t stream) override
    {
        launch_(stream);
    }
    Workload workload() const override
    {
        return { kBytesPerElt * hd_.totalElts, hd_.totalElts };
    }
    bool verify() override
    {
        return countMismatches(hd_, dev_) == 0;
    }
    int blockSize() const override
    {
        return info_.blockSize;
    }
    int maxActiveBlocksPerSM() const override
    {
        return info_.maxActiveBlocksPerSM;
    }

   private:
    std::string name_;
    std::function<void(cudaStream_t)> launch_;
    openzl::gpu::KernelLaunchInfo info_;
    const HostData& hd_;
    const DeviceChunkSet& dev_;
};

// Unified-kernel case. Builds the segment plan once in setup() (host split +
// descriptor upload) so only the kernel launch is timed, then launches it per
// iteration. Shares the DeviceChunkSet fixture like Bf16DecodeCase.
class UnifiedDecodeCase : public KernelCase {
   public:
    UnifiedDecodeCase(
            std::string name,
            size_t maxSegElts,
            const HostData& hd,
            const DeviceChunkSet& dev)
            : name_(std::move(name)),
              maxSegElts_(maxSegElts),
              hd_(hd),
              dev_(dev)
    {
    }

    std::string name() const override
    {
        return name_;
    }
    void setup() override
    {
        plan_ = std::make_unique<openzl::gpu::UnifiedDecodePlan>(
                dev_.hostChunks().data(), dev_.numInBatch(), maxSegElts_);
    }
    void launch(cudaStream_t stream) override
    {
        plan_->launch(stream);
    }
    Workload workload() const override
    {
        return { kBytesPerElt * hd_.totalElts, hd_.totalElts };
    }
    bool verify() override
    {
        return countMismatches(hd_, dev_) == 0;
    }
    int blockSize() const override
    {
        return openzl::gpu::bf16DeconDecodeUnifiedLaunchInfo().blockSize;
    }
    int maxActiveBlocksPerSM() const override
    {
        return openzl::gpu::bf16DeconDecodeUnifiedLaunchInfo()
                .maxActiveBlocksPerSM;
    }

   private:
    std::string name_;
    size_t maxSegElts_;
    const HostData& hd_;
    const DeviceChunkSet& dev_;
    std::unique_ptr<openzl::gpu::UnifiedDecodePlan> plan_;
};

std::vector<size_t> jaggedShape(
        std::mt19937& rng,
        size_t bigElts,
        int nSmall,
        size_t minElts,
        size_t spread)
{
    std::vector<size_t> v{ bigElts };
    std::uniform_int_distribution<size_t> offsetDist(0, spread - 1);
    for (int i = 0; i < nSmall; ++i) {
        v.push_back(minElts + offsetDist(rng));
    }
    return v;
}

void runShape(
        const char* name,
        const std::vector<size_t>& sizes,
        std::mt19937& rng)
{
    const HostData hd = makeHostData(sizes, rng);
    DeviceChunkSet dev(toHostChunks(hd.chunks));

    // One KernelCase per kernel version under test; add more to compare.
    std::vector<std::unique_ptr<KernelCase>> cases;
    cases.push_back(
            std::make_unique<Bf16DecodeCase>(
                    "naive",
                    [&dev](cudaStream_t s) {
                        openzl::gpu::bf16DeconDecode(
                                dev.numInBatch(), dev.deviceChunks(), s);
                    },
                    openzl::gpu::bf16DeconDecodeLaunchInfo(),
                    hd,
                    dev));
    cases.push_back(
            std::make_unique<Bf16DecodeCase>(
                    "tiled",
                    [&dev](cudaStream_t s) {
                        openzl::gpu::bf16DeconDecodeV2(
                                dev.numInBatch(), dev.deviceChunks(), s);
                    },
                    openzl::gpu::bf16DeconDecodeV2LaunchInfo(),
                    hd,
                    dev));
    cases.push_back(
            std::make_unique<Bf16DecodeCase>(
                    "vec",
                    [&dev](cudaStream_t s) {
                        openzl::gpu::bf16DeconDecodeVec(
                                dev.numInBatch(), dev.deviceChunks(), s);
                    },
                    openzl::gpu::bf16DeconDecodeVecLaunchInfo(),
                    hd,
                    dev));
    // Unified kernel at the production default segment size: element-balanced
    // and vectorized, so it tracks the best specialist on every shape. The plan
    // is built in setup(), so only the kernel launch is timed (the host peel
    // and descriptor upload are one-time and excluded).
    cases.push_back(
            std::make_unique<UnifiedDecodeCase>(
                    "unified",
                    openzl::gpu::kUnifiedDefaultMaxSegElts,
                    hd,
                    dev));

    const std::vector<openzl::gpu::bench::BenchResult> results =
            runKernelBench(cases);

    const double cpuMs = cpuDecodeMs(hd);
    printf("[%-10s] chunks=%-6u totalElts=%-9zu  CPU %8.2f ms\n",
           name,
           dev.numInBatch(),
           hd.totalElts,
           cpuMs);
    printResults(name, results);
}

} // namespace

int main()
{
    int dev = 0;
    ZL_CUDA_CHECK(cudaGetDevice(&dev));
    cudaDeviceProp prop;
    ZL_CUDA_CHECK(cudaGetDeviceProperties(&prop, dev));
    printf("GPU: %s | SMs: %d | peak HBM ~ %.0f GB/s\n\n",
           prop.name,
           prop.multiProcessorCount,
           openzl::gpu::bench::peakBandwidthGBs(dev));

    std::mt19937 rng(kRngSeed);

    // Sanity check: a plain device-to-device copy should run near peak HBM, so
    // its %peak validates the bandwidth model before we trust the decode rows.
    {
        std::vector<std::unique_ptr<KernelCase>> sanity;
        sanity.push_back(std::make_unique<PeakBandwidthCase>(kSanityCopyBytes));
        printResults("sanity", runKernelBench(sanity));
        printf("\n");
    }

    // One big 64Mi-element chunk.
    runShape("oneLarge", { 64ull * 1024 * 1024 }, rng);

    // Batched chunk-size sweep: same total elements, varying per-chunk size, to
    // show how finely the work can be partitioned before throughput drops. bf16
    // input is 1 byte/elt, so an N-KiB chunk is N * 1024 elements. 32Mi total
    // keeps the 1KiB case at 32768 chunks, under the kMaxNumInBatch cap that a
    // 64Mi total would exceed.
    constexpr size_t kBatchedChunkKiB[] = { 1, 4, 32, 128, 512 };
    for (const size_t chunkKiB : kBatchedChunkKiB) {
        const size_t chunkElts = chunkKiB * 1024;
        const std::string name = "batched-" + std::to_string(chunkKiB) + "KiB";
        std::vector<size_t> v(kBatchedSweepElts / chunkElts, chunkElts);
        runShape(name.c_str(), v, rng);
    }

    // One big chunk plus many mid-size chunks, like a real uneven frame. Every
    // small chunk stays at or above the smallest real chunk size (16384 elts).
    runShape(
            "jagged",
            jaggedShape(
                    rng,
                    32ull * 1024 * 1024,
                    200,
                    kMinBf16ChunkElts,
                    256 * 1024),
            rng);

    // One big chunk plus thousands of tiny chunks. This cannot happen in a real
    // frame; it stresses the many-chunk path.
    runShape(
            "jaggedTiny",
            jaggedShape(rng, 32ull * 1024 * 1024, 4000, 1, 8),
            rng);

    return 0;
}
