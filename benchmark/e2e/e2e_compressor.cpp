// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "benchmark/e2e/e2e_compressor.h"
#include "benchmark/benchmark_data_utils.h"
#include "openzl/zl_compressor.h"

using namespace zstrong::bench::utils;

namespace zstrong::bench::e2e {

namespace {

void graphCompress(
        ZL_CCtx* cctx,
        const std::string_view src,
        std::vector<uint8_t>& output,
        const ZL_Compressor* graph)
{
    if (output.size() == 0)
        output.resize(ZL_compressBound(src.size()) * 8);
    if (graph != nullptr) {
        ZL_unwrap(
                ZL_CCtx_refCompressor(cctx, graph),
                "Zstrong failure:: failed ZL_CCtx_refCompressor");
    }
    ZL_unwrap(
            ZL_CCtx_setParameter(
                    cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION),
            "Failed setting format version");
    ZL_Report r = ZL_CCtx_compress(
            cctx, output.data(), output.size(), src.data(), src.size());
    output.resize(ZL_unwrap(r, "Failed compressing"));
}

} // namespace

CGraph_unique ZstrongCompressor::getGraph()
{
    auto cgraph = createCGraph();
    auto gid    = configureGraph(cgraph.get());
    ZL_unwrap(
            ZL_Compressor_selectStartingGraphID(cgraph.get(), gid),
            "Failed setting starting graph id");
    return cgraph;
}

void ZstrongCompressor::compress(
        const std::string_view src,
        std::vector<uint8_t>& output)
{
    graphCompress(cctx_.get(), src, output, getGraph().get());
}

void ZstrongCompressor::compress(
        const std::vector<uint8_t>& src,
        std::vector<uint8_t>& output)
{
    compress(getStringView(src), output);
}

void ZstrongCompressor::decompress(
        const std::string_view src,
        std::vector<uint8_t>& output)
{
    registerDTransforms(dctx_.get());
    if (output.size() == 0) {
        size_t decompressedSize = ZL_unwrap(
                ZL_getDecompressedSize(src.data(), src.size()),
                "Zstrong failure: failed getting decompressed size");
        output.resize(decompressedSize);
    }
    ZL_Report r = ZL_DCtx_decompress(
            dctx_.get(), output.data(), output.size(), src.data(), src.size());
    output.resize(ZL_unwrap(r, "Failed decompressing"));
}

void ZstrongCompressor::decompress(
        const std::vector<uint8_t>& src,
        std::vector<uint8_t>& output)
{
    decompress(getStringView(src), output);
}

size_t ZstrongCompressor::roundtrip(const std::string_view src)
{
    std::vector<uint8_t> compressed, decompressed;
    compress(src, compressed);
    decompress(compressed, decompressed);
    if (getStringView(decompressed) != src) {
        throw std::runtime_error{ "Failed roundtrip testing" };
    }
    return compressed.size();
}

void ZstrongCompressor::benchCompression(
        benchmark::State& state,
        const std::string_view src)
{
    ZstrongCompressor::benchCompressions(state, { src });
}

void ZstrongCompressor::benchCompressions(
        benchmark::State& state,
        const std::vector<std::string_view>& srcs)
{
    // Make sure that we roundtrip successfully
    std::vector<std::vector<uint8_t>> compressed;
    size_t totalCompressedSize = 0;
    size_t totalSrcsSize       = 0;
    for (const std::string_view src : srcs) {
        size_t compressedSize = roundtrip(src);
        compressed.emplace_back(compressedSize);
        totalCompressedSize += compressedSize;
        totalSrcsSize += src.size();
    }
    auto graph = getGraph();
    for (auto _ : state) {
        for (size_t i = 0; i < srcs.size(); i++) {
            graphCompress(cctx_.get(), srcs[i], compressed[i], graph.get());
        }
        benchmark::DoNotOptimize(compressed);
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed((int64_t)(totalSrcsSize * state.iterations()));
    state.counters["CompressedSize"] = (double)totalCompressedSize;
    state.counters["Size"]           = (double)totalSrcsSize;
    state.counters["CompressionRatio"] =
            (double)totalSrcsSize / (double)totalCompressedSize;
}

void ZstrongCompressor::benchDecompression(
        benchmark::State& state,
        const std::string_view src)
{
    benchDecompressions(state, { src });
}

void ZstrongCompressor::benchDecompressions(
        benchmark::State& state,
        const std::vector<std::string_view>& srcs)
{
    // Make sure that we roundtrip successfully
    std::vector<std::vector<uint8_t>> compressed;
    std::vector<std::vector<uint8_t>> decompressed;
    size_t totalCompressedSize = 0;
    size_t totalSrcsSize       = 0;
    for (const std::string_view src : srcs) {
        std::vector<uint8_t> currCompressed;
        std::vector<uint8_t> currDecompressed;
        compress(src, currCompressed);
        decompress(currCompressed, currDecompressed);
        compressed.push_back(currCompressed);
        decompressed.push_back(currDecompressed);
        totalCompressedSize += currCompressed.size();
        totalSrcsSize += src.size();
    }
    benchmark::DoNotOptimize(decompressed);
    benchmark::ClobberMemory();
    auto graph = getGraph();
    for (auto _ : state) {
        for (size_t i = 0; i < srcs.size(); i++) {
            decompress(compressed[i], decompressed[i]);
        }
        benchmark::DoNotOptimize(compressed);
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed((int64_t)(totalSrcsSize * state.iterations()));
    state.counters["CompressedSize"] = (double)totalCompressedSize;
    state.counters["Size"]           = (double)totalSrcsSize;
    state.counters["CompressionRatio"] =
            (double)totalSrcsSize / (double)totalCompressedSize;
}

} // namespace zstrong::bench::e2e
