// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <filesystem>

#include "openzl/zl_version.h"

#ifdef ZL_IS_FBCODE
#    include "tools/cxx/Resources.h"
#endif

namespace openzl {
namespace protobuf {

std::filesystem::path getTestDataPath(const std::string& filename);

} // namespace protobuf
} // namespace openzl
