// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "openzl/zl_opaque_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace openzl::visualizer {

/**
 * NOTE: These are *unrelated* to the codec/graph IDs generated at compressor
 * registration time. They are closer to the CNode IDs used internally, but are
 * also unrelated. These IDs are solely to number these objects within the
 * trace.
 */

using StreamID = ZL_DataID;
using CodecID  = size_t;
using GraphID  = size_t;

using StreamPreview = std::variant<
        std::vector<uint8_t>, // default
        std::vector<int64_t>,
        std::vector<std::string>>;

} // namespace openzl::visualizer
