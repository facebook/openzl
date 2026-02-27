// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/dispatchN_byTag/decode_dispatchN_byTag_binding.h" // DI_dispatchN_byTag
#include "openzl/codecs/dispatchN_byTag/decode_dispatchN_byTag_kernel.h" // ZL_dispatchN_byTag_decode
#include "openzl/codecs/range_pack/decode_range_pack_kernel.h" // rangePackDecode
#include "openzl/common/allocation.h"                          // ZL_zeroes
#include "openzl/common/assertion.h"
#include "openzl/common/logging.h"            // ZL_DLOG
#include "openzl/decompress/dictx.h"          // DI_getFrameFormatVersion
#include "openzl/shared/numeric_operations.h" // NUMOP_*
#include "openzl/zl_data.h"                   // ZS2_Data_*

// DI_dispatchN_byTag design notes:
// - Generates a single stream of type ZL_Type_serial
//   from segments collected from multiple streams of type ZL_Type_serial.
//   Segments are defined by:
//   - origin : stream 1
//   - sizes : stream 2
ZL_Report DI_dispatchN_byTag(
        ZL_Decoder* dictx,
        const ZL_Input* inFixed[],
        size_t nbInFixed,
        const ZL_Input* inVariable[],
        size_t nbInVariable)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_DLOG(BLOCK, "DI_dispatchN_byTag (%zu inputs to join)", nbInVariable);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_EQ(nbInFixed, 2);
    ZL_ASSERT_NN(inFixed);

    // Validate inputs
    for (size_t n = 0; n < nbInFixed; n++) {
        // Guaranteed by engine
        ZL_ASSERT_NN(inFixed[n]);
        ZL_ASSERT_EQ(ZL_Input_type(inFixed[n]), ZL_Type_numeric);
    }
    const ZL_Input* tags     = inFixed[dnbt_tags];
    const ZL_Input* segSizes = inFixed[dnbt_segSizes];
    size_t const nbSegments  = ZL_Input_numElts(segSizes);
    ZL_ERR_IF_NE(ZL_Input_numElts(tags), nbSegments, corruption);

    if (DI_getFrameFormatVersion(dictx) < 20) {
        ZL_ERR_IF_GE(nbInVariable, 256, temporaryLibraryLimitation);
        ZL_ERR_IF_GT(ZL_Input_eltWidth(tags), 1, temporaryLibraryLimitation);
    } else {
        ZL_ERR_IF_GE(nbInVariable, 1 << 16, temporaryLibraryLimitation);
        ZL_ERR_IF_GT(ZL_Input_eltWidth(tags), 2, temporaryLibraryLimitation);
    }

    size_t total = 0;
    for (size_t n = 0; n < nbInVariable; n++) {
        // Must be validated
        ZL_ASSERT_NN(inVariable[n]);
        ZL_ERR_IF_NE(ZL_Input_type(inVariable[n]), ZL_Type_serial, corruption);
        total += ZL_Input_numElts(inVariable[n]);
    }
    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, total, 1);
    ZL_ERR_IF_NULL(out, allocation);

    // Note : reserving scratch buffers should be a property offered by DICtx*.
    // Currently it doesn't exist, so allocate directly instead.
    // This should be updated once scratch buffer support is implemented.

    const void** const srcs =
            ZL_Decoder_getScratchSpace(dictx, sizeof(*srcs) * nbInVariable);
    size_t* const srcSizes = ZL_Decoder_getScratchSpace(
            dictx,
            sizeof(*srcSizes) * nbInVariable); // must be init to zero
    ZL_zeroes(srcSizes, sizeof(*srcSizes) * nbInVariable);
    size_t* const segmentSizes = ZL_Decoder_getScratchSpace(
            dictx, sizeof(*segmentSizes) * nbSegments);
    uint16_t* const bufIndex =
            ZL_Decoder_getScratchSpace(dictx, sizeof(*bufIndex) * nbSegments);

    if (!srcs || !srcSizes || !segmentSizes || !bufIndex) {
        ZL_ERR(allocation);
    }

    /* prepare arrays for raw transform */
    for (size_t n = 0; n < nbInVariable; n++) {
        srcs[n] = ZL_Input_ptr(inVariable[n]);
    }
    rangePackDecode(
            segmentSizes,
            sizeof(*segmentSizes),
            ZL_Input_ptr(segSizes),
            ZL_Input_eltWidth(segSizes),
            nbSegments,
            0);
    rangePackDecode(
            bufIndex,
            sizeof(*bufIndex),
            ZL_Input_ptr(tags),
            ZL_Input_eltWidth(tags),
            nbSegments,
            0);

    /* Check validity of tags */
    if (!NUMOP_underLimitU16(bufIndex, nbSegments, (unsigned)nbInVariable)) {
        ZL_ERR(corruption,
               "vector of tags incorrect : some value(s) > nb srcs");
    }

    /* Check validity of segment sizes */
    for (size_t n = 0; n < nbSegments; n++) {
        srcSizes[bufIndex[n]] += segmentSizes[n];
    }
    for (size_t n = 0; n < nbInVariable; n++) {
        if (srcSizes[n] != ZL_Input_numElts(inVariable[n])) {
            ZL_ERR(corruption,
                   "segment sizes incorrect : invalid total size for stream %zu",
                   n);
        }
    }

    size_t const r = ZL_dispatchN_byTag_decode(
            ZL_Output_ptr(out),
            total,
            srcs,
            nbInVariable,
            segmentSizes,
            bufIndex,
            nbSegments);

    ZL_ASSERT_EQ(r, total);

    ZL_ERR_IF_ERR(ZL_Output_commit(out, total));

    return ZL_returnSuccess();
}
