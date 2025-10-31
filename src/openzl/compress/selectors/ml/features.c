// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/compress/selectors/ml/features.h"
#include <math.h>
#include "openzl/shared/estimate.h" // For cardinality and range estimations
#include "openzl/shared/histogram.h"
#include "openzl/shared/mem.h"   // For uint64_t ... types
#include "openzl/shared/utils.h" // for ZL_MIN

typedef struct {
    double mean;
    double stddev;
    double variance;
    double skewness;
    double kurtosis;
} Moments;

ZL_FORCE_INLINE uint64_t readElement(const void* ptr, size_t eltWidth)
{
    switch (eltWidth) {
        case 1:
            return *(const uint8_t*)ptr;
        case 2:
            return ZL_read16(ptr);
        case 4:
            return ZL_read32(ptr);
        case 8:
            return ZL_read64(ptr);
        default:
            ZL_ASSERT_FAIL("Unexpected eltWidth");
            return 0;
    }
}

ZL_FORCE_INLINE double
calcMean(const void* data, size_t eltWidth, size_t nbElts)
{
    if (nbElts == 0) {
        return 0;
    }

    // Handle the elements in groups of two. Using multiple accumulators breaks
    // the dependency chain and allows SLP vectorization, but it also changes
    // the order of operations. Using multiple accumulators always reduces the
    // accumulated error when accumulating positive inputs.
    // Notice: using four accumulators is faster on some data widths and
    // instruction sets, so it's possible to further optimize this.
    double sum0 = 0, sum1 = 0;
    size_t i            = 0;
    const uint8_t* base = (const uint8_t*)data;
    while (i + 2 < nbElts) {
        sum0 += (double)readElement(base + (i + 0) * eltWidth, eltWidth);
        sum1 += (double)readElement(base + (i + 1) * eltWidth, eltWidth);
        i += 2;
    }

    // Handle the remaining elements.
    double sum = sum0 + sum1;
    for (; i < nbElts; i++) {
        sum += (double)readElement(base + i * eltWidth, eltWidth);
    }
    return sum / (double)nbElts;
}

static Moments calcMoments_uint8(const void* data, size_t nbElts)
{
    Moments moments    = { 0, 0, 0, 0, 0 };
    ZL_Histogram* hist = ZL_Histogram_create(256);
    ZL_Histogram_build(hist, data, nbElts, 1);

    double sum = 0;
    for (unsigned i = 0; i <= hist->maxSymbol; i++) {
        sum += i * hist->count[i];
    }
    double mean  = sum / (double)nbElts;
    moments.mean = mean;

    if (nbElts <= 1) {
        ZL_Histogram_destroy(hist);
        return moments;
    }

    double varSum  = 0;
    double skewSum = 0;
    double kurtSum = 0;
    for (unsigned i = 0; i <= hist->maxSymbol; i++) {
        double delta = i - mean;
        double count = hist->count[i];
        varSum += delta * delta * count;
        skewSum += delta * delta * delta * count;
        kurtSum += delta * delta * delta * delta * count;
    }

    const double biasedStddev = sqrt(varSum / (double)nbElts);

    moments.stddev   = sqrt(varSum / (double)(nbElts - 1));
    moments.variance = varSum / ((double)nbElts - 1);
    moments.skewness = skewSum / (biasedStddev * biasedStddev * biasedStddev)
            / (double)nbElts;
    moments.kurtosis = kurtSum
                    / (biasedStddev * biasedStddev * biasedStddev
                       * biasedStddev)
                    / (double)nbElts
            - 3;
    ZL_Histogram_destroy(hist);
    return moments;
}

/**
 *  Calculate the moments of the distribution:
 * mean, variance (unbiased), stddev (unbiased), skewness (normalized), kurtosis
 * (normalized).
 * The algorithm used is naive and could be unstable, but testing shows it
 * performs well for the use-cases we are interested in and is quicker than the
 * one-pass stable algorithm.
 * Improvements can be made in the future if needed.
 */
ZL_FORCE_INLINE Moments
calcMoments(const void* data, size_t eltWidth, size_t nbElts)
{
    Moments moments   = { 0, 0, 0, 0, 0 };
    const double mean = calcMean(data, eltWidth, nbElts);
    moments.mean      = mean;

    if (nbElts <= 1) {
        return moments;
    }

    double varSum  = 0;
    double skewSum = 0;
    double kurtSum = 0;
    for (size_t i = 0; i < nbElts; ++i) {
        uint64_t value =
                readElement((const uint8_t*)data + i * eltWidth, eltWidth);
        const double delta = (double)value - mean;

        varSum += delta * delta;
        skewSum += delta * delta * delta;
        kurtSum += delta * delta * delta * delta;
    }

    const double biasedStdDev = sqrt(varSum / (double)(nbElts));

    moments.stddev   = sqrt(varSum / (double)(nbElts - 1));
    moments.variance = varSum / (double)(nbElts - 1);
    moments.skewness = skewSum / (biasedStdDev * biasedStdDev * biasedStdDev)
            / (double)nbElts;
    moments.kurtosis = kurtSum
                    / (biasedStdDev * biasedStdDev * biasedStdDev
                       * biasedStdDev)
                    / (double)nbElts
            - 3;
    moments.mean   = mean;
    moments.stddev = sqrt(varSum / (double)(nbElts - 1));

    return moments;
}

