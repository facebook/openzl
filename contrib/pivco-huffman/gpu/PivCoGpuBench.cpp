// Copyright (c) Meta Platforms, Inc. and affiliates.

#define HUF_STATIC_LINKING_ONLY

#include "contrib/pivco-huffman/gpu/pivco_gpu.cuh"

#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <cuda_runtime.h>

#include "contrib/pivco-huffman/gpu/pivco_block_index.h"
#include "openzl/codecs/pivco_huffman/common_pivco_kernel.h"
#include "openzl/codecs/pivco_huffman/decode_pivco_kernel.h"
#include "openzl/codecs/pivco_huffman/encode_pivco_kernel.h"
#include "openzl/fse/huf.h"
#include "openzl/shared/histogram.h"
#include "openzl/zl_errors.h"

namespace {

constexpr size_t kSyntheticBaseBytes = 8 * 1024 * 1024;

struct Args {
    size_t size = size_t{ 1 } << 30;
    // 64 KiB is the reselected default: it decodes ~14-20% faster than 32 KiB
    // (halves per-block overhead at no merge cost) with slightly better ratio,
    // and is the max the scheduled fast path supports
    // (kRankSelectMaxBlockSize).
    size_t blockSize = 64 * 1024;
    int iterations   = 20;
    // Real benchmark inputs live in the upstream pivco-huffman repo under
    // extras/datasets (https://github.com/MarcinZukowski/pivco-huffman). Clone
    // it and pass --dataset-dir=<checkout>/extras/datasets; the default assumes
    // the benchmark is run from an upstream checkout root.
    std::string datasetDir = "extras/datasets";
    std::string dataset;
};

struct Weights {
    std::vector<uint8_t> bytes;
    int tableLog;
};

struct Encoded {
    std::vector<uint8_t> bytes;
    std::vector<uint64_t> offsets;
};

struct DatasetInput {
    std::string name;
    std::string kind;
    size_t sourceBytes;
    std::vector<uint8_t> data;
};

struct Result {
    std::string name;
    std::string kind;
    size_t sourceBytes;
    size_t expandedBytes;
    size_t compressedBytes;
    size_t blocks;
    size_t weightsSize;
    int tableLog;
    double ratio;
    double decodeMedianMs;
    double decodeMinMs;
    double decodeMedianGiBps;
    double decodeMinTimeGiBps;
    double encodeMedianMs;
    double encodeMinMs;
    double encodeMedianGiBps;
    double encodeMinTimeGiBps;
    double h2dMs;
    double d2hMs;
};

Args parseArgs(int argc, char** argv)
{
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        const size_t eq = arg.find('=');
        const std::string_view key =
                eq == std::string_view::npos ? arg : arg.substr(0, eq);
        const std::string value = eq == std::string_view::npos
                ? ""
                : std::string(arg.substr(eq + 1));
        if (key == "--size") {
            args.size = std::strtoull(value.c_str(), nullptr, 0);
        } else if (key == "--block-size") {
            args.blockSize = std::strtoull(value.c_str(), nullptr, 0);
        } else if (key == "--iterations") {
            args.iterations = std::atoi(value.c_str());
        } else if (key == "--dataset-dir") {
            args.datasetDir = value;
        } else if (key == "--dataset") {
            args.dataset = value;
        } else {
            throw std::runtime_error("unknown argument: " + std::string(arg));
        }
    }
    return args;
}

