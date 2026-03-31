// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/splitN/decode_splitN_binding.h" // DI_splitN

#include <string.h>

#include "openzl/common/assertion.h"
#include "openzl/common/logging.h" // ZL_DLOG
#include "openzl/shared/varint.h"

// DI_splitN design notes:
// - Reverses EI_splitN operation
// - Generates a single stream of type ZL_Type_serial
//   from multiple streams of type ZL_Type_serial.
//
ZL_Report DI_splitN(
        ZL_Decoder* dictx,
        const ZL_Input* inFixed[],
        size_t nbInFixed,
        const ZL_Input* ins[],
        size_t nbInVariable)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_DLOG(BLOCK, "DI_splitN (%zu inputs to join)", nbInVariable);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_EQ(nbInFixed, 0);
    (void)inFixed;

    // Special case: Zero inputs, must get eltWidth from the header.
    // No header means eltWidth 1.
    size_t eltWidth;
    ZL_Type type;
    if (nbInVariable == 0) {
        ZL_RBuffer header = ZL_Decoder_getCodecHeader(dictx);
        if (header.size != 0) {
            uint8_t const* ptr = (uint8_t const*)header.start;
            ZL_TRY_LET(
                    uint64_t,
                    eltWidth64,
                    ZL_varintDecode(&ptr, ptr + header.size));
            eltWidth = eltWidth64;
        } else {
            eltWidth = 1;
        }
        type = ZL_Type_any; // Not used
    } else {
        eltWidth = ZL_Input_eltWidth(ins[0]);
        type     = ZL_Input_type(ins[0]);
    }

    size_t totalElts = 0;
    for (size_t n = 0; n < nbInVariable; n++) {
        ZL_ASSERT_NN(ins[n]);
        ZL_ERR_IF_NE(
                ZL_Input_type(ins[n]),
                type,
                node_unexpected_input_type,
                "SplitN types must be homogenous");
        ZL_ERR_IF_NE(
                ZL_Input_eltWidth(ins[n]),
                eltWidth,
                node_unexpected_input_type,
                "SplitN widths must be homogenous");
        totalElts += ZL_Input_numElts(ins[n]);
    }
    ZL_Output* const out =
            ZL_Decoder_create1OutStream(dictx, totalElts, eltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    size_t pos       = 0;
    char* const wptr = ZL_Output_ptr(out);
    for (size_t n = 0; n < nbInVariable; n++) {
        size_t const inSize =
                ZL_Input_numElts(ins[n]) * ZL_Input_eltWidth(ins[n]);
        memcpy(wptr + pos, ZL_Input_ptr(ins[n]), inSize);
        pos += inSize;
    }
    ZL_ASSERT_EQ(pos, totalElts * eltWidth);

    ZL_ERR_IF_ERR(ZL_Output_commit(out, totalElts));

    return ZL_returnSuccess();
}
