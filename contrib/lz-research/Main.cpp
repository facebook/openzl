// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "Benchmark.hpp"
#include "tools/arg/arg_parser.h"
#include "tools/arg/parsed_args.h"
#include "tools/io/InputFile.h"
#include "tools/io/InputSetBuilder.h"
#include "tools/io/OutputFile.h"
#include "tools/json.hpp"
#include "tools/logger/Logger.h"

namespace openzl {
namespace {
namespace fs = std::filesystem;

nlohmann::json loadJsonFromFlag(const std::optional<std::string>& flag)
{
    if (!flag.has_value() || flag->empty()) {
        throw std::runtime_error("Specify a value for --compressor");
    }
    nlohmann::json out;
    try {
        out = nlohmann::json::parse(*flag);
    } catch (const std::exception&) {
        tools::io::InputFile file(*flag);
        out = nlohmann::json::parse(file.contents());
    }
    return out;
}

std::unique_ptr<tools::io::InputSet> buildInputSet(
        const std::vector<std::string>& inputs,
        bool staticSet = false)
{
    tools::io::InputSetBuilder builder(true);
    for (const auto& path : inputs) {
        builder.add_path(path);
    }
    return staticSet ? std::move(builder).build_static()
                     : std::move(builder).build();
}

enum class Commands {
    Benchmark  = 1,
    Compress   = 2,
    Decompress = 3,
    Train      = 4,
};

class Command {
   public:
    virtual void addArguments(openzl::arg::ArgParser& parser) const    = 0;
    virtual void parseArguments(const openzl::arg::ParsedArgs& parser) = 0;
    virtual void run() const                                           = 0;
    virtual ~Command() = default;
};

class BenchmarkCommand : public Command {
   public:
    void addArguments(openzl::arg::ArgParser& parser) const override
    {
        parser.addCommand(int(Commands::Benchmark), "benchmark", 'b');
        parser.addCommandPositional(
                int(Commands::Benchmark),
                arg::NumArgs::OneOrMore,
                "input",
                "Input files or directories to benchmark on");
        parser.addCommandFlag(
                int(Commands::Benchmark),
                "min-iters",
                'i',
                true,
                "Minimum number of iterations to run - defaults to 1");
        parser.addCommandFlag(
                int(Commands::Benchmark),
                "min-secs",
                's',
                true,
                "Minimum number of seconds to run - defaults to 1");
        parser.addCommandFlag(
                int(Commands::Benchmark),
                "compressors",
                'c',
                true,
                "Either a JSON array of compressor configs, or a path to a file containing those configs");
        parser.addCommandFlag(
                int(Commands::Benchmark),
                "output",
                'o',
                true,
                "Path to output file");
        parser.addCommandFlag(
                int(Commands::Benchmark),
                "compress-only",
                'C',
                false,
                "Only benchmark compression, not decompression");
        parser.addCommandFlag(
                int(Commands::Benchmark),
                "decompress-only",
                'D',
                false,
                "Only benchmark decompression, not compression");
    }

    void parseArguments(const openzl::arg::ParsedArgs& args) override
    {
        benchmark_ = args.chosenCmd() == int(Commands::Benchmark);
        if (!benchmark_) {
            return;
        }
        inputPaths_ = args.cmdPositionals(int(Commands::Benchmark), "input");

        auto minItersFlag = args.cmdFlag(int(Commands::Benchmark), "min-iters");
        if (minItersFlag.has_value()) {
            args_.minIters = std::stoull(minItersFlag.value());
        }

        auto minSecsFlag = args.cmdFlag(int(Commands::Benchmark), "min-secs");
        if (minSecsFlag.has_value()) {
            args_.minTime =
                    std::chrono::seconds(std::stoull(minSecsFlag.value()));
        }

        auto outputFlag = args.cmdFlag(int(Commands::Benchmark), "output");
        if (!outputFlag.has_value()) {
            throw std::runtime_error("Specify a value for --output");
        }
        output_ = outputFlag.value();

        const bool compressOnly =
                args.cmdFlag(int(Commands::Benchmark), "compress-only")
                        .has_value();
        const bool decompressOnly =
                args.cmdFlag(int(Commands::Benchmark), "decompress-only")
                        .has_value();
        if (compressOnly && decompressOnly) {
            throw std::runtime_error(
                    "Cannot specify both --compress-only and --decompress-only");
        }
        if (compressOnly) {
            args_.mode = bench::BenchmarkMode::Compression;
        } else if (decompressOnly) {
            args_.mode = bench::BenchmarkMode::Decompression;
        } else {
            args_.mode = bench::BenchmarkMode::Both;
        }

        auto compressorsFlag =
                args.cmdFlag(int(Commands::Benchmark), "compressors");
        auto compressors = loadJsonFromFlag(compressorsFlag.value());
        for (const auto& compressor : compressors) {
            args_.compressorConfigs.push_back(compressor);
        }
    }

