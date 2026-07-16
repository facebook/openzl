// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_PIVCO_HUFFMAN_DECODE_PIVCO_BINDING_H
#define OPENZL_CODECS_PIVCO_HUFFMAN_DECODE_PIVCO_BINDING_H

#include "openzl/codecs/pivco_huffman/graph_pivco_huffman.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

ZL_Report DI_pivco_huffman(ZL_Decoder* dictx, const ZL_Input* ins[]);

#define DI_PIVCO_HUFFMAN(id)                \
    {                                       \
        .transform_f = DI_pivco_huffman,    \
        .name        = "!zl.pivco_huffman", \
    }

ZL_END_C_DECLS

#endif
