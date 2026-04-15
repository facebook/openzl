// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <benchmark/benchmark.h>

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/FrameInfo.hpp"
#include "tests/registry/OpenZLComponent.h"
#include "tests/registry/OpenZLComponents.h"

namespace {
using namespace openzl;
using namespace openzl::tests;

bool detailedBenchmark(int* argc, char** argv)
{
    for (int i = 0; i < *argc; ++i) {
        if (std::string_view{ argv[i] } == std::string_view{ "--detailed" }) {
            for (int j = i; j < *argc - 1; ++j) {
                argv[j] = argv[j + 1];
            }
            *argc -= 1;
            return true;
        }
    }
    return false;
}

class ComponentBenchmark {
   public:
    explicit ComponentBenchmark(const OpenZLComponent& component)
            : component_(&component)
    {
        compressor_.selectStartingGraph(ZL_GRAPH_STORE);
        component_->registerComponent(compressor_);
        datagen::DataGen gen(0xdeadbeef);
        benchmarks_ = component_->benchmarks(compressor_, gen);
    }

    static void registerCompressOverallBenchmark(
            std::shared_ptr<ComponentBenchmark> self)
    {
        if (self->benchmarks_.empty()) {
            return;
        }
        auto bench = [self](benchmark::State& state) {
            self->benchCompressOverall(state);
        };
        benchmark::RegisterBenchmark(
                ("Compress/Component:" + self->component_->name() + "/Overall")
                        .c_str(),
                std::move(bench));
    }

    static void registerCompressDetailedBenchmarks(
            std::shared_ptr<ComponentBenchmark> self)
    {
        for (const auto& benchmark : self->benchmarks_) {
            auto bench = [self, &benchmark](benchmark::State& state) {
                self->benchCompressDetailed(state, benchmark);
            };
            benchmark::RegisterBenchmark(
                    ("Compress/Component:" + self->component_->name()
                     + "/Detailed/" + benchmark.name)
                            .c_str(),
                    std::move(bench));
        }
    }

    static void registerDecompressOverallBenchmark(
            std::shared_ptr<ComponentBenchmark> self)
    {
        if (self->benchmarks_.empty()) {
            return;
        }
        auto bench = [self](benchmark::State& state) {
            self->benchDecompressOverall(state);
        };
        benchmark::RegisterBenchmark(
                ("Decompress/Component:" + self->component_->name()
                 + "/Overall")
                        .c_str(),
                std::move(bench));
    }

    static void registerDecompressDetailedBenchmarks(
            std::shared_ptr<ComponentBenchmark> self)
    {
        for (const auto& benchmark : self->benchmarks_) {
            auto bench = [self, &benchmark](benchmark::State& state) {
                self->benchDecompressDetailed(state, benchmark);
            };
            benchmark::RegisterBenchmark(
                    ("Decompress/Component:" + self->component_->name()
                     + "/Detailed/" + benchmark.name)
                            .c_str(),
                    std::move(bench));
        }
    }

   private:
    void setParameters(CCtx& cctx) const
    {
        cctx.refCompressor(compressor_);
        cctx.setParameter(CParam::StickyParameters, true);
        // Disable checksumming
        cctx.setParameter(CParam::CompressedChecksum, ZL_TernaryParam_disable);
        cctx.setParameter(CParam::ContentChecksum, ZL_TernaryParam_disable);
        // Set format version (currently can only be max)
        cctx.setParameter(CParam::FormatVersion, formatVersion_);
    }

    void benchCompressOverall(benchmark::State& state) const
    {
        CCtx cctx;
        setParameters(cctx);

        size_t compressBound = 0;
        std::vector<std::vector<std::vector<Input>>> benchmarkInputs;
        benchmarkInputs.reserve(benchmarks_.size());
        size_t totalSrcSize = 0;
        for (const auto& benchmark : benchmarks_) {
            std::vector<std::vector<Input>> inputs;
            inputs.reserve(benchmark.inputs.size());
            for (const auto& input : benchmark.inputs) {
                auto in = input->inputs();
                compressBound =
                        std::max(compressBound, component_->compressBound(in));
                inputs.push_back(std::move(in));
                totalSrcSize += input->sizeBytes();
            }
            benchmarkInputs.push_back(std::move(inputs));
        }

        std::string compressed(compressBound, '\0');
        size_t totalCompressedSize = 0;
        for (size_t i = 0; i < benchmarks_.size(); ++i) {
            const auto& benchmark = benchmarks_[i];
            for (const auto& input : benchmarkInputs[i]) {
                cctx.selectStartingGraph(benchmark.graph);
                const size_t cSize = cctx.compress(compressed, input);
                totalCompressedSize += cSize;
            }
        }

        for (auto _ : state) {
            for (size_t i = 0; i < benchmarks_.size(); ++i) {
                const auto& benchmark = benchmarks_[i];
                for (const auto& input : benchmarkInputs[i]) {
                    cctx.selectStartingGraph(benchmark.graph);
                    cctx.compress(compressed, input);
                    benchmark::DoNotOptimize(compressed);
                    benchmark::ClobberMemory();
                }
            }
        }

        state.SetBytesProcessed((int64_t)(totalSrcSize * state.iterations()));
        state.counters["CompressedSize"] = (double)totalCompressedSize;
        state.counters["Size"]           = (double)totalSrcSize;
        state.counters["CompressionRatio"] =
                (double)totalSrcSize / totalCompressedSize;
    }

