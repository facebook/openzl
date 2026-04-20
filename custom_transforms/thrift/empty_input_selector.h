// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/zl_compressor.h"
#include "openzl/zl_selector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A selector that behaves as follows:
 *
 *   if (input.empty()) {
 *     return successors[0];
 *   } else {
 *     return successors[1];
 *   }
 */
ZL_SelectorDesc buildEmptyInputSelectorDesc(
        ZL_Type type,
        const ZL_GraphID* successors,
        size_t nbSuccessors);

/**
 * Registers the base empty input selector graph if not already registered.
 */
ZL_GraphID registerEmptyInputSelectorBaseGraph(ZL_Compressor* compressor);

/**
 * Registers a serializable empty input selector graph.
 * The base graph is registered as a named anchor, and the successors
 * are captured in a parameterized graph layer (serializable).
 *
 * @param successors Array of exactly 2 graph IDs: [empty, non-empty].
 * @param nbSuccessors Must be 2.
 */
ZL_GraphID registerEmptyInputSelectorGraph(
        ZL_Compressor* compressor,
        const ZL_GraphID* successors,
        size_t nbSuccessors);

#ifdef __cplusplus
}
#endif
