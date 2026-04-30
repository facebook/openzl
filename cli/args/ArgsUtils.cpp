// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <filesystem>

#include "cli/args/ArgsUtils.h"
#include "cli/utils/util.h"

#include "custom_parsers/dependency_registration.h"

#include "tools/io/InputFile.h"
#include "tools/logger/Logger.h"

namespace openzl {
namespace cli {

using std::filesystem::file_type;
using namespace openzl::tools::logger;

void checkOutput(const std::string& path, bool force)
{
    const auto filetype = std::filesystem::status(path).type();
    if (filetype == file_type::none) {
        std::string msg = "Could not stat output file '" + path + "'";
        throw InvalidArgsException(msg);
    }
    bool shouldAllowOverwriting = filetype == file_type::block
            || filetype == file_type::character || filetype == file_type::fifo
            || filetype == file_type::socket;

    if (filetype != file_type::not_found && !shouldAllowOverwriting && !force) {
        throw InvalidArgsException(
                "Output file already exists. Use --force to overwrite.");
    }
}

std::unique_ptr<Compressor> createCompressorFromArgs(
        const ProfileArgs& profileArgs,
        const std::optional<std::string>& compressorPath)
{
    if (profileArgs.name() && compressorPath) {
        throw InvalidArgsException(
                "Both compressor profile and serialized compressor specified. Please provide only one.");
    }

    if (profileArgs.name()) {
        if (profileArgs.chunkSize()) {
            const auto profileName = profileArgs.name().value();
            const auto profile     = compressProfiles().find(profileName);
            if (profile != compressProfiles().end()
                && !profile->second->supportsChunkSize) {
                Logger::log(
                        INFO,
                        "Profile '" + profileName
                                + "' does not support --chunk-size; ignoring the flag.");
            }
        }
        return util::createCompressorFromProfile(profileArgs);
    }

    if (compressorPath) {
        auto compressorInput =
                std::make_shared<tools::io::InputFile>(compressorPath.value());
        return custom_parsers::createCompressorFromSerialized(
                compressorInput->contents());
    }

    throw InvalidArgsException(
            "No compressor profile or serialized compressor specified.");
}
} // namespace cli
} // namespace openzl
