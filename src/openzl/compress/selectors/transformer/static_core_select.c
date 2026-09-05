// Copyright (c) Meta Platforms, Inc. and affiliates.

/*
 * Static Selector Implementation
 *
 * Heuristic-based compression selector that uses data properties
 * (delta transitions, cardinality, value range) to pick the best
 * compression graph.  Independent of the NN-based transformer selector.
 */

#include "static_selectors.h"

#include <math.h> /* log2 */

#include "openzl/codecs/divide_by/common_gcd.h" /* ZL_gcdVec */
#include "openzl/codecs/zl_constant.h"          /* ZL_GRAPH_CONSTANT */
#include "openzl/codecs/zl_entropy.h"           /* ZL_GRAPH_FSE */
#include "openzl/codecs/zl_field_lz.h"          /* ZL_GRAPH_FIELD_LZ */
#include "openzl/codecs/zl_zstd.h"              /* ZL_GRAPH_ZSTD */
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/compress/selectors/transformer/numeric_features.h"
#include "openzl/compress/selectors/transformer/wide_arith.h"
#include "openzl/shared/portability.h" /* ZL_FORCE_INLINE */
#include "openzl/zl_data.h"            /* ZL_Type_numeric */
#include "openzl/zl_input.h"           /* ZL_Input_* */

/* Maximum unique delta values to track (beyond this we stop caring) */
#define STATIC_DCARD_LIMIT 4

enum {
    FSE_GATE_MAX_CARDINALITY = 16,
    FSE_GATE_TRANSITION_COUNT =
            FSE_GATE_MAX_CARDINALITY * FSE_GATE_MAX_CARDINALITY,
    FSE_GATE_DOMINANT_SHARE_NUMERATOR         = 4,
    FSE_GATE_DOMINANT_SHARE_DENOMINATOR       = 5,
    SMOOTH_DATA_SMALL_DELTA_SHARE_NUMERATOR   = 97,
    SMOOTH_DATA_SMALL_DELTA_SHARE_DENOMINATOR = 100,
};

static const double FSE_GATE_MIN_PAIR_TO_SYMBOL_ENTROPY_RATIO = 1.95;

typedef struct {
    size_t up;
    size_t down;
    size_t delta_card;
    uint64_t min;
    uint64_t max;
    int64_t signed_min;
    int64_t signed_max;
} StaticAnalysis;

ZL_FORCE_INLINE uint64_t
static_load_unsigned(const void* data, size_t index, size_t elt_w)
{
    if (elt_w == 1)
        return ((const uint8_t*)data)[index];
    if (elt_w == 2)
        return ((const uint16_t*)data)[index];
    if (elt_w == 4)
        return ((const uint32_t*)data)[index];
    return ((const uint64_t*)data)[index];
}

ZL_FORCE_INLINE int64_t static_interpret_signed(uint64_t value, size_t elt_w)
{
    if (elt_w == 1)
        return (int8_t)value;
    if (elt_w == 2)
        return (int16_t)value;
    if (elt_w == 4)
        return (int32_t)value;
    return (int64_t)value;
}

ZL_FORCE_INLINE uint64_t
static_compute_delta(uint64_t current, uint64_t previous, size_t elt_w)
{
    /* Preserve the original typed subtraction: num8/num16 promote to int,
     * while num32/num64 wrap at their unsigned element width. */
    if (elt_w == 1)
        return (uint64_t)((uint8_t)current - (uint8_t)previous);
    if (elt_w == 2)
        return (uint64_t)((uint16_t)current - (uint16_t)previous);
    if (elt_w == 4)
        return (uint64_t)((uint32_t)current - (uint32_t)previous);
    return current - previous;
}

/* Count upward/downward transitions, unique delta values, min/max,
 * and signed min/max (two's complement interpretation).
 * delta_card is capped at STATIC_DCARD_LIMIT+1 (meaning "> limit"). */
