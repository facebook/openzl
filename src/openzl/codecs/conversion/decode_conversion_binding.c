// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/conversion/decode_conversion_binding.h"
#include "openzl/common/assertion.h"
#include "openzl/common/stream.h"    // STREAM_*
#include "openzl/decompress/dictx.h" // DI_*
#include "openzl/shared/bits.h"      // ZL_isLittleEndian
#include "openzl/shared/mem.h" // ZL_readLE32, MEM_IS_ALIGNED, MEM_isAlignedForNumericWidth
#include "openzl/shared/numeric_operations.h" // NUMOP_*
#include "openzl/zl_data.h"

/* --------- Conversion transforms --------- */

static ZL_Report convertFromNumWithOptionalSwap(
        ZL_Decoder* decoder,
        const ZL_Input* ins[],
        ZL_Type toType,
        bool needsSwap)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(decoder);
    ZL_ASSERT_NE(toType & (ZL_Type_serial | ZL_Type_struct), 0);

    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);

    const size_t inNumElts  = ZL_Input_numElts(in);
    const size_t inEltWidth = ZL_Input_eltWidth(in);

    needsSwap = needsSwap && inEltWidth > 1;

    size_t eltWidth;
    size_t numElts;
    if (toType == ZL_Type_serial) {
        eltWidth = 1;
        numElts  = ZL_Input_contentSize(in);
    } else {
        eltWidth = inEltWidth;
        numElts  = inNumElts;
    }

    if (needsSwap) {
        ZL_Output* const out =
                ZL_Decoder_createTypedStream(decoder, 0, numElts, eltWidth);
        ZL_ERR_IF_NULL(out, allocation);
        NUMOP_byteswap(
                ZL_Output_ptr(out), ZL_Input_ptr(in), inNumElts, inEltWidth);
        ZL_ERR_IF_ERR(ZL_Output_commit(out, numElts));
    } else {
        ZL_ERR_IF_NULL(
                DI_reference1OutStream(decoder, in, 0, eltWidth, numElts),
                allocation);
    }

    return ZL_returnSuccess();
}

// Effectively, numeric => serial
ZL_Report DI_revert_serial_to_num_le(ZL_Decoder* di, const ZL_Input* ins[])
{
    return convertFromNumWithOptionalSwap(
            di, ins, ZL_Type_serial, !ZL_isLittleEndian());
}

ZL_Report DI_revert_serial_to_num_be(ZL_Decoder* di, const ZL_Input* ins[])
{
    return convertFromNumWithOptionalSwap(
            di, ins, ZL_Type_serial, ZL_isLittleEndian());
}
// Effectively, serial => intX
ZL_Report DI_revert_num_to_serial_le(ZL_Decoder* di, const ZL_Input* ins[])
{
    ZL_ASSERT_NN(di);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_Type const st = ZL_Input_type(in);
    ZL_ASSERT_EQ(
            st,
            ZL_Type_serial); // should already be validated by the graph
                             // engine

    ZL_RESULT_DECLARE_SCOPE_REPORT(di);
    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(di);
    ZL_ERR_IF_NE(header.size, 1, header_unknown, "Invalid transform header!");
    size_t const intSize = (size_t)1 << (((const uint8_t*)header.start)[0]);
    ZL_ERR_IF(
            !(intSize == 1 || intSize == 2 || intSize == 4 || intSize == 8),
            header_unknown,
            "header contains bad integer width");
    size_t const nbBytes = ZL_Input_contentSize(in);
    ZL_ERR_IF_NE(
            nbBytes % intSize,
            0,
            corruption,
            "stream size must be a multiple of the integer size");
    size_t const nbInts = nbBytes / intSize;

    // TODO (@Cyan): support for big-endian systems,
    //               requires a swap operation
    ZL_REQUIRE(
            ZL_isLittleEndian(), "support for big endian not implemented yet");

    if (MEM_isAlignedForNumericWidth(ZL_Input_ptr(in), intSize)) {
        ZL_Output* const out =
                DI_reference1OutStream(di, in, 0, intSize, nbInts);
        ZL_ERR_IF_NULL(out, allocation);
    } else {
        // Not aligned : create new stream, copy into it
        ZL_Output* const out = ZL_Decoder_create1OutStream(di, nbInts, intSize);
        ZL_ERR_IF_NULL(out, allocation);
        ZL_ASSERT(MEM_isAlignedForNumericWidth(ZL_Output_ptr(out), intSize));
        size_t const byteSize = ZL_Input_contentSize(in);
        ZL_ERR_IF_ERR(STREAM_copyBytes(
                ZL_codemodOutputAsData(out),
                ZL_codemodInputAsData(in),
                byteSize));
    }

    return ZL_returnValue(1);
}

// Effectively, token(anylength) => serial
ZL_Report DI_revert_serial_to_struct(ZL_Decoder* di, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(di);
    ZL_ASSERT_NN(di);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];

    ZL_Output* const out =
            DI_reference1OutStream(di, in, 0, 1, ZL_Input_contentSize(in));
    ZL_ERR_IF_NULL(out, allocation);

    return ZL_returnValue(1);
}

