// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "openzl/dev/contrib/gpu/source/common/cuda_error.cuh"
#include "openzl/dev/contrib/gpu/source/common/cuda_raii.cuh"

// Kernel-agnostic GPU decompression benchmark driver: time a list of kernel
// variants over their own on-device workload and report execution time,
// throughput, and theoretical occupancy so multiple versions can be compared
// directly. Each variant is a KernelCase that stages its own data in setup()
// and moves it in launch(). Codec-agnostic: no dependency on any codec header.

namespace openzl::gpu::bench {

// Internal constants: SI unit conversions, the HBM peak-bandwidth model, and
// default sampling knobs
namespace detail {
constexpr double kGiga    = 1e9;
constexpr double kKilo    = 1e3;
constexpr double kPercent = 100.0;

constexpr double kMemTransfersPerClock = 2.0; // DDR: two transfers per clock
constexpr int kBitsPerByte             = 8;
constexpr double kFallbackPeakGBs =
        2039.0; // A100 80GB, if clock/bus unavailable

constexpr int kDefaultWarmup          = 3;
constexpr int kDefaultIters           = 20;
constexpr size_t kDefaultL2FlushBytes = 64ull * 1024 * 1024;
} // namespace detail

// Workload size, used to turn a time into throughput numbers
struct Workload {
    size_t bytesMoved; // bytes read+written per run, for effective bandwidth
    size_t numElts;    // decoded elements per run, for G-elem/s
};

// One kernel variant to benchmark. A subclass stages its data in setup() (run
// once before timing) and moves it in launch() over the given stream;
// workload() reports the bytes/elements one launch touches. Optionally override
// verify() to check correctness (a wrong-but-fast variant is flagged, not timed
// away) and the occupancy hints (compute maxActiveBlocksPerSM in the kernel's
// own translation unit).
class KernelCase {
   public:
    virtual ~KernelCase() = default;

    virtual std::string name() const         = 0;
    virtual void launch(cudaStream_t stream) = 0;
    virtual Workload workload() const        = 0;

