// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_TRANSFORMS_PARSE_ENCODE_PARSE_H
#define CUSTOM_TRANSFORMS_PARSE_ENCODE_PARSE_H

#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

/**
 * Registers a transform that parses ASCII integers into int64.
 *
 * Input: variable_size_field: ASCII integer data. The transform works for any
 * inputs, but it only makes sense for inputs that are mostly ASCII integers.
 *
 * Output 0: numeric: int64's parsed from the input that round trip losslessly.
 *
 * Output 1: numeric: Indices of fields in the input that don't losslessly parse
 * into int64's.
 *
 * Output 2: variable_size_field: The fields that don't losslessly parse into
 * int64's.
 */
extern "C" ZL_NodeID ZL_Compressor_registerParseInt64(
        ZL_Compressor* cgraph,
        ZL_IDType transformID);

/**
 * Registers a transform that parses ASCII floating point into double.
 *
 * Input: variable_size_field: ASCII floating point data. The transform works
 * for any inputs, but it only makes sense for inputs that are mostly ASCII
 * floats.
 *
 * Output 0: numeric: doubles's parsed from the input that round trip
 * losslessly.
 *
 * Output 1: numeric: Indices of fields in the input that don't losslessly parse
 * into double's.
 *
 * Output 2: variable_size_field: The fields that don't losslessly parse into
 * double's.
 *
 * WARNING: This transform is not ready for production, and its signature will
 * likely change. We currently only support the format that
 * folly::to<std::string>(double) produces. In order to be more resiliant and
 * work with other float encodings, we will likely need some more output streams
 * to encode serialization parameters.
 */
extern "C" ZL_NodeID ZL_Compressor_registerParseFloat64(
        ZL_Compressor* cgraph,
        ZL_IDType transformID);

ZL_END_C_DECLS

#endif
