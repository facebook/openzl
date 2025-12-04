// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/lz/encode_field_lz_literals_selector.h"

#include "openzl/codecs/constant/encode_constant_binding.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/data_stats.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_portability.h"
#include "openzl/zl_selector.h"

static size_t reportToSize(ZL_GraphReport report)
{
    if (ZL_isError(report.finalCompressedSize))
        return (size_t)-1;
    return ZL_validResult(report.finalCompressedSize);
}

static size_t gainSize(size_t size, size_t gain)
{
    size_t const gainSize = size + size * gain / 100;
    return ZL_MAX(size, gainSize);
}

// histU8_getNumUnique is currently unused but may be needed for future selector
// logic
static ZL_UNUSED_ATTR size_t histU8_getNumUnique(unsigned int const* hist)
{
    size_t uniqueValues = 256;
    for (int i = 0; i < 256; i++) {
        if (hist[i] == 0) {
            uniqueValues--;
        }
    }
    return uniqueValues;
}

static ZL_GraphID ZL_fastTransposedLiteralStreamSelector(
        const ZL_Selector* selCtx,
        const ZL_Input* input,
        const ZS2_transposedLiteralStreamSelector_Successors* successors)
{
    ZL_ASSERT_EQ(ZL_Input_type(input), ZL_Type_serial);
    size_t const inputSize = ZL_Input_numElts(input);

    DataStatsU8 stats;
    DataStatsU8_init(&stats, ZL_Input_ptr(input), inputSize);

    ZL_GraphID const graphs[] = { successors->store,
                                  successors->constantSerial,
                                  successors->bitpack,
                                  successors->flatpack,
                                  successors->deltaFlatpack };
    size_t const gain[]       = {
        0, 0, 5, 15, 25,
    };

    size_t sizes[]        = { inputSize,
                              DataStatsU8_getConstantSize(&stats),
                              DataStatsU8_getBitpackedSize(&stats),
                              DataStatsU8_getFlatpackedSize(&stats),
                              0 };
    size_t const nbGraphs = ZL_ARRAY_SIZE(graphs);
    for (size_t i = 0; i < nbGraphs; ++i) {
        if (sizes[i] == 0) {
            sizes[i] = reportToSize(
                    ZL_Selector_tryGraph(selCtx, input, graphs[i]));
        }
        sizes[i] = gainSize(sizes[i], gain[i]);
    }

    if (!ZL_Selector_isConstantSupported(selCtx)) {
        sizes[1] = (size_t)-1;
    }

    size_t bestSize      = sizes[0];
    ZL_GraphID bestGraph = graphs[0];
    for (size_t i = 1; i < nbGraphs; ++i) {
        if (sizes[i] < bestSize) {
            bestSize  = sizes[i];
            bestGraph = graphs[i];
        }
    }

    return bestGraph;
}

ZL_GraphID ZS2_transposedLiteralStreamSelector_impl(
        const ZL_Selector* selCtx,
        const ZL_Input* input,
        const ZS2_transposedLiteralStreamSelector_Successors* successors)
{
    if (ZL_Selector_getCParam(selCtx, ZL_CParam_decompressionLevel) == 1) {
        return ZL_fastTransposedLiteralStreamSelector(
                selCtx, input, successors);
    }
    ZL_ASSERT_EQ(ZL_Input_type(input), ZL_Type_serial);
    size_t const inputSize = ZL_Input_numElts(input);
    int const compressionLevel =
            ZL_Selector_getCParam(selCtx, ZL_CParam_compressionLevel);

    size_t const kDeltaGain = 4;
    size_t const kHuffGain  = 2;
    size_t const kZstdGain  = 4;

    DataStatsU8 stats;
    DataStatsU8_init(&stats, ZL_Input_ptr(input), inputSize);
    DataStatsU8_calcHistograms(&stats); // Calculate both histograms at once

    if (DataStatsU8_getCardinality(&stats) == 1) {
        if (inputSize > 1 && ZL_Selector_isConstantSupported(selCtx)) {
            return successors->constantSerial;
        }
        if (inputSize > 20) {
            return successors->huffman;
        }
    }
    if (inputSize < 200) {
        return successors->store;
    }

    // Determine whether we want to delta using Huffman
    size_t huffSize, deltaHuffSize;
    if (compressionLevel <= 3) {
        huffSize      = DataStatsU8_estimateHuffmanSizeFast(&stats, false);
        deltaHuffSize = DataStatsU8_estimateHuffmanSizeFast(&stats, true);
    } else if (compressionLevel < 7) {
        huffSize      = DataStatsU8_getHuffmanSize(&stats);
        deltaHuffSize = DataStatsU8_getDeltaHuffmanSize(&stats);
    } else {
        huffSize = reportToSize(
                ZL_Selector_tryGraph(selCtx, input, successors->huffman));
        deltaHuffSize = reportToSize(
                ZL_Selector_tryGraph(selCtx, input, successors->deltaHuff));
    }
    bool const delta          = gainSize(deltaHuffSize, kDeltaGain) < huffSize;
    size_t const bestHuffSize = delta ? deltaHuffSize : huffSize;

    // If we don't get enough ratio, don't compress at all
    {
        if (gainSize(bestHuffSize, kHuffGain) >= inputSize) {
            return successors->store;
        }
    }

    ZL_GraphID graphs[] = {
        successors->store,
        successors->huffman,
        successors->zstd,
    };
    if (delta) {
        graphs[1] = successors->deltaHuff;
        graphs[2] = successors->deltaZstd;
    }
    size_t gain[]  = { 0, kHuffGain, kZstdGain };
    size_t sizes[] = { inputSize, bestHuffSize, 0 };

    size_t const nbGraphs = ZL_ARRAY_SIZE(graphs);
    for (size_t i = 0; i < nbGraphs; ++i) {
        if (sizes[i] == 0) {
            sizes[i] = reportToSize(
                    ZL_Selector_tryGraph(selCtx, input, graphs[i]));
        }
        sizes[i] = gainSize(sizes[i], gain[i]);
    }

    size_t bestSize      = sizes[0];
    ZL_GraphID bestGraph = graphs[0];
    for (size_t i = 1; i < nbGraphs; ++i) {
        if (sizes[i] < bestSize) {
            bestSize  = sizes[i];
            bestGraph = graphs[i];
        }
    }

    return bestGraph;
}
