// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string>

#include "cli/args/BenchmarkArgs.h"
#include "cli/args/CompressArgs.h"
#include "cli/args/DecompressArgs.h"
#include "cli/args/GlobalArgs.h"
#include "cli/args/InspectArgs.h"
#include "cli/args/ListProfilesArgs.h"
#include "cli/args/TrainArgs.h"

#include "cli/commands/cmd_benchmark.h"
#include "cli/commands/cmd_compress.h"
#include "cli/commands/cmd_decompress.h"
#include "cli/commands/cmd_inspect.h"
#include "cli/commands/cmd_list_profiles.h"
#include "cli/commands/cmd_train.h"

#include "cli/utils/util.h"

#include "tools/arg/ParseException.h"
#include "tools/arg/arg_parser.h"
#include "tools/io/IOException.h"
#include "tools/logger/Logger.h"

#include "openzl/common/logging.h"

using openzl::cli::Cmd;

using namespace openzl;
using namespace openzl::cli;
using namespace openzl::tools::logger;

namespace {

int impl(int argc, char** argv)
{
    arg::ArgParser argParser;

    // set command-line arguments
    GlobalArgs::addArgs(argParser);
    CompressArgs::addArgs(argParser);
    DecompressArgs::addArgs(argParser);
    TrainArgs::addArgs(argParser);
    BenchmarkArgs::addArgs(argParser);
    InspectArgs::addArgs(argParser);
    ListProfilesArgs::addArgs(argParser);

    auto usage = [&](const Cmd& cmd) -> std::string {
        auto help = cmd == Cmd::UNSPECIFIED ? argParser.help()
                                            : argParser.help(cmd);
        return "Demo CLI for OpenZL. NO VERSION STABILITY IS IMPLIED!!\n"
                "\n"
                "Usage: " + std::string(argv[0]) + " <command> [options] <args>\n"
                "\n" +
                std::move(help) + "<<<< NO VERSION STABILITY IS IMPLIED!! >>>>";
    };
    if (argc == 1) {
        Logger::log(INFO, usage(Cmd::UNSPECIFIED));
        return 0;
    }

    // parse the command-line arguments
    auto parsedArgs = argParser.parse(argc, argv);
    const auto cmd  = parsedArgs.chosenCmd();
    const GlobalArgs globalArgs(parsedArgs);

    // If there are any immediates, print the appropriate string and return
    if (globalArgs.immediate.has_value()) {
        switch (globalArgs.immediate.value()) {
            case GlobalImmediate::HELP: {
                Logger::log(INFO, usage((Cmd)cmd));
                return 0;
            }
            case GlobalImmediate::VERSION: {
                Logger::log(INFO, "zstrong-cli version 0.1");
                return 0;
            }
        }
    }

    // now validate the parsed arguments
    argParser.validate(parsedArgs);

    // Set the user-facing log level
    try {
        Logger::instance().setGlobalLoggerVerbosity(globalArgs.verbosity);
    } catch (const std::invalid_argument& e) {
        throw InvalidArgsException(e.what());
    }

    // If user sets the log level is set to EVERYTHING, set the
    // developer-centric ZL_LOG_LVL to the highest level (ZL_LOG_LVL_V9) to
    // output all logs. Otherwise, set it to the least-verbose level
    // (ZL_LOG_LVL_ALWAYS)
    if (globalArgs.verbosity == EVERYTHING) {
        ZL_g_logLevel = ZL_LOG_LVL_V9;
    }

    // populate and run subcommand args
    switch (cmd) {
        case Cmd::COMPRESS: {
            const CompressArgs compressArgs(parsedArgs);
            return cmdCompress(compressArgs);
        }
        case Cmd::DECOMPRESS: {
            const DecompressArgs decompressArgs(parsedArgs);
            return cmdDecompress(decompressArgs);
        }
        case Cmd::TRAIN: {
            const TrainArgs trainArgs(parsedArgs);
            return cmdTrain(trainArgs);
        }
        case Cmd::BENCHMARK: {
            const BenchmarkArgs benchmarkArgs(parsedArgs);
            return cmdBenchmark(benchmarkArgs);
        }
        case Cmd::INSPECT: {
            const InspectArgs inspectArgs(parsedArgs);
            return cmdInspect(inspectArgs);
        }
        case Cmd::LIST_PROFILES: {
            const ListProfilesArgs listProfilesArgs(parsedArgs);
            return cmdListProfiles(listProfilesArgs);
        }
        case Cmd::UNSPECIFIED: {
            Logger::log(ERRORS, "No command specified");
            return 1;
        }
    }
    return 1;
}

} // anonymous namespace

int main(int argc, char** argv)
{
    try {
        return impl(argc, argv);
    } catch (const arg::ParseException& pe) {
        Logger::log(ERRORS, "Error parsing arguments:\n\t ", pe.what());
        return 1;
    } catch (const InvalidArgsException& iae) {
        Logger::log(ERRORS, "Invalid argument(s):\n\t", iae.what());
        return 1;
    } catch (const CLIException& ce) {
        Logger::log(ERRORS, "CLI Exception:\n\t", ce.what());
        return 1;
    } catch (const tools::io::IOException& ioe) {
        Logger::log(ERRORS, "I/O Exception:\n\t", ioe.what());
        return 1;
    } catch (const openzl::Exception& oe) {
        Logger::log(ERRORS, "OpenZL Library Exception:\n\t", oe.what());
        return 1;
    } catch (const std::exception& e) {
        Logger::log(
                ERRORS,
                "Unexpected runtime error. This may mean that your inputs are "
                "invalid (eg your file is invalid, the compressor is invalid, or "
                "the compressor doesn't match the file format), or it may be a bug. "
                "Please report bugs to the OpenZL developers!\n\t",
                e.what());
        return 1;
    }
    return 1;
}
