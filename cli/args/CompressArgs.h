// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <optional>
#include <string>

#include "openzl/cpp/Compressor.hpp"

#include "tools/io/Input.h"
#include "tools/io/Output.h"

#include "cli/args/ArgsUtils.h"
#include "cli/args/GlobalArgs.h"
#include "tools/io/InputFile.h"
#include "tools/io/OutputFile.h"

namespace openzl::cli {

struct CompressArgs : public GlobalArgs {
    static void addArgs(arg::ArgParser& parser)
    {
        // Add the command
        parser.addCommand(Cmd::COMPRESS, "compress", 'c');

        // Add the args
        parser.addCommandPositional(cmd(), kInput, "Input file path.");
        parser.addCommandFlag(cmd(), kOutput, 'o', true, "Output file path.");
        parser.addCommandFlag(
                cmd(), kForce, 'f', false, "Overwrite output file.");
        parser.addCommandFlag(
                cmd(), kProfile, 'p', true, "Compress with the given profile.");
        parser.addCommandFlag(
                cmd(),
                kProfileArg,
                0,
                true,
                "Pass the given value as an argument to constructing the profile.");
        parser.addCommandFlag(
                cmd(),
                kCompressor,
                'c',
                true,
                "Compress with the given serialized compressor file.");
        parser.addCommandFlag(
                cmd(),
                kTrainInline,
                0,
                false,
                "Train the compressor on the input file before compressing.");
        parser.addCommandFlag(
                cmd(),
                kTrainInlineTestLimit,
                0,
                true,
                // "Lime limit spent on inline training, in seconds, for
                // testing."
                "");
        parser.addCommandFlag(
                cmd(),
                kTrace,
                0,
                true,
                "Record a trace of the compression to be visualized with streamdump. Writes a CBOR file to the provided path.");
        parser.addCommandFlag(
                cmd(),
                kTraceStreamsDir,
                0,
                true,
                "Directory to write trace streamdump to.");
    }

    explicit CompressArgs(const arg::ParsedArgs& parsed) : GlobalArgs(parsed)
    {
        // Create the compressor
        compressor = createCompressorFromArgs(
                parsed.cmdFlag(cmd(), kProfile),
                parsed.cmdFlag(cmd(), kProfileArg),
                parsed.cmdFlag(cmd(), kCompressor));

        // Get the input and output files
        auto inputPath = parsed.cmdPositional(cmd(), kInput);
        auto outputPath =
                parsed.cmdFlag(cmd(), kOutput).value_or(inputPath + ".zl");
        checkOutput(outputPath, parsed.cmdHasFlag(cmd(), kForce));
        input  = std::make_unique<tools::io::InputFile>(std::move(inputPath));
        output = std::make_unique<tools::io::OutputFile>(std::move(outputPath));

        trainInline = parsed.cmdHasFlag(cmd(), kTrainInline);
        if (parsed.cmdHasFlag(cmd(), kTrainInlineTestLimit)) {
            trainInlineTestLimit = std::stoul(
                    parsed.cmdFlag(cmd(), kTrainInlineTestLimit).value());
        }

        if (parsed.cmdHasFlag(cmd(), kTrace)) {
            auto path   = parsed.cmdFlag(cmd(), kTrace).value();
            traceOutput = std::make_shared<tools::io::OutputFile>(path);
        }

        traceStreamsDir = parsed.cmdFlag(cmd(), kTraceStreamsDir);
    }

    static Cmd cmd()
    {
        return Cmd::COMPRESS;
    };

    std::shared_ptr<Compressor> compressor;

    std::shared_ptr<tools::io::Input> input;
    std::shared_ptr<tools::io::Output> output;

    bool trainInline{};
    poly::optional<size_t> trainInlineTestLimit;

    std::shared_ptr<tools::io::Output> traceOutput;
    std::optional<std::string> traceStreamsDir;

   private:
    inline static const std::string kInput  = "input";
    inline static const std::string kOutput = "output";
    inline static const std::string kForce  = "force";

    inline static const std::string kProfile    = "profile";
    inline static const std::string kProfileArg = "profile-arg";
    inline static const std::string kCompressor = "compressor";

    inline static const std::string kVerbose     = "verbose";
    inline static const std::string kRecursive   = "recursive";
    inline static const std::string kTrainInline = "train-inline";
    inline static const std::string kTrainInlineTestLimit =
            "train-inline-test-limit";
    inline static const std::string kTrace           = "trace";
    inline static const std::string kTraceStreamsDir = "trace-streams-dir";
};

} // namespace openzl::cli
