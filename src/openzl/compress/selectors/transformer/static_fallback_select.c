// Copyright (c) Meta Platforms, Inc. and affiliates.

/*
 * Static fallback selector implementation.
 *
 * Heuristic-based compression selector that decides when tokenize-based
 * fallback pipelines should run before the static core selector.
 */

#include "cardinality.h"
#include "static_selectors.h"

#include <limits.h> /* CHAR_BIT */
#include <stdint.h> /* uintptr_t */
#include <string.h> /* memset */

#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/shared/bits.h"        /* ZL_popcount64 */
#include "openzl/shared/estimate.h"    /* ZL_estimateCardinality_fixed */
#include "openzl/shared/portability.h" /* ZL_FORCE_INLINE */
#include "openzl/zl_data.h"            /* ZL_Type_numeric */
#include "openzl/zl_input.h"           /* ZL_Input_* */

ZL_FORCE_INLINE uint64_t
tokenize_gate_load_unsigned(const void* data, size_t index, size_t elt_w)
{
    if (elt_w == 1)
        return ((const uint8_t*)data)[index];
    if (elt_w == 2)
        return ((const uint16_t*)data)[index];
    if (elt_w == 4)
        return ((const uint32_t*)data)[index];
    return ((const uint64_t*)data)[index];
}

ZL_FORCE_INLINE double
tokenize_gate_mean_abs_delta_fixed(const void* data, size_t n, size_t elt_w)
{
    double sum = 0;
    for (size_t i = 1; i < n; i++) {
        uint64_t const current = tokenize_gate_load_unsigned(data, i, elt_w);
        uint64_t const previous =
                tokenize_gate_load_unsigned(data, i - 1, elt_w);
        sum += (double)(current > previous ? current - previous
                                           : previous - current);
    }
    return sum / (double)(n - 1);
}

/* Compute mean |delta| for a numeric input. Works for any element width.
 * Uses double accumulation to avoid uint64_t overflow on num64 data
 * with large n and large deltas (e.g. n=380K, MAD=2e14 → sum=8e19
 * exceeds UINT64_MAX=1.8e19). Individual deltas fit in uint64_t;
 * casting to double before accumulation preserves sufficient precision
 * (relative error < n * 2^-53, negligible for n < 10M). */
static double
tokenize_gate_mean_abs_delta(const void* ptr, size_t n, size_t elt_w)
{
    if (n < 2)
        return 0;

    switch (elt_w) {
        case 1:
            return tokenize_gate_mean_abs_delta_fixed(ptr, n, 1);
        case 2:
            return tokenize_gate_mean_abs_delta_fixed(ptr, n, 2);
        case 4:
            return tokenize_gate_mean_abs_delta_fixed(ptr, n, 4);
        case 8:
            return tokenize_gate_mean_abs_delta_fixed(ptr, n, 8);
        default:
            return 0;
    }
}

ZL_FORCE_INLINE size_t
tokenize_gate_count_zeros_fixed(const void* data, size_t n, size_t elt_w)
{
    size_t zero_count = 0;
    for (size_t i = 0; i < n; i++)
        zero_count += tokenize_gate_load_unsigned(data, i, elt_w) == 0;
    return zero_count;
}

static size_t
tokenize_gate_count_zeros(const void* data, size_t n, size_t elt_w)
{
    switch (elt_w) {
        case 1:
            return tokenize_gate_count_zeros_fixed(data, n, 1);
        case 2:
            return tokenize_gate_count_zeros_fixed(data, n, 2);
        case 4:
            return tokenize_gate_count_zeros_fixed(data, n, 4);
        case 8:
            return tokenize_gate_count_zeros_fixed(data, n, 8);
        default:
            return 0;
    }
}

typedef enum {
    TokenizeGateU16RawValues,
    TokenizeGateU16WrappingDeltas,
} TokenizeGateU16CardinalityInput;

/* Static gates require exact cardinality, unlike the deliberately estimated
 * bitmap features consumed by the trained model. */
