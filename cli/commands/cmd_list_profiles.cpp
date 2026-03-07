// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <iomanip>
#include <sstream>

#include "cli/commands/cmd_list_profiles.h"
#include "cli/utils/compress_profiles.h"
#include "tools/logger/Logger.h"

namespace openzl {
namespace cli {

using namespace openzl::tools::logger;

int cmdListProfiles(const ListProfilesArgs&)
{
    std::stringstream ss;
    ss << "Available profiles:\n";
    
    // Find the maximum profile name length for proper alignment
    size_t maxNameLen = 0;
    for (auto const& [_, profile] : compressProfiles()) {
        maxNameLen = std::max(maxNameLen, profile->name.length());
    }
    
    // Print each profile with proper spacing
    for (auto const& [_, profile] : compressProfiles()) {
        auto const& name = profile->name;
        auto const& desc = profile->description;
        ss << "  -| " << std::left << std::setw(maxNameLen) << name << " = " << desc << "\n";
    }
    Logger::log(ALWAYS, ss.str());
    return 0;
}
} // namespace cli
} // namespace openzl
