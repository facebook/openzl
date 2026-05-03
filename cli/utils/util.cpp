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

static constexpr std::array sizeSuffixes{ "B"sv,
                                          "KB"sv,
                                          "MB"sv,
                                          "GB"sv,
                                          "TB"sv };
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
    } while (suffixPtr < sizeSuffixes.size() - 1);
    std::vector<char> ret(20, 0);
    snprintf(
            ret.data(), 19, "%7.2lf %s", szDbl, sizeSuffixes[suffixPtr].data());
    return std::string(ret.data());
}

static unsigned long long suffixToMultiplier(const std::string& suffix)
{
    if (suffix == "K" || suffix == "KB") {
        return 1000ULL;
    } else if (suffix == "M" || suffix == "MB") {
        return 1000ULL * 1000;
    } else if (suffix == "G" || suffix == "GB") {
        return 1000ULL * 1000 * 1000;
    } else if (suffix == "T" || suffix == "TB") {
        return 1000ULL * 1000 * 1000 * 1000;
    } else if (suffix == "KiB") {
        return 1ULL << 10;
    } else if (suffix == "MiB") {
        return 1ULL << 20;
    } else if (suffix == "GiB") {
        return 1ULL << 30;
    } else if (suffix == "TiB") {
        return 1ULL << 40;
    }
    throw InvalidArgsException(
            "Unknown size suffix '" + suffix
            + "'. Use K/KB/KiB, M/MB/MiB, G/GB/GiB, or T/TB/TiB.");
}

template <typename Func>
decltype(auto) checkedInner(const std::string& str, Func&& func)
{
    if (str.empty()) {
        throw InvalidArgsException("Size string must not be empty.");
    }
    size_t pos = 0;
    decltype(func(str, &pos)) value;
    try {
        value = func(str, &pos);
    } catch (const std::invalid_argument&) {
        throw InvalidArgsException("Not a valid number: '" + str + "'.");
    } catch (const std::out_of_range&) {
        throw InvalidArgsException("Value out of range: '" + str + "'.");
    }
    if (pos < str.size()) {
        std::string potentialSuffix = str.substr(pos);
        auto maybeMultiplier        = suffixToMultiplier(potentialSuffix);
        if (maybeMultiplier > static_cast<unsigned long long>(
                    std::numeric_limits<decltype(value)>::max())) {
            throw InvalidArgsException("Value overflows: '" + str + "'.");
        }
        auto multiplier = static_cast<decltype(value)>(maybeMultiplier);
        if (value > std::numeric_limits<decltype(value)>::max() / multiplier) {
            throw InvalidArgsException("Value overflows: '" + str + "'.");
        }
        if (value < std::numeric_limits<decltype(value)>::min() / multiplier) {
            throw InvalidArgsException("Neg. value overflows: '" + str + "'.");
        }
        value *= multiplier;
    }
    return value;
}

int checkedstoi(const std::string& str)
{
    return checkedInner(str, [](const std::string& s, size_t* pos) {
        return std::stoi(s, pos);
    });
}
long checkedstol(const std::string& str)
{
    return checkedInner(str, [](const std::string& s, size_t* pos) {
        return std::stol(s, pos);
    });
}
unsigned long checkedstoul(const std::string& str)
{
    return checkedInner(str, [](const std::string& s, size_t* pos) {
        return std::stoul(s, pos);
    });
}
long long checkedstoll(const std::string& str)
{
    return checkedInner(str, [](const std::string& s, size_t* pos) {
        return std::stoll(s, pos);
    });
}

unsigned long long checkedstoull(const std::string& str)
{
    return checkedInner(str, [](const std::string& s, size_t* pos) {
        return std::stoull(s, pos);
    });
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

} // namespace openzl::cli::util