static uint64_t tokenize_gate_count_distinct_u16(
        const uint16_t* data,
        size_t n,
        TokenizeGateU16CardinalityInput input,
        uint64_t bitmap[TRS_CARDINALITY_U16_BITMAP_WORDS])
{
    memset(bitmap, 0, TRS_CARDINALITY_U16_BITMAP_WORDS * sizeof(bitmap[0]));
    size_t const word_bits = sizeof(bitmap[0]) * CHAR_BIT;
    if (input == TokenizeGateU16WrappingDeltas) {
        for (size_t i = 1; i < n; i++) {
            uint16_t const value = (uint16_t)(data[i] - data[i - 1]);
            bitmap[value / word_bits] |= UINT64_C(1) << (value % word_bits);
        }
    } else {
        for (size_t i = 0; i < n; i++) {
            uint16_t const value = data[i];
            bitmap[value / word_bits] |= UINT64_C(1) << (value % word_bits);
        }
    }

    uint64_t cardinality = 0;
    for (size_t i = 0; i < TRS_CARDINALITY_U16_BITMAP_WORDS; i++)
        cardinality += (uint64_t)ZL_popcount64(bitmap[i]);
    return cardinality;
}

typedef struct {
    const void* data;
    size_t n;
    size_t elt_w;
    uint64_t range_min;
    uint64_t range_u;
    uint64_t cardinality_scale;
    uint64_t card_estimate;
    double mean_abs_delta;
    int has_mean_abs_delta;
} TokenizeGateContext;

enum {
    TOKENIZE_GATE_DELTA_HASH_BITS = 12,
    TOKENIZE_GATE_DELTA_HASH_SIZE = 1u << TOKENIZE_GATE_DELTA_HASH_BITS,
    TOKENIZE_GATE_DELTA_HASH_MASK = TOKENIZE_GATE_DELTA_HASH_SIZE - 1,
    TOKENIZE_GATE_DELTA_HASH_CARDINALITY_LIMIT =
            TOKENIZE_GATE_DELTA_HASH_SIZE / 4,
};

typedef struct {
    uint64_t keys[TOKENIZE_GATE_DELTA_HASH_SIZE];
    uint8_t used[TOKENIZE_GATE_DELTA_HASH_SIZE];
} TokenizeGateDeltaHashTable;

enum {
    TOKENIZE_GATE_LOW_CARDINALITY_NUMERATOR                 = 3,
    TOKENIZE_GATE_LOW_CARDINALITY_DENOMINATOR               = 20,
    TOKENIZE_GATE_MAX_DELTA_CARDINALITY_RATIO_NUMERATOR     = 3,
    TOKENIZE_GATE_MAX_DELTA_CARDINALITY_RATIO_DENOMINATOR   = 5,
    TOKENIZE_GATE_MAX_DELTA_OVERRIDE_RATIO_NUMERATOR        = 9,
    TOKENIZE_GATE_MAX_DELTA_OVERRIDE_RATIO_DENOMINATOR      = 10,
    TOKENIZE_GATE_MAX_FLOAT64_CARDINALITY_RATIO_NUMERATOR   = 193,
    TOKENIZE_GATE_MAX_FLOAT64_CARDINALITY_RATIO_DENOMINATOR = 200,
    TOKENIZE_GATE_FLOAT64_SAMPLE_SIZE                       = 200,
};

/* This calibrated boundary uses the threshold-oriented estimate and span-based
 * cardinality scale; it is not a general-purpose density measurement. */
static int tokenize_gate_estimate_is_low_cardinality(
        const TokenizeGateContext* ctx)
{
    return ctx->card_estimate * TOKENIZE_GATE_LOW_CARDINALITY_DENOMINATOR
            < ctx->cardinality_scale * TOKENIZE_GATE_LOW_CARDINALITY_NUMERATOR;
}

/* Several rejection paths feed the num16 rescue, so cache this full-input
 * statistic instead of rescanning when both phases need it. */
