// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/merge_sorted/encode_merge_sorted_binding.h"

#include "openzl/codecs/merge_sorted/encode_merge_sorted_kernel.h"
#include "openzl/common/errors_internal.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_selector.h"
#include "openzl/zl_selector_declare_helper.h"

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
            ZL_RET_R_IF_GE(node_invalid_input, nbSrcs, kMaxNbSrcs);
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
    uint8_t* header =
            ZL_Encoder_getScratchSpace(eictx, nbSrcs * ZL_VARINT_LENGTH_64);
    uint8_t* hp = header;
    ZL_RET_R_IF_NULL(allocation, header);
    for (size_t i = 0; i < nbSrcs; ++i) {
        uint64_t const srcSize = (uint64_t)(srcEnds[i] - srcs[i]);
        hp += ZL_varintEncode(srcSize, hp);
    }
    ZL_Encoder_sendCodecHeader(eictx, header, (size_t)(hp - header));
    return ZL_returnSuccess();
}

ZL_Report EI_mergeSorted(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in  = ins[0];
    size_t const nbElts = ZL_Input_numElts(in);
    ZL_RET_R_IF_NE(node_invalid_input, ZL_Input_eltWidth(in), 4);
    uint32_t const* srcs[64];
    uint32_t const* srcEnds[64];
    ZL_TRY_LET_R(nbSrcs, getSortedRuns(in, srcs, srcEnds));

    int const bitsetWidthLog = nbSrcs == 0 ? 1 : ZL_nextPow2((nbSrcs + 7) / 8);
    size_t const bitsetWidth = (size_t)1 << bitsetWidthLog;

    ZL_Output* bitset =
            ZL_Encoder_createTypedStream(eictx, 0, nbElts, bitsetWidth);
    ZL_Output* merged = ZL_Encoder_createTypedStream(eictx, 1, nbElts, 4);

    ZL_RET_R_IF_NULL(allocation, bitset);
    ZL_RET_R_IF_NULL(allocation, merged);

    ZL_RET_R_IF_ERR(writeHeader(eictx, srcs, srcEnds, nbSrcs));

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

    ZL_RET_R_IF_ERR(nbUniqueValues);

    ZL_RET_R_IF_ERR(ZL_Output_commit(bitset, ZL_validResult(nbUniqueValues)));
    ZL_RET_R_IF_ERR(ZL_Output_commit(merged, ZL_validResult(nbUniqueValues)));

    return ZL_returnSuccess();
}

ZL_DECLARE_SELECTOR(
        ZL_SelectMergeSorted,
        ZL_Type_numeric,
        SUCCESSOR(mergeSortedGraph),
        SUCCESSOR(backupGraph))

ZL_GraphID ZL_SelectMergeSorted_impl(
        ZL_Selector const* selCtx,
        ZL_Input const* in,
        ZL_SelectMergeSorted_Successors const* successors)
{
    (void)selCtx;
    if (ZL_Input_eltWidth(in) != 4) {
        return successors->backupGraph;
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
        return successors->mergeSortedGraph;
    } else {
        return successors->backupGraph;
    }
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
    return ZL_SelectMergeSorted_declareGraph(
            cgraph,
            ZL_SelectMergeSorted_successors_init(
                    mergeSortedGraph, backupGraph));
}
