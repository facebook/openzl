// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/merge_sorted/encode_merge_sorted_binding.h"

#include "openzl/codecs/merge_sorted/encode_merge_sorted_kernel.h"
#include "openzl/codecs/zl_merge_sorted.h"
#include "openzl/common/errors_internal.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_graph_api.h"

/**
 * Fill in @p srcs and @p srcEnds with the begin/end of each sorted run.
 * Even though srcEnds[i] == srcs[i+1], we still need to produce both because
 * that is what the transform kernel expects.
 *
 * @returns the number of sorted runs (<= 64) or an error.
 */
static ZL_Report getSortedRuns(
        ZL_Input const* in,
        uint32_t const* srcs[64],
        uint32_t const* srcEnds[64])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    size_t const kMaxNbSrcs    = 64;
    uint32_t const* ip         = (uint32_t const*)ZL_Input_ptr(in);
    uint32_t const* const iend = ip + ZL_Input_numElts(in);
    if (ip == iend) {
        return ZL_returnValue(0);
    }
    size_t nbSrcs = 0;
    srcs[nbSrcs]  = ip;
    for (++ip; ip != iend; ++ip) {
        if (ip[0] <= ip[-1]) {
            srcEnds[nbSrcs] = ip;
            ++nbSrcs;
            ZL_ERR_IF_GE(nbSrcs, kMaxNbSrcs, node_invalid_input);
            srcs[nbSrcs] = ip;
        }
    }
    srcEnds[nbSrcs] = ip;
    ++nbSrcs;
    assert(nbSrcs <= kMaxNbSrcs);

    return ZL_returnValue(nbSrcs);
}

static ZL_Report writeHeader(
        ZL_Encoder* eictx,
        uint32_t const* srcs[64],
        uint32_t const* srcEnds[64],
        size_t nbSrcs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    uint8_t* header =
            ZL_Encoder_getScratchSpace(eictx, nbSrcs * ZL_VARINT_LENGTH_64);
    uint8_t* hp = header;
    ZL_ERR_IF_NULL(header, allocation);
    for (size_t i = 0; i < nbSrcs; ++i) {
        uint64_t const srcSize = (uint64_t)(srcEnds[i] - srcs[i]);
        hp += ZL_varintEncode(srcSize, hp);
    }
    ZL_Encoder_sendCodecHeader(eictx, header, (size_t)(hp - header));
    return ZL_returnSuccess();
}

ZL_Report EI_mergeSorted(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in  = ins[0];
    size_t const nbElts = ZL_Input_numElts(in);
    ZL_ERR_IF_NE(ZL_Input_eltWidth(in), 4, node_invalid_input);
    uint32_t const* srcs[64];
    uint32_t const* srcEnds[64];
    ZL_TRY_LET(size_t, nbSrcs, getSortedRuns(in, srcs, srcEnds));

    int const bitsetWidthLog = nbSrcs == 0 ? 1 : ZL_nextPow2((nbSrcs + 7) / 8);
    size_t const bitsetWidth = (size_t)1 << bitsetWidthLog;

    ZL_Output* bitset =
            ZL_Encoder_createTypedStream(eictx, 0, nbElts, bitsetWidth);
    ZL_Output* merged = ZL_Encoder_createTypedStream(eictx, 1, nbElts, 4);

    ZL_ERR_IF_NULL(bitset, allocation);
    ZL_ERR_IF_NULL(merged, allocation);

    ZL_ERR_IF_ERR(writeHeader(eictx, srcs, srcEnds, nbSrcs));

    ZL_Report nbUniqueValues = ZL_returnValue(0);
    if (nbSrcs > 0) {
        switch (bitsetWidth) {
            case 1:
                nbUniqueValues = ZL_MergeSorted_merge8x32(
                        (uint8_t*)ZL_Output_ptr(bitset),
                        (uint32_t*)ZL_Output_ptr(merged),
                        srcs,
                        srcEnds,
                        nbSrcs);
                break;
            case 2:
                nbUniqueValues = ZL_MergeSorted_merge16x32(
                        (uint16_t*)ZL_Output_ptr(bitset),
                        (uint32_t*)ZL_Output_ptr(merged),
                        srcs,
                        srcEnds,
                        nbSrcs);
                break;
            case 4:
                nbUniqueValues = ZL_MergeSorted_merge32x32(
                        (uint32_t*)ZL_Output_ptr(bitset),
                        (uint32_t*)ZL_Output_ptr(merged),
                        srcs,
                        srcEnds,
                        nbSrcs);
                break;
            case 8:
                nbUniqueValues = ZL_MergeSorted_merge64x32(
                        (uint64_t*)ZL_Output_ptr(bitset),
                        (uint32_t*)ZL_Output_ptr(merged),
                        srcs,
                        srcEnds,
                        nbSrcs);
                break;
            default:
                ZL_ASSERT(false);
                break;
        }
    }

    ZL_ERR_IF_ERR(nbUniqueValues);

    ZL_ERR_IF_ERR(ZL_Output_commit(bitset, ZL_validResult(nbUniqueValues)));
    ZL_ERR_IF_ERR(ZL_Output_commit(merged, ZL_validResult(nbUniqueValues)));

    return ZL_returnSuccess();
}