static double tokenize_gate_get_mean_abs_delta(TokenizeGateContext* ctx)
{
    if (!ctx->has_mean_abs_delta) {
        ctx->mean_abs_delta =
                tokenize_gate_mean_abs_delta(ctx->data, ctx->n, ctx->elt_w);
        ctx->has_mean_abs_delta = 1;
    }
    return ctx->mean_abs_delta;
}

/* Try delta_int before tokenize when wide, oscillating data has high raw
 * cardinality but a compact delta alphabet. Static core handles the
 * near-monotonic low-delta-cardinality cases. */
static int tokenize_gate_try_delta_override(
        const ZL_Selector* selCtx,
        const TokenizeGateContext* ctx,
        ZL_GraphID* result)
{
    if (tokenize_gate_estimate_is_low_cardinality(ctx) || ctx->elt_w < 4
        || ctx->n < 1000) {
        return 0;
    }

    const uint8_t* raw = ctx->data;
    /* Numeric stream construction guarantees natural element alignment. */
    ZL_ASSERT_EQ((uintptr_t)raw % ctx->elt_w, (uintptr_t)0);
    size_t up = 0, down = 0;
    TokenizeGateDeltaHashTable* const table =
            (TokenizeGateDeltaHashTable*)ZL_Selector_getScratchSpace(
                    selCtx, sizeof(TokenizeGateDeltaHashTable));
    if (table == NULL) {
        return 0;
    }
    memset(table->used, 0, sizeof(table->used));
    uint64_t delta_card = 0;

    for (size_t i = 1;
         i < ctx->n && delta_card < TOKENIZE_GATE_DELTA_HASH_CARDINALITY_LIMIT;
         i++) {
        uint64_t prev_v, curr_v, delta;
        if (ctx->elt_w == 4) {
            const uint32_t* p = (const uint32_t*)(const void*)raw;
            prev_v            = p[i - 1];
            curr_v            = p[i];
            delta             = (uint32_t)(p[i] - p[i - 1]);
        } else {
            const uint64_t* p = (const uint64_t*)(const void*)raw;
            prev_v            = p[i - 1];
            curr_v            = p[i];
            delta             = p[i] - p[i - 1];
        }
        up += (curr_v > prev_v);
        down += (curr_v < prev_v);

        uint32_t h = (uint32_t)((delta * UINT64_C(0x9E3779B97F4A7C15))
                                >> (64 - TOKENIZE_GATE_DELTA_HASH_BITS));
        /* Bound collision work. Probe exhaustion omits the delta from this
         * calibrated estimate; changing that policy requires evaluation. */
        for (uint32_t probe = 0; probe < 32; probe++) {
            uint32_t slot = (h + probe) & TOKENIZE_GATE_DELTA_HASH_MASK;
            if (!table->used[slot]) {
                table->used[slot] = 1;
                table->keys[slot] = delta;
                delta_card++;
                break;
            }
            if (table->keys[slot] == delta)
                break;
        }
    }

    /* Reaching the cap may leave the direction counts incomplete, and both
     * override rules reject saturated delta cardinality. */
    if (delta_card >= TOKENIZE_GATE_DELTA_HASH_CARDINALITY_LIMIT) {
        return 0;
    }

    size_t minority = up < down ? up : down;
    size_t changes  = up + down;
    if (ctx->range_u > 65536 && delta_card * 5 > (uint64_t)ctx->n
        && (uint64_t)minority * 4 > (uint64_t)ctx->n) {
        /* Re-estimate raw cardinality to verify that delta_int reduces it.
         * The early-exit contract only guarantees a result of at least
         * `delta_card + 1`, which is weaker than the 10:9 comparison below.
         * This calibrated rule therefore relies on the current estimator
         * scanning the full input. Changing the limit or enabling true early
         * exit here requires upstream evaluation. */
        ZL_CardinalityEstimate est2 = ZL_estimateCardinality_fixed(
                raw, ctx->n, ctx->elt_w, delta_card + 1);
        uint64_t card_high = est2.estimate;
        if (delta_card * TOKENIZE_GATE_MAX_DELTA_OVERRIDE_RATIO_DENOMINATOR
            < card_high * TOKENIZE_GATE_MAX_DELTA_OVERRIDE_RATIO_NUMERATOR) {
            *result = ZL_GRAPH_TRANSFORMER_STATIC_DELTA_TOK;
            return 1;
        }
    }

    /* For near-monotonic IDs or timestamps, delta_int creates a compact step
     * dictionary and tokenize exploits repeated step indices. Small num32
     * inputs use a tighter dictionary limit because entropy-table overhead is
     * proportionally larger. The final comparison relies on the estimator's
     * current full scan. */
    uint64_t const tokenize_delta_cardinality_limit =
            (ctx->elt_w == 4 && ctx->n < 100000)
            ? 256
            : TOKENIZE_GATE_DELTA_HASH_CARDINALITY_LIMIT;
    if (delta_card > 4 && delta_card < tokenize_delta_cardinality_limit
        && (uint64_t)minority * 50 < (uint64_t)changes
        && (uint64_t)changes * 10 >= (uint64_t)ctx->n * 9
        && delta_card * 2 < ctx->card_estimate) {
        /* On num32, small inputs and moderately broad step dictionaries can
         * make entropy-table overhead exceed its coding gain. Very small or
         * very broad dictionaries keep entropy; num64 always keeps it. */
        *result = (ctx->elt_w == 4
                   && ((ctx->n < 5000 && delta_card >= 20)
                       || (delta_card >= 150 && delta_card < 250)))
                ? ZL_GRAPH_TRANSFORMER_STATIC_DELTA_TOK_MONO_LZ
                : ZL_GRAPH_TRANSFORMER_STATIC_DELTA_TOK_MONO;
        return 1;
    }

    return 0;
}

