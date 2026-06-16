// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cli/commands/cmd_train.h"
#include "cli/commands/cmd_benchmark.h"
#include "custom_parsers/dependency_registration.h"
#include "tools/logger/Logger.h"
#include "tools/training/clustering/sample_limiter.h"
#include "tools/training/train.h"

#include <filesystem>
#include <stdexcept>
#include <thread>

namespace openzl::cli {

using namespace tools::logger;

const uint64_t kDefaultMaxSingleSampleSize = 150 * 1024 * 1024; /* 150MiB */
const uint64_t kDefaultMaxTotalSize        = 300 * 1024 * 1024; /* 300MiB */

int cmdTrain(const TrainArgs& args)
{
    if (!args.output) {
        throw InvalidArgsException(
                "No output specified. Please provide a path to save the trained compressor to.");
    }
    if (args.trainParams.paretoFrontier) {
        // Create the output directory first so it fails early
        std::filesystem::create_directories(args.output->name());
        if (args.dictBundleOutput) {
            // In Pareto mode the dict bundle output is also a directory.
            std::filesystem::create_directories(args.dictBundleOutput->name());
        }
    } else {
        // Try to open the output file so it fails early
        args.output->open();
    }

    if (!args.inputs) {
        throw InvalidArgsException("Must provide sample inputs for training.");
    }
    auto& inputs = *args.inputs;
    if (args.trainParams.threads.has_value()
        && args.trainParams.threads.value()
                > std::thread::hardware_concurrency()) {
        Logger::log(
                WARNINGS,
                "Number of threads requested is greater than the number of hardware threads available. Performance may be impacted.");
    }
    if (args.useAllSamples && args.trainParams.numSamples.has_value()) {
        Logger::log(
                WARNINGS,
                "Both --use-all-samples and --num-samples were specified. Overriding specified number of samples with all samples.");
    }
    if (args.trainParams.maxTotalSizeMb.has_value()
        && (args.useAllSamples || args.trainParams.numSamples.has_value())) {
        throw InvalidArgsException(
                "Cannot specify --max-total-size-mb together with the number of samples to use.");
    }

    auto numSamples = args.trainParams.numSamples;
    // TODO: Remove this hack to calculate number of inputs by adding size()
    // to InputSet
    const size_t numInputs = std::distance(inputs.begin(), inputs.end());
    if (args.useAllSamples) {
        numSamples = numInputs;
    }
    if (args.trainParams.numSamples > numInputs) {
        Logger::log(
                WARNINGS,
                "Number of samples requested is greater than the number of samples available. Using all samples.");
        numSamples = numInputs;
    }

    auto maxFileSize  = args.trainParams.maxFileSizeMb.has_value()
             ? args.trainParams.maxFileSizeMb.value() * 1024 * 1024
             : kDefaultMaxSingleSampleSize;
    auto maxTotalSize = args.trainParams.maxTotalSizeMb.has_value()
            ? args.trainParams.maxTotalSizeMb.value() * 1024 * 1024
            : kDefaultMaxTotalSize;

    // Get samples for training
    auto limiter =
            training::SampleLimiter(maxTotalSize, maxFileSize, numSamples);
    auto filteredInputsPtr = limiter.getFilteredInputsPtr(inputs);

    // Benchmark the untrained compressor
    BenchmarkArgs benchmarkArgs(args, args.compressor());
    benchmarkArgs.inputs = training::inputSetToMultiInputs(*filteredInputsPtr);
    Logger::log(INFO, "Benchmarking untrained compressor...");
    auto untrainedBenchmark = runCompressionBenchmarks(benchmarkArgs);

    auto serializedTrainedCompressors = openzl::training::train(
            benchmarkArgs.inputs, *args.compressor(), args.trainParams);

    poly::optional<tools::io::OutputFile> resultsCsv;
    if (args.trainParams.paretoFrontier) {
        auto csv = std::string(args.output->name()) + "/benchmark.csv";
        Logger::log_c(
                INFO,
                "Benchmarking %zu trained compressors and saving to %s...",
                serializedTrainedCompressors.size(),
                csv.c_str());
        resultsCsv.emplace(csv);
        resultsCsv->open();
        resultsCsv->get_ostream()
                << "Algorithm, Compressor, Compression Ratio, Compression Speed MB/s, Decompression Speed MB/s"
                << std::endl
                << std::fixed << std::setprecision(2);
    }

    for (size_t i = 0; i < serializedTrainedCompressors.size(); ++i) {
        auto& candidate = serializedTrainedCompressors[i];

        std::string fatBundle;
        if (!candidate.dicts.empty()) {
            fatBundle = candidate.packFatBundle();
        }

        // Benchmark the trained compressor
        benchmarkArgs.setCompressor(
                custom_parsers::createCompressorFromSerialized(
                        candidate.serializedCompressor, fatBundle));
        benchmarkArgs.dictBundleData = fatBundle;

        if (!args.trainParams.paretoFrontier) {
            Logger::log(INFO, "Benchmarking trained compressor...");
        }
        BenchmarkResult trainedBenchmark =
                runCompressionBenchmarks(benchmarkArgs);
        auto improvedRatio = untrainedBenchmark.compressionRatio > 0
                ? (trainedBenchmark.compressionRatio
                           / untrainedBenchmark.compressionRatio
                   - 1) * 100
                : 0.0;
        if (!args.trainParams.paretoFrontier) {
            Logger::log_c(
                    INFO,
                    "Training improved compression ratio by %0.2f%%",
                    improvedRatio);
        }

        if (resultsCsv.has_value()) {
            resultsCsv->get_ostream()
                    << std::setw(9) << "OpenZL" << ", " << std::setw(7) << i
                    << ".zc, " << std::setw(17)
                    << trainedBenchmark.compressionRatio << ", "
                    << std::setw(22) << trainedBenchmark.compressionSpeed
                    << ", " << std::setw(24)
                    << trainedBenchmark.decompressionSpeed << std::endl;
        }

        if (args.trainParams.paretoFrontier) {
            auto outputFilename = std::string(args.output->name()) + "/"
                    + std::to_string(i) + ".zc";
            auto output = tools::io::OutputFile(std::move(outputFilename));
            output.open();
            output.write(candidate.serializedCompressor);
            output.close();

            if (!fatBundle.empty()) {
                auto bundleFilename = std::string(args.dictBundleOutput->name())
                        + "/" + std::to_string(i) + ".zd";
                auto bundleOutput =
                        tools::io::OutputFile(std::move(bundleFilename));
                bundleOutput.open();
                bundleOutput.write(fatBundle);
                bundleOutput.close();
            }
        } else {
            if (i != 0) {
                throw std::logic_error("Must only have one trained compressor");
            }
            // Output file is already open
            args.output->write(candidate.serializedCompressor);
            args.output->close();

            if (!fatBundle.empty()) {
                // Dictionary training only runs when the user opted in with
                // --dict-bundle-output, so the bundle output is always set
                // here.
                args.dictBundleOutput->open();
                args.dictBundleOutput->write(fatBundle);
                args.dictBundleOutput->close();
            }
        }
    }

    if (resultsCsv.has_value()) {
        resultsCsv->close();
    }

    return 0;
}

} // namespace openzl::cli