    void benchDecompressOverall(benchmark::State& state) const
    {
        CCtx cctx;
        setParameters(cctx);
        DCtx dctx;
        component_->registerComponent(dctx);

        size_t compressBound = 0;
        size_t totalSrcSize  = 0;
        std::vector<std::vector<std::vector<Input>>> benchmarkInputs;
        benchmarkInputs.reserve(benchmarks_.size());
        for (const auto& benchmark : benchmarks_) {
            std::vector<std::vector<Input>> inputs;
            inputs.reserve(benchmark.inputs.size());
            for (const auto& input : benchmark.inputs) {
                auto in = input->inputs();
                compressBound =
                        std::max(compressBound, component_->compressBound(in));
                inputs.push_back(std::move(in));
                totalSrcSize += input->sizeBytes();
            }
            benchmarkInputs.push_back(std::move(inputs));
        }

        // Compress all inputs and collect frame info
        std::string compressBuffer(compressBound, '\0');
        std::vector<CompressedFrame> frames;
        for (size_t i = 0; i < benchmarks_.size(); ++i) {
            for (const auto& input : benchmarkInputs[i]) {
                frames.push_back(compressFrame(
                        cctx, benchmarks_[i].graph, input, compressBuffer));
            }
        }
        size_t totalCompressedSize = 0;
        for (const auto& frame : frames) {
            totalCompressedSize += frame.data.size();
        }

        std::vector<Output> outputs;
        outputs.reserve(100);
        for (auto _ : state) {
            for (auto& frame : frames) {
                outputs.clear();
                for (size_t i = 0; i < frame.outputTypes.size(); ++i) {
                    outputs.push_back(wrapOutput(
                            frame.outputTypes[i],
                            frame.outputEltWidths[i],
                            frame.buffers[i]));
                }
                dctx.decompress(outputs, frame.data);
                benchmark::DoNotOptimize(outputs);
                benchmark::ClobberMemory();
            }
        }

        state.SetBytesProcessed((int64_t)(totalSrcSize * state.iterations()));
        state.counters["CompressedSize"] = (double)totalCompressedSize;
        state.counters["Size"]           = (double)totalSrcSize;
        state.counters["CompressionRatio"] =
                (double)totalSrcSize / totalCompressedSize;
    }

    void benchCompressDetailed(
            benchmark::State& state,
            const OpenZLComponent::Benchmark& benchmark) const
    {
        CCtx cctx;
        setParameters(cctx);

        size_t compressBound = 0;
        std::vector<std::vector<Input>> inputs;
        inputs.reserve(benchmark.inputs.size());
        size_t totalSrcSize = 0;
        for (const auto& input : benchmark.inputs) {
            auto in = input->inputs();
            compressBound =
                    std::max(compressBound, component_->compressBound(in));
            inputs.push_back(std::move(in));
            totalSrcSize += input->sizeBytes();
        }

        std::string compressed(compressBound, '\0');
        size_t totalCompressedSize = 0;
        for (const auto& input : inputs) {
            cctx.selectStartingGraph(benchmark.graph);
            const size_t cSize = cctx.compress(compressed, input);
            totalCompressedSize += cSize;
        }

        for (auto _ : state) {
            for (const auto& input : inputs) {
                cctx.selectStartingGraph(benchmark.graph);
                cctx.compress(compressed, input);
            }
        }

        state.SetBytesProcessed((int64_t)(totalSrcSize * state.iterations()));
        state.counters["CompressedSize"] = (double)totalCompressedSize;
        state.counters["Size"]           = (double)totalSrcSize;
        state.counters["CompressionRatio"] =
                (double)totalSrcSize / totalCompressedSize;
    }

