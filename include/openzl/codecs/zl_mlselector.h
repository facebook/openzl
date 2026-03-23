// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_MLSELECTOR_H
#define ZSTRONG_CODECS_MLSELECTOR_H

#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define ZL_GRAPH_ML_SELECTOR ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_ml_selector)

/**
 * @brief Builds an untrained ML selector graph.
 *
 * The ML selector uses an XGBoost model to predict which successor to use for
 * compression. Until trained, this selector always selects the first successor.
 *
 * Supported types: Numeric integer data.
 *
 * Training workflow:
 *   1. Build your compressor with an ML selector graph using this function
 *   2. Wrap the resulting graph with ZL_NODE_CONVERT_SERIAL_TO_NUM_LE# (for
 *      #-bit data) and parameterize using ZL_Compressor_parameterizeGraph
 *   3. Serialize the compressor: compressor.serialize() -> save to file.zlc
 *   4. Train: ./zli train --compressor file.zlc <samples> -o trained.zli
 *   5. Use:   ./zli compress --compressor trained.zli <input> -o <output.zl>
 *
 * Alternatively, use the built-in profile for 64-bit numeric data:
 *   ./zli train --profile numeric-ml-selector-64 <samples> -o trained.zli
 *
 * Note: Successor ordering must stay the same between training and inference.
 *
 * See tools/ml_selector/README.md for more details and example.
 *
 * @param compressor The compressor to register the graph with
 * @param successors The set of successor graphs to choose from
 * @param nbSuccessors The number of successors
 * @return The graph ID of the registered ML selector, or an error
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildUntrainedMLSelector(
        ZL_Compressor* compressor,
        const ZL_GraphID* successors,
        size_t nbSuccessors);

#if defined(__cplusplus)
}
#endif

#endif // ZSTRONG_CODECS_MLSELECTOR_H
