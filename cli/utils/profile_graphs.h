// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>

#include "openzl/zl_opaque_types.h"

namespace openzl::profiles {

ZL_GraphID buildSerialGraph(ZL_Compressor* compressor, size_t chunkByteSize);

ZL_GraphID buildIntGraph(
        ZL_Compressor* compressor,
        size_t eltByteWidth,
        ZL_NodeID conversionNode,
        size_t chunkByteSize);

} // namespace openzl::profiles
