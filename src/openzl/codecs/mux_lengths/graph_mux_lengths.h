// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_MUX_LENGTHS_GRAPH_MUX_LENGTHS_H
#define OPENZL_CODECS_MUX_LENGTHS_GRAPH_MUX_LENGTHS_H

/// Graph definition for the mux_lengths transform
/// used by both the encoder and decoder side.
///
/// Input 0: numeric stream of literal lengths
/// Input 1: numeric stream of match lengths
/// Output 0: serial stream of muxed length bytes
/// Output 1: numeric stream of long lengths (overflow, interleaved)

#define MUX_LENGTHS_GRAPH(id)                                              \
    {                                                                      \
        .CTid       = id,                                                  \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric, ZL_Type_numeric), \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_serial, ZL_Type_numeric),  \
    }

#endif
