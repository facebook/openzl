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

namespace {

/**
 * Sets up introspection hooks for capturing inputs to an untrained graph node.
 *
 * @param cctx Compression context to attach hooks to
 * @param untrainedGraphName Name of the untrained graph node
 * @return A hook object that can be used to retrieve captured inputs
 */
UntrainedGraphHook setupIntrospectionHooks(
        CCtx& cctx,
        const std::vector<std::string>& untrainedGraphNames)
{
    UntrainedGraphHook hooks(untrainedGraphNames);
    ZL_CompressIntrospectionHooks* rawHooks = hooks.getRawHooks();
    openzl::unwrap(
            ZL_CCtx_attachIntrospectionHooks(cctx.get(), rawHooks),
            "Failed to attach introspection hooks",
            cctx.get());
    return hooks;
}

/**
 * Processes a single multi-input sample and updates the input streams map.
 *
 * @param input Multi-input sample to process
 * @param cctx Compression context to use
 * @param hooks Hook object to capture inputs from
 * @param inputStreamsPerSampleFilePerGraph Map to update with captured inputs
 */
void captureInputs(
        const MultiInput& input,
        CCtx& cctx,
        UntrainedGraphHook& hooks,
        std::map<std::string, std::vector<MultiInput>>& samplesPerGraph)
{
    cctx.compress(*input);
    const auto& captured = hooks.getInputs();

    for (const auto& [graphName, samples] : captured) {
        auto [it, _] =
                samplesPerGraph.emplace(graphName, std::vector<MultiInput>());
        for (auto& sample : samples) {
            it->second.push_back(sample);
        }
    }
}

} // anonymous namespace

std::vector<MultiInput> collectInputStreamsForGraph(
        const std::vector<MultiInput>& inputs,
        const std::string& untrainedGraphName,
        CCtx& cctx)
{
    auto map =
            collectInputStreamsForGraphs(inputs, { untrainedGraphName }, cctx);
    return std::move(map[untrainedGraphName]);
}

/**
 * Collects input streams from multi-input samples for training multiple
 * unconfigured nodes.
 *
 * @param inputs Set of multi-input samples to process
 * @param untrainedGraphNames Names of the unconfigured graph nodes to train
 * @param cctx Compression context to use for processing samples
 * @return Map from graph name to vector of streams per sample
 */

std::map<std::string, std::vector<MultiInput>> collectInputStreamsForGraphs(
        const std::vector<MultiInput>& inputs,
        const std::vector<std::string>& untrainedGraphNames,
        CCtx& cctx)
{
    Logger::log_c(
            VERBOSE1,
            "Collecting input streams for %zu graphs",
            untrainedGraphNames.size());

    std::map<std::string, std::vector<MultiInput>> samplesPerGraph;

    for (const auto& mi : inputs) {
        auto hooks = setupIntrospectionHooks(cctx, untrainedGraphNames);
        captureInputs(mi, cctx, hooks, samplesPerGraph);
    }

    openzl::unwrap(
            ZL_CCtx_detachAllIntrospectionHooks(cctx.get()),
            "Failed to detach introspection hooks",
            cctx.get());

    return samplesPerGraph;
}

} // namespace openzl::training
