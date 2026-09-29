// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_SPARSE_NUM_GRAPH_SPARSE_NUM_H
#define OPENZL_CODECS_SPARSE_NUM_GRAPH_SPARSE_NUM_H

#include "openzl/zl_data.h"

/**
 * Graph definition for the sparse_num transform.
 *
 * Input 0: numeric stream to sparsify
 * Output 0: numeric stream of zero-run distances
 * Output 1: numeric stream of literal values
 */
#define SPARSE_NUM_GRAPH(id)                                               \
    {                                                                      \
        .CTid       = id,                                                  \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric),                  \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric, ZL_Type_numeric), \
    }

#endif
