// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/conversion/encode_conversion_binding.h"
#include <stdint.h>
#include "openzl/common/assertion.h"
#include "openzl/common/logging.h"            // ZL_DLOG
#include "openzl/compress/enc_interface.h"    // ENC_refTypedStream
#include "openzl/shared/bits.h"               // ZL_isLittleEndian()
#include "openzl/shared/mem.h"                // MEM_isAlignedForNumericWidth
#include "openzl/shared/numeric_operations.h" // NUMOP_*
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"

/* --------- Conversion transforms --------- */

static ZL_Report convertToNumWithOptionalSwap(
        ZL_Encoder* encoder,
        const ZL_Input* input,
        size_t eltWidth,
        bool needsSwap)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(encoder);
    ZL_ASSERT_NN(input);
    ZL_ASSERT_NE(ZL_Input_type(input) & (ZL_Type_serial | ZL_Type_struct), 0);

    ZL_ERR_IF_NOT(
            eltWidth == 1 || eltWidth == 2 || eltWidth == 4 || eltWidth == 8,
            streamParameter_invalid,
            "Element width must be 1, 2, 4, or 8 bytes, but is %zu bytes",
            eltWidth);

    const void* const src    = ZL_Input_ptr(input);
    const size_t contentSize = ZL_Input_contentSize(input);
    const size_t numElts     = contentSize / eltWidth;

    ZL_ERR_IF_NE(
            contentSize % eltWidth,
            0,
            streamParameter_invalid,
            "Cannont convert to numeric of width %zu with %zu bytes of input",
            eltWidth,
            contentSize);

    if (needsSwap) {
        ZL_Output* output =
                ZL_Encoder_createTypedStream(encoder, 0, numElts, eltWidth);
        ZL_ERR_IF_NULL(output, allocation);
        NUMOP_byteswap(
                ZL_Output_ptr(output), ZL_Input_ptr(input), numElts, eltWidth);
        ZL_ERR_IF_ERR(ZL_Output_commit(output, numElts));
    } else if (MEM_isAlignedForNumericWidth(src, eltWidth)) {
        ZL_ERR_IF_NULL(
                ENC_refTypedStream(encoder, 0, eltWidth, numElts, input, 0),
                allocation);
    } else {
        ZL_Output* output =
                ZL_Encoder_createTypedStream(encoder, 0, numElts, eltWidth);
        if (numElts > 0) {
            ZL_ERR_IF_NULL(output, allocation);
            memcpy(ZL_Output_ptr(output), src, contentSize);
        }
        ZL_ERR_IF_ERR(ZL_Output_commit(output, numElts));
    }
    return ZL_returnSuccess();
}

static ZL_Report EI_convert_serial_to_num_generic(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns,
        size_t intWidth,
        bool needsSwap)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return convertToNumWithOptionalSwap(eictx, in, intWidth, needsSwap);
}

ZL_Report EI_convert_serial_to_num8(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    return EI_convert_serial_to_num_generic(eictx, ins, nbIns, 1, false);
}

ZL_Report EI_convert_serial_to_num_le16(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    return EI_convert_serial_to_num_generic(
            eictx, ins, nbIns, 2, !ZL_isLittleEndian());
}

ZL_Report EI_convert_serial_to_num_le32(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    return EI_convert_serial_to_num_generic(
            eictx, ins, nbIns, 4, !ZL_isLittleEndian());
}

ZL_Report EI_convert_serial_to_num_le64(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    return EI_convert_serial_to_num_generic(
            eictx, ins, nbIns, 8, !ZL_isLittleEndian());
}

ZL_Report EI_convert_serial_to_num_be16(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    return EI_convert_serial_to_num_generic(
            eictx, ins, nbIns, 2, ZL_isLittleEndian());
}

ZL_Report EI_convert_serial_to_num_be32(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    return EI_convert_serial_to_num_generic(
            eictx, ins, nbIns, 4, ZL_isLittleEndian());
}

ZL_Report EI_convert_serial_to_num_be64(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    return EI_convert_serial_to_num_generic(
            eictx, ins, nbIns, 8, ZL_isLittleEndian());
}

static ZL_Report EI_convert_serial_to_struct_generic(
        ZL_Encoder* eictx,
        const ZL_Input* in,
        size_t tokenWidth)
{
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);
    size_t const inByteSize = ZL_Input_contentSize(in);
    if (inByteSize % tokenWidth) {
        ZL_RET_R_ERR(streamParameter_invalid); // Not a clean multiple
    }
    size_t const nbTokens = inByteSize / tokenWidth;
    ZL_RET_R_IF_NULL(
            allocation,
            ENC_refTypedStream(eictx, 0, tokenWidth, nbTokens, in, 0));
    return ZL_returnValue(1);
}

