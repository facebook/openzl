// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <string_view>

#include <openzl/openzl.hpp>

namespace openzl::lz {

std::string getSerializedCompressor(std::string_view name);

void registerGraphComponents(openzl::Compressor& compressor);
void registerCustomCodecs(openzl::DCtx& dctx);

} // namespace openzl::lz
