// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_LZ_ENCODE_FIELD_LZ_LITERALS_SELECTOR_H
#define ZSTRONG_TRANSFORMS_LZ_ENCODE_FIELD_LZ_LITERALS_SELECTOR_H

#include "openzl/compress/private_nodes.h"
#include "openzl/zl_data.h"
#include "openzl/zl_public_nodes.h"
#include "openzl/zl_selector_declare_helper.h"

/**ZL_transposedLiteralStreamSelector::
 * A selector for the coding of transposed literals stream in FieldLZ.
 * This selector is format version aware, and supports all format versions.
 */
ZL_DECLARE_SELECTOR(
        ZL_transposedLiteralStreamSelector,
        ZL_Type_serial,
        SUCCESSOR(deltaHuff, ZL_GRAPH_DELTA_HUFFMAN),
        SUCCESSOR(deltaFlatpack, ZL_GRAPH_DELTA_FLATPACK),
        SUCCESSOR(deltaZstd, ZL_GRAPH_DELTA_ZSTD),
        SUCCESSOR(huffman, ZL_GRAPH_HUFFMAN),
        SUCCESSOR(flatpack, ZL_GRAPH_FLATPACK),
        SUCCESSOR(zstd, ZL_GRAPH_ZSTD),
        SUCCESSOR(bitpack, ZL_GRAPH_BITPACK),
        SUCCESSOR(store, ZL_GRAPH_STORE),
        SUCCESSOR(constantSerial, ZL_GRAPH_CONSTANT_SERIAL))

#endif