/**
 * Function graph that selects between the merge sorted graph and backup graph.
 * Selects mergeSortedGraph if input has <= 64 sorted runs, otherwise
 * backupGraph.
 *
 * Custom graphs are expected in order: [mergeSortedGraph, backupGraph]
 */
ZL_Report
mergeSortedSelectorFnGraph(ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_ASSERT_EQ(nbInputs, 1);
    ZL_Edge* input = inputs[0];

    ZL_GraphIDList const customGraphs = ZL_Graph_getCustomGraphs(graph);
    ZL_ERR_IF_NE(customGraphs.nbGraphIDs, 2, graphParameter_invalid);
    ZL_GraphID const mergeSortedGraph = customGraphs.graphids[0];
    ZL_GraphID const backupGraph      = customGraphs.graphids[1];

    const ZL_Input* in = ZL_Edge_getData(input);
    ZL_ASSERT_NN(in);

    if (ZL_Input_eltWidth(in) != 4) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(input, backupGraph));
        return ZL_returnSuccess();
    }

    size_t const kMaxNbRuns    = 64;
    uint32_t const* ip         = (uint32_t const*)ZL_Input_ptr(in);
    uint32_t const* const iend = ip + ZL_Input_numElts(in);
    size_t nbRuns              = 0;

    if (ip != iend) {
        ++nbRuns;
        for (++ip; ip != iend; ++ip) {
            if (ip[0] <= ip[-1]) {
                ++nbRuns;
                if (nbRuns > kMaxNbRuns) {
                    break;
                }
            }
        }
    }

    if (nbRuns <= kMaxNbRuns) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(input, mergeSortedGraph));
    } else {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(input, backupGraph));
    }

    return ZL_returnSuccess();
}

ZL_GraphID ZL_Compressor_registerMergeSortedGraph(
        ZL_Compressor* cgraph,
        ZL_GraphID bitsetGraph,
        ZL_GraphID mergedGraph,
        ZL_GraphID backupGraph)
{
    ZL_GraphID const mergeSortedGraph =
            ZL_Compressor_registerStaticGraph_fromNode(
                    cgraph,
                    ZL_NODE_MERGE_SORTED,
                    ZL_GRAPHLIST(bitsetGraph, mergedGraph));

    ZL_GraphID const successors[]   = { mergeSortedGraph, backupGraph };
    ZL_GraphParameters const params = {
        .customGraphs   = successors,
        .nbCustomGraphs = 2,
    };

    ZL_RESULT_OF(ZL_GraphID)
    const result = ZL_Compressor_parameterizeGraph(
            cgraph, ZL_GRAPH_MERGE_SORTED, &params);
    if (ZL_RES_isError(result)) {
        return ZL_GRAPH_ILLEGAL;
    }
    return ZL_RES_value(result);
}
