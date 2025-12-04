// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string.h>

#include "openzl/shared/bits.h"
#include "openzl/shared/data_stats.h"
#include "openzl/shared/histogram.h"
#include "openzl/shared/portability.h" // ZL_UNUSED
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"

#define HUF_STATIC_LINKING_ONLY
#include "openzl/fse/huf.h"

// Table was pre-generated in python using [0x80000 ^ int(math.log2(i)*(1<<16))
// for i in range(1<<8, 1<<9)]
#define LOG2_TABLE_PRECISION_BITS (8)
#define LOG2_TABLE_MULTIPLIER (1 << 16)
const uint16_t LOG2_TABLE[1 << LOG2_TABLE_PRECISION_BITS] = {
    0,     368,   735,   1101,  1465,  1828,  2190,  2550,  2909,  3266,  3622,
    3977,  4331,  4683,  5034,  5383,  5731,  6078,  6424,  6769,  7112,  7454,
    7794,  8134,  8472,  8809,  9145,  9480,  9813,  10146, 10477, 10807, 11136,
    11463, 11790, 12115, 12440, 12763, 13085, 13406, 13726, 14045, 14363, 14680,
    14995, 15310, 15624, 15936, 16248, 16558, 16868, 17176, 17484, 17790, 18096,
    18400, 18704, 19006, 19308, 19608, 19908, 20207, 20505, 20801, 21097, 21392,
    21686, 21980, 22272, 22563, 22854, 23143, 23432, 23720, 24007, 24293, 24578,
    24862, 25146, 25429, 25710, 25991, 26272, 26551, 26829, 27107, 27384, 27660,
    27935, 28210, 28483, 28756, 29028, 29300, 29570, 29840, 30109, 30377, 30644,
    30911, 31177, 31442, 31707, 31971, 32234, 32496, 32757, 33018, 33278, 33538,
    33796, 34054, 34312, 34568, 34824, 35079, 35334, 35588, 35841, 36093, 36345,
    36596, 36847, 37096, 37346, 37594, 37842, 38089, 38336, 38582, 38827, 39071,
    39315, 39559, 39801, 40044, 40285, 40526, 40766, 41006, 41245, 41483, 41721,
    41959, 42195, 42431, 42667, 42902, 43136, 43370, 43603, 43836, 44068, 44299,
    44530, 44760, 44990, 45219, 45448, 45676, 45904, 46131, 46357, 46583, 46808,
    47033, 47257, 47481, 47704, 47927, 48149, 48371, 48592, 48813, 49033, 49253,
    49472, 49690, 49909, 50126, 50343, 50560, 50776, 50992, 51207, 51421, 51635,
    51849, 52062, 52275, 52487, 52699, 52910, 53121, 53331, 53541, 53751, 53960,
    54168, 54376, 54584, 54791, 54998, 55204, 55410, 55615, 55820, 56024, 56228,
    56432, 56635, 56837, 57040, 57242, 57443, 57644, 57844, 58044, 58244, 58443,
    58642, 58841, 59039, 59236, 59433, 59630, 59827, 60023, 60218, 60413, 60608,
    60802, 60996, 61190, 61383, 61576, 61768, 61960, 62152, 62343, 62534, 62724,
    62914, 63104, 63293, 63482, 63671, 63859, 64047, 64234, 64421, 64608, 64794,
    64980, 65165, 65351
};

double ZL_calculateEntropy(
        unsigned int const* count,
        size_t maxValue,
        size_t totalElements)
{
    int64_t intEntropy = 0;
    double entropy     = 0;
    if (totalElements == 0) {
        return 0;
    }
    int64_t const normalize = ((int64_t)1 << 62) / (int64_t)totalElements;
    for (size_t i = 0; i <= maxValue; i++) {
        unsigned int const currCount = count[i];
        int64_t logIndex             = (int64_t)(currCount * normalize);
        int const clz                = (int)ZL_clz64(
                (uint64_t)logIndex | (1 << (LOG2_TABLE_PRECISION_BITS + 1)));
        const int shift = (62 - LOG2_TABLE_PRECISION_BITS) - clz + 1;
        logIndex >>= shift;
        logIndex = logIndex & ((1 << LOG2_TABLE_PRECISION_BITS) - 1);
        intEntropy += -(int64_t)currCount
                * (LOG2_TABLE[logIndex] - clz * LOG2_TABLE_MULTIPLIER);
    }
    entropy = ((double)intEntropy)
                    / (double)(totalElements * LOG2_TABLE_MULTIPLIER)
            - 1;
    return entropy;
}

/*
 * ZL_calculateEntropyU8:
 * Calculates an estimation of Shannon entropy for a distribution of 256
 * distinct values. The function uses some mathematical tricks to avoid the most
 * time consuming parts of the calculation and can privde results that are up to
 * ~0.1% accuracy which should be good enough for almost any usage.
 */