#include "openzl/shared/varint.h"
// Effectively, serial => token
ZL_Report DI_revert_struct_to_serial(ZL_Decoder* di, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(di);
    ZL_ASSERT_NN(di);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_Type const st = ZL_Input_type(in);
    ZL_ASSERT_EQ(
            st,
            ZL_Type_serial); // should already be validated by the graph
                             // engine

    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(di);
    const uint8_t* ptr      = header.start;
    ZL_RESULT_OF(uint64_t)
    const r = ZL_varintDecode(&ptr, ptr + header.size);
    ZL_ERR_IF(ZL_RES_isError(r), srcSize_tooSmall);
    uint64_t const eltSize = ZL_RES_value(r);
    ZL_ERR_IF_EQ(eltSize, 0, header_unknown, "eltSize must not be 0");
    ZL_ERR_IF_NE(
            MEM_ptrDistance(header.start, ptr),
            header.size,
            header_unknown,
            "Header size wrong");
    size_t const nbBytes = ZL_Input_contentSize(in);
    ZL_ERR_IF_NE(
            nbBytes % eltSize,
            0,
            corruption,
            "stream size must be a multiple of the token size");
    size_t const nbTokens = nbBytes / eltSize;

    ZL_Output* const out = DI_reference1OutStream(di, in, 0, eltSize, nbTokens);
    ZL_ERR_IF_NULL(out, allocation);

    return ZL_returnValue(1);
}

// Effectively, int => token
ZL_Report DI_revert_struct_to_num_le(ZL_Decoder* di, const ZL_Input* ins[])
{
    return convertFromNumWithOptionalSwap(
            di, ins, ZL_Type_struct, !ZL_isLittleEndian());
}
ZL_Report DI_revert_struct_to_num_be(ZL_Decoder* di, const ZL_Input* ins[])
{
    return convertFromNumWithOptionalSwap(
            di, ins, ZL_Type_struct, ZL_isLittleEndian());
}

// Effectively, token => int
ZL_Report DI_revert_num_to_struct_le(ZL_Decoder* di, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(di);
    // TODO (@Cyan): support for big-endian systems,
    //               requires a swap operation
    ZL_REQUIRE(
            ZL_isLittleEndian(), "support for big endian not implemented yet");
    ZL_ASSERT_NN(di);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(
            ZL_Input_type(in),
            ZL_Type_struct); // should already be validated by the graph
                             // engine
    size_t const eltWidth = ZL_Input_eltWidth(in);
    if (!(eltWidth == 1 || eltWidth == 2 || eltWidth == 4 || eltWidth == 8)) {
        ZL_ERR(streamParameter_invalid);
    }
    size_t const nbElts = ZL_Input_numElts(in);

    if (MEM_isAlignedForNumericWidth(ZL_Input_ptr(in), eltWidth)) {
        ZL_Output* const out =
                DI_reference1OutStream(di, in, 0, eltWidth, nbElts);
        ZL_ERR_IF_NULL(out, allocation);
    } else {
        // Not aligned : create new stream, copy into it
        ZL_Output* const out =
                ZL_Decoder_create1OutStream(di, nbElts, eltWidth);
        ZL_ERR_IF_NULL(out, allocation);
        ZL_ASSERT(MEM_isAlignedForNumericWidth(ZL_Output_ptr(out), eltWidth));
        size_t const byteSize = ZL_Input_contentSize(in);
        ZL_ERR_IF_ERR(STREAM_copyBytes(
                ZL_codemodOutputAsData(out),
                ZL_codemodInputAsData(in),
                byteSize));
    }

    return ZL_returnValue(1);
}

ZL_Report DI_revert_VSF_separation(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const concatFields = ins[0];
    ZL_ASSERT_NN(concatFields);
    ZL_ASSERT_EQ(ZL_Input_type(concatFields), ZL_Type_serial);
    size_t const contentSize         = ZL_Input_contentSize(concatFields);
    const ZL_Input* const fieldSizes = ins[1];
    ZL_ASSERT_NN(fieldSizes);
    ZL_ASSERT_EQ(ZL_Input_type(fieldSizes), ZL_Type_numeric);

    ZL_Output* const vsfRegen =
            DI_reference1OutStream(dictx, concatFields, 0, 1, contentSize);
    ZL_ERR_IF_NULL(vsfRegen, allocation);

    const size_t nbFields = ZL_Input_numElts(fieldSizes);
    // Note : allocation to be changed for local workspace when available
    uint32_t* const arr32 = ZL_Output_reserveStringLens(vsfRegen, nbFields);
    ZL_ERR_IF_NULL(arr32, allocation);

    ZL_ERR_IF_ERR(NUMOP_write32_fromNumerics(
            arr32,
            nbFields,
            ZL_Input_ptr(fieldSizes),
            ZL_Input_eltWidth(fieldSizes)));

    uint64_t const totalSize = NUMOP_sumArray32(arr32, nbFields);
    ZL_DLOG(SEQ,
            "Calculating totalSize=%llu, as sum of arr32 of %zu elts",
            totalSize,
            nbFields);
    ZL_ERR_IF_NE(
            totalSize,
            (uint64_t)contentSize,
            corruption,
            "Incorrect sum of field sizes");

    ZL_ERR_IF_ERR(ZL_Output_commit(vsfRegen, nbFields));
    ZL_DLOG(SEQ,
            "Produced Stream: Type:%u, nbStrings:%u, eltWidth=%u",
            ZL_Output_type(vsfRegen),
            nbFields,
            ZL_validResult(ZL_Output_eltWidth(vsfRegen)));

    return ZL_returnValue(1);
}

ZL_Report DI_extract_concatenatedFields(
        ZL_Decoder* dictx,
        const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const inVSF = ins[0];
    ZL_ASSERT_NN(inVSF);
    ZL_ASSERT_EQ(ZL_Input_type(inVSF), ZL_Type_string);

    ZL_Output* const serialExtract = DI_reference1OutStream(
            dictx, inVSF, 0, 1, ZL_Input_contentSize(inVSF));
    ZL_ERR_IF_NULL(serialExtract, allocation);
    return ZL_returnValue(1);
}