/* Float64 tokenize: the sorted dictionary has tiny inter-value deltas and the
 * indices narrow from 8 bytes to 2 bytes when cardinality is below 50000.
 * Detect likely IEEE754 data through tightly clustered normal exponents in a
 * 200-element sample, then reject nearly-unique inputs with a higher-limit
 * cardinality estimate. */
static int tokenize_gate_try_float64_override(
        const TokenizeGateContext* ctx,
        ZL_GraphID* result)
{
    if (ctx->elt_w != 8 || ctx->n < 10000
        || tokenize_gate_estimate_is_low_cardinality(ctx)) {
        return 0;
    }

    ZL_ASSERT_EQ((uintptr_t)ctx->data % sizeof(uint64_t), (uintptr_t)0);
    const uint64_t* p64 = (const uint64_t*)(const void*)ctx->data;
    uint16_t min_exp = 2047, max_exp = 0;
    size_t fvalid         = 0;
    size_t const fcheck_n = TOKENIZE_GATE_FLOAT64_SAMPLE_SIZE;
    for (size_t i = 0; i < fcheck_n; i++) {
        uint16_t exp = (uint16_t)((p64[i] >> 52) & 0x7FFu);
        if (exp == 0 || exp == 2047)
            continue;
        if (exp < min_exp)
            min_exp = exp;
        if (exp > max_exp)
            max_exp = exp;
        fvalid++;
    }
    if (fvalid * 4 < fcheck_n * 3) {
        return 0;
    }
    ZL_ASSERT_LE(min_exp, max_exp);
    if (min_exp < 100 || max_exp - min_exp >= 15) {
        return 0;
    }

    /* Require enough repetition for narrowed indices to compress. Calibration
     * put beneficial samples at or below 0.960 and harmful ones at or above
     * 0.970; the 0.965 cutoff preserves that margin. */
    ZL_CardinalityEstimate est_hi =
            ZL_estimateCardinality_fixed(ctx->data, ctx->n, ctx->elt_w, 50001);
    uint64_t card_hi = est_hi.estimate;
    if (card_hi < 50000
        && card_hi * TOKENIZE_GATE_MAX_FLOAT64_CARDINALITY_RATIO_DENOMINATOR
                < (uint64_t)ctx->n
                        * TOKENIZE_GATE_MAX_FLOAT64_CARDINALITY_RATIO_NUMERATOR) {
        *result = ZL_GRAPH_TRANSFORMER_STATIC_TOK_SORTED;
        return 1;
    }
    return 0;
}

