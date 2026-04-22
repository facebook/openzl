// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_ZL_MUX_LENGTHS_H
#define OPENZL_CODECS_ZL_MUX_LENGTHS_H

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Multiplexes literal lengths and match lengths into a single byte stream
 * with overflow streams for values that don't fit inline.
 *
 * Each (literal_length, match_length) pair is packed into a single byte:
 *   - Low `split_point` bits: inline literal length
 *   - Upper `8 - split_point` bits: inline match length (after subtracting
 *     match_length_bias)
 *
 * The `split_point` is set via ZL_MUX_LENGTHS_SPLIT_POINT_PID and if unset the
 * optimal `split_point` is computed to minimize overflow lengths.
 *
 * The `match_length_bias` is set via ZL_MUX_LENGTHS_MATCH_LENGTH_BIAS_PID.
 * If unset is read from the ZL_LZ_MIN_MATCH_LENGTH_PID of the match lengths
 * input. If both are unset it defaults to zero.
 *
 * Values exceeding the inline capacity are stored in separate overflow streams.
 *
 * Input 0: Numeric literal lengths
 * Input 1: Numeric match lengths
 * Output 0: Serial muxed byte stream
 * Output 1: Numeric overflow lengths (interleaved literal and match)
 */
#define ZL_NODE_MUX_LENGTHS ZL_MAKE_NODE_ID(ZL_StandardNodeID_mux_lengths)

/// Int param: number of bits assigned to literal lengths in each muxed byte.
/// Default: computed to minimize overflow lengths.
/// Valid range: [0, 8].
#define ZL_MUX_LENGTHS_SPLIT_POINT_PID 0

/// Int param: match length bias subtracted before muxing.
/// Default: read from the ZL_LZ_MIN_MATCH_LENGTH_PID of the match lengths
///          input or zero if unset.
/// Valid range: [0, 15].
#define ZL_MUX_LENGTHS_MATCH_LENGTH_BIAS_PID 1

#if defined(__cplusplus)
}
#endif

#endif