ZL_Report EI_convert_serial_to_struct(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_DLOG(BLOCK, "EI_convert_serial_to_struct");
    ZL_IntParam const tokenSize =
            ZL_Encoder_getLocalIntParam(eictx, ZL_trlip_tokenSize);
    // Parameter **must** be set.
    ZL_RET_R_IF_EQ(
            nodeParameter_invalid, tokenSize.paramId, ZL_LP_INVALID_PARAMID);
    ZL_RET_R_IF_LE(nodeParameter_invalidValue, tokenSize.paramValue, 0);
    return EI_convert_serial_to_struct_generic(
            eictx, in, (size_t)tokenSize.paramValue);
}

ZL_Report EI_convert_struct_to_num_le(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return convertToNumWithOptionalSwap(
            eictx, in, ZL_Input_eltWidth(in), !ZL_isLittleEndian());
}

ZL_Report EI_convert_struct_to_num_be(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    return convertToNumWithOptionalSwap(
            eictx, in, ZL_Input_eltWidth(in), ZL_isLittleEndian());
}

// Design note :
// Width of elements is preserved
ZL_Report EI_convert_num_to_struct_le(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    // TODO (@Cyan): support for big-endian systems,
    //               requires a swap operation
    ZL_REQUIRE(
            ZL_isLittleEndian(), "support for big endian not implemented yet");
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);
    size_t const eltWidth = ZL_Input_eltWidth(in);
    ZL_ASSERT_GT(eltWidth, 0);
    size_t const nbElts = ZL_Input_numElts(in);
    ZL_RET_R_IF_NULL(
            allocation, ENC_refTypedStream(eictx, 0, eltWidth, nbElts, in, 0));
    return ZL_returnValue(1);
}

static ZL_Report
EI_convert_to_serial(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(in);
    size_t const byteSize = ZL_Input_contentSize(in);
    ZL_ASSERT_NN(eictx);
    ZL_RET_R_IF_NULL(
            allocation, ENC_refTypedStream(eictx, 0, 1, byteSize, in, 0));
    return ZL_returnValue(1);
}

ZL_Report EI_convert_num_to_serial_le(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    // TODO (@Cyan): support for big-endian systems,
    //               requires a swap operation
    ZL_REQUIRE(
            ZL_isLittleEndian(), "support for big endian not implemented yet");
    // write eltSize as a 1-byte value
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);
    uint32_t const eltWidth = (uint32_t)ZL_Input_eltWidth(in);
    ZL_ASSERT(eltWidth == 1 || eltWidth == 2 || eltWidth == 4 || eltWidth == 8);
    uint8_t header[1]  = { (uint8_t)ZL_nextPow2(eltWidth) };
    size_t const hSize = sizeof(header);
    ZL_Encoder_sendCodecHeader(eictx, header, hSize);
    return EI_convert_to_serial(eictx, ins, nbIns);
}

#include "openzl/shared/varint.h"
ZL_Report EI_convert_struct_to_serial(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_struct);
    // write eltSize as varint
    uint64_t const eltWidth = (uint64_t)ZL_Input_eltWidth(in);
    uint8_t header[ZL_VARINT_LENGTH_64];
    size_t const hSize = ZL_varintEncode(eltWidth, header);
    ZL_ASSERT_LE(hSize, sizeof(header));
    ZL_ASSERT_NN(eictx);
    ZL_Encoder_sendCodecHeader(eictx, header, hSize);
    return EI_convert_to_serial(eictx, ins, nbIns);
}

ZL_Report EI_separate_VSF_components(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_string);
    ZL_RET_R_IF_ERR(EI_convert_to_serial(eictx, ins, nbIns));
    const uint32_t* fieldSizes = ZL_Input_stringLens(in);
    const size_t nbFields      = ZL_Input_numElts(in);
    size_t const numWidth = NUMOP_numericWidthForArray32(fieldSizes, nbFields);
    ZL_Output* const sizeStream =
            ZL_Encoder_createTypedStream(eictx, 1, nbFields, numWidth);
    ZL_RET_R_IF_NULL(allocation, sizeStream);
    void* const dst = ZL_Output_ptr(sizeStream);
    NUMOP_writeNumerics_fromU32(dst, numWidth, fieldSizes, nbFields);
    ZL_RET_R_IF_ERR(ZL_Output_commit(sizeStream, nbFields));
    return ZL_returnValue(2);
}

ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_parameterizeConvertSerialToStructNode(
        ZL_Compressor* compressor,
        int structSize)
{
    ZL_LocalParams localParams =
            ZL_LP_1INTPARAM(ZL_CONVERT_SERIAL_TO_STRUCT_SIZE_PID, structSize);
    ZL_NodeParameters params = {
        .localParams = &localParams,
    };
    return ZL_Compressor_parameterizeNode(
            compressor, ZL_NODE_CONVERT_SERIAL_TO_STRUCT, &params);
}
