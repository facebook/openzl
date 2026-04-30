// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_SENTINEL_GRAPH_SENTINEL_H
#define OPENZL_CODECS_SENTINEL_GRAPH_SENTINEL_H

/// Graph definition for the sentinel transform
/// used by both the encoder and decoder side.
///
/// Input: 1 numeric stream (any width)
/// Output 0: numeric stream of values (possibly narrower width)
/// Output 1: numeric stream of exceptions (same width as input)

#define SENTINEL_GRAPH(id)                                                 \
    {                                                                      \
        .CTid       = id,                                                  \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric),                  \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric, ZL_Type_numeric), \
    }

#endif
