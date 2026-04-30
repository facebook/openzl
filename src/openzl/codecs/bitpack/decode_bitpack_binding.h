// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITPACK_DECODE_BITPACK_BINDING_H
#define ZSTRONG_TRANSFORMS_BITPACK_DECODE_BITPACK_BINDING_H

#include "openzl/codecs/bitpack/graph_bitpack.h" // *BITPACK_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder

ZL_BEGIN_C_DECLS

/// Parsed bitpack header fields.
typedef struct {
    size_t eltWidth; // Output element width in bytes (1, 2, 4, or 8)
    size_t nbBits;   // Number of bits per packed element (1..64)
    size_t numElts;  // Number of output elements
} ZL_BitpackHeader;

/// Parse a bitpack header and compute the number of output elements.
///
/// @param[out] parsed   Receives the parsed header fields.
/// @param headerData    Pointer to the raw header bytes (1 or 2 bytes).
/// @param headerSize    Size of the header in bytes.
/// @param packedSize    Size of the packed data stream in bytes.
ZL_Report ZL_BitpackHeader_parse(
        ZL_BitpackHeader* parsed,
        const void* headerData,
        size_t headerSize,
        size_t packedSize);

/* new methods, based on typedTransform */
ZL_Report DI_bitpack_numeric(ZL_Decoder* dictx, const ZL_Input* in[]);
ZL_Report DI_bitpack_serialized(ZL_Decoder* dictx, const ZL_Input* in[]);

// Following ZL_TypedEncoderDesc declaration,
// presumed to be used as initializer only
#define DI_BITPACK_INTEGER(id) \
    { .transform_f = DI_bitpack_numeric, .name = "bitpack" }
#define DI_BITPACK_SERIALIZED(id) \
    { .transform_f = DI_bitpack_serialized, .name = "bitpack" }

ZL_END_C_DECLS

#endif
