// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>

#include "openzl/zl_opaque_types.h"

namespace openzl::profiles {

/**
 * Builds an graph that uses LZ by default and wraps it in a serial
 * segmenter.
 *
 * A @p chunkByteSize of 0 uses the segmenter's default chunk size.
 */
ZL_GraphID buildSerialGraph(
        ZL_Compressor* compressor,
        size_t chunkByteSize = 0);

/**
 * Builds a numeric graph preceded by @p conversionNode and wrapped in a
 * numeric-from-serial segmenter.
 *
 * @p eltByteWidth is the width of each numeric element. A @p chunkByteSize of
 * 0 uses the segmenter's default chunk size.
 */
ZL_GraphID buildIntGraph(
        ZL_Compressor* compressor,
        size_t eltByteWidth,
        ZL_NodeID conversionNode,
        size_t chunkByteSize = 0);

/**
 * Builds a trainable LZ graph wrapped in a serial segmenter.
 *
 * A @p chunkByteSize of 0 uses the segmenter's default chunk size.
 */
ZL_GraphID buildLzGraph(ZL_Compressor* compressor, size_t chunkByteSize = 0);

/** Builds a trainable Zstandard graph without a segmenter. */
ZL_GraphID buildZstdGraph(ZL_Compressor* compressor);

/**
 * Builds a graph for CSV data with a header row.
 *
 * A @p chunkByteSize of 0 uses the CSV parser's default chunk size. A
 * @p separator of '\0' uses a comma; any other value is used as-is.
 *
 * Returns ZL_GRAPH_ILLEGAL if the graph cannot be constructed.
 */
ZL_GraphID buildCsvGraph(
        ZL_Compressor* compressor,
        size_t chunkByteSize = 0,
        char separator       = '\0');

/**
 * Builds a graph for canonical Parquet data using a generic clustering
 * successor.
 *
 * A @p chunkByteSize of 0 disables chunking. Returns
 * ZL_GRAPH_ILLEGAL if the graph cannot be constructed or if @p chunkByteSize
 * exceeds INT_MAX, which is the largest value accepted by the Parquet graph
 * registration API.
 */
ZL_GraphID buildParquetGraph(ZL_Compressor* compressor, size_t chunkByteSize);

/**
 * Builds a graph for PyTorch models saved with torch.save().
 *
 * This graph does not support training.
 */
ZL_GraphID buildPytorchGraph(ZL_Compressor* compressor);

} // namespace openzl::profiles
