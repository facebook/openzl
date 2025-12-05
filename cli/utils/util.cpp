// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdio.h>
#include <array>
#include <memory>

#include "custom_parsers/dependency_registration.h"

#include "openzl/cpp/Compressor.hpp"

#include "cli/utils/compress_profiles.h"
#include "cli/utils/util.h"

using namespace std::string_view_literals;

namespace openzl::cli::util {

static constexpr std::array suffix{ "B"sv, "KB"sv, "MB"sv, "GB"sv, "TB"sv };
static constexpr double kilo = 1000.0;
static constexpr double tenK = 10000.0;
std::string sizeString(size_t sz)
{
    // figure out the proper suffix
    size_t suffixPtr = 0;
    double szDbl     = (double)sz;
    do {
        if (szDbl < tenK) {
            break;
        }
        szDbl /= kilo;
        ++suffixPtr;
    } while (suffixPtr < suffix.size() - 1);
    std::vector<char> ret(20, 0);
    snprintf(ret.data(), 19, "%7.2lf %s", szDbl, suffix[suffixPtr].data());
    return std::string(ret.data());
}

std::unique_ptr<Compressor> createCompressorFromProfile(const ProfileArgs& args)
{
    auto compressor = std::make_unique<Compressor>();

    if (args.name().value_or("").empty()) {
        throw InvalidArgsException(
                "Please provide a profile. See `zli list-profiles` for a list of supported profiles.");
    }
    auto name    = args.name().value();
    auto profile = compressProfiles().find(name);
    if (profile == compressProfiles().end()) {
        throw InvalidArgsException(
                "Profile not found: '" + name
                + "'. See `zli list-profiles` for a list of supported profiles.");
    }

    auto graphId = profile->second->gen(
            compressor->get(), profile->second->opaque.get(), args);
    compressor->selectStartingGraph(graphId);

    return compressor;
}

int parseStrictInt(const std::string& str)
{
    if (str.empty()) {
        throw InvalidArgsException("Empty string is not a valid integer");
    }

    size_t pos = 0;
    int result;

    try {
        result = std::stoi(str, &pos);
    } catch (const std::exception& e) {
        throw InvalidArgsException(
                "Invalid integer: '" + str + "' - " + e.what());
    }

    // Check if entire string was consumed
    if (pos != str.length()) {
        throw InvalidArgsException(
                "Invalid integer: '" + str
                + "' contains trailing characters starting at position "
                + std::to_string(pos));
    }

    return result;
}

unsigned long parseStrictULong(const std::string& str)
{
    if (str.empty()) {
        throw InvalidArgsException("Empty string is not a valid number");
    }

    size_t pos = 0;
    unsigned long result;

    try {
        result = std::stoul(str, &pos);
    } catch (const std::exception& e) {
        throw InvalidArgsException(
                "Invalid number: '" + str + "' - " + e.what());
    }

    if (pos != str.length()) {
        throw InvalidArgsException(
                "Invalid number: '" + str
                + "' contains trailing characters starting at position "
                + std::to_string(pos));
    }

    return result;
}

unsigned long long parseStrictULL(const std::string& str)
{
    if (str.empty()) {
        throw InvalidArgsException("Empty string is not a valid number");
    }

    size_t pos = 0;
    unsigned long long result;

    try {
        result = std::stoull(str, &pos);
    } catch (const std::exception& e) {
        throw InvalidArgsException(
                "Invalid number: '" + str + "' - " + e.what());
    }

    if (pos != str.length()) {
        throw InvalidArgsException(
                "Invalid number: '" + str
                + "' contains trailing characters starting at position "
                + std::to_string(pos));
    }

    return result;
}

} // namespace openzl::cli::util
