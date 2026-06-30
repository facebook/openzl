// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_PIVCO_HUFFMAN_GRAPH_PIVCO_HUFFMAN_H
#define OPENZL_CODECS_PIVCO_HUFFMAN_GRAPH_PIVCO_HUFFMAN_H

#include "openzl/zl_data.h"

#define PIVCO_HUFFMAN_GRAPH(id)                                           \
    {                                                                     \
        .CTid       = id,                                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_serial),                  \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric, ZL_Type_serial), \
    }

#endif
