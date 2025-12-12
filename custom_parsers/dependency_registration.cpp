// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/poly/StringView.hpp"

#include "custom_parsers/csv/csv_profile.h"
#include "custom_parsers/dependency_registration.h"
#include "custom_parsers/parquet/parquet_graph.h"
#include "tools/ml_selector/ml_selector_graph.h"

namespace openzl::custom_parsers {

void processDependencies(Compressor& compressor, poly::string_view serialized)
{
    const auto deps = compressor.getUnmetDependencies(serialized);

    for (const auto& graphName : deps.graphNames) {
        if (graphName == "Parquet Parser") {
            // Parquet graph
            auto parquetResult =
                    ZL_Parquet_registerGraph(compressor.get(), ZL_GRAPH_STORE);
            if (parquetResult == ZL_GRAPH_ILLEGAL) {
                throw std::runtime_error("Failed to create parquet graph");
            }
        }

        if (graphName == "CSV Parser") {
            // CSV graph
            auto csvResult =
                    ZL_createGraph_genericCSVCompressor(compressor.get());
            if (csvResult == ZL_GRAPH_ILLEGAL) {
                throw std::runtime_error("Failed to create CSV graph");
            }
        }

        if (graphName == "mlSelector") {
            // ML Selector
            compressor.unwrap(
                    ZL_MLSelector_registerBaseGraph(compressor.get()));
        }
    }

    if (deps.nodeNames.size() > 0) {
        // TODO register any non-standard nodes that may appear in a
        // compressor
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
