// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_PARTITION_GRAPH_PARTITION_H
#define OPENZL_CODECS_PARTITION_GRAPH_PARTITION_H

/// Graph definition for the partition transform
/// used by both the encoder and decoder side.
///
/// Input: 1 numeric stream of unsigned integers (1, 2, 4, or 8 bytes)
/// Output 0: numeric stream of 8-bit bucket IDs
/// Output 1: serial stream of extra bits (bitstream)

#define PARTITION_GRAPH(id)                                               \
    {                                                                     \
        .CTid       = id,                                                 \
        .inputTypes = ZL_STREAMTYPELIST(ZL_Type_numeric),                 \
        .soTypes    = ZL_STREAMTYPELIST(ZL_Type_numeric, ZL_Type_serial), \
    }

#endif
