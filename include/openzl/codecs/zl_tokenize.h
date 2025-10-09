// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_TOKENIZE_H
#define ZSTRONG_CODECS_TOKENIZE_H

#include <stdbool.h>
#include <stddef.h>

#include "openzl/zl_errors.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_stream.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Builds a tokenize node for the given parameters.
 *
 * Input: @p inputType
 * Output 0: @p inputType - alphabet of unique values
 * Output 1: numeric - indices into the alphabet for each value
 *
 * @param inputType The type of the input data. It can be either struct,
 * numeric, or string.
 * @param sort Whether or not to sort the alphabet. Struct types cannot be
 * sorted. Numeric types are sorted in ascending order. String types are sorted
 * in lexographical order.
 *
 * @returns The tokenize node, or an error.
 */
ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_parameterizeTokenizeNode(
        ZL_Compressor* compressor,
        ZL_Type inputType,
        bool sort);

/**
 * Builds a tokenize graph for the given parameters & successors.
 *
 * @note If sorting the alphabet is not beneficial avoid it, as the sort will
 * slow down compression.
 *
 * @param inputType The type of the input data. It can be either struct,
 * numeric, or string.
 * @param sort Whether or not to sort the alphabet. Struct types cannot be
 * sorted. Numeric types are sorted in ascending order. String types are sorted
 * in lexographical order.
 * @param alphabetGraph The graph to pass the alphabet output to. It must accept
 * an input of type @p inputType.
 * @param indicesGraph The graph to pass the indices to. It must accept a
 * numeric input.
 *
 * @returns The tokenize graph, or an error.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildTokenizeGraph(
        ZL_Compressor* compressor,
        ZL_Type inputType,
        bool sort,
        ZL_GraphID alphabetGraph,
        ZL_GraphID indicesGraph);

/// @see ZL_Compressor_buildTokenizeGraph
/// @returns The tokenize graph, or ZL_GRAPH_ILLEGAL on error.
ZL_GraphID ZL_Compressor_registerTokenizeGraph(
        ZL_Compressor* compressor,
        ZL_Type inputType,
        bool sort,
        ZL_GraphID alphabetGraph,
        ZL_GraphID indicesGraph);

/**
 * Parameter to control whether or not to sort the alphabet.
 * If equal to 0, or not present, don't sort the alphabet.
 * Otherwise, sort the alphabet.
 */
#define ZL_TOKENIZE_SORT_PID 0

/**
 * Tokenizes struct values into an alphabet of structs and indices.
 *
 * @note Sorting is not supported
 *
 * Input: struct
 * Output 0: struct - alphabet of unique values
 * Output 1: numeric - index into the alphabet stream for each value
 */
#define ZL_NODE_TOKENIZE_STRUCT           \
    (ZL_NodeID)                           \
    {                                     \
        ZL_StandardNodeID_tokenize_struct \
    }

/**
 * Tokenizes numeric values into an alphabet of structs and indices.
 * Optionally sorts the alphabet depending on `ZL_TOKENIZE_SORT_PID`.
 *
 * Input: numeric
 * Output 0: numeric - alphabet of sorted unique values
 * Output 1: numeric - index into the alphabet stream for each value
 */
#define ZL_NODE_TOKENIZE_NUMERIC           \
    (ZL_NodeID)                            \
    {                                      \
        ZL_StandardNodeID_tokenize_numeric \
    }

/**
 * Tokenizes string values into an alphabet of strings and indices.
 * Optionally sorts the alphabet depending on `ZL_TOKENIZE_SORT_PID`.
 *
 * Input: string
 * Output 0: string - alphabet of unique values
 * Output 1: numeric - index into the alphabet stream for each value
 */
#define ZL_NODE_TOKENIZE_STRING           \
    (ZL_NodeID)                           \
    {                                     \
        ZL_StandardNodeID_tokenize_string \
    }

typedef struct ZL_CustomTokenizeState_s ZL_CustomTokenizeState;

/**
 * @returns The opaque pointer passed into @fn ZL_createGraph_customTokenize().
 */
void const* ZL_CustomTokenizeState_getOpaquePtr(
        ZL_CustomTokenizeState const* ctx);

/**
 * Creates the alphabet stream to store the tokenized alphabet. The width of
 * each element in the alphabet must be the same width as the input stream.
 *
 * @param alphabetSize The exact size of the alphabet.
 *
 * @returns A pointer to write the alphabet into or NULL on error.
 */
void* ZL_CustomTokenizeState_createAlphabetOutput(
        ZL_CustomTokenizeState* ctx,
        size_t alphabetSize);

/**
 * Creates the index stream with the given width. The index stream must contain
 * exactly the same number of elements as the input.
 *
 * @param indexWidth The width of the index integer, either 1, 2, 4, or 8.
 *
 * @returns A pointer to write the indices into or NULL on error.
 */
void* ZL_CustomTokenizeState_createIndexOutput(
        ZL_CustomTokenizeState* ctx,
        size_t indexWidth);

/**
 * A custom tokenization function to tokenize the input. The output of this
 * function is not checked in production builds, and it is UB to tokenize
 * incorrectly.
 */
typedef ZL_Report (*ZL_CustomTokenizeFn)(
        ZL_CustomTokenizeState* ctx,
        ZL_Input const* input);

/**
 * Tokenize with a custom tokenization function. This is useful if you want to
 * define a custom order for your alphabet that is neither insertion nor sorted
 * order.
 *
 * WARNING: Zstrong does not manage the lifetime of the @p opaque pointer. It
 * must outlive the @p cgraph or be NULL.
 */
ZL_GraphID ZL_Compressor_registerCustomTokenizeGraph(
        ZL_Compressor* cgraph,
        ZL_Type streamType,
        ZL_CustomTokenizeFn customTokenizeFn,
        void const* opaque,
        ZL_GraphID alphabetGraph,
        ZL_GraphID indicesGraph);

#if defined(__cplusplus)
}
#endif

#endif
