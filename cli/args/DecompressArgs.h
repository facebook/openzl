// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "cli/args/ArgsUtils.h"
#include "cli/args/GlobalArgs.h"
#include "cli/utils/util.h"

#include "tools/io/InputFile.h"
#include "tools/io/OutputFile.h"

namespace openzl::cli {

class DecompressArgs : GlobalArgs {
   public:
    static void addArgs(arg::ArgParser& parser)
    {
        // Add the command
        parser.addCommand(cmd(), "decompress", 'd');

        // Add the args
        parser.addCommandPositional(cmd(), kInput, "Input file path.");
        parser.addCommandFlag(cmd(), kOutput, 'o', true, "Output file path.");
        parser.addCommandFlag(
                cmd(), kForce, 'f', false, "Overwrite output file.");
        parser.addCommandFlag(
                cmd(),
                kTrace,
                0,
                true,
                "Record a trace of the decompression to be visualized with streamdump. Writes a CBOR file to the provided path.");
        parser.addCommandFlag(
                cmd(),
                kTraceStreamsDir,
                0,
                true,
                "Directory to write trace streamdump to.");
        parser.addCommandFlag(
                cmd(),
                kNoStreamPreview,
                0,
                false,
                "Omit stream preview data from the trace CBOR output. Requires --trace.");
    }

    explicit DecompressArgs(const arg::ParsedArgs& parsed) : GlobalArgs(parsed)
    {
        // Validate the input and output paths
        auto inputPath  = parsed.cmdPositional(cmd(), kInput);
        auto outputPath = parsed.cmdFlag(cmd(), kOutput);
        if (!outputPath) {
            if (std::filesystem::path(inputPath).extension() != ".zl") {
                throw InvalidArgsException(
                        "Input file must have a .zl extension to infer output file path!");
            }
            outputPath = std::filesystem::path(inputPath)
                                 .replace_extension()
                                 .string();
        }
        checkOutput(outputPath.value(), parsed.cmdHasFlag(cmd(), kForce));

        // Set the input and output files
        input  = std::make_unique<tools::io::InputFile>(std::move(inputPath));
        output = std::make_unique<tools::io::OutputFile>(
                std::move(outputPath).value());

        if (parsed.cmdHasFlag(cmd(), kTrace)) {
            auto path   = parsed.cmdFlag(cmd(), kTrace).value();
            traceOutput = std::make_shared<tools::io::OutputFile>(path);
        }

        traceStreamsDir = parsed.cmdFlag(cmd(), kTraceStreamsDir);
        streamPreview   = !parsed.cmdHasFlag(cmd(), kNoStreamPreview);

        if (!streamPreview && !traceOutput) {
            throw InvalidArgsException(
                    "--no-stream-preview requires --trace to be specified.");
        }
    }

    static Cmd cmd()
    {
        return Cmd::DECOMPRESS;
    }

    std::unique_ptr<tools::io::Input> input;
    std::unique_ptr<tools::io::Output> output;

    std::shared_ptr<tools::io::Output> traceOutput;
    std::optional<std::string> traceStreamsDir;
    bool streamPreview = true;

   private:
    inline static const std::string kInput           = "input";
    inline static const std::string kOutput          = "output";
    inline static const std::string kForce           = "force";
    inline static const std::string kTrace           = "trace";
    inline static const std::string kTraceStreamsDir = "trace-streams-dir";
    inline static const std::string kNoStreamPreview = "no-stream-preview";
};

} // namespace openzl::cli
