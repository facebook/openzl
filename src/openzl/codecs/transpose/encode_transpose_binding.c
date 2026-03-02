// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/transpose/encode_transpose_binding.h"
#include "openzl/codecs/transpose/encode_transpose_kernel.h" // ZS_transposeEncode
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_data.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_selector_declare_helper.h"

// EI_transpose design notes:
// - Accepts a single stream of type ZL_Type_struct
// - Generates a single stream of type ZL_Type_struct of same size as input
// - An N x W input stream becomes a W x N output stream
ZL_Report EI_transpose(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_struct);
    size_t const fieldWidth = ZL_Input_eltWidth(in);
    ZL_ASSERT_GT(fieldWidth, 0);
    size_t const nbFields = ZL_Input_numElts(in);
    size_t const newFieldWidth =
            nbFields ? nbFields : fieldWidth; // FieldWidth not allowed to be 0
    size_t const newNbFields = nbFields ? fieldWidth : 0;
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eictx, 0, newNbFields, newFieldWidth);
    ZL_ERR_IF_NULL(out, allocation);
    const void* const src = ZL_Input_ptr(in);
    void* const dst       = ZL_Output_ptr(out);
    // TODO(@Cyan) : optimize with a reference when newFieldWidth==1, or
    // nbFields<=1
    ZS_transposeEncode(dst, src, nbFields, fieldWidth);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, newNbFields));
    return ZL_returnValue(1);
}

ZL_Report
EI_transpose_split(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT(ZL_Input_type(in) == ZL_Type_struct);

    const uint8_t* const src = ZL_Input_ptr(in);
    size_t const nbElts      = ZL_Input_numElts(in);
    size_t const eltWidth    = ZL_Input_eltWidth(in);
    ZL_ASSERT_NN(src);
    ZL_ASSERT_GE(eltWidth, 1);
    size_t const nbOutStreams = eltWidth;
    size_t const dstNbElts    = nbElts;

    uint8_t** const outPtrs =
            ZL_Encoder_getScratchSpace(eictx, nbOutStreams * sizeof(uint8_t*));
    ZL_ERR_IF_NULL(outPtrs, allocation);
    for (size_t i = 0; i < nbOutStreams; i++) {
        ZL_Output* const out =
                ZL_Encoder_createTypedStream(eictx, 0, dstNbElts, 1);
        ZL_ERR_IF_NULL(
                out,
                allocation,
                "allocation error in transposeVO while trying to create output stream %zu of size %zu",
                i,
                dstNbElts);

        outPtrs[i] = (uint8_t*)ZL_Output_ptr(out);
        ZL_ERR_IF_ERR(ZL_Output_commit(out, dstNbElts));
    }

    ZS_splitTransposeEncode(outPtrs, src, nbElts, eltWidth);
    return ZL_returnSuccess();
}

/* =======================================================
 * Legacy Transpose transforms operating on serial streams
 * using the typedTransform model
 * =======================================================
 */

// ZL_TypedEncoderFn
static ZL_Report EI_transpose_serial_typed(
        ZL_Encoder* eictx,
        const ZL_Input* in,
        size_t eltWidth)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_ASSERT_EQ(ZL_Input_eltWidth(in), 1);
    size_t const srcSize = ZL_Input_numElts(in);
    ZL_ERR_IF_NE(
            srcSize % eltWidth,
            0,
            GENERIC,
            "source size is not a multiple of transpose width");
    size_t const dstCapacity = srcSize;
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eictx, 0, dstCapacity, 1);
    ZL_ERR_IF_NULL(out, allocation);
    // Note : we should also check alignment here,
    // but since this interface will disappear in the near future,
    // this is a disappearing concern too
    ZS_transposeEncode(
            ZL_Output_ptr(out), ZL_Input_ptr(in), srcSize / eltWidth, eltWidth);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, srcSize));
    return ZL_returnValue(1);
}

// ZL_TypedEncoderFn
ZL_Report EI_transpose_2bytes_typed(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return EI_transpose_serial_typed(eictx, in, 2);
}

// ZL_TypedEncoderFn
ZL_Report EI_transpose_4bytes_typed(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return EI_transpose_serial_typed(eictx, in, 4);
}

// ZL_TypedEncoderFn
ZL_Report EI_transpose_8bytes_typed(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return EI_transpose_serial_typed(eictx, in, 8);
}

/* ===================================================
 * Legacy Encoder Interfaces for Transpose transforms
 * using the pipeTransform model
 * (no longer used)
 * ===================================================
 */

// ZL_PipeEncoderFn
size_t EI_transpose_2bytes(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize)
{
    ZL_ASSERT_EQ(srcSize % 2, 0);       // clean multiple of 4-bytes
    ZL_ASSERT_GE(dstCapacity, srcSize); // large enough capacity
    // Note : most of above conditions could become compile-time checks with
    // ZS_ENABLE_ENSURE
    ZS_transposeEncode(dst, src, srcSize / 2, 2);
    return srcSize;
}

// ZL_PipeEncoderFn
size_t EI_transpose_4bytes(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize)
{
    ZL_ASSERT_EQ(srcSize % 4, 0);       // clean multiple of 4-bytes
    ZL_ASSERT_GE(dstCapacity, srcSize); // large enough capacity
    // Note : most of above conditions could become compile-time checks with
    // ZS_ENABLE_ENSURE
    ZS_transposeEncode(dst, src, srcSize / 4, 4);
    return srcSize;
}

