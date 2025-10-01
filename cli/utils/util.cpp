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

    if (args.name.empty()) {
        throw InvalidArgsException(
                "Please provide a profile. See `zli list-profiles` for a list of supported profiles.");
    }

    auto profile = compressProfiles().find(args.name);
    if (profile == compressProfiles().end()) {
        throw InvalidArgsException(
                "Profile not found: '" + args.name
                + "'. See `zli list-profiles` for a list of supported profiles.");
    }

    auto graphId = profile->second->gen(
            compressor->get(), profile->second->opaque.get(), args);
    compressor->selectStartingGraph(graphId);

    return compressor;
}

} // namespace openzl::cli::util
