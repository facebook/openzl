// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/selectors/selector_constant.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h"

/* Returns non-zero if all bytes of the first element are identical.
 * This covers 0x00..00, 0xFF..FF, 0x55..55, etc.
 * For such patterns, Serial path is more efficient. */
static int isSingleBytePattern(const ZL_Input* inputStream)
{
    const uint8_t* ptr    = ZL_Input_ptr(inputStream);
    size_t const eltWidth = ZL_Input_eltWidth(inputStream);
    if (ZL_Input_numElts(inputStream) == 0) {
        return 0;
    }
    uint8_t const firstByte = ptr[0];
    for (size_t i = 1; i < eltWidth; ++i) {
        if (ptr[i] != firstByte)
            return 0;
    }
    return 1;
}

/* SI_selector_constant():
 *
 * The goal of this selector is to select between serialized and
 * fixed-size constant encoding given an input that can be any of
 * these types.
 *
 * Single-byte patterns (0x00, 0xFF, 0x55, etc.) are routed
 * to CONSTANT_SERIAL (more efficient).
 */

ZL_GraphID SI_selector_constant(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    (void)selCtx;
    (void)customGraphs;
    (void)nbCustomGraphs;

    ZL_Type const inType = ZL_Input_type(inputStream);
    ZL_ASSERT(inType == ZL_Type_serial || inType == ZL_Type_struct || inType == ZL_Type_numeric);

    /* If all bytes are identical, Serial path is more efficient */
    if (ZL_Input_eltWidth(inputStream) > 1
        && isSingleBytePattern(inputStream)) {
        return ZL_GRAPH_CONSTANT_SERIAL;
    }

    switch (inType) {
        case ZL_Type_serial:
            return ZL_GRAPH_CONSTANT_SERIAL;
        case ZL_Type_struct:
        case ZL_Type_numeric:
            return ZL_GRAPH_CONSTANT_FIXED;
        /* fallthrough - not supported */
        case ZL_Type_string:
        default:
            ZL_REQUIRE(0, "Unsupported input type for constant selector");
    }
}
