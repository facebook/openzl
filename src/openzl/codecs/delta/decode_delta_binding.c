// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/delta/decode_delta_binding.h"
#include "openzl/codecs/delta/decode_delta_kernel.h" // ZS_deltaEncodeXX
#include "openzl/common/assertion.h"
#include "openzl/decompress/dictx.h"

// ZL_TypedEncoderFn
// This variant is compatible with any allowed integer width
ZL_Report DI_delta_int(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);
    size_t const intWidth = ZL_Input_eltWidth(in);
    ZL_ASSERT(intWidth == 1 || intWidth == 2 || intWidth == 4 || intWidth == 8);
    size_t const nbDeltas = ZL_Input_numElts(in);

    void const* first  = NULL;
    void const* deltas = NULL;
    size_t nbInts      = 0;
    if (DI_getFrameFormatVersion(dictx) < 13) {
        // Old variant: First element is written into the deltas stream
        nbInts = nbDeltas;
        if (nbInts > 0) {
            first  = ZL_Input_ptr(in);
            deltas = (char const*)first + intWidth;
        }
    } else {
        // New variant: First element is written into the transform header.
        ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dictx);
        if (header.size != 0) {
            ZL_ERR_IF_NE(
                    header.size,
                    intWidth,
                    corruption,
                    "Header must be a single int");
            first  = header.start;
            deltas = ZL_Input_ptr(in);
            nbInts = nbDeltas + 1;
        } else {
            // Special case: If the transform header is empty, then we must have
            // nbDeltas == 0, and then nbInts == 0.
            ZL_ERR_IF_NE(
                    nbDeltas,
                    0,
                    corruption,
                    "Empty header but non-empty deltas");
        }
    }

    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, nbInts, intWidth);
    ZL_ERR_IF_NULL(out, allocation);

    // Note : proper alignment is guaranteed by graph engine
    ZS_deltaDecode(ZL_Output_ptr(out), first, deltas, nbInts, intWidth);

    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbInts));
    return ZL_returnValue(1);
}
