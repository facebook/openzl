// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/poly/StringView.hpp"

#include "custom_parsers/csv/csv_profile.h"
#include "custom_parsers/dependency_registration.h"
#include "custom_parsers/parquet/parquet_graph.h"
#include "custom_parsers/shared_components/clustering.h"

namespace openzl::custom_parsers {

void processDependencies(Compressor& compressor, poly::string_view serialized)
{
    const auto deps = compressor.getUnmetDependencies(serialized);
    if (deps.graphNames.size() > 0) {
        // Generic clustering graph
        auto clustering = ZL_createGraph_genericClustering(compressor.get());
        if (clustering == ZL_GRAPH_ILLEGAL) {
            throw std::runtime_error(
                    "Failed to create generic clustering graph");
        }

        // Parquet graph
        auto parquetResult =
                ZL_Parquet_registerGraph(compressor.get(), ZL_GRAPH_STORE);
        if (parquetResult == ZL_GRAPH_ILLEGAL) {
            throw std::runtime_error("Failed to create parquet graph");
        }

        // CSV graph
        auto csvResult = ZL_createGraph_genericCSVCompressor(compressor.get());
        if (csvResult == ZL_GRAPH_ILLEGAL) {
            throw std::runtime_error("Failed to create CSV graph");
        }

        // TODO register any additional non-standard graphs that may appear in a
        // compressor
    }

    if (deps.nodeNames.size() > 0) {
        // TODO register any non-standard nodes that may appear in a compressor
    }
}
std::unique_ptr<Compressor> createCompressorFromSerialized(
        poly::string_view serialized)
{
    auto compressor = std::make_unique<Compressor>();
    processDependencies(*compressor, serialized);
    compressor->deserialize(serialized);
    return compressor;
}
} // namespace openzl::custom_parsers
