// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CLI_CSV_PROFILE_H
#define ZSTRONG_CLI_CSV_PROFILE_H

#include "openzl/zl_compressor.h"

namespace openzl::custom_parsers {

const size_t kDefaultChunkSize = 20 * 1000 * 1000;

/**
 * @brief Registers a generic CSV graph where the clustering of the columns is
 * still unconfigured.
 *
 * This function creates a clustering graph with default type configurations but
 * no specific column clusters, and sets up appropriate successors for different
 * data types.
 *
 * @param compressor The compressor to register the graph with
 * @returns The graph ID registered for the clustering graph
 */
ZL_GraphID ZL_createGraph_genericCSVCompressor(
        ZL_Compressor* compressor) noexcept;

/**
 * @brief Specialized version of the "default" @ref
 * ZL_createGraph_genericCSVCompressor with additional parameters.
 * @param hasHeader Whether the input has a header row. (default: true)
 * @param separator The character used to separate columns. (default: ',')
 * @param useNullAware Whether to use null-aware column coalescing. (default:
 * false)
 */
ZL_GraphID ZL_createGraph_genericCSVCompressorWithOptions(
        ZL_Compressor* compressor,
        size_t chunkByteSizeMax,
        bool hasHeader,
        char separator,
        bool useNullAware) noexcept;

} // namespace openzl::custom_parsers

#endif // ZSTRONG_CLI_CSV_PROFILE_H
