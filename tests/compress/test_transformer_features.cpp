// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/compress/selectors/transformer/cardinality.h"
#include "openzl/compress/selectors/transformer/numeric_extract.h"
#include "openzl/compress/selectors/transformer/numeric_stats.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/xxhash.h"

namespace {

template <typename T, size_t Size>
const uint8_t* bytes(const std::array<T, Size>& values)
{
    return reinterpret_cast<const uint8_t*>(values.data());
}

struct LegacyExtractScratch {
    std::array<TRS_NumericKmvEntry, TRS_NUMERIC_KMV_K> kmvEntries = {};
    std::array<uint64_t, TRS_NUMERIC_CARDINALITY_SCRATCH_WORDS>
            cardinalityBitmap = {};

    void attach(TRS_NumericExtractWorkspace& workspace)
    {
        workspace.kmv_entries                 = kmvEntries.data();
        workspace.kmv_capacity                = kmvEntries.size();
        workspace.cardinality_bitmap          = cardinalityBitmap.data();
        workspace.cardinality_bitmap_capacity = cardinalityBitmap.size();
    }
};

uint64_t hashLittleEndianU64(uint64_t value)
{
    std::array<uint8_t, sizeof(value)> encoded = {};
    ZL_writeLE64(encoded.data(), value);
    return XXH3_64bits(encoded.data(), encoded.size());
}

double referenceSortedGapMode(std::vector<uint64_t> values)
{
    constexpr size_t maxValues = 512;
    constexpr size_t keepWidth = 384;
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    if (values.size() > maxValues) {
        values.resize(maxValues);
    }

    size_t const width = std::min(values.size(), keepWidth);
    if (width < 2) {
        return 0.0;
    }
    size_t const start = values.size() - width;
    size_t maxCount    = 0;
    for (size_t i = start + 1; i < values.size(); ++i) {
        uint64_t const gap = values[i] - values[i - 1];
        size_t count       = 0;
        for (size_t j = i; j < values.size(); ++j) {
            count += values[j] - values[j - 1] == gap;
        }
        maxCount = std::max(maxCount, count);
    }
    return static_cast<double>(maxCount) / static_cast<double>(width - 1);
}

double computeSortedGapMode(const std::vector<uint64_t>& values)
{
    constexpr uint64_t canary = UINT64_C(0xD15EA5E5CAFEBABE);
    std::array<uint64_t, TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES + 2> storage;
    storage.front()     = canary;
    storage.back()      = canary;
    double const result = TRS_numeric_compute_sorted_gap_mode(
            values.data(),
            values.size(),
            storage.data() + 1,
            TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES);
    EXPECT_EQ(storage.front(), canary);
    EXPECT_EQ(storage.back(), canary);
    return result;
}

TEST(TransformerFeaturesTest, EmptyInputKeepsWidthMetadata)
{
    for (const int width : { 1, 2, 4, 8 }) {
        SCOPED_TRACE(width);
        TRS_NumericFeatures features;
        ASSERT_TRUE(TRS_numericFeatures_extract_from_bytes(
                &features, nullptr, 0, width, nullptr));
        EXPECT_EQ(features.count, 0);
        EXPECT_EQ(features.elt_width, width);
        EXPECT_EQ(features.min_u, 0);
        EXPECT_EQ(features.max_u, 0);
        EXPECT_EQ(features.cardinality_est, 0);
    }
}

TEST(TransformerFeaturesTest, PreservesSignedAndUnsignedViews)
{
    const std::array<uint16_t, 4> values = {
        0,
        std::numeric_limits<int16_t>::max(),
        uint16_t{ 1 } << 15,
        std::numeric_limits<uint16_t>::max(),
    };

    std::array<uint32_t, values.size()> scratchValues                 = {};
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    LegacyExtractScratch extractScratch;
    TRS_NumericExtractWorkspace workspace = {
        .values          = scratchValues.data(),
        .capacity        = scratchValues.size(),
        .match4_table    = matchTable.data(),
        .match4_capacity = matchTable.size(),
    };
    extractScratch.attach(workspace);

    TRS_NumericFeatures features;
    ASSERT_TRUE(TRS_numericFeatures_extract_from_bytes(
            &features,
            bytes(values),
            sizeof(values),
            sizeof(values[0]),
            &workspace));

    EXPECT_EQ(features.count, values.size());
    EXPECT_EQ(features.min_u, 0);
    EXPECT_EQ(features.max_u, std::numeric_limits<uint16_t>::max());
    EXPECT_EQ(features.sum_u, 131070);
    EXPECT_EQ(features.min_s, std::numeric_limits<int16_t>::min());
    EXPECT_EQ(features.max_s, std::numeric_limits<int16_t>::max());
    EXPECT_EQ(features.sum_s, -2);
    EXPECT_EQ(features.range_u, std::numeric_limits<uint16_t>::max());
    EXPECT_EQ(features.range_s, std::numeric_limits<uint16_t>::max());
    EXPECT_EQ(features.zero_count, 1);
    EXPECT_EQ(features.delta_up_count, 3);
    EXPECT_EQ(features.delta_down_count, 0);
}

TEST(TransformerFeaturesTest, RejectsMissingOrUndersizedWorkspace)
{
    const std::array<uint16_t, 4> values = { 1, 2, 3, 4 };
    TRS_NumericFeatures features;

    EXPECT_FALSE(TRS_numericFeatures_extract_from_bytes(
            &features,
            bytes(values),
            sizeof(values),
            sizeof(values[0]),
            nullptr));

    std::array<uint32_t, values.size() - 1> scratchValues             = {};
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    LegacyExtractScratch extractScratch;
    TRS_NumericExtractWorkspace workspace = {
        .values          = scratchValues.data(),
        .capacity        = scratchValues.size(),
        .match4_table    = matchTable.data(),
        .match4_capacity = matchTable.size(),
    };
    extractScratch.attach(workspace);
    EXPECT_FALSE(TRS_numericFeatures_extract_from_bytes(
            &features,
            bytes(values),
            sizeof(values),
            sizeof(values[0]),
            &workspace));

    workspace.capacity        = values.size();
    workspace.match4_capacity = matchTable.size() - 1;
    EXPECT_FALSE(TRS_numericFeatures_extract_from_bytes(
            &features,
            bytes(values),
            sizeof(values),
            sizeof(values[0]),
            &workspace));

    workspace.match4_capacity = matchTable.size();
    workspace.kmv_capacity    = TRS_NUMERIC_KMV_K - 1;
    EXPECT_FALSE(TRS_numericFeatures_extract_from_bytes(
            &features,
            bytes(values),
            sizeof(values),
            sizeof(values[0]),
            &workspace));

    workspace.kmv_capacity = TRS_NUMERIC_KMV_K;
    workspace.cardinality_bitmap_capacity =
            TRS_CARDINALITY_U16_BITMAP_WORDS - 1;
    EXPECT_FALSE(TRS_numericFeatures_extract_from_bytes(
            &features,
            bytes(values),
            sizeof(values),
            sizeof(values[0]),
            &workspace));
}

template <typename T>
void expectLegacyExtractionSupportsWidth()
{
    const std::array<T, 6> values                     = { 0, 1, 2, 3, 2, 1 };
    const std::array<uint64_t, 6> widenedValues       = { 0, 1, 2, 3, 2, 1 };
    std::array<uint32_t, values.size()> scratchValues = {};
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    LegacyExtractScratch extractScratch;
    TRS_NumericExtractWorkspace workspace = {
        .values          = scratchValues.data(),
        .capacity        = scratchValues.size(),
        .match4_table    = matchTable.data(),
        .match4_capacity = matchTable.size(),
    };
    extractScratch.attach(workspace);

    TRS_NumericFeatures fromWidened;
    ASSERT_TRUE(TRS_numericFeatures_extract(
            &fromWidened,
            widenedValues.data(),
            widenedValues.size(),
            sizeof(T),
            &workspace));
    EXPECT_EQ(fromWidened.count, values.size());
    EXPECT_EQ(fromWidened.elt_width, sizeof(T));
    EXPECT_EQ(fromWidened.min_u, 0);
    EXPECT_EQ(fromWidened.max_u, 3);
    EXPECT_EQ(fromWidened.sum_u, 9);
    EXPECT_EQ(fromWidened.min_s, 0);
    EXPECT_EQ(fromWidened.max_s, 3);
    EXPECT_EQ(fromWidened.sum_s, 9);
    EXPECT_EQ(fromWidened.zero_count, 1);
    EXPECT_EQ(fromWidened.delta_up_count, 3);
    EXPECT_EQ(fromWidened.delta_down_count, 2);
    EXPECT_EQ(fromWidened.match4, 0);
    EXPECT_EQ(fromWidened.d8_cardinality_est, 0);

    TRS_NumericFeatures fromBytes;
    ASSERT_TRUE(TRS_numericFeatures_extract_from_bytes(
            &fromBytes, bytes(values), sizeof(values), sizeof(T), &workspace));
    EXPECT_EQ(fromBytes.count, fromWidened.count);
    EXPECT_EQ(fromBytes.elt_width, fromWidened.elt_width);
    EXPECT_EQ(fromBytes.min_u, fromWidened.min_u);
    EXPECT_EQ(fromBytes.max_u, fromWidened.max_u);
    EXPECT_EQ(fromBytes.sum_u, fromWidened.sum_u);
    EXPECT_EQ(fromBytes.min_s, fromWidened.min_s);
    EXPECT_EQ(fromBytes.max_s, fromWidened.max_s);
    EXPECT_EQ(fromBytes.sum_s, fromWidened.sum_s);
    EXPECT_EQ(fromBytes.zero_count, fromWidened.zero_count);
    EXPECT_EQ(fromBytes.delta_up_count, fromWidened.delta_up_count);
    EXPECT_EQ(fromBytes.delta_down_count, fromWidened.delta_down_count);
}

TEST(TransformerFeaturesTest, LegacyExtractionSupportsAllWidths)
{
    expectLegacyExtractionSupportsWidth<uint8_t>();
    expectLegacyExtractionSupportsWidth<uint16_t>();
    expectLegacyExtractionSupportsWidth<uint32_t>();
    expectLegacyExtractionSupportsWidth<uint64_t>();
}

TEST(TransformerFeaturesTest, WidenedExtractionEnforcesWorkspaceContract)
{
    const std::array<uint64_t, 4> values = { 1, 2, 3, 4 };
    TRS_NumericFeatures features;

    EXPECT_FALSE(TRS_numericFeatures_extract(
            &features, values.data(), values.size(), 2, nullptr));

    std::array<uint32_t, values.size() - 1> scratchValues = {};
    LegacyExtractScratch extractScratch;
    TRS_NumericExtractWorkspace workspace = {
        .values   = scratchValues.data(),
        .capacity = scratchValues.size(),
    };
    extractScratch.attach(workspace);
    EXPECT_FALSE(TRS_numericFeatures_extract(
            &features, values.data(), values.size(), 2, &workspace));

    EXPECT_TRUE(TRS_numericFeatures_extract(
            &features, values.data(), values.size(), 8, nullptr));
    EXPECT_EQ(features.count, values.size());

    EXPECT_TRUE(TRS_numericFeatures_extract(&features, nullptr, 0, 4, nullptr));
    EXPECT_EQ(features.count, 0);
    EXPECT_EQ(features.elt_width, 4);
}

void expectLegacyDecisionFieldsEqual(
        const TRS_NumericFeatures& full,
        const TRS_NumericFeatures& decision,
        bool compareD8Cardinality)
{
    EXPECT_EQ(decision.count, full.count);
    EXPECT_EQ(decision.elt_width, full.elt_width);
    EXPECT_EQ(decision.min_u, full.min_u);
    EXPECT_EQ(decision.max_u, full.max_u);
    EXPECT_EQ(decision.min_s, full.min_s);
    EXPECT_EQ(decision.max_s, full.max_s);
    EXPECT_EQ(decision.zero_count, full.zero_count);
    EXPECT_EQ(decision.delta_up_count, full.delta_up_count);
    EXPECT_EQ(decision.delta_down_count, full.delta_down_count);
    EXPECT_EQ(decision.delta_min_s, full.delta_min_s);
    EXPECT_EQ(decision.delta_max_s, full.delta_max_s);
    EXPECT_EQ(decision.mean_abs_delta_u_fp, full.mean_abs_delta_u_fp);
    EXPECT_EQ(decision.cardinality_est, full.cardinality_est);
    EXPECT_EQ(decision.pair_cardinality_est, full.pair_cardinality_est);
    EXPECT_EQ(decision.sorted_gap_cv_fp, full.sorted_gap_cv_fp);
    EXPECT_EQ(decision.min_lb0, full.min_lb0);
    EXPECT_EQ(decision.range_u, full.range_u);
    EXPECT_EQ(decision.range_s, full.range_s);
    EXPECT_EQ(decision.zero_ratio_fp, full.zero_ratio_fp);
    EXPECT_EQ(decision.delta_up_ratio_fp, full.delta_up_ratio_fp);
    EXPECT_EQ(decision.delta_down_ratio_fp, full.delta_down_ratio_fp);
    if (compareD8Cardinality) {
        EXPECT_EQ(decision.d8_cardinality_est, full.d8_cardinality_est);
    }
}

template <typename T>
void expectSub64DecisionMatchesFull()
{
    constexpr T signBit           = T{ 1 } << (sizeof(T) * 8 - 1);
    const std::array<T, 8> values = {
        0,
        1,
        static_cast<T>(signBit - 1),
        signBit,
        std::numeric_limits<T>::max(),
        4,
        4,
        2,
    };
    std::array<uint32_t, values.size()> scratchValues                 = {};
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    LegacyExtractScratch extractScratch;
    TRS_NumericExtractWorkspace workspace = {
        .values          = scratchValues.data(),
        .capacity        = scratchValues.size(),
        .match4_table    = matchTable.data(),
        .match4_capacity = matchTable.size(),
    };
    extractScratch.attach(workspace);

    TRS_NumericFeatures full;
    ASSERT_TRUE(TRS_numericFeatures_extract_from_bytes(
            &full, bytes(values), sizeof(values), sizeof(T), &workspace));
    TRS_NumericFeatures decision;
    ASSERT_TRUE(TRS_numericFeatures_extract_sub64_decision_from_bytes(
            &decision, bytes(values), sizeof(values), sizeof(T), &workspace));
    expectLegacyDecisionFieldsEqual(full, decision, sizeof(T) == 2);
}

TEST(TransformerFeaturesTest, Sub64DecisionExtractorsMatchFullFeatures)
{
    expectSub64DecisionMatchesFull<uint16_t>();
    expectSub64DecisionMatchesFull<uint32_t>();
}

void expectNum64DecisionFieldsEqual(
        const TRS_NumericFeaturesV2& full,
        const TRS_NumericFeaturesV2& decision)
{
    EXPECT_EQ(decision.count, full.count);
    EXPECT_EQ(decision.elt_width, full.elt_width);
    EXPECT_EQ(decision.zero_count, full.zero_count);
    EXPECT_EQ(decision.delta_up_count, full.delta_up_count);
    EXPECT_EQ(decision.delta_down_count, full.delta_down_count);
    EXPECT_EQ(decision.min_u, full.min_u);
    EXPECT_EQ(decision.max_u, full.max_u);
    EXPECT_EQ(decision.min_s, full.min_s);
    EXPECT_EQ(decision.max_s, full.max_s);
    EXPECT_EQ(decision.range_u, full.range_u);
    EXPECT_EQ(decision.range_s, full.range_s);
    EXPECT_EQ(decision.cardinality_est, full.cardinality_est);
    EXPECT_EQ(decision.pair_cardinality_est, full.pair_cardinality_est);
    EXPECT_EQ(decision.min_lb0, full.min_lb0);
    EXPECT_DOUBLE_EQ(decision.mean_abs_delta_u, full.mean_abs_delta_u);
    EXPECT_DOUBLE_EQ(decision.zero_ratio, full.zero_ratio);
    EXPECT_DOUBLE_EQ(decision.delta_up_ratio, full.delta_up_ratio);
    EXPECT_DOUBLE_EQ(decision.delta_down_ratio, full.delta_down_ratio);
    EXPECT_DOUBLE_EQ(decision.sorted_gap_nmad, full.sorted_gap_nmad);
    EXPECT_DOUBLE_EQ(decision.sorted_gap_mode, full.sorted_gap_mode);
}

TEST(TransformerFeaturesTest, Num64DecisionExtractorMatchesFullFeatures)
{
    const std::array<uint64_t, 8> values = {
        0,
        1,
        std::numeric_limits<int64_t>::max(),
        uint64_t{ 1 } << 63,
        std::numeric_limits<uint64_t>::max(),
        4,
        4,
        2,
    };
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    std::array<uint64_t, TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES>
            sortedGapBuffer               = {};
    TRS_NumericExtractWorkspace workspace = {
        .match4_table        = matchTable.data(),
        .match4_capacity     = matchTable.size(),
        .sorted_gap_buffer   = sortedGapBuffer.data(),
        .sorted_gap_capacity = sortedGapBuffer.size(),
    };

    TRS_NumericFeaturesV2 full;
    ASSERT_TRUE(TRS_numericFeaturesV2_extract_from_bytes(
            &full,
            bytes(values),
            sizeof(values),
            sizeof(values[0]),
            &workspace));
    TRS_NumericFeaturesV2 const decision =
            TRS_numericFeaturesV2_extract_num64_decision(
                    values.data(), values.size(), &workspace);
    expectNum64DecisionFieldsEqual(full, decision);
}

TEST(TransformerFeaturesTest, DecisionExtractorsHandleShortInputs)
{
    TRS_NumericFeatures sub64;
    ASSERT_TRUE(TRS_numericFeatures_extract_sub64_decision_from_bytes(
            &sub64, nullptr, 0, 2, nullptr));
    EXPECT_EQ(sub64.count, 0);
    EXPECT_EQ(sub64.elt_width, 2);

    TRS_NumericFeaturesV2 const num64Empty =
            TRS_numericFeaturesV2_extract_num64_decision(nullptr, 0, nullptr);
    EXPECT_EQ(num64Empty.count, 0);
    EXPECT_EQ(num64Empty.elt_width, 8);

    const std::array<uint64_t, 1> value                               = { 7 };
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    std::array<uint64_t, TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES>
            sortedGapBuffer               = {};
    TRS_NumericExtractWorkspace workspace = {
        .match4_table        = matchTable.data(),
        .match4_capacity     = matchTable.size(),
        .sorted_gap_buffer   = sortedGapBuffer.data(),
        .sorted_gap_capacity = sortedGapBuffer.size(),
    };
    TRS_NumericFeaturesV2 full;
    ASSERT_TRUE(TRS_numericFeaturesV2_extract_from_bytes(
            &full, bytes(value), sizeof(value), sizeof(value[0]), &workspace));
    TRS_NumericFeaturesV2 const decision =
            TRS_numericFeaturesV2_extract_num64_decision(
                    value.data(), value.size(), &workspace);
    expectNum64DecisionFieldsEqual(full, decision);
}

TEST(TransformerFeaturesTest, Num16DecisionRequiresWideningWorkspace)
{
    const std::array<uint16_t, 4> values = { 1, 2, 3, 4 };
    TRS_NumericFeatures features;
    EXPECT_FALSE(TRS_numericFeatures_extract_sub64_decision_from_bytes(
            &features,
            bytes(values),
            sizeof(values),
            sizeof(values[0]),
            nullptr));

    std::array<uint32_t, values.size() - 1> scratchValues = {};
    LegacyExtractScratch extractScratch;
    TRS_NumericExtractWorkspace workspace = {
        .values   = scratchValues.data(),
        .capacity = scratchValues.size(),
    };
    extractScratch.attach(workspace);
    EXPECT_FALSE(TRS_numericFeatures_extract_sub64_decision_from_bytes(
            &features,
            bytes(values),
            sizeof(values),
            sizeof(values[0]),
            &workspace));
}

TEST(TransformerFeaturesTest, Num64V2RepresentsFullWidthExtrema)
{
    const std::array<uint64_t, 3> values = {
        0,
        uint64_t{ 1 } << 63,
        std::numeric_limits<uint64_t>::max(),
    };

    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    std::array<uint64_t, TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES>
            sortedGapBuffer               = {};
    TRS_NumericExtractWorkspace workspace = {
        .match4_table        = matchTable.data(),
        .match4_capacity     = matchTable.size(),
        .sorted_gap_buffer   = sortedGapBuffer.data(),
        .sorted_gap_capacity = sortedGapBuffer.size(),
    };
    TRS_NumericFeaturesV2 features;
    ASSERT_TRUE(TRS_numericFeaturesV2_extract_from_bytes(
            &features,
            bytes(values),
            sizeof(values),
            sizeof(values[0]),
            &workspace));

    EXPECT_EQ(features.count, values.size());
    EXPECT_EQ(features.elt_width, sizeof(values[0]));
    EXPECT_EQ(features.min_u, 0);
    EXPECT_EQ(features.max_u, std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(features.min_s, std::numeric_limits<int64_t>::min());
    EXPECT_EQ(features.max_s, 0);
    EXPECT_EQ(features.range_u, std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(features.range_s, uint64_t{ 1 } << 63);
}

TEST(TransformerFeaturesTest, V2RejectsMissingSortedGapWorkspace)
{
    const std::array<uint64_t, 4> values = { 1, 2, 3, 4 };
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    std::array<uint64_t, TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES - 1>
            sortedGapBuffer               = {};
    TRS_NumericExtractWorkspace workspace = {
        .match4_table        = matchTable.data(),
        .match4_capacity     = matchTable.size(),
        .sorted_gap_buffer   = sortedGapBuffer.data(),
        .sorted_gap_capacity = sortedGapBuffer.size(),
    };
    TRS_NumericFeaturesV2 features;

    EXPECT_FALSE(TRS_numericFeaturesV2_extract_from_bytes(
            &features,
            bytes(values),
            sizeof(values),
            sizeof(values[0]),
            nullptr));
    EXPECT_FALSE(TRS_numericFeaturesV2_extract_from_bytes(
            &features,
            bytes(values),
            sizeof(values),
            sizeof(values[0]),
            &workspace));
}

TEST(TransformerFeaturesTest, EmptySub64V2DoesNotRequireWorkspace)
{
    for (size_t const width : { 1, 2, 4 }) {
        SCOPED_TRACE(width);
        TRS_NumericFeaturesV2 fromWidened;
        ASSERT_TRUE(TRS_numericFeaturesV2_extract(
                &fromWidened, nullptr, 0, width, nullptr));
        EXPECT_EQ(fromWidened.count, 0);
        EXPECT_EQ(fromWidened.elt_width, width);
        EXPECT_DOUBLE_EQ(fromWidened.sorted_gap_mode, 0.0);

        TRS_NumericFeaturesV2 fromBytes;
        ASSERT_TRUE(TRS_numericFeaturesV2_extract_from_bytes(
                &fromBytes, nullptr, 0, width, nullptr));
        EXPECT_EQ(fromBytes.count, 0);
        EXPECT_EQ(fromBytes.elt_width, width);
        EXPECT_DOUBLE_EQ(fromBytes.sorted_gap_mode, 0.0);
    }
}

template <typename T>
void expectSub64V2SortedGapMode()
{
    const std::array<T, 4> values                     = { 7, 1, 5, 3 };
    const std::array<uint64_t, 4> widenedValues       = { 7, 1, 5, 3 };
    std::array<uint32_t, values.size()> scratchValues = {};
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    std::array<uint64_t, TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES>
            sortedGapBuffer = {};
    LegacyExtractScratch extractScratch;
    TRS_NumericExtractWorkspace workspace = {
        .values              = scratchValues.data(),
        .capacity            = scratchValues.size(),
        .match4_table        = matchTable.data(),
        .match4_capacity     = matchTable.size(),
        .sorted_gap_buffer   = sortedGapBuffer.data(),
        .sorted_gap_capacity = sortedGapBuffer.size(),
    };
    extractScratch.attach(workspace);

    TRS_NumericFeaturesV2 fromBytes;
    ASSERT_TRUE(TRS_numericFeaturesV2_extract_from_bytes(
            &fromBytes,
            bytes(values),
            sizeof(values),
            sizeof(values[0]),
            &workspace));
    EXPECT_DOUBLE_EQ(fromBytes.sorted_gap_mode, 1.0);

    TRS_NumericFeaturesV2 fromWidened;
    ASSERT_TRUE(TRS_numericFeaturesV2_extract(
            &fromWidened,
            widenedValues.data(),
            widenedValues.size(),
            sizeof(values[0]),
            &workspace));
    EXPECT_DOUBLE_EQ(fromWidened.sorted_gap_mode, 1.0);
}

TEST(TransformerFeaturesTest, Sub64V2IncludesSortedGapMode)
{
    expectSub64V2SortedGapMode<uint8_t>();
    expectSub64V2SortedGapMode<uint16_t>();
    expectSub64V2SortedGapMode<uint32_t>();
}

TEST(TransformerFeaturesTest, CardinalityCountsSmallDistinctSet)
{
    const std::array<uint64_t, 8> values = { 1, 2, 3, 4, 1, 2, 3, 4 };
    EXPECT_EQ(TRS_estimate_cardinality_u64(values.data(), values.size()), 4);
}

TEST(TransformerFeaturesTest, CardinalityCountsOverlappingSequences)
{
    const std::array<uint32_t, 4> values = { 1, 2, 1, 2 };
    const std::array<uint8_t, 9> bytes   = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    EXPECT_EQ(
            TRS_estimate_pair_cardinality_u32(values.data(), values.size()), 2);
    EXPECT_EQ(TRS_estimate_d8_cardinality(bytes.data(), bytes.size(), 1), 1);
}

TEST(TransformerFeaturesTest, CardinalityCountsPresenceBitmaps)
{
    std::array<uint64_t, TRS_CARDINALITY_U8_BITMAP_WORDS> seenValues     = {};
    std::array<uint64_t, TRS_CARDINALITY_U8_PAIR_BITMAP_WORDS> seenPairs = {};
    seenValues[1 >> 6] |= uint64_t{ 1 } << (1 & 63);
    seenValues[2 >> 6] |= uint64_t{ 1 } << (2 & 63);
    seenValues[255 >> 6] |= uint64_t{ 1 } << (255 & 63);
    seenPairs[0x0102 >> 6] |= uint64_t{ 1 } << (0x0102 & 63);
    seenPairs[0xFFFF >> 6] |= uint64_t{ 1 } << (0xFFFF & 63);

    EXPECT_EQ(TRS_estimate_cardinality_u8_bitmap(seenValues.data(), 5), 3);
    EXPECT_EQ(TRS_estimate_cardinality_u16_bitmap(seenPairs.data(), 5), 2);
    EXPECT_EQ(TRS_estimate_pair_cardinality_u8_bitmap(seenPairs.data(), 5), 2);
}

TEST(TransformerFeaturesTest, NumericStatisticsRecognizeRegularSequences)
{
    const std::array<uint64_t, 4> values                   = { 1, 3, 5, 7 };
    std::array<TRS_NumericKmvEntry, TRS_NUMERIC_KMV_K> kmv = {};
    size_t kmvSize                                         = 0;
    for (uint64_t value : values)
        TRS_numeric_kmv_track_value(kmv.data(), &kmvSize, value);

    EXPECT_EQ(TRS_numeric_kmv_compute_gap_nmad_fp(kmv.data(), kmvSize), 0);
    EXPECT_DOUBLE_EQ(
            computeSortedGapMode(
                    std::vector<uint64_t>(values.begin(), values.end())),
            1.0);
    EXPECT_DOUBLE_EQ(TRS_numeric_compute_transition_gap_cv(4.0, 8.0, 2), 0.0);
}

TEST(TransformerFeaturesTest, NumericSortedGapModeCountsSeparatedMatches)
{
    const std::array<uint64_t, 5> values = { 6, 0, 4, 1, 3 };
    EXPECT_DOUBLE_EQ(
            computeSortedGapMode(
                    std::vector<uint64_t>(values.begin(), values.end())),
            0.5);
}

TEST(TransformerFeaturesTest, NumericSortedGapModeMatchesReference)
{
    constexpr std::array<size_t, 9> sizes = {
        0, 1, 2, 511, 512, 513, 1023, 1024, 4097,
    };

    for (size_t const size : sizes) {
        SCOPED_TRACE(size);
        std::vector<uint64_t> ascending(size);
        std::vector<uint64_t> repeated(size);
        std::vector<uint64_t> generated(size);
        uint64_t state = UINT64_C(0x9E3779B185EBCA87);
        for (size_t i = 0; i < size; ++i) {
            ascending[i] = i;
            repeated[i]  = (i * 17) % 73;
            state        = state * UINT64_C(6364136223846793005) + 1;
            generated[i] = state ^ ((uint64_t)i << (i % 31));
        }
        std::vector<uint64_t> descending = ascending;
        std::reverse(descending.begin(), descending.end());
        if (!generated.empty()) {
            generated.front() = 0;
            generated.back()  = UINT64_MAX;
        }

        for (const std::vector<uint64_t>* values :
             { &ascending, &descending, &repeated, &generated }) {
            EXPECT_DOUBLE_EQ(
                    computeSortedGapMode(*values),
                    referenceSortedGapMode(*values));
        }
    }
}

TEST(TransformerFeaturesTest, NumericKmvRetainsSmallestHashes)
{
    constexpr size_t inputSize = TRS_NUMERIC_KMV_K * 4;
    std::array<TRS_NumericKmvEntry, TRS_NUMERIC_KMV_K> kmv = {};
    std::array<uint64_t, inputSize> allHashes              = {};
    size_t kmvSize                                         = 0;

    for (size_t i = 0; i < inputSize; ++i) {
        uint64_t const value = (uint64_t)i;
        allHashes[i]         = hashLittleEndianU64(value);
        TRS_numeric_kmv_track_value(kmv.data(), &kmvSize, value);

        ASSERT_EQ(kmvSize, std::min(i + 1, (size_t)TRS_NUMERIC_KMV_K));
        for (size_t child = 1; child < kmvSize; ++child) {
            size_t const parent = (child - 1) / 2;
            ASSERT_GE(kmv[parent].hash, kmv[child].hash)
                    << "after inserting value " << value;
        }
    }

    std::array<uint64_t, TRS_NUMERIC_KMV_K> retainedHashes = {};
    for (size_t i = 0; i < kmvSize; ++i) {
        retainedHashes[i] = kmv[i].hash;
    }
    std::sort(allHashes.begin(), allHashes.end());
    std::sort(retainedHashes.begin(), retainedHashes.end());
    EXPECT_TRUE(
            std::equal(
                    retainedHashes.begin(),
                    retainedHashes.end(),
                    allHashes.begin()));
}

TEST(TransformerFeaturesTest, NumericStatisticsCountRepeatedFourByteWindows)
{
    const std::array<uint8_t, 8> data = {
        'a', 'b', 'c', 'd', 'a', 'b', 'c', 'd'
    };
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    EXPECT_EQ(
            TRS_numeric_compute_lz_matches_with_table(
                    data.data(),
                    data.size(),
                    1,
                    matchTable.data(),
                    matchTable.size()),
            1);
}

TEST(TransformerFeaturesTest, CanonicalizesBigEndianNumericBytes)
{
    const auto expectCanonical = [](const std::vector<uint8_t>& bigEndian,
                                    size_t eltWidth,
                                    const std::vector<uint8_t>& expected) {
        ASSERT_EQ(bigEndian.size(), expected.size());
        for (size_t i = 0; i < bigEndian.size(); ++i) {
            EXPECT_EQ(
                    TRS_numeric_canonical_byte_from_big_endian(
                            bigEndian.data(), i, eltWidth),
                    expected[i])
                    << "byte " << i << " at element width " << eltWidth;
        }
    };

    expectCanonical({ 0x12, 0x34 }, 1, { 0x12, 0x34 });
    expectCanonical({ 0x12, 0x34, 0xAB, 0xCD }, 2, { 0x34, 0x12, 0xCD, 0xAB });
    expectCanonical(
            { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF },
            4,
            { 0x67, 0x45, 0x23, 0x01, 0xEF, 0xCD, 0xAB, 0x89 });
    expectCanonical(
            { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF },
            8,
            { 0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01 });
}

TEST(TransformerFeaturesTest, NumericStatisticsEncodeDoubleFpBoundary)
{
    double const saturationInput = 0x1p32;
    double const below           = std::nextafter(saturationInput, 0.0);
    double const above           = std::nextafter(
            saturationInput, std::numeric_limits<double>::infinity());

    EXPECT_EQ(TRS_numeric_encode_double_fp(0.5), uint64_t{ 1 } << 31);
    EXPECT_EQ(
            TRS_numeric_encode_double_fp(below), UINT64_MAX - uint64_t{ 2047 });
    EXPECT_EQ(TRS_numeric_encode_double_fp(saturationInput), UINT64_MAX);
    EXPECT_EQ(TRS_numeric_encode_double_fp(above), UINT64_MAX);
}

TEST(TransformerFeaturesTest, Num8UsesCallerProvidedMatchTable)
{
    const std::array<uint8_t, 8> data = {
        'a', 'b', 'c', 'd', 'a', 'b', 'c', 'd'
    };
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    LegacyExtractScratch extractScratch;
    TRS_NumericExtractWorkspace workspace = {};
    workspace.match4_table                = matchTable.data();
    workspace.match4_capacity             = matchTable.size();
    extractScratch.attach(workspace);
    TRS_NumericFeatures features;

    ASSERT_TRUE(TRS_numericFeatures_extract_from_bytes(
            &features, data.data(), data.size(), 1, &workspace));
    EXPECT_EQ(features.match4, 1);
}

TEST(TransformerFeaturesTest, CanonicalHashesMatchTrainingFeatureContract)
{
    /* Golden values for the canonical little-endian representation used by
     * the current Transformer training pipeline. */
    std::array<uint64_t, 512> values = {};
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = (uint64_t)i * UINT64_C(0x9E3779B185EBCA87)
                ^ ((uint64_t)i << (i % 29));
    }

    EXPECT_EQ(TRS_estimate_cardinality_u64(values.data(), values.size()), 508);
    EXPECT_EQ(
            TRS_estimate_pair_cardinality_u64(values.data(), values.size()),
            511);
    EXPECT_EQ(
            TRS_estimate_d8_cardinality(
                    bytes(values), sizeof(values), sizeof(values[0])),
            4089);
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    EXPECT_EQ(
            TRS_numeric_compute_lz_matches_with_table(
                    bytes(values),
                    sizeof(values),
                    sizeof(values[0]),
                    matchTable.data(),
                    matchTable.size()),
            5);

    std::array<TRS_NumericKmvEntry, TRS_NUMERIC_KMV_K> kmv = {};
    size_t kmvSize                                         = 0;
    TRS_numeric_kmv_track_value(
            kmv.data(), &kmvSize, UINT64_C(0x0102030405060708));
    ASSERT_EQ(kmvSize, 1);
    EXPECT_EQ(kmv[0].hash, UINT64_C(10416736986388286110));

    std::vector<uint64_t> hllValues(70000);
    for (size_t i = 0; i < hllValues.size(); ++i) {
        hllValues[i] = (uint64_t)(i % 10000) * UINT64_C(0xD6E8FEB86659FD93);
    }
    EXPECT_EQ(
            TRS_estimate_cardinality_u64(hllValues.data(), hllValues.size()),
            10000);
}
} // namespace