std::string pathJoin(const std::string& dir, const std::string& name)
{
    if (!dir.empty() && dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

bool isDirectory(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isRegularFile(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string resolveDatasetDir(const std::string& requested)
{
    if (isDirectory(requested)) {
        return requested;
    }

    throw std::runtime_error(
            "dataset directory does not exist: " + requested
            + " -- clone https://github.com/MarcinZukowski/pivco-huffman and pass "
              "--dataset-dir=<checkout>/extras/datasets");
}

std::vector<std::string> listDatasetFiles(const std::string& dir)
{
    DIR* const handle = opendir(dir.c_str());
    if (handle == nullptr) {
        throw std::runtime_error("cannot open dataset directory: " + dir);
    }

    std::vector<std::string> names;
    while (dirent* const entry = readdir(handle)) {
        const std::string name = entry->d_name;
        if (name == "." || name == ".." || name == "README.md") {
            continue;
        }
        if (isRegularFile(pathJoin(dir, name))) {
            names.push_back(name);
        }
    }
    closedir(handle);
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<uint8_t> readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("cannot open dataset file: " + path);
    }
    const std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(size > 0 ? static_cast<size_t>(size) : 0);
    if (size > 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("failed to read dataset file: " + path);
    }
    return data;
}

uint64_t nextRandom(uint64_t& state)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

uint64_t seedFromName(const std::string& name)
{
    uint64_t seed = 1469598103934665603ull;
    for (const char c : name) {
        seed ^= static_cast<uint8_t>(c);
        seed *= 1099511628211ull;
    }
    return seed == 0 ? 0x9E3779B97F4A7C15ull : seed;
}

std::array<uint32_t, 256> probaCounts(int percent)
{
    std::array<uint32_t, 256> counts{};
    uint32_t remaining = 2048;
    for (size_t symbol = 0; symbol < counts.size() && remaining != 0;
         ++symbol) {
        uint32_t count = (remaining * static_cast<uint32_t>(percent)) / 100u;
        if (count == 0) {
            count = 1;
        }
        if (symbol + 1 == counts.size() || count > remaining) {
            count = remaining;
        }
        counts[symbol] = count;
        remaining -= count;
    }
    return counts;
}

std::array<uint32_t, 256> equalCounts(size_t distinct)
{
    std::array<uint32_t, 256> counts{};
    for (size_t i = 0; i < distinct; ++i) {
        counts[i] = 1;
    }
    return counts;
}

std::array<uint32_t, 256> bellCounts(double sigma)
{
    std::array<double, 256> weights{};
    double sum = 0.0;
    for (size_t i = 0; i < weights.size(); ++i) {
        const double x = (static_cast<double>(i) - 127.5) / sigma;
        weights[i]     = std::exp(-0.5 * x * x);
        sum += weights[i];
    }

    std::array<uint32_t, 256> counts{};
    const double scale = 65536.0 / sum;
    for (size_t i = 0; i < counts.size(); ++i) {
        counts[i] = std::max<uint32_t>(
                1, static_cast<uint32_t>(std::llround(weights[i] * scale)));
    }
    return counts;
}

std::array<uint32_t, 256> englishCounts()
{
    std::array<uint32_t, 256> counts{};
    counts[' ']  = 18288;
    counts['e']  = 10266;
    counts['t']  = 7517;
    counts['a']  = 6532;
    counts['o']  = 6160;
    counts['n']  = 5701;
    counts['i']  = 5668;
    counts['s']  = 5317;
    counts['r']  = 4988;
    counts['h']  = 4979;
    counts['l']  = 3318;
    counts['d']  = 3283;
    counts['u']  = 2276;
    counts['c']  = 2234;
    counts['m']  = 2027;
    counts['f']  = 1983;
    counts['w']  = 1703;
    counts['g']  = 1624;
    counts['p']  = 1504;
    counts['y']  = 1428;
    counts['b']  = 1260;
    counts['v']  = 796;
    counts['k']  = 560;
    counts['x']  = 140;
    counts['j']  = 98;
    counts['q']  = 84;
    counts['z']  = 51;
    counts['.']  = 1200;
    counts[',']  = 1000;
    counts['\n'] = 800;
    return counts;
}

std::array<uint32_t, 256> zipfianCounts()
{
    std::array<uint32_t, 256> counts{};
    for (size_t i = 0; i < counts.size(); ++i) {
        counts[i] = std::max<uint32_t>(
                1,
                static_cast<uint32_t>(
                        std::llround(65536.0 / static_cast<double>(i + 1))));
    }
    return counts;
}

std::array<uint32_t, 256> geometricCounts()
{
    std::array<uint32_t, 256> counts{};
    for (size_t i = 0; i < counts.size(); ++i) {
        counts[i] = i < 31 ? (uint32_t{ 1 } << (30 - i)) : 1;
    }
    return counts;
}

std::vector<uint8_t> sampleCounts(
        const std::string& name,
        const std::array<uint32_t, 256>& counts,
        size_t size)
{
    std::array<uint64_t, 256> cumulative{};
    uint64_t total  = 0;
    size_t distinct = 0;
    for (size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] != 0) {
            ++distinct;
        }
        total += counts[i];
        cumulative[i] = total;
    }
    if (total == 0 || size < distinct) {
        throw std::runtime_error("invalid synthetic distribution: " + name);
    }

    std::vector<uint8_t> data;
    data.reserve(size);
    for (size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] != 0) {
            data.push_back(static_cast<uint8_t>(i));
        }
    }

    uint64_t state = seedFromName(name);
    while (data.size() < size) {
        const uint64_t sample = nextRandom(state) % total;
        const auto it =
                std::upper_bound(cumulative.begin(), cumulative.end(), sample);
        data.push_back(static_cast<uint8_t>(it - cumulative.begin()));
    }

    for (size_t i = data.size(); i > 1; --i) {
        const size_t j = static_cast<size_t>(nextRandom(state) % i);
        std::swap(data[i - 1], data[j]);
    }
    return data;
}

