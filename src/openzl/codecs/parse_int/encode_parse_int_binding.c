// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/parse_int/encode_parse_int_binding.h"
#include "openzl/codecs/parse_int/encode_parse_int_kernel.h"
#include "openzl/codecs/zl_parse_int.h"

#include "openzl/common/assertion.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"

ZL_Report EI_parseInt(ZL_Encoder* encoder, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(encoder);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    char const* data      = (char const*)ZL_Input_ptr(ins[0]);
    uint32_t const* sizes = ZL_Input_stringLens(ins[0]);
    size_t const nbElts   = ZL_Input_numElts(ins[0]);
    size_t const eltWidth = 8;
    ZL_Output* numbers =
            ZL_Encoder_createTypedStream(encoder, 0, nbElts, eltWidth);
    ZL_ERR_IF_NULL(numbers, allocation);
    int64_t* const nums = (int64_t*)ZL_Output_ptr(numbers);
    const ZL_RefParam parsedInts =
            ZL_Encoder_getLocalParam(encoder, ZL_PARSE_INT_PREPARSED_PARAMS);
    if (nbElts > 0 && parsedInts.paramRef) {
        ZL_ERR_IF_NE(nbElts * 8, parsedInts.paramSize, nodeParameter_invalid);
        // Copy the prepared list of ints to the output
        memcpy(nums, parsedInts.paramRef, parsedInts.paramSize);
    } else {
        ZL_ERR_IF_NOT(
                ZL_parseInt(nums, data, sizes, nbElts), node_invalid_input);
    }
    ZL_ERR_IF_ERR(ZL_Output_commit(numbers, nbElts));
    return ZL_returnSuccess();
}

ZL_Report parseIntSafeFnGraph(ZL_Graph* graph, ZL_Edge* edges[], size_t nbEdges)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_ASSERT_EQ(nbEdges, 1);
    ZL_ASSERT_NN(edges);
    const size_t parsedIntEdgeIdx  = 0;
    const size_t exceptionsEdgeIdx = 1;

    const ZL_Input* input = ZL_Edge_getData(edges[0]);
    char const* data      = ZL_Input_ptr(input);
    uint32_t const* sizes = ZL_Input_stringLens(input);
    size_t const nbElts   = ZL_Input_numElts(input);

    uint16_t* indices =
            ZL_Graph_getScratchSpace(graph, nbElts * sizeof(uint16_t));
    int64_t* parsedInts = ZL_Graph_getScratchSpace(graph, nbElts * 8);
    int64_t* rPtr       = parsedInts;
    size_t offset       = 0;
    // TODO: Optimize this loop
    for (size_t i = 0; i < nbElts; i++) {
        bool res;
        if (offset < 32) {
            res = ZL_parseInt64_fallback(rPtr, data, data + sizes[i]);
        } else {
            // Requires at least 32 bytes of data to be parsed
            res = ZL_parseInt64Unsafe(rPtr, data, data + sizes[i]);
            offset += sizes[i];
        }
        if (res) {
            rPtr++;
            indices[i] = parsedIntEdgeIdx;
        } else {
            indices[i] = exceptionsEdgeIdx;
        }
        data += sizes[i];
    }
    size_t numParsed        = (size_t)(rPtr - parsedInts);
    ZL_GraphIDList succList = ZL_Graph_getCustomGraphs(graph);
    ZL_ERR_IF_NE(succList.nbGraphIDs, 3, nodeParameter_invalid);
    // Construct local params for parse int node to run with pre-parsed
    ZL_RefParam refParam = {
        .paramId   = ZL_PARSE_INT_PREPARSED_PARAMS,
        .paramRef  = (void*)parsedInts,
        .paramSize = numParsed * 8,
    };
    ZL_LocalParams params = { .refParams = { .refParams   = &refParam,
                                             .nbRefParams = 1 } };
    if (numParsed == nbElts) {
        // Run parse int node with localParam of pre-parsed integers
        ZL_TRY_LET_T(
                ZL_EdgeList,
                so,
                ZL_Edge_runNode_withParams(
                        edges[0], ZL_NODE_PARSE_INT, &params));
        ZL_ASSERT_EQ(so.nbEdges, 1);
        ZL_ERR_IF_ERR(
                ZL_Edge_setDestination(so.edges[0], succList.graphids[0]));
    } else if (numParsed == 0) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(edges[0], succList.graphids[1]));
    } else {
        ZL_TRY_LET_T(
                ZL_EdgeList,
                dispatchedEdges,
                ZL_Edge_runDispatchStringNode(edges[0], 2, indices));
        ZL_ASSERT_EQ(dispatchedEdges.nbEdges, 3);

        // Send dispatch indices to successor
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(
                dispatchedEdges.edges[0], succList.graphids[2]));
        // Run parse int node with localParam of pre-parsed integers
        ZL_TRY_LET_T(
                ZL_EdgeList,
                so,
                ZL_Edge_runNode_withParams(
                        dispatchedEdges.edges[parsedIntEdgeIdx + 1],
                        ZL_NODE_PARSE_INT,
                        &params));
        // Send parsed int edge to first specified successor
        ZL_ERR_IF_ERR(
                ZL_Edge_setDestination(so.edges[0], succList.graphids[0]));
        // Send exceptions to string successor
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(
                dispatchedEdges.edges[exceptionsEdgeIdx + 1],
                succList.graphids[1]));
    }
    return ZL_returnSuccess();
}

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_parameterizeTryParseIntGraph(
        ZL_Compressor* compressor,
        ZL_GraphID numSuccessor,
        ZL_GraphID exceptionSuccessor)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(compressor);
    ZL_GraphID successors[3] = {
        numSuccessor,
        exceptionSuccessor,
        ZL_GRAPH_COMPRESS_GENERIC,
    };
    ZL_GraphParameters const parseIntSafeGraphParams = {
        .customGraphs   = successors,
        .nbCustomGraphs = 3,
    };
    return ZL_Compressor_parameterizeGraph(
            compressor, ZL_GRAPH_TRY_PARSE_INT, &parseIntSafeGraphParams);
}
