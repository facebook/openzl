// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <CLI/CLI.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "model.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_registerStaticGraph_fromPipelineNodes1o
#include "tools/zstrong_cpp.h"
#include "tools/zstrong_ml.h"

using namespace zstrong;
namespace fs = std::filesystem;

namespace {
std::string readFile(const fs::path& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filePath << '\n';
        return "";
    }
    return std::string(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
}

template <typename Func>
std::vector<typename std::result_of<Func(std::string_view)>::type>
mapOverFilesInDirectory(const fs::path& directoryPath, Func func)
{
    std::vector<typename std::result_of<Func(std::string_view)>::type> results;
    // Check if the provided path is a directory
    if (!fs::is_directory(directoryPath)) {
        std::cerr << "Error: Provided path is not a directory.\n";
        return results;
    }
    // Iterate over each entry in the directory
    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        if (entry.is_regular_file()) {
            auto result = func(readFile(entry.path()));
            results.push_back(result);
        }
    }
    return std::move(results);
}

std::vector<uint8_t> compress(
        const ZL_Compressor* cgraph,
        std::string_view data)
{
    std::vector<uint8_t> compressed(ZL_compressBound(data.size()));
    CCtx cctx{};
    if (ZL_isError(ZL_CCtx_refCompressor(cctx.get(), cgraph))) {
        std::cerr << "Failed to set graph";
        return { data.data(), data.data() + data.size() };
    }

    ZL_TypedRef* const tref = ZL_TypedRef_createNumeric(
            data.data(), sizeof(uint64_t), data.size() / sizeof(uint64_t));

    if (ZL_isError(ZL_CCtx_setParameter(
                cctx.get(), ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        std::cerr << "Failed to set format version";
    }
    ZL_Report const csize = ZL_CCtx_compressTypedRef(
            cctx.get(), compressed.data(), compressed.size(), tref);

    if (ZL_isError(csize)) {
        std::cerr << "Compression failed: "
                  << ZL_CCtx_getErrorContextString(cctx.get(), csize) << '\n';
        return { data.data(), data.data() + data.size() };
    }
    compressed.resize(ZL_validResult(csize));
    return compressed;
}

ZL_GraphID declareGraph(
        ZL_Compressor* cgraph,
        ZL_NodeID node,
        std::initializer_list<ZL_GraphID> successors)
{
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, node, successors.begin(), successors.size());
}

std::tuple<std::vector<std::string>, std::vector<ZL_GraphID>>
generateSuccessors(ZL_Compressor* cgraph)
{
    ZL_GraphID fieldlz = ZL_Compressor_registerFieldLZGraph(cgraph);
    ZL_GraphID range_pack =
            declareGraph(cgraph, ZL_NODE_RANGE_PACK, { fieldlz });
    ZL_GraphID range_pack_zstd =
            declareGraph(cgraph, ZL_NODE_RANGE_PACK, { ZL_GRAPH_ZSTD });

    ZL_GraphID delta_fieldlz =
            declareGraph(cgraph, ZL_NODE_DELTA_INT, { fieldlz });
    ZL_GraphID tokenize_delta_fieldlz = ZL_Compressor_registerTokenizeGraph(
            cgraph, ZL_Type_numeric, /* sort */ true, delta_fieldlz, fieldlz);

    // Successors are in alphabetical order
    std::map<std::string, ZL_GraphID> successors = {
        { "fieldlz", fieldlz },
        { "range_pack", range_pack },
        { "range_pack_zstd", range_pack_zstd },
        { "delta_fieldlz", delta_fieldlz },
        { "tokenize_delta_fieldlz", tokenize_delta_fieldlz },
        { "zstd", ZL_GRAPH_ZSTD },
    };
    std::vector<std::string> labels;
    std::vector<ZL_GraphID> graphs;
    for (const auto& [label, graph] : successors) {
        labels.push_back(label);
        graphs.push_back(graph);
    }
    return { labels, graphs };
}

CGraph generateTrainingGraph(std::string outputPath)
{
    CGraph cgraph{};
    const auto [labels, graphs] = generateSuccessors(cgraph.get());

    if (std::filesystem::exists(outputPath)
        && std::filesystem::file_size(outputPath) > 0) {
        std::cerr << fmt::format(
                "File {} already exists, overwriting\n", outputPath);
    }
    std::ofstream output{ outputPath, std::ios::trunc };
    auto featureGenerator =
            std::make_shared<ml::features::IntFeatureGenerator>();
    auto selector = std::make_shared<ml::FileMLTrainingSelector>(
            ZL_Type_numeric,
            labels,
            std::move(output),
            false,
            featureGenerator);
    if (ZL_isError(ZL_Compressor_selectStartingGraphID(
                cgraph.get(),
                registerOwnedSelector(*cgraph, selector, graphs)))) {
        throw std::runtime_error("Failed to register training selector");
    }
    return cgraph;
}

CGraph generateInferenceGraph()
{
    CGraph cgraph{};
    const auto [labels, graphs] = generateSuccessors(cgraph.get());

    auto model = std::make_shared<ml::GBTModel>(EXAMPLE_MODEL);
    auto featureGenerator =
            std::make_shared<ml::features::IntFeatureGenerator>();
    auto selector = std::make_shared<ml::MLSelector>(
            ZL_Type_numeric, model, featureGenerator, labels);
    if (ZL_isError(ZL_Compressor_selectStartingGraphID(
                cgraph.get(),
                registerOwnedSelector(*cgraph, selector, graphs)))) {
        throw std::runtime_error("Failed to register inference selector");
    }
    return cgraph;
}
} // namespace