std::array<uint32_t, 256> syntheticCounts(const std::string& name)
{
    if (name == "proba80") {
        return probaCounts(80);
    }
    if (name == "proba50") {
        return probaCounts(50);
    }
    if (name == "proba14") {
        return probaCounts(14);
    }
    if (name == "proba02") {
        return probaCounts(2);
    }
    if (name == "bell_s10") {
        return bellCounts(10.0);
    }
    if (name == "bell_s30") {
        return bellCounts(30.0);
    }
    if (name == "bell_s80") {
        return bellCounts(80.0);
    }
    if (name == "uniform") {
        return equalCounts(256);
    }
    if (name == "english") {
        return englishCounts();
    }
    if (name == "zipfian") {
        return zipfianCounts();
    }
    if (name == "sparse_4") {
        return equalCounts(4);
    }
    if (name == "sparse_16") {
        return equalCounts(16);
    }
    if (name == "geometric") {
        return geometricCounts();
    }
    if (name == "two_sym_eq") {
        return equalCounts(2);
    }
    if (name == "two_sym_90/10") {
        std::array<uint32_t, 256> counts{};
        counts[0] = 9;
        counts[1] = 1;
        return counts;
    }
    if (name == "flat_M3") {
        return equalCounts(8);
    }
    if (name == "flat_M5") {
        return equalCounts(32);
    }
    if (name == "flat_M6") {
        return equalCounts(64);
    }
    if (name == "flat_M7") {
        return equalCounts(128);
    }
    throw std::runtime_error("unknown synthetic distribution: " + name);
}

std::vector<std::string> syntheticNames()
{
    return {
        "proba80",  "proba50",   "proba14",   "proba02",    "bell_s10",
        "bell_s30", "bell_s80",  "uniform",   "english",    "zipfian",
        "sparse_4", "sparse_16", "geometric", "two_sym_eq", "two_sym_90/10",
        "flat_M3",  "flat_M5",   "flat_M6",   "flat_M7",
    };
}

std::vector<uint8_t> repeatToSize(
        const std::vector<uint8_t>& source,
        size_t targetSize)
{
    if (source.empty()) {
        throw std::runtime_error("cannot repeat empty dataset");
    }
    std::vector<uint8_t> data(targetSize);
    size_t written = 0;
    while (written < data.size()) {
        const size_t chunk = std::min(source.size(), data.size() - written);
        std::copy_n(source.data(), chunk, data.data() + written);
        written += chunk;
    }
    return data;
}

DatasetInput
makeSyntheticInput(const std::string& name, size_t targetSize, size_t blockSize)
{
    const size_t baseSize =
            std::max(blockSize + 1, std::min(targetSize, kSyntheticBaseBytes));
    std::vector<uint8_t> base =
            sampleCounts(name, syntheticCounts(name), baseSize);
    return DatasetInput{
        name,
        "synthetic",
        base.size(),
        repeatToSize(base, targetSize),
    };
}

DatasetInput makeRealInput(
        const std::string& dir,
        const std::string& name,
        size_t targetSize,
        size_t blockSize)
{
    std::vector<uint8_t> file = readFile(pathJoin(dir, name));
    if (file.size() <= blockSize) {
        throw std::runtime_error(
                "dataset file must be larger than block size: " + name);
    }
    const size_t sourceBytes = file.size();
    return DatasetInput{
        name,
        "real",
        sourceBytes,
        repeatToSize(file, targetSize),
    };
}

