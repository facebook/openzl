// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <string>

#include "cli/utils/parse.h"

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/Exception.hpp"

#include "tools/logger/Logger.h"

namespace openzl::cli {
class ProfileArgs;
} // namespace openzl::cli

namespace openzl::cli {
namespace util {
struct CompressorSerializerDeleter {
    void operator()(ZL_CompressorSerializer* serializer) const
    {
        ZL_CompressorSerializer_free(serializer);
    }
};

void setVerbosity(int level);

/**
 * Creates a compressor based on the provided profile.
 *
 * @param profilePath The path to the compression profile to use
 * @return The created compressor
 * @throws InvalidArgsException if the profile is invalid
 */
std::unique_ptr<Compressor> createCompressorFromProfile(
        const ProfileArgs& profileArgs);

template <typename Ctx>
void logWarnings(const Ctx& ctx)
{
    if (tools::logger::Logger::instance().getGlobalLoggerVerbosity()
        >= tools::logger::WARNINGS) {
        const auto warning_strings = get_warning_strings(ctx);
        if (!warning_strings.empty()) {
            tools::logger::Logger::log(
                    tools::logger::WARNINGS,
                    "Encountered warnings during operation!:");
        }
        for (const auto& pair : warning_strings) {
            const auto& error = pair.first;
            const auto& str   = pair.second;
            (void)error;
            tools::logger::Logger::log(tools::logger::WARNINGS, str);
        }
    }
}

} // namespace util
} // namespace openzl::cli