ZL_FORCE_INLINE bool calcIntegerFeaturesInner(
        VECTOR(LabeledFeature) * features,
        const void* data,
        size_t eltWidth,
        size_t nbElts)
{
    const ZL_ElementRange range =
            ZL_computeUnsignedRange(data, nbElts, eltWidth);
    const uint64_t rangeSize = range.max - range.min;
    const uint64_t rangeSizePlusOne =
            rangeSize == UINT64_MAX ? rangeSize : rangeSize + 1;
    const uint64_t maxCard = (uint64_t)ZL_MIN(rangeSizePlusOne, nbElts);

    const ZL_CardinalityEstimate card =
            ZL_estimateCardinality_fixed(data, nbElts, eltWidth, maxCard);
    Moments moments = eltWidth == 1 ? calcMoments_uint8(data, nbElts)
                                    : calcMoments(data, eltWidth, nbElts);

    LabeledFeature nbEltsFeature      = { "nbElts", (float)nbElts };
    LabeledFeature eltWidthFeature    = { "eltWidth", (float)eltWidth };
    LabeledFeature cardinalityFeature = { "cardinality", (float)card.estimate };
    LabeledFeature cardinalityUpperFeature = { "cardinality_upper",
                                               (float)card.estimateUpperBound };
    LabeledFeature cardinalityLowerFeature = { "cardinality_lower",
                                               (float)card.estimateLowerBound };
    LabeledFeature rangeSizeFeature        = { "range_size", (float)rangeSize };
    LabeledFeature meanFeature             = { "mean", (float)moments.mean };
    LabeledFeature varianceFeature = { "variance", (float)moments.variance };
    LabeledFeature stddevFeature   = { "stddev", (float)moments.stddev };
    LabeledFeature skewnessFeature = { "skewness", (float)moments.skewness };
    LabeledFeature kurtosisFeature = { "kurtosis", (float)moments.kurtosis };

    bool badAlloc = false;
    badAlloc |= !VECTOR_PUSHBACK(*features, nbEltsFeature);
    badAlloc |= !VECTOR_PUSHBACK(*features, eltWidthFeature);
    badAlloc |= !VECTOR_PUSHBACK(*features, cardinalityFeature);
    badAlloc |= !VECTOR_PUSHBACK(*features, cardinalityUpperFeature);
    badAlloc |= !VECTOR_PUSHBACK(*features, cardinalityLowerFeature);
    badAlloc |= !VECTOR_PUSHBACK(*features, rangeSizeFeature);
    badAlloc |= !VECTOR_PUSHBACK(*features, meanFeature);
    badAlloc |= !VECTOR_PUSHBACK(*features, varianceFeature);
    badAlloc |= !VECTOR_PUSHBACK(*features, stddevFeature);
    badAlloc |= !VECTOR_PUSHBACK(*features, skewnessFeature);
    badAlloc |= !VECTOR_PUSHBACK(*features, kurtosisFeature);
    return badAlloc;
}

static bool calcIntegerFeatures(
        VECTOR(LabeledFeature) * features,
        const void* data,
        size_t eltWidth,
        size_t nbElts)
{
    switch (eltWidth) {
        case 1:
            return calcIntegerFeaturesInner(features, data, 1, nbElts);
        case 2:
            return calcIntegerFeaturesInner(features, data, 2, nbElts);

        case 4:
            return calcIntegerFeaturesInner(features, data, 4, nbElts);

        case 8:
            return calcIntegerFeaturesInner(features, data, 8, nbElts);

        default:
            ZL_ASSERT_FAIL("Unexpected eltWidth");
            return true;
    }
}

ZL_Report FeatureGen_integer(
        const ZL_Input* inputStream,
        VECTOR(LabeledFeature) * features,
        const void* featureContext)
{
    (void)featureContext;

    ZL_ASSERT(ZL_Input_type(inputStream) == ZL_Type_numeric);
    const void* data      = ZL_Input_ptr(inputStream);
    const size_t nbElts   = ZL_Input_numElts(inputStream);
    const size_t eltWidth = ZL_Input_eltWidth(inputStream);

    bool badAlloc = calcIntegerFeatures(features, data, eltWidth, nbElts);

    ZL_RET_R_IF(allocation, badAlloc, "Failed to add features to vector");
    return ZL_returnSuccess();
}

ZL_RESULT_OF(FeatureGenerator)
FeatureGen_getFeatureGen(FeatureGenId id)
{
    if (id == FeatureGenId_Int) {
        return ZL_RESULT_WRAP_VALUE(FeatureGenerator, FeatureGen_integer);
    }

    ZL_RET_T_ERR(
            FeatureGenerator,
            compressionParameter_invalid,
            "Must use standard feature generator");
}

FeatureGenId FeatureGen_getId(FeatureGenerator featureGenerator)
{
    if (featureGenerator == FeatureGen_integer) {
        return FeatureGenId_Int;
    }

    return FeatureGenId_Invalid;
}
