// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/concat/encode_concat_binding.h"

#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h" // ZL_memcpy
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

ZL_Report EI_concat(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_GE(nbIns, 1);
    ZL_ASSERT_NN(ins);
    size_t nbElts   = 0;
    ZL_Type type    = ZL_Input_type(ins[0]);
    size_t eltWidth = ZL_Input_eltWidth(ins[0]);
    for (size_t n = 0; n < nbIns; n++) {
        ZL_ASSERT_NN(ins[n]);
        ZL_ERR_IF_NE(
                ZL_Input_type(ins[n]),
                type,
                node_unexpected_input_type,
                "Concat types must be homogenous");
        ZL_ERR_IF_NE(
                ZL_Input_eltWidth(ins[n]),
                eltWidth,
                node_unexpected_input_type,
                "Concat widths must be homogenous");

        ZL_ERR_IF_GE(ZL_Input_numElts(ins[n]), UINT32_MAX, node_invalid_input);
        ZL_ERR_IF(
                ZL_overflowAddST(nbElts, ZL_Input_numElts(ins[n]), &nbElts),
                node_invalid_input);
    }
    size_t eltsCapacity = nbElts;
    if (type == ZL_Type_string) {
        eltWidth     = 1;
        eltsCapacity = 0;
        for (size_t n = 0; n < nbIns; n++) {
            ZL_ERR_IF(
                    ZL_overflowAddST(
                            eltsCapacity,
                            ZL_Input_contentSize(ins[n]),
                            &eltsCapacity),
                    node_invalid_input);
        }
    }

    ZL_Output* const sizes = ZL_Encoder_createTypedStream(eictx, 0, nbIns, 4);
    ZL_ERR_IF_NULL(sizes, allocation);
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eictx, 1, eltsCapacity, eltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    uint8_t* outPtr = (uint8_t*)ZL_Output_ptr(out);
    ZL_ASSERT_NN(outPtr);
    uint8_t* const outEnd    = outPtr + eltsCapacity * eltWidth;
    uint32_t* const sizesPtr = (uint32_t*)ZL_Output_ptr(sizes);
    ZL_ASSERT_NN(sizesPtr);

    for (size_t n = 0; n < nbIns; n++) {
        sizesPtr[n] = (uint32_t)ZL_Input_numElts(ins[n]);
        size_t size = sizesPtr[n] * eltWidth;
        if (type == ZL_Type_string) {
            size = (uint32_t)ZL_Input_contentSize(ins[n]);
        }
        if (size > 0) {
            ZL_ASSERT_NN(ZL_Input_ptr(ins[n]));
            ZL_memcpy(outPtr, ZL_Input_ptr(ins[n]), size);
        }
        outPtr += size;
    }

    if (type == ZL_Type_string) {
        uint32_t* wptr = ZL_Output_reserveStringLens(out, nbElts);
        size_t rPos    = 0;
        for (size_t n = 0; n < nbIns; n++) {
            size_t size = ZL_Input_numElts(ins[n]);
            if (size > 0) {
                ZL_ASSERT_NN(ZL_Input_stringLens(ins[n]));
                ZL_memcpy(
                        wptr + rPos,
                        ZL_Input_stringLens(ins[n]),
                        size * sizeof(uint32_t));
            }
            rPos += size;
        }
    }
    ZL_ASSERT_EQ(outPtr, outEnd);
    (void)outEnd;

    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbElts));
    ZL_ERR_IF_ERR(ZL_Output_commit(sizes, nbIns));
    return ZL_returnSuccess();
}
