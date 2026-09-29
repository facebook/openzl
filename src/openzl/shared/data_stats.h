// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_DATA_STATS_H
#define ZSTRONG_DATA_STATS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

#define LAZY_FIELD(TYPE, NAME) \
    TYPE NAME;                 \
    bool NAME##Initialized;

#define LAZY_ARRAY(TYPE, NAME, SIZE) \
    TYPE NAME[SIZE];                 \
    bool NAME##Initialized;

typedef struct {
    size_t srcSize;
    uint8_t const* src;

    LAZY_ARRAY(unsigned int, histogram, 256)
    LAZY_ARRAY(unsigned int, deltaHistogram, 256)
    LAZY_FIELD(uint8_t, maxElt)
    LAZY_FIELD(double, entropy)
    LAZY_FIELD(double, deltaEntropy)
    LAZY_FIELD(size_t, cardinality)
    LAZY_FIELD(size_t, huffmanSize)
    LAZY_FIELD(size_t, deltaHuffmanSize)
    LAZY_FIELD(size_t, bitpackedSize)
    LAZY_FIELD(size_t, flatpackedSize)
    LAZY_FIELD(size_t, constantSize)
} DataStatsU8;

/* ZL_calculateEntropyU8::
 * Estimated Shannon entropy of 256 tokens based on an histogram given in the
 * count parameter. */
double ZL_calculateEntropyU8(unsigned int const* count, size_t totalElements);

/**
 * Estimated Shannon entropy of tokens based on an histogram given in the
 * count parameter.
 */
double ZL_calculateEntropy(
        unsigned int const* count,
        size_t maxValue,
        size_t totalElements);

/* DataStatsU8_init::
 * Inits a DataStatsU8 structure, must be invoked before the structure is
 * used for any other API. From this point the `stats` will contain a reference
 * to `src` and must be outlived by it. */
void DataStatsU8_init(DataStatsU8* stats, void const* src, size_t srcSize);

/* DataStatsU8_totalElements::
 * Returns the totalElements considered in these statistics. */
size_t DataStatsU8_totalElements(DataStatsU8* stats);

/* DataStatsU8_getCardinality::
 * Returns the amount of unique numbers (cardinality) in these statistics . */
size_t DataStatsU8_getCardinality(DataStatsU8* stats);

/* DataStatsU8_getMaxElt::
 * Returns the maximum element considered in these statistics. If the source has
 * no elements, this returns `0` */
uint8_t DataStatsU8_getMaxElt(DataStatsU8* stats);

/* DataStatsU8_calcHistograms::
 * Calculates both the histogram and delta histogram for later usage.
 * Note: current implementaion is slow and should be replaced by a faster
 * implementation. */
void DataStatsU8_calcHistograms(DataStatsU8* stats);

/* DataStatsU8_getHistogram::
 * Returns the histogram of the data, histogram is a 256 values array. */
unsigned int const* DataStatsU8_getHistogram(DataStatsU8* stats);

/* DataStatsU8_getDeltaHistogram::
 * Returns the histogram of delta of the data, histogram is a 256 values array.
 */
unsigned int const* DataStatsU8_getDeltaHistogram(DataStatsU8* stats);

/* DataStatsU8_getEntropy::
 * Returns an estimated Shannon entropy of the data. */
double DataStatsU8_getEntropy(DataStatsU8* stats);

/* DataStatsU8_getDeltaEntropy::
 * Returns an estimated Shannon entropy of the delta of the data. */
double DataStatsU8_getDeltaEntropy(DataStatsU8* stats);

/* DataStatsU8_getHuffmanSize::
 * Returns an estimated huffman encoding size of the data (including header). */
size_t DataStatsU8_getHuffmanSize(DataStatsU8* stats);

/* DataStatsU8_getDeltaEntropy::
 * Returns an estimated huffman encoding size of the delta of the data
 * (including header). */
size_t DataStatsU8_getDeltaHuffmanSize(DataStatsU8* stats);

/* DataStatsU8_estimateHuffmanSizeFast::
 * Uses entropy estimation to estimate size of huffman encoding.
 * Does not calculate the loss from Huffman compared to optimal entropy coding,
 * but does include the header size.
 *
 * NOTE: Consider not using this in new code, it is not a good estimate.
 */
size_t DataStatsU8_estimateHuffmanSizeFast(DataStatsU8* stats, bool delta);

/* DataStatsU8_getBitpackedSize::
 * Returns an estimated bitpacked size of the data in bytes */
size_t DataStatsU8_getBitpackedSize(DataStatsU8* stats);

/* DataStatsU8_getBitpackedSize::
 * Returns an estimated flatpacked size of the data in bytes */
size_t DataStatsU8_getFlatpackedSize(DataStatsU8* stats);

/* DataStatsU8_getConstantSize::
 * Returns an estimated size of the data in bytes using constant encoding
 * Note: input data must be constant (e.g., 111) and non-empty */
size_t DataStatsU8_getConstantSize(DataStatsU8* stats);

#undef LAZY_ARRAY
#undef LAZY_ARRAY

ZL_END_C_DECLS

#endif // ZSTRONG_DATA_STATS_H
