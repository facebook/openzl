// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "LzCompressors.hpp"

namespace openzl::lz {

std::string getSerializedCompressor(std::string_view name)
{
    throw std::runtime_error("TODO");
}

void registerGraphComponents(openzl::Compressor&) {}

void registerCustomCodecs(openzl::DCtx&) {}

} // namespace openzl::lz
