// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/zl_compressor.h"
#include "openzl/zl_selector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The int metadata id / key that should be set with the index of the successor
 * to use.
 */
static const int kDirectedSelectorMetadataID = 0;

/**
 * The directed selector implementation function.
 * Selects a successor graph based on integer metadata at
 * kDirectedSelectorMetadataID.
 */
ZL_GraphID directed_selector_impl(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

/**
 * Registers the base directed selector graph if not already registered.
 * The base graph is serializable and can be parameterized with successors.
 *
 * @param compressor The compressor to register the graph with.
 * @return The base graph ID, or ZL_GRAPH_ILLEGAL on error.
 */
ZL_GraphID registerDirectedSelectorBaseGraph(ZL_Compressor* compressor);

/**
 * A directed selector graph that selects a successor based on integer metadata.
 * The selector expects to receive direction as to which successor to select
 * in the form of an integer metadata on the input stream (at
 * kDirectedSelectorMetadataID).
 *
 * This function registers a base graph (if not already registered) and returns
 * a parameterized graph with the provided successors. The base graph is
 * serializable.
 *
 * @param compressor The compressor to register the graph with.
 * @param successors Array of successor graph IDs to select from.
 * @param nbSuccessors Number of successors in the array.
 * @return The parameterized graph ID, or ZL_GRAPH_ILLEGAL on error.
 */
ZL_GraphID registerDirectedSelectorGraph(
        ZL_Compressor* compressor,
        const ZL_GraphID* successors,
        size_t nbSuccessors);

#ifdef __cplusplus
}
#endif