double ZL_calculateEntropyU8(unsigned int const* count, size_t totalElements)
{
    return ZL_calculateEntropy(count, 255, totalElements);
}

#define CLEAR_LAZY(STATS, NAME)           \
    {                                     \
        STATS->NAME##Initialized = false; \
    }
#define RETURN_OR_SET_LAZY(STATS, NAME, VALUE_CALCULATION) \
    {                                                      \
        if (!STATS->NAME##Initialized) {                   \
            STATS->NAME              = VALUE_CALCULATION;  \
            STATS->NAME##Initialized = true;               \
        }                                                  \
        return STATS->NAME;                                \
    }
#define RETURN_OR_CALC_LAZY(STATS, NAME, VALUE_CALCULATION) \
    {                                                       \
        if (!STATS->NAME##Initialized) {                    \
            VALUE_CALCULATION;                              \
            STATS->NAME##Initialized = true;                \
        }                                                   \
        return STATS->NAME;                                 \
    }

void DataStatsU8_init(DataStatsU8* stats, void const* src, size_t srcSize)
{
    stats->src     = src;
    stats->srcSize = srcSize;

    CLEAR_LAZY(stats, histogram);
    CLEAR_LAZY(stats, deltaHistogram);
    CLEAR_LAZY(stats, cardinality);
    CLEAR_LAZY(stats, maxElt);
    CLEAR_LAZY(stats, entropy);
    CLEAR_LAZY(stats, deltaEntropy);
    CLEAR_LAZY(stats, huffmanSize);
    CLEAR_LAZY(stats, deltaHuffmanSize);
    CLEAR_LAZY(stats, bitpackedSize);
    CLEAR_LAZY(stats, flatpackedSize);
    CLEAR_LAZY(stats, constantSize);
}

size_t DataStatsU8_totalElements(DataStatsU8* stats)
{
    return stats->srcSize;
}

static size_t DataStatsU8_calcCardinality(DataStatsU8* stats)
{
    unsigned int const* hist = DataStatsU8_getHistogram(stats);

    size_t cardinality = 0;
    for (int i = 0; i < 256; i++) {
        if (hist[i] != 0) {
            cardinality++;
        }
    }

    return cardinality;
}

size_t DataStatsU8_getCardinality(DataStatsU8* stats)
{
    RETURN_OR_SET_LAZY(stats, cardinality, DataStatsU8_calcCardinality(stats));
}

static uint8_t DataStatsU8_calcMaxElt(DataStatsU8* stats)
{
    if (stats->srcSize == 0) {
        return 0;
    }

    unsigned int const* hist = DataStatsU8_getHistogram(stats);

    uint8_t maxElt = 0;
    for (int i = 255; i >= 0; i--) {
        if (hist[i] != 0) {
            maxElt = (uint8_t)i;
            break;
        }
    }

    return maxElt;
}

uint8_t DataStatsU8_getMaxElt(DataStatsU8* stats)
{
    RETURN_OR_SET_LAZY(stats, maxElt, DataStatsU8_calcMaxElt(stats));
}

void DataStatsU8_calcHistograms(DataStatsU8* stats)
{
    memset(stats->histogram, 0, sizeof(stats->histogram));
    memset(stats->deltaHistogram, 0, sizeof(stats->deltaHistogram));
    uint8_t prev = 0;
    for (size_t i = 0; i < stats->srcSize; i++) {
        uint8_t curr = stats->src[i];
        stats->histogram[curr]++;
        stats->deltaHistogram[(uint8_t)(curr - prev)]++;
        prev = curr;
    }
    stats->histogramInitialized      = true;
    stats->deltaHistogramInitialized = true;
}

static void DataStatsU8_calcHistogram(DataStatsU8* stats)
{
    unsigned maxSymbolValue = 255;
    unsigned cardinality    = 0;
    (void)ZL_Histogram_count(
            stats->histogram,
            &maxSymbolValue,
            &cardinality,
            stats->src,
            stats->srcSize,
            1);
    stats->histogramInitialized = true;

    stats->maxElt                 = (uint8_t)maxSymbolValue;
    stats->maxEltInitialized      = true;
    stats->cardinality            = (size_t)cardinality;
    stats->cardinalityInitialized = true;
}

unsigned int const* DataStatsU8_getHistogram(DataStatsU8* stats)
{
    RETURN_OR_CALC_LAZY(stats, histogram, DataStatsU8_calcHistogram(stats));
}

static ZL_UNUSED void DataStatsU8_calcDeltaHistogram(DataStatsU8* stats)
{
    if (!stats->histogramInitialized) {
        DataStatsU8_calcHistograms(stats);
        return;
    }

    memset(stats->deltaHistogram, 0, sizeof(stats->deltaHistogram));
    uint8_t prev = 0;
    for (size_t i = 0; i < stats->srcSize; i++) {
        uint8_t curr = stats->src[i];
        stats->deltaHistogram[(uint8_t)(curr - prev)]++;
        prev = curr;
    }
    stats->deltaHistogramInitialized = true;
}

