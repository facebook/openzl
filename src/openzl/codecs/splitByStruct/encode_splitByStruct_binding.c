// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/splitByStruct/encode_splitByStruct_binding.h"
#include "openzl/codecs/splitByStruct/encode_splitByStruct_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/common/logging.h"         // ZL_DLOG
#include "openzl/common/wire_format.h"     // ZS2_strid_*
#include "openzl/compress/cgraph.h"        // CGRAPH_registerStandardVOTransform
#include "openzl/compress/enc_interface.h" // ZL_Encoder_getScratchSpace
#include "openzl/compress/private_nodes.h" // ZS2_NODE_SPLITBYSTRUCT_TRANSFORM
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_selector.h" // ZL_SelectorDesc

static size_t sum(const size_t array_st[], size_t arr_size)
{
    ZL_ASSERT_NN(array_st);
    size_t total = 0;
    for (size_t n = 0; n < arr_size; n++) {
        total += array_st[n];
    }
    return total;
}

ZL_Report
EI_splitByStruct(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);
    ZL_DLOG(BLOCK, "EI_splitByStruct (in:%zu bytes)", ZL_Input_numElts(in));
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);

    ZL_CopyParam const structure = ZL_Encoder_getLocalCopyParam(
            eictx, ZL_SPLITBYSTRUCT_FIELDSIZES_PID);
    // Note : it shouldn't be possible for this transform to be invoked
    //        without proper parameters, because it's private,
    //        and can only be instantiated via createNode,
    //        which ensures the presence of wanted parameter.
    //        However, our test fuzzer framework seems to be setup differently,
    //        and is nonetheless invoking this node directly, without parameter.
    ZL_ERR_IF_EQ(
            structure.paramId,
            ZL_LP_INVALID_PARAMID,
            nodeParameter_invalid,
            "splitByStruct requires structure description (parameter ZL_SPLITBYSTRUCT_FIELDSIZES_PID)");
    const size_t* fieldSizes = structure.paramPtr;
    ZL_ASSERT_NN(fieldSizes);
    size_t const paramSize = structure.paramSize;
    ZL_ASSERT_EQ(paramSize % sizeof(size_t), 0);

    size_t const nbFields = paramSize / sizeof(size_t);
    ZL_DLOG(BLOCK, "EI_split_byStruct: splitting into %zu fields", nbFields);
    size_t const inSize     = ZL_Input_numElts(in);
    size_t const structSize = sum(fieldSizes, nbFields);
    /* Note: an alternative could be to ensure this condition (structSize > 0)
     * at graph construction time,
     * so that there is no need to check it at run time.
     * This would require a proper way to invalidate a graph (NaG) */
    ZL_ERR_IF_EQ(
            structSize,
            0,
            nodeParameter_invalidValue,
            "structure must have a size > 0");
    ZL_ERR_IF_NE(
            inSize % structSize,
            0,
            node_invalid_input,
            "splitByStruct transform requires an input size which is a strict multiple of structure size");
    ZL_ASSERT_NE(nbFields, 0); // Guaranteed by structSize > 0
    size_t const nbStructs = inSize / structSize;

    void** const outArr =
            ZL_Encoder_getScratchSpace(eictx, nbFields * sizeof(size_t));
    ZL_ERR_IF_NULL(
            outArr,
            allocation,
            "allocation error in splitByStruct while trying to create array of %zu output pointers",
            nbFields);

    for (size_t n = 0; n < nbFields; n++) {
        ZL_ERR_IF_EQ(
                fieldSizes[n],
                0,
                nodeParameter_invalidValue,
                "Must not have a field size of zero!");
        ZL_Output* const out = ZL_Encoder_createTypedStream(
                eictx, 0, nbStructs, fieldSizes[n]);
        ZL_ERR_IF_NULL(
                out,
                allocation,
                "allocation error in splitByStruct while trying to create output stream %zu of size %zu",
                n,
                nbStructs * fieldSizes[n]);
        outArr[n] = ZL_Output_ptr(out);
        ZL_ERR_IF_ERR(ZL_Output_commit(out, nbStructs));
    }

    ZS_dispatchArrayFixedSizeStruct(
            outArr, nbFields, ZL_Input_ptr(in), inSize, fieldSizes);

    return ZL_returnSuccess();
}

ZL_NodeID ZL_createNode_splitByStruct(
        ZL_Compressor* cgraph,
        const size_t* fieldSizes,
        size_t nbFields)
{
    ZL_CopyParam const ssp = { .paramId   = ZL_SPLITBYSTRUCT_FIELDSIZES_PID,
                               .paramPtr  = fieldSizes,
                               .paramSize = nbFields * sizeof(size_t) };

    ZL_LocalCopyParams const lgp        = { &ssp, 1 };
    ZL_LocalParams const lParams        = { .copyParams = lgp };
    ZL_ParameterizedNodeDesc nodeParams = {
        .name        = "zl.split_by_struct",
        .node        = ZL_NODE_SPLIT_BY_STRUCT,
        .localParams = &lParams,
    };
    return ZL_Compressor_registerParameterizedNode(cgraph, &nodeParams);
}

ZL_GraphID ZL_Compressor_registerSplitByStructGraph(
        ZL_Compressor* cgraph,
        const size_t* fieldSizes,
        const ZL_GraphID* successors,
        size_t nbFields)
{
    ZL_NodeID const node =
            ZL_createNode_splitByStruct(cgraph, fieldSizes, nbFields);

    ZL_ParameterizedGraphDesc graphParams = {
        .name           = "zl.split_by_struct",
        .graph          = ZL_GRAPH_SPLIT_SERIAL,
        .customGraphs   = successors,
        .nbCustomGraphs = nbFields,
        .customNodes    = &node,
        .nbCustomNodes  = 1,
    };

    return ZL_Compressor_registerParameterizedGraph(cgraph, &graphParams);
}
