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

void SampleCollectionHook::on_migraphEncode_start(
        ZL_Graph* /* gctx */,
        const ZL_Compressor* compressor,
        ZL_GraphID gid,
        ZL_Edge* inputs[],
        size_t nbInputs)
{
    if (targetGraphNames_.empty()) {
        return;
    }

    std::string graphName = ZL_Compressor_Graph_getName(compressor, gid);
    if (graphName.empty()) {
        throw Exception("Graph name is null!");
    }

    bool isTarget = false;
    for (const auto& target : targetGraphNames_) {
        if (graphName == target) {
            isTarget = true;
            break;
        }
    }

    if (!isTarget) {
        return;
    }

    Logger::log_c(
            VERBOSE1,
            "Capturing %zu inputs for target graph: %s",
            nbInputs,
            graphName.c_str());

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

void SampleCollectionHook::on_codecEncode_start(
        ZL_Encoder* /* eictx */,
        const ZL_Compressor* compressor,
        ZL_NodeID nid,
        const ZL_Input* inStreams[],
        size_t nbInStreams)
{
    if (targetNodeNames_.empty()) {
        return;
    }

    const char* name = ZL_Compressor_Node_getName(compressor, nid);
    if (name == nullptr || name[0] == '\0') {
        return;
    }

    std::string nodeName(name);
    bool isTarget = false;
    for (const auto& target : targetNodeNames_) {
        if (nodeName == target) {
            isTarget = true;
            break;
        }
    }

    if (!isTarget) {
        return;
    }

    Logger::log_c(
            VERBOSE1,
            "Capturing %zu inputs for target node: %s",
            nbInStreams,
            nodeName.c_str());

    auto encodeInputs = MultiInput();
    for (size_t i = 0; i < nbInStreams; ++i) {
        if (!inStreams[i]) {
            errorMessage_ = "Node input is null at index " + std::to_string(i);
            return;
        }
        encodeInputs.add(InputCopy(inStreams[i]));
    }
    inputs_[nodeName].push_back(std::move(encodeInputs));
}

const std::map<std::string, std::vector<MultiInput>>&
SampleCollectionHook::getInputs() const
{
    if (!errorMessage_.empty()) {
        Logger::log(ERRORS, "Error message present: ", errorMessage_);
        throw Exception("Failed to get inputs: " + errorMessage_);
    }

    return inputs_;
}

} // namespace openzl::training