unsigned int const* DataStatsU8_getDeltaHistogram(DataStatsU8* stats)
{
    RETURN_OR_CALC_LAZY(
            stats, deltaHistogram, DataStatsU8_calcHistograms(stats));
}

double DataStatsU8_getEntropy(DataStatsU8* stats)
{
    RETURN_OR_SET_LAZY(
            stats,
            entropy,
            ZL_calculateEntropyU8(
                    DataStatsU8_getHistogram(stats),
                    DataStatsU8_totalElements(stats)));
}

double DataStatsU8_getDeltaEntropy(DataStatsU8* stats)
{
    RETURN_OR_SET_LAZY(
            stats,
            deltaEntropy,
            ZL_calculateEntropyU8(
                    DataStatsU8_getDeltaHistogram(stats),
                    DataStatsU8_totalElements(stats)));
}

static size_t DataStatsU8_estimateHuffmanSize(DataStatsU8* stats, bool delta)
{
    if (stats->srcSize <= 4) {
        return 4;
    }
    unsigned int const* hist = delta ? DataStatsU8_getDeltaHistogram(stats)
                                     : DataStatsU8_getHistogram(stats);
    unsigned int huffLog =
            HUF_optimalTableLog(FSE_MAX_TABLELOG, stats->srcSize, 255);
    HUF_CElt CTable[1 << FSE_MAX_TABLELOG];
    size_t maxBits = HUF_buildCTable(CTable, hist, 255, huffLog);
    const size_t estimateEncodedSize =
            ZS_HUF_estimateCompressedSize(CTable, hist, 255);
    char header[HUF_CTABLEBOUND];
    const size_t estimateHeaderSize = ZS_HUF_writeCTable(
            header, HUF_CTABLEBOUND, CTable, 255, (unsigned int)maxBits);
    return estimateEncodedSize + estimateHeaderSize + 4;
}

size_t DataStatsU8_getHuffmanSize(DataStatsU8* stats)
{
    RETURN_OR_SET_LAZY(
            stats, huffmanSize, DataStatsU8_estimateHuffmanSize(stats, false););
}

size_t DataStatsU8_getDeltaHuffmanSize(DataStatsU8* stats)
{
    RETURN_OR_SET_LAZY(
            stats,
            deltaHuffmanSize,
            DataStatsU8_estimateHuffmanSize(stats, true););
}

size_t DataStatsU8_estimateHuffmanSizeFast(DataStatsU8* stats, bool delta)
{
    double entropy = delta ? DataStatsU8_getDeltaEntropy(stats)
                           : DataStatsU8_getEntropy(stats);
    if (entropy > 7) {
        return stats->srcSize;
    }
    entropy = ZL_MAX(
            entropy,
            1); // We need at least one bit if we have more than one symbol
    return (size_t)((entropy * (double)stats->srcSize) / 8);
}

static size_t DataStatsU8_estimateBitpackedSize(DataStatsU8* stats)
{
    if (stats->srcSize == 0) {
        return 0;
    }

    unsigned int const maxElt = DataStatsU8_getMaxElt(stats);

    int const nbBitsPerElt = ZL_nextPow2(maxElt + 1);

    return (stats->srcSize * (size_t)nbBitsPerElt + 7) / 8;
}

size_t DataStatsU8_getBitpackedSize(DataStatsU8* stats)
{
    RETURN_OR_SET_LAZY(
            stats, bitpackedSize, DataStatsU8_estimateBitpackedSize(stats));
}

static size_t DataStatsU8_estimateFlatpackedSize(DataStatsU8* stats)
{
    size_t const nbElts = DataStatsU8_getCardinality(stats);

    int const nbBitsPerElt = ZL_nextPow2(nbElts);

    return (stats->srcSize * (size_t)nbBitsPerElt + 7) / 8 + nbElts;
}

size_t DataStatsU8_getFlatpackedSize(DataStatsU8* stats)
{
    RETURN_OR_SET_LAZY(
            stats, flatpackedSize, DataStatsU8_estimateFlatpackedSize(stats);)
}

static size_t DataStatsU8_estimateConstantSize(DataStatsU8* stats)
{
    if (DataStatsU8_getCardinality(stats) != 1) {
        return (size_t)-1;
    }

    return 1 + ZL_varintSize(DataStatsU8_totalElements(stats));
}

size_t DataStatsU8_getConstantSize(DataStatsU8* stats)
{
    RETURN_OR_SET_LAZY(
            stats, constantSize, DataStatsU8_estimateConstantSize(stats));
}