    void run() const override
    {
        if (!benchmark_) {
            return;
        }
        auto inputs  = buildInputSet(inputPaths_);
        auto results = bench::benchmark(*inputs, args_);

        nlohmann::json out = nlohmann::json::array();
        for (const auto& result : results) {
            out.push_back(result.json());
        }
        std::ofstream outStream(output_);
        outStream << out.dump(2);
    }

    ~BenchmarkCommand() override = default;

   private:
    bool benchmark_ = false;
    std::vector<std::string> inputPaths_;
    bench::BenchmarkArgs args_;
    std::string output_;
};

class CompressCommand : public Command {
   public:
    void addArguments(openzl::arg::ArgParser& parser) const
    {
        parser.addCommand(int(Commands::Compress), "compress", 'c');
        parser.addCommandPositional(
                int(Commands::Compress),
                arg::NumArgs::OneOrMore,
                "input",
                "Input files or directories to compress");
        parser.addCommandFlag(
                int(Commands::Compress),
                "compressor",
                'c',
                true,
                "Either a JSON compressor config, or a path to a file containing a config");
        parser.addCommandFlag(
                int(Commands::Compress),
                "output",
                'o',
                true,
                "Path to output file");
    }

    void parseArguments(const openzl::arg::ParsedArgs& parser)
    {
        compress_ = parser.chosenCmd() == int(Commands::Compress);
        if (!compress_) {
            return;
        }
        inputPaths_ = parser.cmdPositionals(int(Commands::Compress), "input");

        auto compressorFlag =
                parser.cmdFlag(int(Commands::Compress), "compressor");
        compressor_ = loadJsonFromFlag(compressorFlag.value());

        outputPath_ = parser.cmdFlag(int(Commands::Compress), "output");

        if (outputPath_.has_value()
            && (inputPaths_.size() != 1 || fs::is_directory(inputPaths_[0]))) {
            throw std::runtime_error(
                    "--output can only be used with a single input");
        }
    }

    void run() const
    {
        if (!compress_) {
            return;
        }

        auto compressor = bench::makeCompressor(compressor_);
        auto inputs     = buildInputSet(inputPaths_);
        for (const auto& input : *inputs) {
            auto compressed = compressor->compress(input->contents());
            auto outputPath =
                    outputPath_.value_or(std::string(input->name()) + ".zl");
            tools::io::OutputFile out(outputPath);
            out.write(compressed);
        }
    }

    ~CompressCommand() override = default;

   private:
    bool compress_{ false };
    std::vector<std::string> inputPaths_;
    nlohmann::json compressor_;
    std::optional<std::string> outputPath_;
};

class DecompressCommand : public Command {
   public:
    void addArguments(openzl::arg::ArgParser& parser) const
    {
        parser.addCommand(int(Commands::Decompress), "decompress", 'd');
        parser.addCommandPositional(
                int(Commands::Decompress),
                arg::NumArgs::OneOrMore,
                "input",
                "Input files or directories to compress");
        parser.addCommandFlag(
                int(Commands::Decompress),
                "compressor",
                'c',
                true,
                "Either a JSON compressor config, or a path to a file containing a config");
        parser.addCommandFlag(
                int(Commands::Decompress),
                "output",
                'o',
                true,
                "Path to output file");
    }

    void parseArguments(const openzl::arg::ParsedArgs& parser)
    {
        decompress_ = parser.chosenCmd() == int(Commands::Decompress);
        if (!decompress_) {
            return;
        }
        inputPaths_ = parser.cmdPositionals(int(Commands::Decompress), "input");

        auto compressorFlag =
                parser.cmdFlag(int(Commands::Decompress), "compressor");
        compressor_ = loadJsonFromFlag(compressorFlag.value());

        outputPath_ = parser.cmdFlag(int(Commands::Decompress), "output");
    }