Weights buildWeights(const std::vector<uint8_t>& data)
{
    if (data.empty()) {
        return Weights{};
    }
    if (data.size() > UINT32_MAX) {
        throw std::runtime_error("input is too large to histogram");
    }

    ZL_Histogram8 hist;
    ZL_Histogram_init(&hist.base, 255);
    ZL_Histogram_build(&hist.base, data.data(), data.size(), 1);

    const uint32_t maxSymbol = hist.base.maxSymbol;
    std::vector<uint8_t> weights(static_cast<size_t>(maxSymbol) + 1);
    if (hist.base.cardinality == 1) {
        weights[maxSymbol] = 1;
        return Weights{ std::move(weights), 0 };
    }

    HUF_CREATE_STATIC_CTABLE(ctable, HUF_SYMBOLVALUE_MAX);
    unsigned tableLog =
            HUF_optimalTableLog(ZL_PIVCO_MAX_TABLE_LOG, data.size(), maxSymbol);
    const size_t hufRet =
            HUF_buildCTable(ctable, hist.base.count, maxSymbol, tableLog);
    if (HUF_isError(hufRet)) {
        throw std::runtime_error("HUF_buildCTable failed");
    }
    tableLog = static_cast<unsigned>(hufRet);
    if (tableLog > ZL_PIVCO_MAX_TABLE_LOG) {
        throw std::runtime_error("Huffman tableLog exceeds PivCo limit");
    }

    for (uint32_t symbol = 0; symbol <= maxSymbol; ++symbol) {
        const unsigned numBits = HUF_getNbBitsFromCTable(ctable, symbol);
        weights[symbol] =
                numBits == 0 ? 0 : static_cast<uint8_t>(tableLog + 1 - numBits);
    }
    return Weights{ std::move(weights), static_cast<int>(tableLog) };
}

Encoded cpuEncode(
        const Weights& weights,
        const std::vector<uint8_t>& data,
        size_t blockSize)
{
    const size_t bound = ZL_PivCoHuffmanEncode_bound(data.size(), blockSize);
    if (bound == SIZE_MAX) {
        throw std::runtime_error("encode bound overflow");
    }
    std::vector<uint8_t> encoded(bound);
    std::vector<uint8_t> scratch(
            ZL_PivCoHuffmanEncode_scratchElements(data.size(), blockSize));
    const size_t encodedSize = ZL_PivCoHuffman_encode(
            encoded.data(),
            encoded.size(),
            scratch.data(),
            scratch.size(),
            weights.bytes.data(),
            weights.bytes.size(),
            weights.tableLog,
            data.data(),
            data.size(),
            blockSize,
            &ZL_PivCoHuffmanEncode_generic);
    if (encodedSize == SIZE_MAX) {
        throw std::runtime_error("CPU encode failed");
    }
    encoded.resize(encodedSize);

    const size_t numBlocks = (data.size() + blockSize - 1) / blockSize;
    std::vector<uint64_t> offsets(numBlocks + 1);
    const ZL_Report report = pivcoFindBlockOffsets(
            offsets.data(),
            offsets.size(),
            weights.bytes.data(),
            weights.bytes.size(),
            encoded.data(),
            encoded.size(),
            data.size(),
            blockSize);
    if (ZL_isError(report)) {
        throw std::runtime_error("offset indexing failed");
    }
    return Encoded{ std::move(encoded), std::move(offsets) };
}

void checkCuda(cudaError_t err, const char* what)
{
    if (err != cudaSuccess) {
        throw std::runtime_error(
                std::string(what) + ": " + cudaGetErrorString(err));
    }
}

void checkStatus(const PivCoGpuStatus& status, const char* what)
{
    if (status.code != PIVCO_GPU_STATUS_OK) {
        throw std::runtime_error(
                std::string(what) + " status=" + std::to_string(status.code));
    }
}

