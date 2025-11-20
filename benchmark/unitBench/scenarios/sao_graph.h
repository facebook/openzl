// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TESTS_SAOGRAPH_H
#define ZSTRONG_TESTS_SAOGRAPH_H

/* ===   Dependencies   === */

#include "openzl/shared/portability.h"
#include "openzl/zl_compressor.h"

ZL_BEGIN_C_DECLS

/* ==================================================
 * Graph for SAO
 * ==================================================
 */

// Goal of this graph :
// stronger compression ratio than cmix on sao (3726989)
// at fastest compression speed possible
ZL_GraphID sao_graph_v1(ZL_Compressor* cgraph);

ZL_END_C_DECLS

#endif
