// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstring>
#include <map>
#include <stdexcept>

#include "openzl/zl_graph_api.h"
#include "openzl/zl_reflection.h"

#include "tools/logger/Logger.h"
#include "tools/training/sample_collection/introspection_hooks.h"

namespace openzl::training {

using namespace tools::logger;

void UntrainedGraphHook::on_migraphEncode_start(
        ZL_Graph* /* gctx */,
        const ZL_Compressor* compressor,
        ZL_GraphID gid,
        ZL_Edge* inputs[],
        size_t nbInputs)
{
    std::string graphName = ZL_Compressor_Graph_getName(compressor, gid);
    if (graphName.empty()) {
        throw Exception("Graph name is null!");
    }

    bool isTargetGraph = false;
    for (const auto& targetGraphName : targetGraphNames_) {
        if (graphName == targetGraphName) {
            isTargetGraph = true;
            break;
        }
    }

    if (!isTargetGraph) {
        return;
    }

    Logger::log_c(
            VERBOSE1,
            "Capturing %zu inputs for target graph: %s",
            nbInputs,
            graphName.c_str());

    // Each time a graph is called, we should record a separate set of inputs
    // for that graph.
    auto encodeInputs = MultiInput();
    for (size_t i = 0; i < nbInputs; ++i) {
        if (!inputs[i]) {
            errorMessage_ = "Input is null at index " + std::to_string(i);
            return;
        }

        const ZL_Input* edgeInputData = ZL_Edge_getData(inputs[i]);
        if (!edgeInputData) {
            errorMessage_ =
                    "Input at index " + std::to_string(i) + " has no data";
            Logger::log(ERRORS, errorMessage_);
            return;
        }
        encodeInputs.add(InputCopy(edgeInputData));
    }
    inputs_[graphName].push_back(std::move(encodeInputs));
}

const std::map<std::string, std::vector<MultiInput>>&
UntrainedGraphHook::getInputs() const
{
    if (!errorMessage_.empty()) {
        Logger::log(ERRORS, "Error message present: ", errorMessage_);
        throw Exception("Failed to get inputs: " + errorMessage_);
    }

    return inputs_;
}

} // namespace openzl::training