    void run() const
    {
        if (!decompress_) {
            return;
        }

        auto compressor = bench::makeCompressor(compressor_);
        auto inputs     = buildInputSet(inputPaths_);
        for (const auto& input : *inputs) {
            auto decompressed = compressor->decompress(input->contents());

            auto outputPath = outputPath_;
            if (!outputPath.has_value()) {
                if (input->name().ends_with(".zl")) {
                    outputPath =
                            input->name().substr(0, input->name().size() - 3);
                } else {
                    throw std::runtime_error(
                            "Cannot determine output for "
                            + std::string(input->name()));
                }
            }
            tools::io::OutputFile out(*outputPath);
            out.write(decompressed);
        }
    }

    ~DecompressCommand() override = default;

   private:
    bool decompress_{ false };
    std::vector<std::string> inputPaths_;
    nlohmann::json compressor_;
    std::optional<std::string> outputPath_;
};

class TrainCommand : public Command {
   public:
    void addArguments(openzl::arg::ArgParser& parser) const
    {
        parser.addCommand(int(Commands::Train), "train", 't');
        parser.addCommandPositional(
                int(Commands::Train),
                arg::NumArgs::OneOrMore,
                "input",
                "Input files or directories to compress");
        parser.addCommandFlag(
                int(Commands::Train),
                "compressor",
                'c',
                true,
                "Either a JSON compressor config, or a path to a file containing a config");
        parser.addCommandFlag(
                int(Commands::Train),
                "output",
                'o',
                true,
                "Path to output file");
    }

    void parseArguments(const openzl::arg::ParsedArgs& parser)
    {
        train_ = parser.chosenCmd() == int(Commands::Train);
        if (!train_) {
            return;
        }
        inputPaths_ = parser.cmdPositionals(int(Commands::Train), "input");

        auto compressorFlag =
                parser.cmdFlag(int(Commands::Train), "compressor");
        compressor_ = loadJsonFromFlag(compressorFlag.value());

        auto outputFlag = parser.cmdFlag(int(Commands::Train), "output");
        if (!outputFlag.has_value()) {
            throw std::runtime_error("--output must be provided");
        }
        outputPath_ = *outputFlag;
    }

    void run() const
    {
        if (!train_) {
            return;
        }

        auto compressor = bench::makeCompressor(compressor_);
        auto inputs     = buildInputSet(inputPaths_, /* static */ true);
        std::vector<std::string_view> samples;
        for (const auto& input : *inputs) {
            samples.push_back(input->contents());
        }
        auto config = compressor->train(samples);

        tools::io::OutputFile out(outputPath_);
        out.write(config.dump(2));
    }

    ~TrainCommand() override = default;

   private:
    bool train_{ false };
    std::vector<std::string> inputPaths_;
    nlohmann::json compressor_;
    std::string outputPath_;
};

} // namespace
} // namespace openzl

int main(int argc, char** argv)
{
    using openzl::tools::logger::Logger;
    using openzl::tools::logger::LogLevel;

    std::vector<std::unique_ptr<openzl::Command>> commands;
    commands.emplace_back(std::make_unique<openzl::BenchmarkCommand>());
    commands.emplace_back(std::make_unique<openzl::CompressCommand>());
    commands.emplace_back(std::make_unique<openzl::DecompressCommand>());
    commands.emplace_back(std::make_unique<openzl::TrainCommand>());

    openzl::arg::ArgParser parser;
    for (const auto& command : commands) {
        command->addArguments(parser);
    }

    try {
        auto parsedArgs = parser.parse(argc, argv);
        for (auto& command : commands) {
            command->parseArguments(parsedArgs);
        }
    } catch (const std::exception& e) {
        Logger::log_c(LogLevel::INFO, "%s", parser.help().c_str());
        Logger::log_c(
                LogLevel::ERRORS, "Argument Parsing Error:\n\t %s", e.what());
        return 1;
    }

    try {
        for (auto& command : commands) {
            command->run();
        }
    } catch (const std::exception& e) {
        Logger::log_c(
                LogLevel::ERRORS, "Unhandled Exception:\n\t %s", e.what());
        return 1;
    }

    return 0;
}
