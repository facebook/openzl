// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cli/commands/cmd_inspect.h"
#include "cli/args/InspectArgs.h"
#include "custom_parsers/dependency_registration.h"
#include "tools/logger/Logger.h"

namespace openzl::cli {

using namespace tools::logger;

int cmdInspect(const InspectArgs& args)
{
    if (!args.output) {
        Logger::log(
                ERRORS,
                "No output file specified. Please provide a path using the -o or --output flag.");
        return 1;
    }

    // Load compressor from serialized file
    const auto compressor = custom_parsers::createCompressorFromSerialized(
            args.input->contents(), "");

    // Convert to json
    const auto json = compressor->serializeToJson();

    // Save to file
    auto& output = *args.output;
    output.write(json);
    output.close();

    return 0;
}

} // namespace openzl::cli
