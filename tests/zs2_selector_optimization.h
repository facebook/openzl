// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_ZS_SELECTOR_OPTIMIZATION_H
#define ZSTRONG_ZS_SELECTOR_OPTIMIZATION_H

#include "openzl/zl_compressor.h"
#include "openzl/zl_selector.h"

#if defined(__cplusplus)
extern "C" {
#endif

// State

void ZL_SelectorOpt_setEnabled(int enabled);

int ZL_SelectorOpt_isEnabled(void);

typedef struct ZL_SelectorOptState_s ZL_SelectorOptState;
struct ZL_SelectorOptState_s {
    ZL_GraphID* possibleGraphs;
    size_t nbPossibleGraphs;
    size_t idx;
    ZL_GraphID selected;
    int done;
};

ZL_SelectorOptState ZL_SelectorOptState_init(void);

void ZL_SelectorOptState_next(
        ZL_SelectorOptState* state,
        const ZL_GraphID* possibleGraphs,
        size_t nbPossibleGraphs);

void ZL_SelectorOptState_destroy(ZL_SelectorOptState* state);

// Shim

ZL_GraphID ZL_selector_opt_shim_generic(
        ZL_SelectorOptState* state,
        ZL_SerialSelectorFn selector,
        const void* src,
        size_t srcSize,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs);

// Results

typedef struct {
    ZL_GraphID graphid;
    size_t srcSize;
    size_t size;
    double durationNs;
} ZL_SelectorOptResult;

typedef struct {
    ZL_SelectorOptResult* results;
    size_t nbResults;
} ZL_SelectorOptResults;

ZL_SelectorOptResults ZL_SelectorOptResults_init(void);

void ZL_SelectorOptResults_addResult(
        ZL_SelectorOptResults* results,
        ZL_SelectorOptResult result);

void ZL_SelectorOptResults_print(const ZL_SelectorOptResults* results);

size_t ZL_SelectorOptResults_lastSize(const ZL_SelectorOptResults* results);

void ZL_SelectorOptResults_destroy(ZL_SelectorOptResults* results);

// Results Aggregation

typedef struct {
    size_t in_size_sum;  // sum of input sizes (should be == to other choices)
    size_t out_size_sum; // sum of output sizes using this choice

    // The total number of bytes saved by using this graph when it's best over
    // the next best. I.e., the value of having this choice assuming perfect
    // selection.
    size_t improvement_sum;

    size_t avail_count; // how many times was this available (should be always?)
    size_t best_count;  // how many times was this best (or tied for best)
    size_t best_exc_count; // how many times was this uniquely best
    size_t sel_count;      // how many times was this selected
    size_t selbest_count;  // how many times was this best & selected

    size_t avail_size;    // for how many bytes was this available (should be
                          // always?)
    size_t best_size;     // for how many bytes was this best (or tied for best)
    size_t best_exc_size; // for how many bytes was this uniquely best
    size_t sel_size;      // for how many bytes was this selected
    size_t selbest_size;  // for how many bytes was this best & selected
} ZL_SelectorOptAggrChoiceResult;

typedef struct {
    // Aggregations for if the selector had always chosen a particular graph.
    ZL_SelectorOptAggrChoiceResult* graph_results;
    size_t nb_graphs;

    // A synthetic aggregation for if the selector had always chosen the best
    // graph.
    ZL_SelectorOptAggrChoiceResult best_result;

    // An aggregation based on the selections actually made by the selector.
    ZL_SelectorOptAggrChoiceResult selected_result;
} ZL_SelectorOptAggrResults;

ZL_SelectorOptAggrChoiceResult ZL_SelectorOptAggrChoiceResult_init(void);

ZL_SelectorOptAggrResults ZL_SelectorOptAggrResults_init(void);

ZL_SelectorOptAggrChoiceResult* ZL_SelectorOptAggrResults_getChoiceResult(
        ZL_SelectorOptAggrResults* aggr,
        ZL_GraphID graphid);

void ZL_SelectorOptAggrResults_addResult(
        ZL_SelectorOptAggrResults* aggr,
        const ZL_SelectorOptResults* result);

void ZL_SelectorOptAggrResults_print(const ZL_SelectorOptAggrResults* aggr);

void ZL_SelectorOptAggrResults_destroy(ZL_SelectorOptAggrResults* aggr);

// Runner

ZL_SelectorOptResults ZL_selector_opt_run(
        ZL_SelectorOptState* state,
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize,
        ZL_GraphFn graph);

ZL_SelectorOptResults ZL_selector_opt_run_cgraph(
        ZL_SelectorOptState* state,
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize,
        ZL_Compressor const* cgraph);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS_SELECTOR_OPTIMIZATION_H
