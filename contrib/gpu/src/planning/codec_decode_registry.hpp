// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>

#include "contrib/gpu/src/common/codec_decode_planning.hpp"

namespace openzl::gpu {

/**
 * Resolves a wire transform and frame version to GPU decode planning support.
 *
 * @param transform Exact transform namespace and ID decoded from the node.
 * @param formatVersion Frame format version used for compatibility lookup.
 * @returns The matching planner, or nullptr when the transform is unsupported.
 */
CodecDecodePlanningFn* findCodecDecodePlanner(
        PublicTransformInfo transform,
        uint32_t formatVersion);

} // namespace openzl::gpu