    virtual void setup() {}
    virtual bool verify()
    {
        return true;
    }
    virtual int blockSize() const
    {
        return 0;
    }
    virtual int maxActiveBlocksPerSM() const
    {
        return 0;
    }
};

struct BenchConfig {
    int warmup          = detail::kDefaultWarmup;
    int iters           = detail::kDefaultIters;
    size_t l2FlushBytes = detail::kDefaultL2FlushBytes;
};

struct BenchResult {
    std::string name;
    bool correct        = true;
    double medianMs     = 0.0;
    double gbps         = 0.0;
    double pctPeak      = 0.0;
    double gElemPerS    = 0.0;
    double occupancyPct = 0.0; // 0 if occupancy inputs not provided
    int maxActiveBlocks = 0;   // per SM; 0 if not provided
};

// Theoretical peak HBM bandwidth (GB/s) from device memory clock + bus width
inline double peakBandwidthGBs(int dev)
{
    int memClockKHz = 0, busWidthBits = 0;
    cudaDeviceGetAttribute(&memClockKHz, cudaDevAttrMemoryClockRate, dev);
    cudaDeviceGetAttribute(&busWidthBits, cudaDevAttrGlobalMemoryBusWidth, dev);
    if (memClockKHz <= 0 || busWidthBits <= 0) {
        return detail::kFallbackPeakGBs;
    }
    return detail::kMemTransfersPerClock * (double)memClockKHz * detail::kKilo
            * ((double)busWidthBits / detail::kBitsPerByte) / detail::kGiga;
}

// Theoretical occupancy (% of max warps per SM) from a kernel's block size and
// its max active blocks per SM (computed in-TU by the kernel owner)
inline double occupancyPct(int blockSize, int maxActiveBlocksPerSM)
{
    int dev = 0;
    cudaGetDevice(&dev);
    int maxThreadsPerSM = 0;
    cudaDeviceGetAttribute(
            &maxThreadsPerSM, cudaDevAttrMaxThreadsPerMultiProcessor, dev);
    if (maxThreadsPerSM <= 0) {
        return 0.0;
    }
    return detail::kPercent * (double)maxActiveBlocksPerSM * (double)blockSize
            / (double)maxThreadsPerSM;
}

// Times one kernel: warmup, then `iters` timed launches each preceded by an L2
// flush (so reads hit HBM), taking the median. Returns 0 if timing is disabled.
inline double medianKernelMs(KernelCase& kc, const BenchConfig& cfg)
{
    if (cfg.iters <= 0) {
        return 0.0;
    }
    const cudaStream_t stream = 0;

    uint8_t* flushRaw = nullptr;
    ZL_CUDA_CHECK(cudaMalloc(&flushRaw, cfg.l2FlushBytes));
    std::unique_ptr<uint8_t, CudaFreeDeleter> flush(flushRaw);

    for (int i = 0; i < cfg.warmup; ++i) {
        kc.launch(stream);
    }
    ZL_CUDA_CHECK(cudaGetLastError());
    ZL_CUDA_CHECK(cudaDeviceSynchronize());

    std::vector<CudaEvent> starts(cfg.iters), stops(cfg.iters);
    for (int i = 0; i < cfg.iters; ++i) {
        ZL_CUDA_CHECK(
                cudaMemsetAsync(flush.get(), 0, cfg.l2FlushBytes, stream));
        starts[i].record(stream);
        kc.launch(stream);
        stops[i].record(stream);
    }
    ZL_CUDA_CHECK(cudaDeviceSynchronize());

    std::vector<double> ms(cfg.iters);
    for (int i = 0; i < cfg.iters; ++i) {
        ms[i] = stops[i].elapsedMsSince(starts[i]);
    }
    std::sort(ms.begin(), ms.end());
    return ms[cfg.iters / 2];
}

// Runs every case over its own workload; one result per case. Each case is set
// up once, verified once, then timed. Throughput is zeroed (not inf/NaN) when
// timing is disabled or the peak model is unavailable.
inline std::vector<BenchResult> runKernelBench(
        const std::vector<std::unique_ptr<KernelCase>>& cases,
        BenchConfig cfg = {})
{
    int dev = 0;
    ZL_CUDA_CHECK(cudaGetDevice(&dev));
    const double peak = peakBandwidthGBs(dev);

    std::vector<BenchResult> out;
    out.reserve(cases.size());
    for (const std::unique_ptr<KernelCase>& kc : cases) {
        kc->setup();

        // Correctness once (one launch populates the outputs), then time.
        kc->launch(0);
        ZL_CUDA_CHECK_LAST();
        ZL_CUDA_CHECK(cudaDeviceSynchronize());

        const Workload work = kc->workload();
        BenchResult r;
        r.name         = kc->name();
        r.correct      = kc->verify();
        r.medianMs     = medianKernelMs(*kc, cfg);
        const double s = r.medianMs / detail::kKilo;
        r.gbps = s > 0.0 ? (double)work.bytesMoved / s / detail::kGiga : 0.0;
        r.gElemPerS = s > 0.0 ? (double)work.numElts / s / detail::kGiga : 0.0;
        r.pctPeak   = peak > 0.0 ? detail::kPercent * r.gbps / peak : 0.0;
        if (kc->blockSize() > 0 && kc->maxActiveBlocksPerSM() > 0) {
            r.maxActiveBlocks = kc->maxActiveBlocksPerSM();
            r.occupancyPct =
                    occupancyPct(kc->blockSize(), kc->maxActiveBlocksPerSM());
        }
        out.push_back(std::move(r));
    }
    return out;
}

// Prints one row per case, labelled with `label` (e.g. the workload shape)
inline void printResults(const char* label, const std::vector<BenchResult>& rs)
{
    for (const BenchResult& r : rs) {
        printf("%-10s | %-18s | %s | %8.3f ms | %7.1f GB/s (%5.1f%% peak) |"
               " %6.1f G-elem/s | occ %5.1f%% (%d blk/SM)\n",
               label,
               r.name.c_str(),
               r.correct ? "OK  " : "FAIL",
               r.medianMs,
               r.gbps,
               r.pctPeak,
               r.gElemPerS,
               r.occupancyPct,
               r.maxActiveBlocks);
    }
}

// Sanity case: a plain device-to-device copy of `bytes` bytes, which should run
// near peak HBM bandwidth. Use it to validate that the reported GB/s and %peak
// are sane before trusting a kernel's numbers. Moves 2x bytes (read + write).
class PeakBandwidthCase : public KernelCase {
   public:
    explicit PeakBandwidthCase(size_t bytes) : bytes_(bytes) {}

    std::string name() const override
    {
        return "peakBW(copy)";
    }

    void setup() override
    {
        src_ = deviceAlloc<uint8_t>(bytes_);
        dst_ = deviceAlloc<uint8_t>(bytes_);
        ZL_CUDA_CHECK(cudaMemset(src_.get(), 0, bytes_));
    }

    void launch(cudaStream_t stream) override
    {
        ZL_CUDA_CHECK(cudaMemcpyAsync(
                dst_.get(),
                src_.get(),
                bytes_,
                cudaMemcpyDeviceToDevice,
                stream));
    }

    Workload workload() const override
    {
        return { 2 * bytes_, 0 };
    }

   private:
    size_t bytes_;
    DevicePtr<uint8_t> src_;
    DevicePtr<uint8_t> dst_;
};

} // namespace openzl::gpu::bench
