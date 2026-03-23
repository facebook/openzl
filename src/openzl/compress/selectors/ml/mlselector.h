// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_COMPRESS_SELECTORS_MLSELECTOR_H
#define ZSTRONG_COMPRESS_SELECTORS_MLSELECTOR_H

#include "openzl/common/stream.h" //ZL_Type
#include "openzl/shared/portability.h"
#include "openzl/zl_opaque_types.h" //ZL_GraphID

ZL_BEGIN_C_DECLS

/**
 * Descriptor for a ML model, would allow us to support
 * multiple / custom models in the future
 */
typedef struct ZL_MLModelDesc ZS2_MLModel_Desc;

/**
 * Defines type ZL_MLModelPredictFn, which is a function that takes an input
 * stream and generates various features from it. These features are then used
 * to make a prediction using the model's predict and opaque fields. The
 * corresponding predicted successor indice is returned.
 @returns size_t specifying the index of the predicted successor
 */
typedef size_t (*ZL_MLModelPredictFn)(const void* opaque, const ZL_Input* in);

/**
 * Defines type ZL_MLModelFreeFn, which is a function that frees the model
 * using the free function specified inside the model. If the free function is
 * NULL, then function just returns.
 */
typedef void (*ZL_MLModelFreeFn)(const void* opaque);

struct ZL_MLModelDesc {
    ZL_MLModelPredictFn predict; // The function to call for prediction

    /**
     * Optional free function (can be NULL), called to free the opaque pointer
     * when the selector is destroyed.
     */
    ZL_MLModelFreeFn free;

    /**
     * Optional pointer for more context. For example, a GBTModel
     * may be assigned here to be used by the predict function
     */
    const void* opaque;
};

typedef struct {
    /**
     * Corresponding label for each graph - used to match result
     * from MLModel prediction and return the corresponding graph
     */
    const char* label;
    const ZL_GraphID graph;
} ZL_LabeledGraphID;

typedef struct {
    ZS2_MLModel_Desc model; // The model to used
    ZL_Type inStreamType;   // The stream type that the selector expects
    /// The labeled graphs that the selector can use
    const ZL_LabeledGraphID* graphs;
    size_t nbGraphs;           // The number of graphs
    unsigned minFormatVersion; //< @see ZL_SelectorDesc::minFormatVersion
    unsigned maxFormatVersion; //< @see ZL_SelectorDesc::maxFormatVersion
    const char* name;          // Optional name for the selector
} ZL_MLSelectorDesc;

/**
 * Creates a typed selector based on the information in the ZL_MLSelectorDesc.
 * NOTE: This function takes ownership of `desc->model`.
 *
 * @param desc Create the selector based on this description.
 * This function takes ownership of `desc->model`, but not any
 * other member of the struct.
 *
 * @returns The Graph ID of the newly created selector.
 */
ZL_GraphID ZL_Compressor_registerMLSelectorGraph(
        ZL_Compressor* cgraph,
        const ZL_MLSelectorDesc* desc);

ZL_END_C_DECLS

#endif
