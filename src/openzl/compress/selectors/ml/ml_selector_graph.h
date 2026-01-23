// Copyright (c) Meta Platforms, Inc. and affiliates.
// Note: This file is work in progress and is not ready for use yet.

#ifndef OPENZL_TOOLS_ML_SELECTOR_GRAPH_H
#define OPENZL_TOOLS_ML_SELECTOR_GRAPH_H

#include "openzl/compress/selectors/ml/gbt.h"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_graph_api.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define ZL_GENERIC_ML_SELECTOR_CONFIG_ID 555

typedef enum {
    ZL_GBT,
} ZL_MLSelectorModelType;

/**
 * A serializable configuration used to select a successor.
 */
typedef struct {
    ZL_MLSelectorModelType model;
    void* runtimeConfig;
} ZL_MLSelectorConfig;

/**
 * A buffer containing serialized ml selector config
 */
typedef struct {
    char* data;  // Pointer to the serialized data
    size_t size; // Size of the serialized data
} ZL_SerializedMLConfig;

ZL_RESULT_DECLARE_TYPE(ZL_MLSelectorConfig);
ZL_RESULT_DECLARE_TYPE(ZL_SerializedMLConfig);

/** @brief Serializes the @p config using @p a1cArena for allocations.
 *  All allocated memory is tied to @p a1cArena 's underlying  arena. Serialized
 * data remains valid until arena is freed. When caller frees the arena, all
 * memory is cleaned up.
 *
 * @returns Failure if unable to serialize. On success returns success status
 * and the serialized config.
 * @param errCtx Error context for reporting errors
 * @param config The config to be serialized
 * @param a1cArena The arena wrapper in which memory allocations for
 * serialization happens
 */
ZL_RESULT_OF(ZL_SerializedMLConfig)
MLSelector_serializeMLSelectorConfig(
        ZL_ErrorContext* errCtx,
        const ZL_MLSelectorConfig* config,
        A1C_Arena* a1cArena);

/** @brief Deserializes the @p config and returns the result. Uses @p a1cArena
 * to initialize decoder, memory is automatically cleaned when graph execution
 * completes.
 *
 * @returns Failure if the config is invalid or an allocation fails. On success
 * returns success status and the deserialized config.
 * @param errCtx Error context for reporting errors
 * @param config The config to be deserialized
 * @param configSize The size of @p config
 * @param a1cArena The arena wrapper needed for deserialization
 */
ZL_RESULT_OF(ZL_MLSelectorConfig)
MLSelector_deserializeMLSelectorConfig(
        ZL_ErrorContext* errCtx,
        const char* config,
        size_t configSize,
        A1C_Arena* a1cArena);

/**
 * @brief Registers a ml selector graph. This graph selects successor
 * specified by the config.
 *
 * @returns The graph ID registered for the ml selector graph
 * @param compressor The compressor to register the graph with
 * @param config The ml selector configuration
 * @param successors The set of successors to send to
 * @param nbSuccessors The number of successors
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_MLSelector_registerGraph(
        ZL_Compressor* compressor,
        const ZL_MLSelectorConfig* config,
        const ZL_GraphID* successors,
        size_t nbSuccessors);

/** @brief Retrieves list of successors and ZL_MLSelectorConfig from graph and
 * selects successor based on prediction made by model specified inside the
 * ZL_MLSelectorConfig.
 *
 * @param graph      Graph containing ZL_MLSelectorConfig and list of successors
 * @param inputs     Array of input edges to be routed to selected successor
 * @param nbInputs   Number of input edges in the inputs array
 * @return           Failure if unable to get config from graph or if the
 * selected successor is out of bounds or if unable to select successor. Success
 * otherwise.
 */
ZL_Report
ZL_MLSel_dynGraph(ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs);

#if defined(__cplusplus)
}
#endif

#endif // OPENZL_TOOLS_ML_SELECTOR_GRAPH_H
