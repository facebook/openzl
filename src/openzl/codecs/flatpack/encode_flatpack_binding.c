// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/flatpack/encode_flatpack_binding.h"

#include "openzl/codecs/flatpack/encode_flatpack_kernel.h"
#include "openzl/zl_errors.h"

ZL_Report EI_flatpack(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);

    size_t const nbElts = ZL_Input_numElts(in);

    ZL_Output* alphabet = ZL_Encoder_createTypedStream(eictx, 0, 256, 1);

    size_t const packedCapacity = ZS_flatpackEncodeBound(nbElts);
    ZL_Output* packed =
            ZL_Encoder_createTypedStream(eictx, 1, packedCapacity, 1);

    if (alphabet == NULL || packed == NULL) {
        ZL_ERR(allocation);
    }

    ZS_FlatPackSize const size = ZS_flatpackEncode(
            (uint8_t*)ZL_Output_ptr(alphabet),
            256,
            (uint8_t*)ZL_Output_ptr(packed),
            packedCapacity,
            (uint8_t const*)ZL_Input_ptr(in),
            nbElts);
    ZL_ASSERT(!ZS_FlatPack_isError(size));

    ZL_ERR_IF_ERR(ZL_Output_commit(alphabet, ZS_FlatPack_alphabetSize(size)));
    ZL_ERR_IF_ERR(
            ZL_Output_commit(packed, ZS_FlatPack_packedSize(size, nbElts)));

    return ZL_returnValue(2);
}
