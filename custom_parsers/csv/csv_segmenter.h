// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_SEGMENTERS_CSV_H
#define ZSTRONG_COMPRESS_SEGMENTERS_CSV_H

#include "openzl/shared/portability.h"
#include "openzl/zl_segmenter.h"

ZL_BEGIN_C_DECLS

/**
 * Registers the csv graph with a segmenter and returns the graph ID or error if
 * failed.
 *
 * @param chunkByteSizeMax The maximum size of a chunk in bytes requested.
 * @param clusterGraph The graph ID of the clustering graph to use.
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_CsvSegmenter_registerSegmenter(
        ZL_Compressor* compressor,
        size_t chunkByteSizeMax,
        bool hasHeader,
        char sep,
        bool useNullAware,
        const ZL_GraphID clusteringGraph);

ZL_RESULT_OF(ZL_GraphID)
ZL_CsvSegmenter_registerSegmenterNoChunks(
        ZL_Compressor* compressor,
        bool hasHeader,
        char sep,
        bool useNullAware,
        const ZL_GraphID clusteringGraph);

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_SEGMENTERS_CSV_H
