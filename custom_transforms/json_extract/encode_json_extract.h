// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_TRANSFORMS_JSON_EXTRACT_ENCODE_JSON_EXTRACT_H
#define CUSTOM_TRANSFORMS_JSON_EXTRACT_ENCODE_JSON_EXTRACT_H

#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

/**
 * Registers JSON extract custom transform using the given @p transformID.
 *
 * Input: serialized: JSON-like input data. The transform works on any input
 * data, in that it will succeed and round-trip successfully, but likely won't
 * be efficient for input that doesn't contain JSON-like data.
 *
 * Output 0: serialized: JSON "structure" with {strings, ints, floats, true,
 * false, none} replaced with tokens.
 *
 * Output 1: variable_size_field: ASCII integer-like data extracted from the
 * input. Not guaranteed to be valid integers.
 *
 * Output 2: variable_size_field: ASCII float-like data extracted from the
 * input. Not guaranteed to be valid floats.
 *
 * Output 3: variable_size_field: ASCII strings extracted from the input.
 */
ZL_NodeID ZL_Compressor_registerJsonExtract(
        ZL_Compressor* cgraph,
        ZL_IDType transformID);

ZL_END_C_DECLS

#endif