/* Return nonzero with `result` set when a tokenize pipeline is selected.
 * Return zero when tokenize is rejected and the caller should try rescue. */
static int tokenize_gate_try_low_cardinality(
        TokenizeGateContext* ctx,
        ZL_GraphID* result)
{
    /* `card_estimate` is below the early-exit limit in this phase, so the
     * absolute cardinality thresholds below can rely on it. */
    if (ctx->elt_w == 2 && ctx->card_estimate >= 1500) {
        return 0;
    }

    /* Sparse data (>50% zeros): static's range_pack already handles well */
    if (ctx->range_min == 0) {
        size_t const zero_count =
                tokenize_gate_count_zeros(ctx->data, ctx->n, ctx->elt_w);

        /* Sparse num16/num32 can still benefit at moderate cardinality because
         * sorted dictionaries compress well and indices narrow to one byte. */
        if ((uint64_t)zero_count * 2 > (uint64_t)ctx->n
            && !((ctx->elt_w == 4 || ctx->elt_w == 2) && ctx->card_estimate > 8
                 && ctx->card_estimate <= 256)) {
            return 0;
        }
    }

    /* Allow a looser locality threshold when num16+ indices narrow to num8.
     * Num8 keeps the stricter default: tokenization can still improve symbol
     * locality even though its indices remain one byte wide. Tiny
     * cardinalities do not amortize tokenize overhead. */
    double mean_ad   = tokenize_gate_get_mean_abs_delta(ctx);
    double delta_thr = (ctx->elt_w >= 2 && ctx->card_estimate >= 50
                        && ctx->card_estimate < 256 && ctx->n >= 10000)
            ? 2.0
            : 0.7;
    if ((double)ctx->card_estimate / (mean_ad + 1.0) >= delta_thr) {
        return 0;
    }

    /* Sparse hash-like num64 data retains byte patterns that field_lz can
     * exploit but tokenize destroys. */
    if (ctx->elt_w == 8 && ctx->card_estimate > 256
        && ctx->n < (size_t)ctx->card_estimate * 25 && mean_ad > 1e14) {
        return 0;
    }

    /* Sorted tokenize disrupts useful byte patterns in sparse, run-heavy
     * num64 data that oscillates in both directions. Monotonic order remains
     * compressible and is intentionally not rejected here. */
    if (ctx->elt_w == 8 && ctx->card_estimate > 256
        && ctx->n < (size_t)ctx->card_estimate * 25) {
        ZL_ASSERT_EQ((uintptr_t)ctx->data % sizeof(uint64_t), (uintptr_t)0);
        const uint64_t* p64 = (const uint64_t*)(const void*)ctx->data;
        size_t check_n      = ctx->n < 1000 ? ctx->n : 1000;
        size_t ups = 0, downs = 0;
        for (size_t i = 1; i < check_n; i++) {
            ups += (p64[i] > p64[i - 1]);
            downs += (p64[i] < p64[i - 1]);
        }
        size_t changes = ups + downs;
        size_t min_dir = ups < downs ? ups : downs;
        if (min_dir * 4 > changes && changes * 7 < check_n) {
            return 0;
        }
    }

    /* For num16, apply delta_int first when it produces a meaningfully smaller
     * dictionary and the resulting delta stream still passes the tokenize
     * density gate. */
    if (ctx->elt_w == 2 && ctx->n >= 1000) {
        uint64_t bitmap[TRS_CARDINALITY_U16_BITMAP_WORDS];
        const uint16_t* p16       = (const uint16_t*)(const void*)ctx->data;
        uint64_t const delta_card = tokenize_gate_count_distinct_u16(
                p16, ctx->n, TokenizeGateU16WrappingDeltas, bitmap);

        if (delta_card * TOKENIZE_GATE_MAX_DELTA_CARDINALITY_RATIO_DENOMINATOR
                    < ctx->card_estimate
                            * TOKENIZE_GATE_MAX_DELTA_CARDINALITY_RATIO_NUMERATOR
            && delta_card * TOKENIZE_GATE_LOW_CARDINALITY_DENOMINATOR
                    < (uint64_t)ctx->n
                            * TOKENIZE_GATE_LOW_CARDINALITY_NUMERATOR) {
            *result = ZL_GRAPH_TRANSFORMER_STATIC_DELTA_TOK;
            return 1;
        }
    }

    /* Without index-width narrowing, highly oscillating num16 data keeps more
     * useful structure in its raw representation. */
    if (ctx->elt_w == 2 && ctx->card_estimate >= 256
        && mean_ad * 4 > (double)ctx->range_u) {
        return 0;
    }

    *result = ZL_GRAPH_TRANSFORMER_STATIC_TOK_SORTED;
    return 1;
}