ZL_FORCE_INLINE StaticAnalysis
static_analyze_fixed(const void* data, size_t n, size_t elt_w)
{
    StaticAnalysis analysis = { 0 };

    if (n < 2)
        return analysis;

    /* Small array of unique delta values seen so far */
    uint64_t uniq[STATIC_DCARD_LIMIT];
    size_t n_uniq = 0;
    analysis.min = analysis.max = static_load_unsigned(data, 0, elt_w);
    analysis.signed_min         = analysis.signed_max =
            static_interpret_signed(analysis.min, elt_w);

    for (size_t i = 1; i < n; i++) {
        uint64_t const previous      = static_load_unsigned(data, i - 1, elt_w);
        uint64_t const current       = static_load_unsigned(data, i, elt_w);
        int64_t const signed_current = static_interpret_signed(current, elt_w);
        if (current < analysis.min)
            analysis.min = current;
        if (current > analysis.max)
            analysis.max = current;
        if (signed_current < analysis.signed_min)
            analysis.signed_min = signed_current;
        if (signed_current > analysis.signed_max)
            analysis.signed_max = signed_current;
        if (current > previous)
            analysis.up++;
        else if (current < previous)
            analysis.down++;
        if (n_uniq <= STATIC_DCARD_LIMIT) {
            uint64_t const delta =
                    static_compute_delta(current, previous, elt_w);
            size_t j;
            for (j = 0; j < n_uniq; j++) {
                if (uniq[j] == delta)
                    break;
            }
            if (j == n_uniq) {
                if (n_uniq < STATIC_DCARD_LIMIT)
                    uniq[n_uniq] = delta;
                n_uniq++;
            }
        }
    }

    analysis.delta_card = n_uniq;
    return analysis;
}

static StaticAnalysis static_analyze(const ZL_Input* input)
{
    size_t const n        = ZL_Input_numElts(input);
    size_t const elt_w    = ZL_Input_eltWidth(input);
    const void* const ptr = ZL_Input_ptr(input);

    switch (elt_w) {
        case 1:
            return static_analyze_fixed(ptr, n, 1);
        case 2:
            return static_analyze_fixed(ptr, n, 2);
        case 4:
            return static_analyze_fixed(ptr, n, 4);
        case 8:
            return static_analyze_fixed(ptr, n, 8);
        default:
            return (StaticAnalysis){ 0 };
    }
}

/* High conditional-to-symbol entropy indicates little sequential structure,
 * favoring FSE over an LZ-based graph for low-cardinality num8 data. */
static int fse_gate_check(const uint8_t* data, size_t n)
{
    if (n < 2)
        return 0;

    /* Pass 1: histogram → cardinality */
    size_t hist[256] = { 0 };
    for (size_t i = 0; i < n; i++)
        hist[data[i]]++;

    /* Count distinct symbols, compute value range, track max frequency,
     * and build a compact symbol map */
    unsigned card    = 0;
    unsigned val_min = 255, val_max = 0;
    size_t max_freq = 0;
    uint8_t sym_to_idx[256];
    for (unsigned s = 0; s < 256; s++) {
        if (hist[s] > 0) {
            sym_to_idx[s] = (uint8_t)card;
            card++;
            if (s < val_min)
                val_min = s;
            if (s > val_max)
                val_max = s;
            if (hist[s] > max_freq)
                max_freq = hist[s];
        }
    }
    if (card < 2)
        return 0;
    /* Above cardinality 8, require a dense value domain; sparse spacing leaves
     * byte-level structure that LZ exploits better. */
    if (card > FSE_GATE_MAX_CARDINALITY)
        return 0;
    if (card > 8 && card * 2 <= (val_max - val_min + 1))
        return 0;

    /* Compute H0 from histogram */
    double h0 = 0.0;
    for (unsigned s = 0; s < 256; s++) {
        if (hist[s] > 0) {
            double p = (double)hist[s] / (double)n;
            h0 -= p * log2(p);
        }
    }
    if (h0 < 0.001)
        return 0;

    /* Pass 2: trans[previous * card + current] transition counts. */
    ZL_ASSERT_LE(card, FSE_GATE_MAX_CARDINALITY);
    size_t trans[FSE_GATE_TRANSITION_COUNT] = { 0 };
    for (size_t i = 1; i < n; i++)
        trans[sym_to_idx[data[i - 1]] * card + sym_to_idx[data[i]]]++;

    /* Compute H(pair) from transition counts */
    double n_pairs = (double)(n - 1);
    double h_pair  = 0.0;
    for (unsigned j = 0; j < card * card; j++) {
        if (trans[j] > 0) {
            double p = (double)trans[j] / n_pairs;
            h_pair -= p * log2(p);
        }
    }

    /* H1 = H(pair) - H0;  check H1/H0 > 0.95  ↔  H(pair) > 1.95 * H0 */
    if (h_pair <= FSE_GATE_MIN_PAIR_TO_SYMBOL_ENTROPY_RATIO * h0)
        return 0;

    /* Moderate runs (mean run length roughly 10-17) are an empirical dead zone
     * where LZ beats FSE. Keep cardinalities 7-8 eligible because their skew
     * can still favor FSE. */
    if (card <= 8 && n >= 1000) {
        size_t off_diag = 0;
        for (unsigned i = 0; i < card; i++)
            for (unsigned j = 0; j < card; j++)
                if (i != j)
                    off_diag += trans[i * card + j];
        size_t np = n - 1;
        if (card <= 6 && (uint64_t)off_diag * 10 <= (uint64_t)np
            && (uint64_t)off_diag * 17 > (uint64_t)np)
            return 0;
        /* A dominant symbol with frequent sparse events forms repeated motifs
         * that LZ captures better than FSE. The range check also excludes
         * dense tokenize index streams. */
        if (n >= 5000 && (uint64_t)off_diag * 5 > (uint64_t)np
            && (val_max - val_min) > 100
            && (uint64_t)max_freq * FSE_GATE_DOMINANT_SHARE_DENOMINATOR
                    >= (uint64_t)n * FSE_GATE_DOMINANT_SHARE_NUMERATOR)
            return 0;
    }

    return 1;
}