template <typename Fn>
double timeHostMs(Fn&& fn)
{
    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

template <typename Fn>
std::vector<float> timeCudaMs(int iterations, cudaStream_t stream, Fn&& fn)
{
    cudaEvent_t start;
    cudaEvent_t stop;
    checkCuda(cudaEventCreate(&start), "cudaEventCreate(start)");
    checkCuda(cudaEventCreate(&stop), "cudaEventCreate(stop)");

    std::vector<float> times;
    times.reserve(iterations);
    for (int i = 0; i < iterations; ++i) {
        checkCuda(cudaEventRecord(start, stream), "cudaEventRecord(start)");
        fn();
        checkCuda(cudaEventRecord(stop, stream), "cudaEventRecord(stop)");
        checkCuda(cudaEventSynchronize(stop), "cudaEventSynchronize(stop)");
        float elapsedMs = 0.0f;
        checkCuda(
                cudaEventElapsedTime(&elapsedMs, start, stop),
                "cudaEventElapsedTime");
        times.push_back(elapsedMs);
    }

    checkCuda(cudaEventDestroy(stop), "cudaEventDestroy(stop)");
    checkCuda(cudaEventDestroy(start), "cudaEventDestroy(start)");
    return times;
}

double median(std::vector<float> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double minimum(const std::vector<float>& values)
{
    return *std::min_element(values.begin(), values.end());
}

double gibPerSecond(size_t bytes, double milliseconds)
{
    return static_cast<double>(bytes) / (1ull << 30) / (milliseconds / 1000.0);
}

Result runDataset(const Args& args, const DatasetInput& input)
{
    const Weights weights   = buildWeights(input.data);
    const Encoded encoded   = cpuEncode(weights, input.data, args.blockSize);
    const size_t numOffsets = encoded.offsets.size();
    const size_t encodeBound =
            ZL_PivCoHuffmanEncode_bound(input.data.size(), args.blockSize);
    const size_t workspaceBytes = std::max(
            pivcoGpuEncodeWorkspaceBytes(input.data.size(), args.blockSize),
            pivcoGpuDecodeWorkspaceBytes(input.data.size(), args.blockSize));

    PivCoGpuContext* context      = nullptr;
    const ZL_Report contextReport = pivcoGpuContextCreate(
            &context,
            weights.bytes.data(),
            weights.bytes.size(),
            weights.tableLog);
    if (ZL_isError(contextReport)) {
        throw std::runtime_error("context creation failed");
    }

    uint8_t* src_d           = nullptr;
    uint8_t* encoded_d       = nullptr;
    uint8_t* gpuEncoded_d    = nullptr;
    uint8_t* decoded_d       = nullptr;
    uint8_t* workspace_d     = nullptr;
    uint64_t* offsets_d      = nullptr;
    uint64_t* gpuOffsets_d   = nullptr;
    PivCoGpuStatus* status_d = nullptr;
    uint64_t* totalSize_d    = nullptr;
    cudaStream_t stream      = nullptr;

    checkCuda(cudaStreamCreate(&stream), "cudaStreamCreate");
    checkCuda(cudaMalloc(&src_d, input.data.size()), "cudaMalloc(src)");
    checkCuda(
            cudaMalloc(
                    &encoded_d,
                    encoded.bytes.size() + PIVCO_GPU_DECODE_SRC_SLOP),
            "cudaMalloc(encoded)");
    checkCuda(cudaMalloc(&gpuEncoded_d, encodeBound), "cudaMalloc(gpuEncoded)");
    checkCuda(
            cudaMalloc(
                    &decoded_d, input.data.size() + PIVCO_GPU_DECODE_DST_SLOP),
            "cudaMalloc(decoded)");
    checkCuda(
            cudaMalloc(&workspace_d, workspaceBytes), "cudaMalloc(workspace)");
    checkCuda(
            cudaMalloc(&offsets_d, numOffsets * sizeof(uint64_t)),
            "cudaMalloc(offsets)");
    checkCuda(
            cudaMalloc(&gpuOffsets_d, numOffsets * sizeof(uint64_t)),
            "cudaMalloc(gpuOffsets)");
    checkCuda(
            cudaMalloc(&status_d, sizeof(PivCoGpuStatus)),
            "cudaMalloc(status)");
    checkCuda(cudaMalloc(&totalSize_d, sizeof(uint64_t)), "cudaMalloc(total)");

    const double h2dMs = timeHostMs([&] {
        checkCuda(
                cudaMemcpyAsync(
                        src_d,
                        input.data.data(),
                        input.data.size(),
                        cudaMemcpyHostToDevice,
                        stream),
                "cudaMemcpyAsync(src)");
        checkCuda(
                cudaMemcpyAsync(
                        encoded_d,
                        encoded.bytes.data(),
                        encoded.bytes.size(),
                        cudaMemcpyHostToDevice,
                        stream),
                "cudaMemcpyAsync(encoded)");
        checkCuda(
                cudaMemcpyAsync(
                        offsets_d,
                        encoded.offsets.data(),
                        numOffsets * sizeof(uint64_t),
                        cudaMemcpyHostToDevice,
                        stream),
                "cudaMemcpyAsync(offsets)");
        checkCuda(cudaStreamSynchronize(stream), "cudaStreamSynchronize(H2D)");
    });

    for (int i = 0; i < 3; ++i) {
        checkCuda(
                cudaMemsetAsync(status_d, 0, sizeof(PivCoGpuStatus), stream),
                "warm status");
        checkCuda(
                pivcoGpuDecodeAsync(
                        context,
                        decoded_d,
                        input.data.size(),
                        encoded_d,
                        encoded.bytes.size(),
                        offsets_d,
                        numOffsets,
                        args.blockSize,
                        workspace_d,
                        workspaceBytes,
                        status_d,
                        stream),
                "warm decode");
    }
    checkCuda(cudaStreamSynchronize(stream), "warm sync");

    const auto decodeTimes = timeCudaMs(args.iterations, stream, [&] {
        checkCuda(
                cudaMemsetAsync(status_d, 0, sizeof(PivCoGpuStatus), stream),
                "decode status");
        checkCuda(
                pivcoGpuDecodeAsync(
                        context,
                        decoded_d,
                        input.data.size(),
                        encoded_d,
                        encoded.bytes.size(),
                        offsets_d,
                        numOffsets,
                        args.blockSize,
                        workspace_d,
                        workspaceBytes,
                        status_d,
                        stream),
                "decode");
    });

    PivCoGpuStatus status{};
    checkCuda(
            cudaMemcpy(
                    &status, status_d, sizeof(status), cudaMemcpyDeviceToHost),
            "copy status");
    checkStatus(status, "decode");

    const auto encodeTimes = timeCudaMs(args.iterations, stream, [&] {
        checkCuda(
                cudaMemsetAsync(status_d, 0, sizeof(PivCoGpuStatus), stream),
                "encode status");
        checkCuda(
                cudaMemsetAsync(totalSize_d, 0, sizeof(uint64_t), stream),
                "encode total");
        checkCuda(
                pivcoGpuEncodeAsync(
                        context,
                        gpuEncoded_d,
                        encodeBound,
                        gpuOffsets_d,
                        numOffsets,
                        src_d,
                        input.data.size(),
                        args.blockSize,
                        workspace_d,
                        workspaceBytes,
                        status_d,
                        totalSize_d,
                        stream),
                "encode");
    });
    checkCuda(
            cudaMemcpy(
                    &status, status_d, sizeof(status), cudaMemcpyDeviceToHost),
            "copy encode status");
    checkStatus(status, "encode");

    uint64_t gpuEncodedSize = 0;
    checkCuda(
            cudaMemcpy(
                    &gpuEncodedSize,
                    totalSize_d,
                    sizeof(gpuEncodedSize),
                    cudaMemcpyDeviceToHost),
            "copy encoded size");
    if (gpuEncodedSize != encoded.bytes.size()) {
        throw std::runtime_error("GPU encoded size differs from CPU size");
    }

    std::vector<uint8_t> decoded(input.data.size());
    const double d2hMs = timeHostMs([&] {
        checkCuda(
                cudaMemcpyAsync(
                        decoded.data(),
                        decoded_d,
                        decoded.size(),
                        cudaMemcpyDeviceToHost,
                        stream),
                "cudaMemcpyAsync(decoded)");
        checkCuda(cudaStreamSynchronize(stream), "cudaStreamSynchronize(D2H)");
    });
    if (decoded != input.data) {
        throw std::runtime_error("decode verification failed");
    }

    cudaFree(totalSize_d);
    cudaFree(status_d);
    cudaFree(gpuOffsets_d);
    cudaFree(offsets_d);
    cudaFree(workspace_d);
    cudaFree(decoded_d);
    cudaFree(gpuEncoded_d);
    cudaFree(encoded_d);
    cudaFree(src_d);
    cudaStreamDestroy(stream);
    pivcoGpuContextDestroy(context);

    const double decodeMedian = median(decodeTimes);
    const double decodeMin    = minimum(decodeTimes);
    const double encodeMedian = median(encodeTimes);
    const double encodeMin    = minimum(encodeTimes);
    return Result{
        input.name,
        input.kind,
        input.sourceBytes,
        input.data.size(),
        encoded.bytes.size(),
        numOffsets - 1,
        weights.bytes.size(),
        weights.tableLog,
        static_cast<double>(encoded.bytes.size()) / input.data.size(),
        decodeMedian,
        decodeMin,
        gibPerSecond(input.data.size(), decodeMedian),
        gibPerSecond(input.data.size(), decodeMin),
        encodeMedian,
        encodeMin,
        gibPerSecond(input.data.size(), encodeMedian),
        gibPerSecond(input.data.size(), encodeMin),
        h2dMs,
        d2hMs,
    };
}

void printResult(const Result& result)
{
    std::cout << "dataset=" << result.name << " kind=" << result.kind
              << " sourceBytes=" << result.sourceBytes
              << " expandedBytes=" << result.expandedBytes
              << " blocks=" << result.blocks
              << " compressedBytes=" << result.compressedBytes
              << " ratio=" << std::fixed << std::setprecision(4) << result.ratio
              << " weightsSize=" << result.weightsSize
              << " tableLog=" << result.tableLog
              << " decode_median_ms=" << result.decodeMedianMs
              << " decode_min_ms=" << result.decodeMinMs
              << " decode_median_GiBps=" << result.decodeMedianGiBps
              << " decode_min_time_GiBps=" << result.decodeMinTimeGiBps
              << " encode_median_ms=" << result.encodeMedianMs
              << " encode_min_ms=" << result.encodeMinMs
              << " encode_median_GiBps=" << result.encodeMedianGiBps
              << " encode_min_time_GiBps=" << result.encodeMinTimeGiBps
              << " host_h2d_ms=" << result.h2dMs
              << " host_d2h_ms=" << result.d2hMs << "\n";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Args args = parseArgs(argc, argv);
        if (args.size == 0 || args.blockSize == 0 || args.iterations <= 0) {
            throw std::runtime_error(
                    "--size, --block-size, and --iterations must be positive");
        }
        const size_t targetSize      = std::max(args.size, args.blockSize + 1);
        const std::string datasetDir = resolveDatasetDir(args.datasetDir);

        cudaDeviceProp props;
        int device = 0;
        checkCuda(cudaGetDevice(&device), "cudaGetDevice");
        checkCuda(
                cudaGetDeviceProperties(&props, device),
                "cudaGetDeviceProperties");
        checkCuda(
                cudaDeviceSetLimit(cudaLimitStackSize, 64 * 1024),
                "cudaDeviceSetLimit(stack)");

        const std::vector<std::string> realFiles = listDatasetFiles(datasetDir);
        const std::vector<std::string> synthetic = syntheticNames();
        std::cout << "device=" << props.name << " targetSize=" << targetSize
                  << " blockSize=" << args.blockSize
                  << " iterations=" << args.iterations
                  << " syntheticDatasets=" << synthetic.size()
                  << " realDatasets=" << realFiles.size()
                  << " datasetDir=" << datasetDir << "\n";

        for (const std::string& name : synthetic) {
            if (!args.dataset.empty() && args.dataset != name) {
                continue;
            }
            printResult(runDataset(
                    args,
                    makeSyntheticInput(name, targetSize, args.blockSize)));
        }
        for (const std::string& name : realFiles) {
            if (!args.dataset.empty() && args.dataset != name) {
                continue;
            }
            printResult(runDataset(
                    args,
                    makeRealInput(
                            datasetDir, name, targetSize, args.blockSize)));
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
