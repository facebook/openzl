// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <map>
#include <vector>

#include "tools/training/utils/utils.h"

namespace openzl::training {

/**
 * Collects input streams from a set of multi-input samples for training.
 *
 * This function compresses the samples using the provided compression
 * context, and captures the input streams that would be processed by
 * the targeted graphs and codec nodes. These input streams can then
 * be used for training.
 *
 * Graph targets intercept at graph entry (on_migraphEncode_start).
 * Node targets intercept at codec entry (on_codecEncode_start).
 * Both may be specified simultaneously; the results are merged into a
 * single map keyed by graph name or node name.
 *
 * @param inputs        Multi-input samples to compress.
 * @param graphNames    Names of graphs to capture inputs for (may be empty).
 * @param nodeNames     Names of codec nodes to capture inputs for (may be
 *                      empty).
 * @param cctx          Compression context to use for processing samples.
 * @return Map from target name (graph or node) to captured samples.
 */
std::map<std::string, std::vector<MultiInput>> collectInputStreams(
        const std::vector<MultiInput>& inputs,
        const std::vector<std::string>& graphNames,
        const std::vector<std::string>& nodeNames,
        CCtx& cctx);

/// Convenience: collect input streams for a single graph.
std::vector<MultiInput> collectInputStreamsForGraph(
        const std::vector<MultiInput>& inputs,
        const std::string& untrainedGraphName,
        CCtx& cctx);

/// Convenience: collect input streams for multiple graphs.
std::map<std::string, std::vector<MultiInput>> collectInputStreamsForGraphs(
        const std::vector<MultiInput>& inputs,
        const std::vector<std::string>& untrainedGraphNames,
        CCtx& cctx);

/// Convenience: collect input streams for a single codec node.
std::vector<MultiInput> collectInputStreamsForNode(
        const std::vector<MultiInput>& inputs,
        const std::string& nodeName,
        CCtx& cctx);

} // namespace openzl::training