/* Detect temporal smoothness that makes delta_int effective after range_pack.
 */
ZL_FORCE_INLINE int smooth_data_check_fixed(
        const void* data,
        size_t n,
        size_t elt_w,
        uint64_t range_u)
{
    uint64_t thr = range_u / 4;
    if (thr < 3)
        thr = 3;
    size_t small_count = 0;
    for (size_t i = 1; i < n; i++) {
        uint64_t const prev = static_load_unsigned(data, i - 1, elt_w);
        uint64_t const cur  = static_load_unsigned(data, i, elt_w);
        uint64_t d          = cur > prev ? cur - prev : prev - cur;
        if (d <= thr)
            small_count++;
    }
    return (uint64_t)small_count * SMOOTH_DATA_SMALL_DELTA_SHARE_DENOMINATOR
            >= (uint64_t)(n - 1) * SMOOTH_DATA_SMALL_DELTA_SHARE_NUMERATOR;
}

static int
smooth_data_check(const void* data, size_t n, size_t elt_w, uint64_t range_u)
{
    if (n < 1000 || range_u < 3)
        return 0;

    switch (elt_w) {
        case 2:
            return smooth_data_check_fixed(data, n, 2, range_u);
        case 4:
            return smooth_data_check_fixed(data, n, 4, range_u);
        case 8:
            return smooth_data_check_fixed(data, n, 8, range_u);
        default:
            return 0;
    }
}

static ZL_GraphID static_terminal_graph(size_t elt_w)
{
    return elt_w == 1 ? ZL_GRAPH_ZSTD : ZL_GRAPH_FIELD_LZ;
}

/* Ordered policy: trivial and depth exits, delta_int, zigzag, divide_by_gcd,
 * range_pack, the num8 FSE gate, then the width-specific terminal graph.
 * Transform order matters because several choices re-enter this selector. */