int main(int argc, const char* argv[])
{
    CLI::App app{ "Zstrong ML Selector Example" };
    app.set_help_all_flag("--help-all", "Expand all help");

    CLI::App* train = app.add_subcommand("train", "Train a model");
    CLI::App* infer = app.add_subcommand("infer", "Train a model");
    app.require_subcommand(1);

    std::string trainingInputsDirPath = "/tmp/ml_train_samples";
    train->add_option(
                 "-i,--input-path",
                 trainingInputsDirPath,
                 "Path to a directory with training input files (default = /tmp/ml_train_samples).")
            ->check(CLI::ExistingDirectory);

    std::string outputPath = "/tmp/ml_features";
    train->add_option(
            "-o,--output-path",
            outputPath,
            "Write generated features to this file (default = /tmp/ml_features).");

    std::string inferenceInputsDirPath = "/tmp/ml_test_samples";
    infer->add_option(
                 "-i,--input-path",
                 inferenceInputsDirPath,
                 "Path to a directory with inference input files for testing (default = /tmp/ml_test_samples).")
            ->check(CLI::ExistingDirectory);

    CLI11_PARSE(app, argc, argv);

    auto cgraph = [&app, &outputPath]() {
        if (app.got_subcommand("train")) {
            return generateTrainingGraph(outputPath);
        } else {
            return generateInferenceGraph();
        }
    }();
    auto inputsDirPath = app.got_subcommand("train") ? trainingInputsDirPath
                                                     : inferenceInputsDirPath;

    auto originalSizes = mapOverFilesInDirectory(
            inputsDirPath,
            [](std::string_view data) -> size_t { return data.size(); });
    auto results = mapOverFilesInDirectory(
            inputsDirPath,
            [cgraph = std::move(cgraph)](std::string_view data) -> size_t {
                return compress(cgraph.get(), data).size();
            });
    size_t originalSize = std::accumulate(
            originalSizes.begin(), originalSizes.end(), (size_t)0);
    size_t compressedSize =
            std::accumulate(results.begin(), results.end(), (size_t)0);
    std::cout << fmt::format(
            "Completed compression of {} files with x{:.3} CR ({} -> {})",
            results.size(),
            (double)originalSize / compressedSize,
            originalSize,
            compressedSize)
              << std::endl;
}
