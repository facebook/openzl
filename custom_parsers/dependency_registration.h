// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/poly/StringView.hpp"

namespace openzl::custom_parsers {

/**
 * Helper function which can be called as part of deserializing a compressor
 * containing non-standard graphs or codecs.
 * This function can be called via calling createCompressorFromSerialized,
 * or if the caller has more dependencies to register, than it can be
 * called directly before registering those dependencies.
 * Graphs added to zstrong/custom_parsers/ should be added
 * to this function. It's okay for  dependencies to be registereed on the
 * compressor even if they are not part of the final graph of a compressor.
 */
void processDependencies(Compressor& compressor, poly::string_view serialized);

/**
 * This function should be used to deserialize compressors containing
 * non-standard graphs or codecs.
 */
std::unique_ptr<Compressor> createCompressorFromSerialized(
        poly::string_view serialized,
        poly::string_view fatBundle);

} // namespace openzl::custom_parsers
