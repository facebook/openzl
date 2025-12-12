// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "tools/ml_selector/ml_selector_trainer.h"
#include <string>
#include <vector>
#include "openzl/zl_reflection.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"

namespace openzl::training {
const std::string ML_SELECTOR_GRAPH_NAME = "mlSelector";

std::shared_ptr<const std::string_view> trainMLSelectorGraph(
        const std::vector<MultiInput>& inputs,
        Compressor& compressor,
        const TrainParams& trainParams)
{
    (void)inputs;
    (void)trainParams;
    std::vector<ZL_GraphID> successorGraphs;
    std::vector<std::string> successorLabels;

    if (successorGraphs.empty()) {
        // Find the ML selector graph by prefix
        auto mlSelectorGraphNames = graph_mutation::findAllGraphsWithPrefix(
                compressor.serialize(), ML_SELECTOR_GRAPH_NAME);

        if (mlSelectorGraphNames.empty() || mlSelectorGraphNames.size() > 1) {
            throw std::runtime_error(
                    "Error finding ML selector graph with prefix '"
                    + ML_SELECTOR_GRAPH_NAME + "'");
        }

        // Use the first mlSelector graph found
        const ZL_GraphID mlSelectorGraph =
                compressor.getGraph(mlSelectorGraphNames[0]).value();
        const auto successors = ZL_Compressor_Graph_getCustomGraphs(
                compressor.get(), mlSelectorGraph);
        for (size_t i = 0; i < successors.nbGraphIDs; ++i) {
            successorGraphs.push_back(successors.graphids[i]);
            successorLabels.emplace_back(ZL_Compressor_Graph_getName(
                    compressor.get(), successorGraphs.back()));
        }
    }

    return graph_mutation::createSharedStringView(compressor.serialize());
}
} // namespace openzl::training
