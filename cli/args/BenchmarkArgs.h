// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <optional>
#include <string>

#include "openzl/cpp/Compressor.hpp"

#include "tools/io/InputSetBuilder.h"
#include "tools/io/OutputFile.h"
#include "tools/training/utils/utils.h"

#include "cli/args/ArgsUtils.h"
#include "cli/args/GlobalArgs.h"

namespace openzl::cli {

struct BenchmarkArgs : public GlobalArgs, public ProfileArgs {
    static void addArgs(arg::ArgParser& parser)
    {
        // Add the command
        parser.addCommand(cmd(), "benchmark", 'b');

        // Add the args
        parser.addCommandPositional(cmd(), kInput, "Input directory.");
        parser.addCommandFlag(
                cmd(),
                kOutputCsv,
                0,
                true,
                "Output file path for CSV-formatted summary statistic.");
        parser.addCommandFlag(
                cmd(),
                kCompressor,
                'c',
                true,
                "Benchmark the given serialized compressor file.");

        parser.addCommandFlag(
                cmd(),
                kLevel,
                'l',
                true,
                "Benchmark the given compression level.");
        parser.addCommandFlag(
                cmd(), kNumIters, 'n', true, "Number of benchmark iterations.");
        parser.addCommandFlag(
                cmd(),
                kStrict,
                0,
                false,
                "Enforce strict mode compression. This will fail the compression in cases of errors, instead of falling back.");
    }

    explicit BenchmarkArgs(const arg::ParsedArgs& parsed)
            : GlobalArgs(parsed), ProfileArgs(parsed)
    {
        // Create the compressor
        setCompressor(createCompressorFromArgs(
                *this, parsed.cmdFlag(cmd(), kCompressor)));
        auto inputPath = parsed.cmdPositional(Cmd::BENCHMARK, kInput);

        auto input_set = tools::io::InputSetBuilder(recursive)
                                 .add_path(std::move(inputPath))
                                 .build();

        inputs = training::inputSetToMultiInputs(*input_set);

        auto outputCsvPath = parsed.cmdFlag(Cmd::BENCHMARK, kOutputCsv);
        if (outputCsvPath) {
            outputCsv = std::make_unique<tools::io::OutputFile>(
                    std::move(outputCsvPath).value());
        }
        auto levelArg = parsed.cmdFlag(cmd(), kLevel);
        if (levelArg) {
            level = std::stoi(levelArg.value());
        }
        auto numItersArg = parsed.cmdFlag(cmd(), kNumIters);
        if (numItersArg) {
            numIters = std::stoi(numItersArg.value());
        }
        strict = parsed.cmdHasFlag(Cmd::BENCHMARK, kStrict);
    }

    explicit BenchmarkArgs(
            const GlobalArgs& globalArgs,
            const std::shared_ptr<Compressor>& compressor)
            : GlobalArgs(globalArgs), ProfileArgs(compressor)
    {
    }

    static Cmd cmd()
    {
        return Cmd::BENCHMARK;
    }

    std::vector<training::MultiInput> inputs;
    std::unique_ptr<tools::io::Output> outputCsv;

    std::optional<int> level;

    size_t numIters = 10;
    bool strict     = false;

   private:
    inline static const std::string kInput      = "input";
    inline static const std::string kOutputCsv  = "output-csv";
    inline static const std::string kCompressor = "compressor";

    inline static const std::string kLevel    = "level";
    inline static const std::string kStrict   = "strict";
    inline static const std::string kNumIters = "num-iters";
};

} // namespace openzl::cli
