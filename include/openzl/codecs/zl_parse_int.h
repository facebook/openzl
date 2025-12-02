// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_CODECS_PARSE_INT_H
#define ZSTRONG_CODECS_PARSE_INT_H

#include "openzl/zl_errors.h"
#include "openzl/zl_nodes.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Parse Int
// Input: 1 string stream, each string containing an ASCII representation of an
// int64
// Output: 1 numeric stream, integers converted from input strings
#define ZL_NODE_PARSE_INT ZL_MAKE_NODE_ID(ZL_StandardNodeID_parse_int)

/**
 * Try Parse Int
 * Input: 1 string typed input.
 * The dynamic graph parses the string and dispatches it according to whether
 * int parsing can succeed or not. This result in 2 outputs in addition to the
 * dispatch indices output. The dipatch indices output is sent to the generic
 * compressor.
 * Output 1: 1 numeric output which is the int64 value of the ASCII
 * string. This output is sent to the numSuccessor.
 * Output 2: 1 string output which are the strings in the input that are not
 * ASCII representations of an int64. This output is sent to the
 * exceptionSuccessor.
 *
 * Graph Parameters:
 * 3 successors must be passed as parameters to the graph. The first is the
 * successor for numeric outputs, then the successor for exception outputs, and
 * finally the successor for dispatch indices.
 */
#define ZL_GRAPH_TRY_PARSE_INT           \
    (ZL_GraphID)                         \
    {                                    \
        ZL_StandardGraphID_try_parse_int \
    }

/** Returns a parameterized version of the try parse int graph with the required
 * successors of the graph.
 *
 * @param numSuccessor The successor to send strings that successfully parse as
 * integers
 * @param exceptionSucesssor The successor to send strings that fail to parse as
 * integers
 * @return The graphID for the parameterized Try Parse Int graph
 */
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_parameterizeTryParseIntGraph(
        ZL_Compressor* compressor,
        ZL_GraphID numSuccessor,
        ZL_GraphID exceptionSuccessor);

#if defined(__cplusplus)
}
#endif
#endif
