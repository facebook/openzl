// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <memory>

#include "custom_parsers/dependency_registration.h"

#include "openzl/cpp/Compressor.hpp"

#include "cli/utils/compress_profiles.h"
#include "cli/utils/util.h"

namespace openzl::cli::util {

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
