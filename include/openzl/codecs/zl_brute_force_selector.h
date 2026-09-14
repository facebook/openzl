// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_BRUTE_FORCE_SELECTOR_H
#define ZSTRONG_CODECS_BRUTE_FORCE_SELECTOR_H

#include <stddef.h>

#include "openzl/zl_errors.h"
#include "openzl/zl_graphs.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Selects the successor that produces the smallest compressed size, by trying
 * each of them in turn. It must be parameterized with the list of successors to
 * choose from, e.g. with @ref ZL_Compressor_buildBruteForceSelectorGraph().
 */
#define ZL_GRAPH_BRUTE_FORCE ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_brute_force)

/**
 * Parameterized brute force selector that selects the best successor from a
 * user-provided list of candidates.
 * @param successors the list of successors to select from. Successors that
 * can't handle the input stream type are skipped; if none apply the selector
 * falls back to @ref ZL_GRAPH_STORE.
 * @returns @ref ZL_GRAPH_BRUTE_FORCE parameterized with @p successors.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildBruteForceSelectorGraph(
        ZL_Compressor* cgraph,
        const ZL_GraphID* successors,
        size_t numSuccessors);

#if defined(__cplusplus)
}
#endif

#endif
