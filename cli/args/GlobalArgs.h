// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <sstream>

#include "cli/cmd.h"
#include "cli/utils/parse.h"

#include "tools/arg/arg_parser.h"
#include "tools/arg/parsed_args.h"

namespace openzl {
namespace cli {
enum class GlobalImmediate { HELP, VERSION };

class GlobalArgs {
   public:
    static void addArgs(arg::ArgParser& parser)
    {
        // Immediates
        parser.addGlobalImmediate(
                kHelp, 'h', false, "Display this help message.");
        parser.addGlobalImmediate(kVersion, 'V', false, "Display version.");

        // Flags
        parser.addGlobalFlag(
                kVerbose,
                'v',
                true,
                "Set log level (0=NOTHING, 1=ERROR, 2=WARNING, 3=INFO, 4=VERBOSE1, 5=VERBOSE2, 6=VERBOSE3, 7=EVERYTHING). Default is INFO. Levels above INFO can be set with -v, -vv, -vvv, -vvvv");
        parser.addGlobalFlag(
                kRecursive,
                'r',
                false,
                "Traverse input directories recursively.");
    }

    explicit GlobalArgs(const arg::ParsedArgs& parsed)
    {
        verbosity =
                util::checkedstoi(parsed.globalFlag(kVerbose).value_or("3"));
        recursive = parsed.globalHasFlag(kRecursive);

        if (parsed.immediate().has_value()) {
            auto val = parsed.immediate().value();
            if (val == kHelp) {
                immediate = GlobalImmediate::HELP;
            } else if (val == kVersion) {
                immediate = GlobalImmediate::VERSION;
            } else {
                std::stringstream msg;
                msg << "Invalid immediate argument: " << val
                    << "! Please use -h to see valid arguments.";
                throw InvalidArgsException(msg.str());
            }
        }
    }

    static Cmd cmd()
    {
        return Cmd::UNSPECIFIED;
    }

    int verbosity;
    bool recursive;
    std::optional<GlobalImmediate> immediate;

   private:
    inline static const std::string kHelp         = "help";
    inline static const std::string kVersion      = "version";
    inline static const std::string kVerbose      = "verbose";
    inline static const std::string kRecursive    = "recursive";
    inline static const std::string kListProfiles = "list-profiles";
};

} // namespace cli
} // namespace openzl
