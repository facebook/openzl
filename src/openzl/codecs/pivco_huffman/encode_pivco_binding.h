// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_PIVCO_HUFFMAN_ENCODE_PIVCO_BINDING_H
#define OPENZL_CODECS_PIVCO_HUFFMAN_ENCODE_PIVCO_BINDING_H

#include "openzl/codecs/pivco_huffman/graph_pivco_huffman.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

ZL_Report
EI_pivco_huffman(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_PIVCO_HUFFMAN(id)                    \
    {                                           \
        .gd          = PIVCO_HUFFMAN_GRAPH(id), \
        .transform_f = EI_pivco_huffman,        \
        .name        = "!zl.pivco_huffman",     \
    }

ZL_END_C_DECLS

#endif
