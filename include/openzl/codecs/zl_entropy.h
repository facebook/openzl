// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_ENTROPY_H
#define ZSTRONG_CODECS_ENTROPY_H

#include "openzl/zl_graphs.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Entropy backend that can use backends that are at least as fast as FSE
// Supports serialized inputs
#define ZL_GRAPH_FSE ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_fse)
// Entropy backend that can use backends that are at least as fast as Huffman
// Supports both serialized & 2-byte struct inputs
#define ZL_GRAPH_HUFFMAN ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_huffman)
// Entropy backend that can use any backend that satisfies the compression &
// decompression speed requirements. Supports both serialized & 2-byte struct
// inputs
#define ZL_GRAPH_ENTROPY ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_entropy)

#if defined(__cplusplus)
}
#endif

#endif