static ZL_GraphID tokenize_gate_select_num16_rescue(TokenizeGateContext* ctx)
{
    /* Rescue slowly varying num16 streams before static fallback. Exact bitmap
     * counts avoid misses from the low-resolution estimate; large inputs can
     * tolerate a wider delta dictionary because transform overhead is better
     * amortized. Small ranges and near-constant inputs are handled elsewhere.
     */
    if (ctx->elt_w == 2 && ctx->n >= 3000 && ctx->range_u >= 100) {
        uint64_t bitmap[TRS_CARDINALITY_U16_BITMAP_WORDS];
        const uint16_t* p16 = (const uint16_t*)(const void*)ctx->data;
        /* Pass 1: compute delta_card */
        uint64_t const delta_card = tokenize_gate_count_distinct_u16(
                p16, ctx->n, TokenizeGateU16WrappingDeltas, bitmap);
        uint64_t const rescue_delta_cardinality_limit =
                (ctx->n >= 10000) ? 512 : 256;
        if (delta_card < rescue_delta_cardinality_limit) {
            /* Compute exact card because the threshold-oriented estimate can
             * substantially underestimate cardinality in this branch. */
            uint64_t const exact_card = tokenize_gate_count_distinct_u16(
                    p16, ctx->n, TokenizeGateU16RawValues, bitmap);

            /* Tiered dc/card threshold: for n >= 10000, delta_int overhead is
             * well amortized so dc/card < 0.6 is safe; for smaller files the
             * overhead is higher, use the tighter dc/card < 0.5 to avoid
             * borderline ratios around 0.51, where delta_int alone hurts
             * because the delta stream expands to the full 16-bit range. */
            int dc_ok = (ctx->n >= 10000)
                    ? (delta_card
                               * TOKENIZE_GATE_MAX_DELTA_CARDINALITY_RATIO_DENOMINATOR
                       < exact_card
                               * TOKENIZE_GATE_MAX_DELTA_CARDINALITY_RATIO_NUMERATOR)
                    : (delta_card * 2 < exact_card);
            if (dc_ok && exact_card > 30) {
                return ZL_GRAPH_TRANSFORMER_STATIC_DELTA;
            }
        } else if (
                ctx->n >= 10000 && ctx->range_u >= 500 && ctx->range_u < 3000) {
            /* High delta cardinality does not imply large deltas. Smooth
             * streams can still benefit from delta_int even when they fail
             * the cardinality-reduction rule above. */
            double rescue_mad = tokenize_gate_get_mean_abs_delta(ctx);
            if (rescue_mad < 35.0) {
                /* Density check: require card/range > 0.5 to avoid sparse data
                 * where delta_int disrupts byte-level patterns. */
                uint64_t const exact_card = tokenize_gate_count_distinct_u16(
                        p16, ctx->n, TokenizeGateU16RawValues, bitmap);
                if (exact_card * 2 > ctx->range_u) {
                    return ZL_GRAPH_TRANSFORMER_STATIC_DELTA;
                }
            }
        }
    }

    return ZL_GRAPH_TRANSFORMER_STATIC_CORE_SELECTOR;
}

