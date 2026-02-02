// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <openzl/openzl.hpp>

namespace openzl::lz {

struct LzParameters {
    int llBits{ 3 };
};

NodeID registerLz(Compressor& compressor, LzParameters params);

void registerLz(DCtx& dctx);

} // namespace openzl::lz
