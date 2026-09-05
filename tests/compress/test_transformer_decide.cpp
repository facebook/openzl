// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/compress/selectors/transformer/generated/score_numeric16.h"
#include "openzl/compress/selectors/transformer/generated/score_numeric32.h"
#include "openzl/compress/selectors/transformer/generated/score_numeric64.h"
#include "openzl/compress/selectors/transformer/generated/score_numeric8.h"
#include "openzl/compress/selectors/transformer/generic_numeric_decide.h"
#include "openzl/compress/selectors/transformer/numeric_stats.h"
#include "openzl/compress/selectors/transformer/score_guard.h"
#include "tests/datagen/DataGen.h"

namespace {

using openzl::tests::datagen::DataGen;

template <typename T, size_t Size>
const uint8_t* bytes(const std::array<T, Size>& values)
{
    return reinterpret_cast<const uint8_t*>(values.data());
}

template <typename T>
const uint8_t* bytes(const std::vector<T>& values)
{
    return reinterpret_cast<const uint8_t*>(values.data());
}

struct LegacyExtractScratch {
    std::array<TRS_NumericKmvEntry, TRS_NUMERIC_KMV_K> kmvEntries = {};
    std::array<uint64_t, TRS_NUMERIC_CARDINALITY_SCRATCH_WORDS>
            cardinalityBitmap = {};
};

TRS_NumericExtractWorkspace makeWorkspace(
        uint32_t* values,
        size_t capacity,
        std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES>& matchTable,
        LegacyExtractScratch& extractScratch)
{
    return {
        .values                      = values,
        .capacity                    = capacity,
        .match4_table                = matchTable.data(),
        .match4_capacity             = matchTable.size(),
        .kmv_entries                 = extractScratch.kmvEntries.data(),
        .kmv_capacity                = extractScratch.kmvEntries.size(),
        .cardinality_bitmap          = extractScratch.cardinalityBitmap.data(),
        .cardinality_bitmap_capacity = extractScratch.cardinalityBitmap.size(),
    };
}

TRS_NumericExtractWorkspace makeNum64Workspace(
        std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES>& matchTable,
        std::array<uint64_t, TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES>&
                sortedGapBuffer)
{
    return {
        .match4_table        = matchTable.data(),
        .match4_capacity     = matchTable.size(),
        .sorted_gap_buffer   = sortedGapBuffer.data(),
        .sorted_gap_capacity = sortedGapBuffer.size(),
    };
}

enum class NumericDataShape { UNIFORM, LOW_CARDINALITY, STRIDED, RUNS };

template <typename T>
std::vector<T>
generateNumericValues(DataGen& gen, size_t size, NumericDataShape shape)
{
    std::vector<T> values(size);
    switch (shape) {
        case NumericDataShape::UNIFORM:
            for (T& value : values) {
                value = gen.randVal<T>("uniform_value");
            }
            break;
        case NumericDataShape::LOW_CARDINALITY:
            for (T& value : values) {
                value = gen.randVal<T>("low_cardinality_value", 0, 15);
            }
            break;
        case NumericDataShape::STRIDED: {
            T value        = gen.randVal<T>("strided_start");
            T const stride = gen.randVal<T>("stride", 1, 1024);
            for (T& element : values) {
                element = value;
                value   = static_cast<T>(value + stride);
            }
            break;
        }
        case NumericDataShape::RUNS: {
            T value          = 0;
            size_t remaining = 0;
            for (T& element : values) {
                if (remaining == 0) {
                    value     = gen.randVal<T>("run_value");
                    remaining = gen.usize_range("run_length", 1, 32);
                }
                element = value;
                remaining--;
            }
            break;
        }
    }
    return values;
}

void expectSameSub64ConsumedFeatures(
        const TRS_NumericFeatures& full,
        const TRS_NumericFeatures& fast,
        int eltWidth)
{
    EXPECT_EQ(full.count, fast.count);
    EXPECT_EQ(full.min_u, fast.min_u);
    EXPECT_EQ(full.max_u, fast.max_u);
    EXPECT_EQ(full.min_s, fast.min_s);
    EXPECT_EQ(full.max_s, fast.max_s);
    EXPECT_EQ(full.zero_count, fast.zero_count);
    EXPECT_EQ(full.delta_up_count, fast.delta_up_count);
    EXPECT_EQ(full.delta_down_count, fast.delta_down_count);
    EXPECT_EQ(full.delta_min_s, fast.delta_min_s);
    EXPECT_EQ(full.delta_max_s, fast.delta_max_s);
    EXPECT_EQ(full.range_u, fast.range_u);
    EXPECT_EQ(full.range_s, fast.range_s);
    EXPECT_EQ(full.zero_ratio_fp, fast.zero_ratio_fp);
    EXPECT_EQ(full.delta_up_ratio_fp, fast.delta_up_ratio_fp);
    EXPECT_EQ(full.delta_down_ratio_fp, fast.delta_down_ratio_fp);
    EXPECT_EQ(full.mean_abs_delta_u_fp, fast.mean_abs_delta_u_fp);
    EXPECT_EQ(full.cardinality_est, fast.cardinality_est);
    EXPECT_EQ(full.pair_cardinality_est, fast.pair_cardinality_est);
    EXPECT_EQ(full.sorted_gap_cv_fp, fast.sorted_gap_cv_fp);
    EXPECT_EQ(full.min_lb0, fast.min_lb0);
    if (eltWidth == 2) {
        EXPECT_EQ(full.d8_cardinality_est, fast.d8_cardinality_est);
    }
}

void expectSameNum64ConsumedFeatures(
        const TRS_NumericFeaturesV2& full,
        const TRS_NumericFeaturesV2& fast)
{
    EXPECT_EQ(full.count, fast.count);
    EXPECT_EQ(full.min_u, fast.min_u);
    EXPECT_EQ(full.max_u, fast.max_u);
    EXPECT_EQ(full.min_s, fast.min_s);
    EXPECT_EQ(full.max_s, fast.max_s);
    EXPECT_EQ(full.zero_count, fast.zero_count);
    EXPECT_EQ(full.delta_up_count, fast.delta_up_count);
    EXPECT_EQ(full.delta_down_count, fast.delta_down_count);
    EXPECT_EQ(full.range_u, fast.range_u);
    EXPECT_EQ(full.range_s, fast.range_s);
    EXPECT_EQ(full.cardinality_est, fast.cardinality_est);
    EXPECT_EQ(full.pair_cardinality_est, fast.pair_cardinality_est);
    EXPECT_EQ(full.min_lb0, fast.min_lb0);
    EXPECT_DOUBLE_EQ(full.zero_ratio, fast.zero_ratio);
    EXPECT_DOUBLE_EQ(full.delta_up_ratio, fast.delta_up_ratio);
    EXPECT_DOUBLE_EQ(full.delta_down_ratio, fast.delta_down_ratio);
    EXPECT_DOUBLE_EQ(full.mean_abs_delta_u, fast.mean_abs_delta_u);
    EXPECT_DOUBLE_EQ(full.sorted_gap_nmad, fast.sorted_gap_nmad);
    EXPECT_DOUBLE_EQ(full.sorted_gap_mode, fast.sorted_gap_mode);
}

void expectSameDecisionCore(
        const TRS_GenericNumericDecisionCore& full,
        const TRS_GenericNumericDecisionCore& fast)
{
    ASSERT_EQ(full.score_count, fast.score_count);
    EXPECT_EQ(full.best_op, fast.best_op);
    EXPECT_EQ(full.best_index, fast.best_index);
    for (int i = 0; i < full.score_count; ++i) {
        EXPECT_FLOAT_EQ(full.scores[i], fast.scores[i]);
        EXPECT_FLOAT_EQ(full.raw_scores[i], fast.raw_scores[i]);
        EXPECT_FLOAT_EQ(full.logits[i], fast.logits[i]);
    }
}

void expectSameDecision(
        const TRS_GenericNumericDecision& full,
        const TRS_GenericNumericDecision& fast,
        int eltWidth)
{
    expectSameSub64ConsumedFeatures(full.features, fast.features, eltWidth);
    expectSameDecisionCore(full.core, fast.core);
}

void expectSameDecision(
        const TRS_GenericNumericDecision64V2& full,
        const TRS_GenericNumericDecision64V2& fast)
{
    expectSameNum64ConsumedFeatures(full.features, fast.features);
    expectSameDecisionCore(full.core, fast.core);
}

void expectFastPathMatchesFull(const std::vector<uint16_t>& values)
{
    std::vector<uint32_t> scratchValues(values.size());
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    LegacyExtractScratch extractScratch;
    TRS_NumericExtractWorkspace scratch = makeWorkspace(
            scratchValues.data(),
            scratchValues.size(),
            matchTable,
            extractScratch);
    expectSameDecision(
            TRS_generic_numeric_decide16(
                    bytes(values), values.size() * sizeof(values[0]), &scratch),
            TRS_generic_numeric_decide16_fast(
                    bytes(values), values.size() * sizeof(values[0]), &scratch),
            sizeof(values[0]));
}

void expectFastPathMatchesFull(const std::vector<uint32_t>& values)
{
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    LegacyExtractScratch extractScratch;
    TRS_NumericExtractWorkspace scratch =
            makeWorkspace(nullptr, 0, matchTable, extractScratch);
    expectSameDecision(
            TRS_generic_numeric_decide32(
                    bytes(values), values.size() * sizeof(values[0]), &scratch),
            TRS_generic_numeric_decide32_fast(
                    bytes(values), values.size() * sizeof(values[0])),
            sizeof(values[0]));
}

void expectFastPathMatchesFull(const std::vector<uint64_t>& values)
{
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    std::array<uint64_t, TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES>
            sortedGapBuffer = {};
    TRS_NumericExtractWorkspace scratch =
            makeNum64Workspace(matchTable, sortedGapBuffer);
    expectSameDecision(
            TRS_generic_numeric_decide64_v2(
                    bytes(values), values.size() * sizeof(values[0]), &scratch),
            TRS_generic_numeric_decide64_v2_fast(
                    bytes(values),
                    values.size() * sizeof(values[0]),
                    &scratch));
}

void expectValidOperationIds(
        const TRS_GenericNumericOpId* operationIds,
        size_t count)
{
    std::array<bool, TRS_GENERIC_NUMERIC_OP_COUNT> seen = {};
    for (size_t i = 0; i < count; ++i) {
        int const value = static_cast<int>(operationIds[i]);
        ASSERT_GT(value, TRS_GENERIC_NUMERIC_OP_CONSTANT);
        ASSERT_LT(value, TRS_GENERIC_NUMERIC_OP_COUNT);
        EXPECT_FALSE(seen[value]);
        seen[value] = true;
    }
}

TEST(TransformerDecisionTest, ConstantInputsBypassScoring)
{
    const std::array<uint8_t, 8> values8   = { 7, 7, 7, 7, 7, 7, 7, 7 };
    const std::array<uint16_t, 8> values16 = { 11, 11, 11, 11, 11, 11, 11, 11 };
    const std::array<uint32_t, 8> values32 = { 12345, 12345, 12345, 12345,
                                               12345, 12345, 12345, 12345 };
    const std::array<uint64_t, 8> values64 = {
        123456789, 123456789, 123456789, 123456789,
        123456789, 123456789, 123456789, 123456789,
    };
    std::array<uint32_t, values16.size()> scratch16Values             = {};
    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    std::array<uint64_t, TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES>
            sortedGapBuffer = {};
    LegacyExtractScratch extractScratch;
    TRS_NumericExtractWorkspace scratch16 = makeWorkspace(
            scratch16Values.data(),
            scratch16Values.size(),
            matchTable,
            extractScratch);
    TRS_NumericExtractWorkspace scratch =
            makeWorkspace(nullptr, 0, matchTable, extractScratch);
    TRS_NumericExtractWorkspace scratch64 =
            makeNum64Workspace(matchTable, sortedGapBuffer);

    const TRS_GenericNumericDecision decision8 = TRS_generic_numeric_decide8(
            bytes(values8), sizeof(values8), &scratch);
    const TRS_GenericNumericDecision decision16 = TRS_generic_numeric_decide16(
            bytes(values16), sizeof(values16), &scratch16);
    const TRS_GenericNumericDecision decision32 = TRS_generic_numeric_decide32(
            bytes(values32), sizeof(values32), &scratch);
    const TRS_GenericNumericDecision64V2 decision64 =
            TRS_generic_numeric_decide64_v2(
                    bytes(values64), sizeof(values64), &scratch64);

    for (const TRS_GenericNumericDecisionCore* decision :
         { &decision8.core,
           &decision16.core,
           &decision32.core,
           &decision64.core }) {
        EXPECT_EQ(decision->best_op, TRS_GENERIC_NUMERIC_OP_CONSTANT);
        EXPECT_EQ(decision->best_index, -1);
        EXPECT_EQ(decision->score_count, 0);
    }
}

TEST(TransformerDecisionTest, EmptyInputsBypassScoring)
{
    const TRS_GenericNumericDecision decision8 =
            TRS_generic_numeric_decide8(nullptr, 0, nullptr);
    const TRS_GenericNumericDecision decision16 =
            TRS_generic_numeric_decide16_fast(nullptr, 0, nullptr);
    const TRS_GenericNumericDecision decision32 =
            TRS_generic_numeric_decide32_fast(nullptr, 0);
    const TRS_GenericNumericDecision64V2 decision64 =
            TRS_generic_numeric_decide64_v2_fast(nullptr, 0, nullptr);

    for (const TRS_GenericNumericDecisionCore* decision :
         { &decision8.core,
           &decision16.core,
           &decision32.core,
           &decision64.core }) {
        EXPECT_EQ(decision->best_op, TRS_GENERIC_NUMERIC_OP_CONSTANT);
        EXPECT_EQ(decision->best_index, -1);
        EXPECT_EQ(decision->score_count, 0);
    }
}

TEST(TransformerDecisionTest, NonConstantInputsProduceValidMetadata)
{
    const std::array<uint8_t, 32> values = {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    };

    std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
    LegacyExtractScratch extractScratch;
    TRS_NumericExtractWorkspace workspace =
            makeWorkspace(nullptr, 0, matchTable, extractScratch);
    const TRS_GenericNumericDecision decision = TRS_generic_numeric_decide8(
            bytes(values), sizeof(values), &workspace);
    EXPECT_EQ(decision.core.score_count, TRS_SCORE_NUM8_N_OPERATIONS);
    EXPECT_GE(decision.core.best_index, 0);
    EXPECT_LT(decision.core.best_index, decision.core.score_count);
    EXPECT_NE(decision.core.best_op, TRS_GENERIC_NUMERIC_OP_INVALID);
}

TEST(TransformerDecisionTest, MissingNum8WorkspaceLeavesDecisionInvalid)
{
    const std::array<uint8_t, 32> values = {
        0,  1,  2,  3,  0,  1,  2,  3,  8,  9,  10, 11, 8,  9,  10, 11,
        16, 17, 18, 19, 16, 17, 18, 19, 24, 25, 26, 27, 24, 25, 26, 27,
    };
    const TRS_GenericNumericDecision decision =
            TRS_generic_numeric_decide8(bytes(values), sizeof(values), nullptr);
    EXPECT_EQ(decision.core.best_op, TRS_GENERIC_NUMERIC_OP_INVALID);
    EXPECT_EQ(decision.core.best_index, -1);
    EXPECT_EQ(decision.core.score_count, 0);
}

TEST(TransformerDecisionTest, AllMaskedScoresHaveNoWinner)
{
    constexpr std::array<float, 3> rankings = { 3.0f, 2.0f, 1.0f };
    constexpr std::array<float, 3> scores   = { 0.0f, 0.0f, 0.0f };

    EXPECT_EQ(
            TRS_score_guard_argmax_masked(
                    rankings.data(), scores.data(), scores.size()),
            -1);
}

TEST(TransformerDecisionTest, GcdGuardMasksKnownUnitGcd)
{
    std::array<float, 2> scores = { 1.0f, 0.5f };
    constexpr std::array<TRS_GenericNumericOpId, 2> operationIds = {
        TRS_GENERIC_NUMERIC_OP_DIVIDE_BY_GCD,
        TRS_GENERIC_NUMERIC_OP_STORE,
    };
    TRS_NumericFeatures features = {};
    features.cardinality_est     = 2;
    features.range_u             = 8;
    features.min_lb0             = 1;
    features.min_u               = 1;
    features.max_u               = 9;

    TRS_apply_score_guards(
            scores.data(),
            static_cast<int>(scores.size()),
            operationIds.data(),
            &features,
            TRS_numeric_half_width_threshold(2));

    EXPECT_FLOAT_EQ(scores[0], 0.0f);
    EXPECT_FLOAT_EQ(scores[1], 0.5f);
}

TEST(TransformerDecisionTest, RangePackGuardRequiresNoOffsetAndNoNarrowing)
{
    struct TestCase {
        uint64_t min;
        uint64_t max;
        bool masked;
    };
    constexpr uint64_t threshold                = 256;
    constexpr std::array<TestCase, 3> testCases = {
        TestCase{ .min = 0, .max = threshold - 1, .masked = false },
        TestCase{ .min = 1, .max = threshold, .masked = false },
        TestCase{ .min = 0, .max = threshold, .masked = true },
    };
    constexpr std::array<TRS_GenericNumericOpId, 1> operationIds = {
        TRS_GENERIC_NUMERIC_OP_RANGE_PACK,
    };

    for (const TestCase& testCase : testCases) {
        SCOPED_TRACE(testCase.min);
        SCOPED_TRACE(testCase.max);
        std::array<float, 1> scores  = { 1.0f };
        TRS_NumericFeatures features = {};
        features.cardinality_est     = 1;
        features.range_u             = testCase.max - testCase.min;
        features.min_lb0             = 1;
        features.min_u               = testCase.min;
        features.max_u               = testCase.max;

        TRS_apply_score_guards(
                scores.data(),
                static_cast<int>(scores.size()),
                operationIds.data(),
                &features,
                threshold);

        EXPECT_FLOAT_EQ(scores[0], testCase.masked ? 0.0f : 1.0f);
    }
}

TEST(TransformerDecisionTest, FullWidthRangeDoesNotLookDense)
{
    std::array<float, 2> scores = { 1.0f, 0.5f };
    constexpr std::array<TRS_GenericNumericOpId, 2> operationIds = {
        TRS_GENERIC_NUMERIC_OP_TOKENIZE_NUMERIC,
        TRS_GENERIC_NUMERIC_OP_TOKENIZE_NUMERIC_SORTED,
    };
    TRS_NumericFeatures features = {};
    features.cardinality_est     = 1;
    features.range_u             = UINT64_MAX;
    features.min_lb0             = 1;
    features.min_u               = 0;
    features.max_u               = UINT64_MAX;

    TRS_apply_score_guards(
            scores.data(),
            static_cast<int>(scores.size()),
            operationIds.data(),
            &features,
            TRS_numeric_half_width_threshold(8));

    EXPECT_FLOAT_EQ(scores[0], 1.0f);
    EXPECT_FLOAT_EQ(scores[1], 0.5f);
}

TEST(TransformerDecisionTest, FastSub64PathsMatchFullDecisions)
{
    const uint16_t max16 = std::numeric_limits<uint16_t>::max();
    const std::array<std::array<uint16_t, 8>, 4> cases16 = {
        std::array<uint16_t, 8>{ 0, 10, 20, 1000, 500, max16, 17, 17 },
        std::array<uint16_t, 8>{ 0, max16, 0, max16, 0, max16, 0, max16 },
        std::array<uint16_t, 8>{ 256, 512, 768, 1024, 1280, 1536, 1792, 2048 },
        std::array<uint16_t, 8>{ 7, 7, 7, 8, 8, 8, 7, 7 },
    };
    const uint32_t max32 = std::numeric_limits<uint32_t>::max();
    const std::array<std::array<uint32_t, 8>, 4> cases32 = {
        std::array<uint32_t, 8>{
                0, 1, 65535, 65536, uint32_t{ 1 } << 31, max32, 17, 17 },
        std::array<uint32_t, 8>{ 0, max32, 0, max32, 0, max32, 0, max32 },
        std::array<uint32_t, 8>{
                65536, 131072, 196608, 262144, 327680, 393216, 458752, 524288 },
        std::array<uint32_t, 8>{ 7, 7, 7, 8, 8, 8, 7, 7 },
    };

    for (const auto& values : cases16) {
        std::array<uint32_t, 8> scratchValues                             = {};
        std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
        LegacyExtractScratch extractScratch;
        TRS_NumericExtractWorkspace scratch = makeWorkspace(
                scratchValues.data(),
                scratchValues.size(),
                matchTable,
                extractScratch);
        const TRS_GenericNumericDecision full = TRS_generic_numeric_decide16(
                bytes(values), sizeof(values), &scratch);
        const TRS_GenericNumericDecision fast =
                TRS_generic_numeric_decide16_fast(
                        bytes(values), sizeof(values), &scratch);
        expectSameDecision(full, fast, 2);
    }
    for (const auto& values : cases32) {
        std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
        LegacyExtractScratch extractScratch;
        TRS_NumericExtractWorkspace scratch =
                makeWorkspace(nullptr, 0, matchTable, extractScratch);
        const TRS_GenericNumericDecision full = TRS_generic_numeric_decide32(
                bytes(values), sizeof(values), &scratch);
        const TRS_GenericNumericDecision fast =
                TRS_generic_numeric_decide32_fast(
                        bytes(values), sizeof(values));
        expectSameDecision(full, fast, 4);
    }
}

TEST(TransformerDecisionTest, MissingWorkspaceLeavesDecisionInvalid)
{
    const std::array<uint16_t, 4> values = { 1, 2, 3, 4 };
    const TRS_GenericNumericDecision decision =
            TRS_generic_numeric_decide16_fast(
                    bytes(values), sizeof(values), nullptr);

    EXPECT_EQ(decision.core.best_op, TRS_GENERIC_NUMERIC_OP_INVALID);
    EXPECT_EQ(decision.core.best_index, -1);
    EXPECT_EQ(decision.core.score_count, 0);
}

TEST(TransformerDecisionTest, FastNum64PathMatchesFullDecision)
{
    const uint64_t max64 = std::numeric_limits<uint64_t>::max();
    const std::array<std::array<uint64_t, 8>, 4> cases = {
        std::array<uint64_t, 8>{ 0,
                                 1,
                                 uint64_t{ 1 } << 32,
                                 uint64_t{ 1 } << 48,
                                 uint64_t{ 1 } << 63,
                                 max64,
                                 17,
                                 17 },
        std::array<uint64_t, 8>{ 0, max64, 0, max64, 0, max64, 0, max64 },
        std::array<uint64_t, 8>{ 256, 512, 768, 1024, 1280, 1536, 1792, 2048 },
        std::array<uint64_t, 8>{ 7, 7, 7, 8, 8, 8, 7, 7 },
    };

    for (const auto& values : cases) {
        std::array<uint32_t, TRS_NUMERIC_MATCH4_TABLE_ENTRIES> matchTable = {};
        std::array<uint64_t, TRS_NUMERIC_SORTED_GAP_BUFFER_ENTRIES>
                sortedGapBuffer = {};
        TRS_NumericExtractWorkspace scratch =
                makeNum64Workspace(matchTable, sortedGapBuffer);
        expectSameDecision(
                TRS_generic_numeric_decide64_v2(
                        bytes(values), sizeof(values), &scratch),
                TRS_generic_numeric_decide64_v2_fast(
                        bytes(values), sizeof(values), &scratch));
    }
}

TEST(TransformerDecisionTest, FastPathsMatchFullDecisionsForGeneratedInputs)
{
    constexpr std::array<size_t, 9> sizes = {
        7, 8, 255, 256, 257, 511, 512, 513, 4097,
    };
    constexpr std::array<NumericDataShape, 4> shapes = {
        NumericDataShape::UNIFORM,
        NumericDataShape::LOW_CARDINALITY,
        NumericDataShape::STRIDED,
        NumericDataShape::RUNS,
    };

    for (size_t const size : sizes) {
        for (NumericDataShape const shape : shapes) {
            SCOPED_TRACE(
                    ::testing::Message() << "size=" << size << ", shape="
                                         << static_cast<int>(shape));
            uint32_t const seed = 0x51F00D00u ^ static_cast<uint32_t>(size)
                    ^ (static_cast<uint32_t>(shape) << 24);

            DataGen gen16(seed ^ 16);
            expectFastPathMatchesFull(
                    generateNumericValues<uint16_t>(gen16, size, shape));

            DataGen gen32(seed ^ 32);
            expectFastPathMatchesFull(
                    generateNumericValues<uint32_t>(gen32, size, shape));

            DataGen gen64(seed ^ 64);
            expectFastPathMatchesFull(
                    generateNumericValues<uint64_t>(gen64, size, shape));
        }
    }
}

TEST(TransformerDecisionTest, GeneratedOperationIdsAreValidAndUnique)
{
    expectValidOperationIds(
            TRS_score_num8_operation_ids, TRS_SCORE_NUM8_N_OPERATIONS);
    expectValidOperationIds(
            TRS_score_num16_operation_ids, TRS_SCORE_NUM16_N_OPERATIONS);
    expectValidOperationIds(
            TRS_score_num32_operation_ids, TRS_SCORE_NUM32_N_OPERATIONS);
    expectValidOperationIds(
            TRS_score_num64_operation_ids, TRS_SCORE_NUM64_N_OPERATIONS);
}

} // namespace