ZL_GraphID SI_transformer_static_core_select(
        const ZL_Selector* selCtx,
        const ZL_Input* input,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    (void)customGraphs;
    (void)nbCustomGraphs;

    /* The registry declares this selector numeric-only, which the engine
     * validates before invocation. */
    ZL_ASSERT_EQ(ZL_Input_type(input), ZL_Type_numeric);
    size_t const elt_w = ZL_Input_eltWidth(input);
    ZL_ASSERT(
            elt_w == 1 || elt_w == 2 || elt_w == 4 || elt_w == 8,
            "numeric element width must be 1, 2, 4, or 8");

    StaticAnalysis const analysis = static_analyze(input);
    size_t const up               = analysis.up;
    size_t const down             = analysis.down;
    size_t const dcard            = analysis.delta_card;
    uint64_t const vmin           = analysis.min;
    uint64_t const vmax           = analysis.max;
    int64_t const smin            = analysis.signed_min;
    int64_t const smax            = analysis.signed_max;

    size_t const n = ZL_Input_numElts(input);
    uint64_t const half_width_threshold =
            TRS_numeric_half_width_threshold(elt_w);
    int const formatVersion =
            ZL_Selector_getCParam(selCtx, ZL_CParam_formatVersion);
    if (n == 0) {
        return ZL_GRAPH_STORE;
    }

    if (up == 0 && down == 0) {
        return formatVersion >= 11 ? ZL_GRAPH_CONSTANT : ZL_GRAPH_STORE;
    }
    if (ZL_Selector_getGraphDepth(selCtx) > TRS_TRANSFORMER_MAX_STATIC_DEPTH) {
        return static_terminal_graph(elt_w);
    }

    /* --- delta_int --- */
    {
        size_t changes = up + down;

        if (up == 0 || down == 0) {
            uint64_t range_u = vmax - vmin;
            if (dcard <= 3 && (uint64_t)n * 2 < (uint64_t)(changes + 1) * 3) {
                return ZL_GRAPH_TRANSFORMER_STATIC_DELTA;
            }
            if (dcard <= 2 && range_u <= (uint64_t)n && elt_w >= 2
                && changes >= 16) {
                return ZL_GRAPH_TRANSFORMER_STATIC_DELTA;
            }
        } else {
            size_t minority = up < down ? up : down;
            size_t majority = up > down ? up : down;
            /* Of the n - 1 transitions, everything outside `majority` is
             * either a repeated value or a minority-direction transition.
             * Allow roughly one such exception per 256 elements; 200 is the
             * calibrated small-input rounding offset. */
            size_t const non_dominant_transitions = (n - 1) - majority;
            size_t const non_dominant_limit       = (n + 200) / 256;
            if ((uint64_t)minority * 1000 < (uint64_t)changes
                && non_dominant_transitions < non_dominant_limit) {
                if (vmax >= half_width_threshold) {
                    return ZL_GRAPH_TRANSFORMER_STATIC_DELTA;
                }
            }
            /* Rare num8 wrap resets become sparse nonzero deltas. Require
             * enough, but not too many, transitions for the transform to pay.
             */
            if (elt_w == 1 && (uint64_t)minority * 20 < (uint64_t)changes
                && (uint64_t)changes * 4 < (uint64_t)n && changes >= 100) {
                return ZL_GRAPH_TRANSFORMER_STATIC_DELTA;
            }
        }
    }

    /* --- zigzag --- */
    if (elt_w >= 2 && smin < 0 && smax >= 0) {
        uint64_t const negative_extent = (uint64_t)0 - (uint64_t)smin;
        uint64_t range_s = TRS_wide_i64_abs_diff_to_u64(smax, smin);
        /* Require negative extent >= 25% of signed range: ensures data
         * meaningfully crosses zero, not just isolated negative outliers. */
        if (range_s < half_width_threshold && negative_extent * 4 >= range_s) {
            return ZL_GRAPH_TRANSFORMER_STATIC_ZIGZAG;
        }
    }

    /* --- divide_by_gcd ---
     * Large common factors can expose range packing. Power-of-two factors
     * often reflect float mantissa alignment and can destroy useful LZ
     * patterns, so accept them only when narrowing is decisive. */
    if (formatVersion >= 16 && elt_w >= 4) {
        uint64_t range_u = vmax - vmin;
        if (range_u >= 512) {
            uint64_t g = ZL_gcdVec(ZL_Input_ptr(input), n, elt_w);
            /* A nonzero range proves the input is neither empty nor all zero,
             * so ZL_gcdVec() must return a nonzero divisor. */
            ZL_ASSERT_NE(g, 0);
            /* Power-of-two factors require enough data and a 16-bit
             * post-division range to outweigh the lost byte patterns. */
            if (g >= 256) {
                int const gcd_ok = (g & (g - 1)) != 0
                        || (n >= 2000 && range_u / g < 65536);
                if (gcd_ok) {
                    return ZL_GRAPH_TRANSFORMER_STATIC_GCD;
                }
            }
        }
    }

    /* --- range_pack --- */
    {
        uint64_t range_u = vmax - vmin;
        if (elt_w >= 2 && range_u < 100) {
            /* Smooth active or nearly monotonic streams benefit from a second
             * delta stage; sparse bidirectional jumps do not. */
            size_t changes    = up + down;
            int mostly_active = (uint64_t)changes * 2 > (uint64_t)n;
            size_t minority   = up < down ? up : down;
            int mostly_mono =
                    (uint64_t)minority * 10 < (uint64_t)(up > down ? up : down);
            if (range_u >= 8 && (mostly_active || (mostly_mono && changes >= 4))
                && smooth_data_check(ZL_Input_ptr(input), n, elt_w, range_u)) {
                return ZL_GRAPH_TRANSFORMER_STATIC_RANGE_PACK_DELTA;
            } else {
                return ZL_GRAPH_TRANSFORMER_STATIC_RANGE_PACK;
            }
        }
        if (elt_w >= 4 && range_u < half_width_threshold
            && vmax >= half_width_threshold) {
            return ZL_GRAPH_TRANSFORMER_STATIC_RANGE_PACK;
        }
    }

    /* --- num8 gates --- */
    if (elt_w == 1) {
        if (fse_gate_check((const uint8_t*)ZL_Input_ptr(input), n)) {
            return ZL_GRAPH_FSE;
        }
    }
    return static_terminal_graph(elt_w);
}
