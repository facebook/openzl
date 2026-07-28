// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <cuda_runtime.h>

#include "contrib/gpu/src/common/cuda_error.cuh"
#include "contrib/gpu/src/common/cuda_raii.cuh"

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

constexpr int kDefaultWarmup         = 3;
constexpr int kDefaultMinSamples     = 10;
constexpr int kDefaultMaxSamples     = 200;
constexpr double kDefaultMaxSeconds  = 0.5;
constexpr double kDefaultTargetNoise = 0.005; // relative stddev of the mean

// Flush buffer is sized to 1.5x the device L2 so reads miss cache; this is the
// floor used when the L2 size is unavailable.
constexpr size_t kMinL2FlushBytes = 64ull * 1024 * 1024;

// Population mean and relative stddev of the mean ("noise") from the running
// sums of n timing samples. noise is a fraction; callers scale to percent.
struct NoiseStats {
    double mean  = 0.0;
    double noise = 0.0;
};
inline NoiseStats computeNoise(double sum, double sumSq, int n)
{
    const double mean   = sum / n;
    const double var    = sumSq / n - mean * mean;
    const double stddev = var > 0.0 ? std::sqrt(var) : 0.0;
    const double noise =
            mean > 0.0 ? stddev / std::sqrt((double)n) / mean : 0.0;
    return { mean, noise };
}
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
    int minSamples      = detail::kDefaultMinSamples;
    int maxSamples      = detail::kDefaultMaxSamples;
    double maxSeconds   = detail::kDefaultMaxSeconds;
    double targetNoise  = detail::kDefaultTargetNoise;
    size_t l2FlushBytes = 0; // 0 sizes the flush from the device L2 cache
};

struct BenchResult {
    std::string name;
    bool correct        = true;
    double minMs        = 0.0;
    double meanMs       = 0.0;
    double noisePct     = 0.0; // relative stddev of the mean, percent
    int samples         = 0;
    double gbps         = 0.0; // computed from minMs (best observed)
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

// Flush buffer size: 1.5x the device L2 cache, or the floor if unavailable
inline size_t l2FlushBytes(const BenchConfig& cfg)
{
    if (cfg.l2FlushBytes != 0) {
        return cfg.l2FlushBytes;
    }
    int dev = 0;
    ZL_CUDA_CHECK(cudaGetDevice(&dev));
    int l2 = 0;
    ZL_CUDA_CHECK(cudaDeviceGetAttribute(&l2, cudaDevAttrL2CacheSize, dev));
    const size_t sized = (size_t)(l2 > 0 ? l2 : 0) * 3 / 2;
    return sized > detail::kMinL2FlushBytes ? sized : detail::kMinL2FlushBytes;
}

struct SampleStats {
    double minMs    = 0.0;
    double meanMs   = 0.0;
    double noisePct = 0.0;
    int samples     = 0;
};

// Cold-cache adaptive timing: warm up, then time one launch per sample (each
// preceded by an L2 flush) until the relative stddev of the mean drops below
// cfg.targetNoise, or the sample-count or accumulated-time cap is hit.
inline SampleStats sampleKernel(KernelCase& kc, const BenchConfig& cfg)
{
    SampleStats st;
    if (cfg.maxSamples <= 0) {
        return st;
    }
    const cudaStream_t stream      = 0;
    const size_t flushBytes        = l2FlushBytes(cfg);
    const DevicePtr<uint8_t> flush = deviceAlloc<uint8_t>(flushBytes);

    for (int i = 0; i < cfg.warmup; ++i) {
        kc.launch(stream);
    }
    ZL_CUDA_CHECK(cudaGetLastError());
    ZL_CUDA_CHECK(cudaDeviceSynchronize());

    std::vector<double> ms;
    ms.reserve(cfg.maxSamples);
    double sum      = 0.0;
    double sumSq    = 0.0;
    double elapsedS = 0.0;
    for (int i = 0; i < cfg.maxSamples; ++i) {
        CudaEvent start, stop;
        ZL_CUDA_CHECK(cudaMemsetAsync(flush.get(), 0, flushBytes, stream));
        start.record(stream);
        kc.launch(stream);
        stop.record(stream);
        ZL_CUDA_CHECK(cudaDeviceSynchronize());
        const double t = stop.elapsedMsSince(start);
        ms.push_back(t);
        sum += t;
        sumSq += t * t;
        elapsedS += t / detail::kKilo;

        const int n = (int)ms.size();
        if (n >= cfg.minSamples) {
            const double noise = detail::computeNoise(sum, sumSq, n).noise;
            if (noise < cfg.targetNoise || elapsedS > cfg.maxSeconds) {
                break;
            }
        }
    }

    st.samples                  = (int)ms.size();
    st.minMs                    = *std::min_element(ms.begin(), ms.end());
    const detail::NoiseStats ns = detail::computeNoise(sum, sumSq, st.samples);
    st.meanMs                   = ns.mean;
    st.noisePct                 = ns.noise * detail::kPercent;
    return st;
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
        r.name    = kc->name();
        r.correct = kc->verify();

        const SampleStats st = sampleKernel(*kc, cfg);
        r.minMs              = st.minMs;
        r.meanMs             = st.meanMs;
        r.noisePct           = st.noisePct;
        r.samples            = st.samples;

        const double s = r.minMs / detail::kKilo;
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
        printf("%-10s | %-12s | %s | min %8.3f ms | mean %8.3f ms |"
               " noise %4.1f%% | n=%-4d | %7.1f GB/s (%5.1f%% peak) |"
               " occ %5.1f%%\n",
               label,
               r.name.c_str(),
               r.correct ? "OK  " : "FAIL",
               r.minMs,
               r.meanMs,
               r.noisePct,
               r.samples,
               r.gbps,
               r.pctPeak,
               r.occupancyPct);
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
