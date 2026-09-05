// Copyright (c) Meta Platforms, Inc. and affiliates.

/*
 * Static tokenize-index selector implementation.
 *
 * Heuristic-based compression selector for tokenize index streams.
 */

#include "static_selectors.h"

#include <stdint.h>

#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_data.h"
#include "openzl/zl_input.h"

enum {
    INDEX_GATE_SMALL_DELTA_SHARE_NUMERATOR   = 4,
    INDEX_GATE_SMALL_DELTA_SHARE_DENOMINATOR = 5,
};

static const double INDEX_GATE_STRONG_LOCALITY_RANGE_TO_MAD = 25.0;
static const double INDEX_GATE_NARROW_RANGE_TO_MAD          = 40.0;

/* Smooth tokenize index streams benefit from delta_int before entropy or LZ.
 * For num8, strong locality or extremely small absolute deltas distinguish
 * them from run-dominated and random-looking streams. */
ZL_GraphID SI_transformer_static_index_select(
        const ZL_Selector* selCtx,
        const ZL_Input* input,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    (void)selCtx;
    (void)customGraphs;
    (void)nbCustomGraphs;

    if (ZL_Input_type(input) != ZL_Type_numeric)
        return ZL_GRAPH_TRANSFORMER_STATIC_CORE_SELECTOR;

    size_t n     = ZL_Input_numElts(input);
    size_t elt_w = ZL_Input_eltWidth(input);

    if (elt_w == 1 && n >= 64) {
        const uint8_t* p = (const uint8_t*)ZL_Input_ptr(input);

        /* Single-pass: compute mad, range, transition count, and
         * small-delta count (deltas <= range/16 for smooth-index check). */
        uint8_t vmin = p[0], vmax = p[0];
        uint64_t mad_sum   = 0;
        size_t transitions = 0;
        for (size_t i = 1; i < n; i++) {
            uint8_t a = p[i], b = p[i - 1];
            mad_sum += a > b ? (uint64_t)(a - b) : (uint64_t)(b - a);
            transitions += (a != b);
            if (a < vmin)
                vmin = a;
            if (a > vmax)
                vmax = a;
        }
        uint64_t range_u = (uint64_t)vmax - (uint64_t)vmin;
        double mad       = (double)mad_sum / (double)(n - 1);

        if (range_u >= 64
            && mad * INDEX_GATE_STRONG_LOCALITY_RANGE_TO_MAD < (double)range_u
            && ((uint64_t)transitions * 2 > (uint64_t)n || mad < 0.5)) {
            return ZL_GRAPH_TRANSFORMER_STATIC_DELTA;
        }

        /* A concentrated small-delta majority catches right-skewed streams
         * missed by mean MAD. Restrict it to large, frequently changing inputs
         * so entropy gain amortizes and zstd keeps simple runs. */
        if (range_u >= 64 && n >= 16384
            && (uint64_t)transitions * 4 > (uint64_t)n) {
            uint64_t const small_thr = range_u / 16;
            size_t small_count       = 0;
            for (size_t i = 1; i < n; i++) {
                uint8_t a = p[i], b = p[i - 1];
                uint8_t d = a > b ? (uint8_t)(a - b) : (uint8_t)(b - a);
                if (d <= small_thr)
                    small_count++;
            }
            if ((uint64_t)small_count * INDEX_GATE_SMALL_DELTA_SHARE_DENOMINATOR
                >= (uint64_t)(n - 1) * INDEX_GATE_SMALL_DELTA_SHARE_NUMERATOR) {
                return ZL_GRAPH_TRANSFORMER_STATIC_DELTA;
            }
        }
    }

    /* Num16 indices need strong temporal locality and frequent changes;
     * narrower ranges require tighter normalized MAD because the raw indices
     * are already compact. */
    if (elt_w == 2 && n >= 10000) {
        /* Numeric stream construction guarantees natural element alignment. */
        ZL_ASSERT_EQ(
                (uintptr_t)ZL_Input_ptr(input) % sizeof(uint16_t),
                (uintptr_t)0);
        const uint16_t* p16 = (const uint16_t*)ZL_Input_ptr(input);
        uint16_t vmin16 = p16[0], vmax16 = p16[0];
        uint64_t mad_sum16   = 0;
        size_t transitions16 = 0;
        for (size_t i = 1; i < n; i++) {
            uint16_t a = p16[i], b = p16[i - 1];
            mad_sum16 += a > b ? (uint64_t)(a - b) : (uint64_t)(b - a);
            transitions16 += (a != b);
            if (a < vmin16)
                vmin16 = a;
            if (a > vmax16)
                vmax16 = a;
        }
        uint64_t range16 = (uint64_t)vmax16 - (uint64_t)vmin16;
        double mad16     = (double)mad_sum16 / (double)(n - 1);

        if (range16 >= 200
            && mad16 * INDEX_GATE_STRONG_LOCALITY_RANGE_TO_MAD < (double)range16
            && (uint64_t)transitions16 * 2 > (uint64_t)n
            && (range16 >= 500
                || mad16 * INDEX_GATE_NARROW_RANGE_TO_MAD < (double)range16)) {
            return ZL_GRAPH_TRANSFORMER_STATIC_DELTA;
        }
    }

    return ZL_GRAPH_TRANSFORMER_STATIC_CORE_SELECTOR;
}
