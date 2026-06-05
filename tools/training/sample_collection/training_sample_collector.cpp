// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <map>
#include <vector>

#include "openzl/cpp/Exception.hpp"
#include "openzl/zl_compress.h"

#include "tools/logger/Logger.h"
#include "tools/training/sample_collection/introspection_hooks.h"
#include "tools/training/sample_collection/training_sample_collector.h"

namespace openzl::training {

using namespace tools::logger;

std::map<std::string, std::vector<MultiInput>> collectInputStreams(
        const std::vector<MultiInput>& inputs,
        const std::vector<std::string>& graphNames,
        const std::vector<std::string>& nodeNames,
        CCtx& cctx)
{
    if (graphNames.empty() && nodeNames.empty()) {
        return {};
    }

    // double-check for duplicates
    {
        std::unordered_set<std::string> names;
        for (const auto& graphName : graphNames) {
            if (names.find(graphName) != names.end()) {
                throw Exception("Duplicate graph name: " + graphName);
            }
            names.insert(graphName);
        }
        for (const auto& nodeName : nodeNames) {
            if (names.find(nodeName) != names.end()) {
                throw Exception("Duplicate node name: " + nodeName);
            }
            names.insert(nodeName);
        }
    }

    Logger::log_c(
            VERBOSE1,
            "Collecting input streams for %zu graphs and %zu nodes",
            graphNames.size(),
            nodeNames.size());

    std::map<std::string, std::vector<MultiInput>> result;

    for (const auto& mi : inputs) {
        SampleCollectionHook hooks(graphNames, nodeNames);
        openzl::unwrap(
                ZL_CCtx_attachIntrospectionHooks(
                        cctx.get(), hooks.getRawHooks()),
                "Failed to attach introspection hooks",
                cctx.get());

        cctx.compress(*mi);

        for (const auto& [name, samples] : hooks.getInputs()) {
            for (auto& sample : samples) {
                result[name].push_back(sample);
            }
        }

        openzl::unwrap(
                ZL_CCtx_detachAllIntrospectionHooks(cctx.get()),
                "Failed to detach introspection hooks",
                cctx.get());
    }

    return result;
}

std::vector<MultiInput> collectInputStreamsForGraph(
        const std::vector<MultiInput>& inputs,
        const std::string& untrainedGraphName,
        CCtx& cctx)
{
    auto map = collectInputStreams(inputs, { untrainedGraphName }, {}, cctx);
    return std::move(map[untrainedGraphName]);
}

std::map<std::string, std::vector<MultiInput>> collectInputStreamsForGraphs(
        const std::vector<MultiInput>& inputs,
        const std::vector<std::string>& untrainedGraphNames,
        CCtx& cctx)
{
    return collectInputStreams(inputs, untrainedGraphNames, {}, cctx);
}

std::vector<MultiInput> collectInputStreamsForNode(
        const std::vector<MultiInput>& inputs,
        const std::string& nodeName,
        CCtx& cctx)
{
    auto map = collectInputStreams(inputs, {}, { nodeName }, cctx);
    return std::move(map[nodeName]);
}

} // namespace openzl::training
