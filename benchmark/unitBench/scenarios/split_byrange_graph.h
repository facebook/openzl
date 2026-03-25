// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_BENCHMARK_UNITBENCH_SPLIT_BYRANGE_GRAPH_H
#define ZSTRONG_BENCHMARK_UNITBENCH_SPLIT_BYRANGE_GRAPH_H

#include "openzl/shared/portability.h"
#include "openzl/zl_compressor.h"

ZL_BEGIN_C_DECLS

/* split_byrange -> STORE: measures pure range-detection overhead */
ZL_GraphID splitByRange8_graph(ZL_Compressor* cgraph);
ZL_GraphID splitByRange16_graph(ZL_Compressor* cgraph);
ZL_GraphID splitByRange32_graph(ZL_Compressor* cgraph);
ZL_GraphID splitByRange64_graph(ZL_Compressor* cgraph);

/* split_byrange -> ZSTD: realistic pipeline with compression */
ZL_GraphID splitByRange32_zstd_graph(ZL_Compressor* cgraph);
ZL_GraphID splitByRange64_zstd_graph(ZL_Compressor* cgraph);

/* split_byrange -> tokenize_sorted(delta_int+numeric, numeric) */
ZL_GraphID splitByRange64_tokenSort_graph(ZL_Compressor* cgraph);

/* tokenize_sorted -> split_byrange on indices only */
ZL_GraphID tokenSort64_splitIndices_graph(ZL_Compressor* cgraph);

/* split_byrange -> tokenize(sorted) -> concat alphabets -> delta_int+NUMERIC
 * Function Graph: keeps indices separate (narrow width), merges alphabets */
ZL_GraphID splitByRange64_concatAlpha_tokenSort_graph(ZL_Compressor* cgraph);

ZL_END_C_DECLS

#endif
