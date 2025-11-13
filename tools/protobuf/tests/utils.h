// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <filesystem>
#ifdef OPENZL_BUCK_BUILD
#    include "tools/cxx/Resources.h"
#endif

namespace openzl {
namespace protobuf {

std::filesystem::path getTestDataPath(const std::string& filename);

} // namespace protobuf
} // namespace openzl
