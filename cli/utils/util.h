// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/Exception.hpp"

#include "cli/utils/compress_profiles.h"
#include "tools/logger/Logger.h"

namespace openzl::cli {

/**
 * Exception thrown when an invalid argument is provided.
 */
class InvalidArgsException : public std::runtime_error {
   public:
    explicit InvalidArgsException(const std::string& msg)
            : std::runtime_error(msg)
    {
    }
};

/**
 * A general uncategorized exception thrown when the CLI is used incorrectly.
 */
class CLIException : public std::runtime_error {
   public:
    explicit CLIException(const std::string& msg) : std::runtime_error(msg) {}
};

namespace util {
struct CompressorSerializerDeleter {
    void operator()(ZL_CompressorSerializer* serializer) const
    {
        ZL_CompressorSerializer_free(serializer);
    }
};

void setVerbosity(int level);

std::string sizeString(size_t sz);

/**
 * Strict integer parsing that throws InvalidArgsException on invalid input.
 * Unlike std::stoi, this rejects trailing characters.
 *
 * @param str The string to parse
 * @return The parsed integer
 * @throws InvalidArgsException if string contains non-numeric characters
 */
int parseStrictInt(const std::string& str);

/**
 * Strict unsigned long parsing.
 * @throws InvalidArgsException if string contains non-numeric characters
 *
 * Note: parseStrictULL could technically handle all unsigned integer cases,
 * but we provide type-specific functions to match the actual variable types
 * in the codebase (reduces implicit conversions and makes intent clearer).
 */
unsigned long parseStrictULong(const std::string& str);

/**
 * Strict unsigned long long parsing.
 * @throws InvalidArgsException if string contains non-numeric characters
 */
unsigned long long parseStrictULL(const std::string& str);

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