/* Static fallback entry: try specialized high-cardinality overrides, then the
 * low-cardinality tokenize policy, and finally the num16 delta rescue. */
ZL_GraphID SI_transformer_static_fallback_select(
        const ZL_Selector* selCtx,
        const ZL_Input* input,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    (void)customGraphs;
    (void)nbCustomGraphs;

    if (ZL_Input_type(input) != ZL_Type_numeric)
        return ZL_GRAPH_TRANSFORMER_STATIC_CORE_SELECTOR;

    size_t n     = ZL_Input_numElts(input);
    size_t elt_w = ZL_Input_eltWidth(input);
    if (n < 2)
        return ZL_GRAPH_TRANSFORMER_STATIC_CORE_SELECTOR;

    ZL_ElementRange range =
            ZL_computeUnsignedRange(ZL_Input_ptr(input), n, elt_w);
    uint64_t range_u = range.max - range.min;
    /*
     * This calibrated density scale uses the value span, not the inclusive
     * domain size `range_u + 1`. Changing that definition changes decisions
     * near the thresholds and requires upstream evaluation.
     */
    uint64_t cardinality_scale = (uint64_t)n < range_u ? (uint64_t)n : range_u;
    if (cardinality_scale == 0)
        return ZL_GRAPH_TRANSFORMER_STATIC_CORE_SELECTOR;

    /*
     * Threshold-oriented cardinality estimate: values at or above this limit
     * are only useful as evidence that the 15% gate failed. The current
     * implementation scans the full input, but callers must not generally
     * depend on estimates above the documented early-exit limit.
     */
    uint64_t cardinality_limit =
            (uint64_t)((double)cardinality_scale * 0.15) + 1;
    ZL_CardinalityEstimate est = ZL_estimateCardinality_fixed(
            ZL_Input_ptr(input), n, elt_w, cardinality_limit);
    uint64_t card_estimate  = est.estimate;
    TokenizeGateContext ctx = {
        .data               = ZL_Input_ptr(input),
        .n                  = n,
        .elt_w              = elt_w,
        .range_min          = range.min,
        .range_u            = range_u,
        .cardinality_scale  = cardinality_scale,
        .card_estimate      = card_estimate,
        .mean_abs_delta     = 0,
        .has_mean_abs_delta = 0,
    };
    ZL_GraphID result;

    /* Dense small-range num16 data can tokenize despite failing the 15% gate:
     * the sorted dictionary is tiny and indices narrow. This absolute
     * threshold relies on the estimator's current full scan. */
    if (elt_w == 2 && range_u < 300 && range_u >= 100 && card_estimate >= 100
        && n >= 2000) {
        return ZL_GRAPH_TRANSFORMER_STATIC_TOK_SORTED;
    }

    if (tokenize_gate_try_delta_override(selCtx, &ctx, &result)) {
        return result;
    }

    if (tokenize_gate_try_float64_override(&ctx, &result)) {
        return result;
    }

    int const is_low_cardinality =
            tokenize_gate_estimate_is_low_cardinality(&ctx);
    if (!is_low_cardinality) {
        /* Slowly varying num8 signals benefit from repeated delta stages.
         * Require a large range to exclude tokenize index streams that would
         * recurse, and low MAD to reject random or coarsely quantized data. */
        if (elt_w == 1 && n >= 2000 && range_u >= 200) {
            double mean_ad = tokenize_gate_get_mean_abs_delta(&ctx);
            if (mean_ad < 25.0) {
                return ZL_GRAPH_TRANSFORMER_STATIC_DELTA;
            }
        }
    } else if (tokenize_gate_try_low_cardinality(&ctx, &result)) {
        return result;
    }

    return tokenize_gate_select_num16_rescue(&ctx);
}
