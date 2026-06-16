// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <unordered_set>

#include "openzl/common/logging.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/poly/StringView.hpp"

#include "custom_parsers/csv/csv_profile.h"
#include "custom_parsers/dependency_registration.h"
#include "custom_parsers/parquet/parquet_graph.h"

namespace openzl::custom_parsers {

void processDependencies(Compressor& compressor, poly::string_view serialized)
{
    std::unordered_set<std::string> attemptedGraphDeps;

    while (true) {
        const auto deps   = compressor.getUnmetDependencies(serialized);
        bool madeProgress = false;

        for (const auto& graphName : deps.graphNames) {
            if (!attemptedGraphDeps.insert(graphName).second) {
                continue;
            }

            if (graphName == "Parquet Parser") {
                auto parquetResult = ZL_Parquet_registerGraph(
                        compressor.get(), ZL_GRAPH_STORE);
                if (parquetResult == ZL_GRAPH_ILLEGAL) {
                    throw std::runtime_error("Failed to create parquet graph");
                }
                madeProgress = true;
            }

            else if (graphName == "CSV Parser") {
                auto csvResult =
                        ZL_createGraph_genericCSVCompressor(compressor.get());
                if (csvResult == ZL_GRAPH_ILLEGAL) {
                    throw std::runtime_error("Failed to create CSV graph");
                }
                madeProgress = true;
            } else {
                ZL_LOG(WARN,
                       "processDependencies: unresolved graph dependency '%s'",
                       graphName.c_str());
            }
        }

        if (!madeProgress) {
            if (deps.nodeNames.size() > 0) {
                // TODO register any non-standard nodes that may appear in a
                // compressor
            }
            return;
        }
    }
}

std::unique_ptr<Compressor> createCompressorFromSerialized(
        poly::string_view serialized,
        poly::string_view fatBundle)
{
    auto compressor = std::make_unique<Compressor>();
    processDependencies(*compressor, serialized);
    compressor->deserialize(serialized, fatBundle);
    return compressor;
}

} // namespace openzl::custom_parsers