// ZL_PipeEncoderFn
size_t EI_transpose_8bytes(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize)
{
    ZL_ASSERT_EQ(srcSize % 8, 0);       // clean multiple of 8-bytes
    ZL_ASSERT_GE(dstCapacity, srcSize); // large enough capacity
    ZS_transposeEncode(dst, src, srcSize / 8, 8);
    return srcSize;
}

// Split transposes, supports up to width 8 (TODO: consider using generics
// instead?)
static ZL_Report
EI_transpose_split_bytes(ZL_Encoder* eictx, const ZL_Input* in, size_t eltWidth)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_struct);
    ZL_ERR_IF_NE(ZL_Input_eltWidth(in), eltWidth, GENERIC);

    // Create one output buffer per elt byte
    size_t const nbElts = ZL_Input_numElts(in);
    ZL_Output* out[8];
    uint8_t* dst[8];
    for (size_t i = 0; i < eltWidth; ++i) {
        out[i] = ZL_Encoder_createTypedStream(eictx, (int)i, nbElts, 1);
        ZL_ERR_IF_NULL(out[i], allocation);
        dst[i] = (uint8_t*)ZL_Output_ptr(out[i]);
    }

    // Transpose into split streams
    uint8_t const* src = (uint8_t const*)ZL_Input_ptr(in);
    ZS_splitTransposeEncode(dst, src, nbElts, eltWidth);

    for (size_t i = 0; i < eltWidth; ++i) {
        ZL_ERR_IF_ERR(ZL_Output_commit(out[i], nbElts));
    }
    return ZL_returnValue(eltWidth);
}

ZL_Report
EI_transpose_split2bytes(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return EI_transpose_split_bytes(eictx, in, 2);
}

ZL_Report
EI_transpose_split4bytes(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return EI_transpose_split_bytes(eictx, in, 4);
}

ZL_Report
EI_transpose_split8bytes(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return EI_transpose_split_bytes(eictx, in, 8);
}

ZL_DECLARE_SELECTOR(
        ZL_splitTransposeSelector,
        ZL_Type_struct,
        SUCCESSOR(transposeSplit1),
        SUCCESSOR(transposeSplit2),
        SUCCESSOR(transposeSplit4),
        SUCCESSOR(transposeSplit8),
        SUCCESSOR(transposeSplit))

ZL_GraphID ZL_splitTransposeSelector_impl(
        const ZL_Selector* selCtx,
        ZL_Input const* input,
        ZL_splitTransposeSelector_Successors const* successors)
{
    if (ZL_Selector_isTransposeSplitSupported(selCtx)) {
        return successors->transposeSplit;
    }

    switch (ZL_Input_eltWidth(input)) {
        case 1:
            return successors->transposeSplit1;
        case 2:
            return successors->transposeSplit2;
        case 4:
            return successors->transposeSplit4;
        case 8:
            return successors->transposeSplit8;
        default:
            return ZL_GRAPH_ILLEGAL;
    }
}

ZL_NodeID ZL_Graph_getTransposeSplitNode(const ZL_Graph* gctx, size_t eltWidth)
{
    if (ZL_Graph_isNodeSupported(gctx, ZL_NODE_TRANSPOSE_SPLIT)) {
        return ZL_NODE_TRANSPOSE_SPLIT;
    }
    if (eltWidth == 2) {
        return ZL_NODE_TRANSPOSE_SPLIT2_DEPRECATED;
    }
    if (eltWidth == 4) {
        return ZL_NODE_TRANSPOSE_SPLIT4_DEPRECATED;
    }
    if (eltWidth == 8) {
        return ZL_NODE_TRANSPOSE_SPLIT8_DEPRECATED;
    }
    ZL_LOG(ERROR,
           "Invalid transpose element width for old format version: %zu",
           eltWidth);
    return ZL_NODE_ILLEGAL;
}

ZL_GraphID ZL_Compressor_registerTransposeSplitGraph(
        ZL_Compressor* cgraph,
        ZL_GraphID successor)
{
    ZL_GraphID const transpose1 = successor;
    ZL_GraphID const transpose2 = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph,
            ZL_NODE_TRANSPOSE_SPLIT2_DEPRECATED,
            ZL_GRAPHLIST(successor, successor));
    ZL_GraphID const transpose4 = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph,
            ZL_NODE_TRANSPOSE_SPLIT4_DEPRECATED,
            ZL_GRAPHLIST(successor, successor, successor, successor));
    ZL_GraphID const transpose8 = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph,
            ZL_NODE_TRANSPOSE_SPLIT8_DEPRECATED,
            ZL_GRAPHLIST(
                    successor,
                    successor,
                    successor,
                    successor,
                    successor,
                    successor,
                    successor,
                    successor));
    ZL_GraphID const transposeSplit =
            ZL_Compressor_registerStaticGraph_fromNode(
                    cgraph, ZL_NODE_TRANSPOSE_SPLIT, ZL_GRAPHLIST(successor));

    return ZL_splitTransposeSelector_declareGraph(
            cgraph,
            ZL_splitTransposeSelector_successors_init(
                    transpose1,
                    transpose2,
                    transpose4,
                    transpose8,
                    transposeSplit));
}

ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runTransposeSplit(ZL_Edge* edge, const ZL_Graph* graph)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_EdgeList, edge);
    const size_t eltWidth = ZL_Input_eltWidth(ZL_Edge_getData(edge));
    ZL_NodeID node        = ZL_Graph_getTransposeSplitNode(graph, eltWidth);
    ZL_ERR_IF_EQ(
            node.nid,
            ZL_NODE_ILLEGAL.nid,
            formatVersion_unsupported,
            "Invalid transpose element width for older format version %zu",
            eltWidth);
    return ZL_Edge_runNode(edge, node);
}