    void benchDecompressDetailed(
            benchmark::State& state,
            const OpenZLComponent::Benchmark& bm) const
    {
        CCtx cctx;
        setParameters(cctx);
        DCtx dctx;
        component_->registerComponent(dctx);

        size_t compressBound = 0;
        std::vector<std::vector<Input>> inputs;
        inputs.reserve(bm.inputs.size());
        size_t totalSrcSize = 0;
        for (const auto& input : bm.inputs) {
            auto in = input->inputs();
            compressBound =
                    std::max(compressBound, component_->compressBound(in));
            inputs.push_back(std::move(in));
            totalSrcSize += input->sizeBytes();
        }

        // Compress all inputs and collect frame info
        std::string compressBuffer(compressBound, '\0');
        std::vector<CompressedFrame> frames;
        frames.reserve(inputs.size());
        for (const auto& input : inputs) {
            frames.push_back(
                    compressFrame(cctx, bm.graph, input, compressBuffer));
        }
        size_t totalCompressedSize = 0;
        for (const auto& frame : frames) {
            totalCompressedSize += frame.data.size();
        }

        std::vector<Output> outputs;
        outputs.reserve(100);
        for (auto _ : state) {
            for (auto& frame : frames) {
                outputs.clear();
                for (size_t i = 0; i < frame.outputTypes.size(); ++i) {
                    outputs.push_back(wrapOutput(
                            frame.outputTypes[i],
                            frame.outputEltWidths[i],
                            frame.buffers[i]));
                }
                dctx.decompress(outputs, frame.data);
                benchmark::DoNotOptimize(outputs);
                benchmark::ClobberMemory();
            }
        }

        state.SetBytesProcessed((int64_t)(totalSrcSize * state.iterations()));
        state.counters["CompressedSize"] = (double)totalCompressedSize;
        state.counters["Size"]           = (double)totalSrcSize;
        state.counters["CompressionRatio"] =
                (double)totalSrcSize / totalCompressedSize;
    }

    struct CompressedFrame {
        std::string data;
        std::vector<Type> outputTypes;
        std::vector<size_t> outputEltWidths;
        std::vector<std::vector<char>> buffers;
    };

    static CompressedFrame compressFrame(
            CCtx& cctx,
            GraphID graph,
            poly::span<const Input> inputs,
            std::string& compressBuffer)
    {
        cctx.selectStartingGraph(graph);
        const size_t cSize = cctx.compress(compressBuffer, inputs);

        CompressedFrame frame;
        frame.data = compressBuffer.substr(0, cSize);
        FrameInfo info(frame.data);
        for (size_t i = 0; i < info.numOutputs(); ++i) {
            frame.outputTypes.push_back(info.outputType(i));
            frame.outputEltWidths.push_back(inputs[i].eltWidth());
            if (info.outputType(i) != Type::String) {
                frame.buffers.emplace_back(info.outputContentSize(i));
            } else {
                frame.buffers.emplace_back();
            }
        }
        return frame;
    }

    static Output
    wrapOutput(Type type, size_t eltWidth, std::vector<char>& buffer)
    {
        switch (type) {
            case Type::Serial:
                return Output::wrapSerial(buffer.data(), buffer.size());
            case Type::Struct:
                return Output::wrapStruct(
                        buffer.data(), eltWidth, buffer.size() / eltWidth);
            case Type::Numeric:
                return Output::wrapNumeric(
                        buffer.data(), eltWidth, buffer.size() / eltWidth);
            case Type::String:
                return Output();
        }
        return Output();
    }

    Compressor compressor_;
    const OpenZLComponent* component_;
    std::vector<OpenZLComponent::Benchmark> benchmarks_;
    int formatVersion_ = ZL_MAX_FORMAT_VERSION;
};

} // namespace

int main(int argc, char** argv)
{
    const bool detailed = detailedBenchmark(&argc, argv);

    for (const auto& component : getAllOpenZLComponents()) {
        auto bench = std::make_shared<ComponentBenchmark>(*component);
        ComponentBenchmark::registerCompressOverallBenchmark(bench);
        ComponentBenchmark::registerDecompressOverallBenchmark(bench);
        if (detailed) {
            ComponentBenchmark::registerCompressDetailedBenchmarks(bench);
            ComponentBenchmark::registerDecompressDetailedBenchmarks(bench);
        }
    }

    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
