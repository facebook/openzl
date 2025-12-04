// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitunpack/encode_bitunpack_binding.h"

#include "openzl/codecs/bitpack/common_bitpack_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_public_nodes.h"

static ZL_Report getNbBits(ZL_Encoder* eictx)
{
    ZL_IntParam const nbBits =
            ZL_Encoder_getLocalIntParam(eictx, ZL_Bitunpack_numBits);
    // Parameter **must** be set.
    ZL_RET_R_IF_EQ(
            nodeParameter_invalid, nbBits.paramId, ZL_LP_INVALID_PARAMID);
    if (nbBits.paramValue <= 0 || nbBits.paramValue > 64) {
        ZL_RET_R_ERR(nodeParameter_invalidValue);
    }
    return ZL_returnValue((size_t)nbBits.paramValue);
}

static size_t getEltWidth(const size_t nbBits)
{
    ZL_ASSERT_LE(nbBits, 64);
    if (nbBits <= 8) {
        return 1;
    } else if (nbBits <= 16) {
        return 2;
    } else if (nbBits <= 32) {
        return 4;
    } else {
        return 8;
    }
}

ZL_Report EI_bitunpack(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);
    ZL_ASSERT(ZL_Input_type(in) == ZL_Type_serial);
    ZL_TRY_LET_R(nbBits, getNbBits(eictx));
    const void* const src = ZL_Input_ptr(in);
    size_t const srcSize  = ZL_Input_numElts(in);

    size_t const nbElts = srcSize * 8 / nbBits;

    // Make sure we fit well, and that remaining bits are zero
    ZL_RET_R_IF_NE(GENERIC, (nbElts * nbBits + 7) / 8, srcSize);

    size_t const eltWidth = getEltWidth(nbBits);
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eictx, 0, nbElts, eltWidth);
    ZL_RET_R_IF_NULL(allocation, out);

    const size_t bytesRead = ZS_bitpackDecode(
            ZL_Output_ptr(out), nbElts, eltWidth, src, srcSize, (int)nbBits);
    ZL_RET_R_IF_NE(logicError, bytesRead, srcSize);

    ZL_RET_R_IF_ERR(ZL_Output_commit(out, nbElts));

    // Header's structure:
    // Byte 1 - nbBits
    // Byte 2 - optional, remainder bits that don't fit into an element
    uint8_t header[2];
    size_t headerSize = 1;
    header[0]         = (uint8_t)nbBits;

    const size_t remNbBits = srcSize * 8 - nbElts * nbBits;
    if (remNbBits) {
        const uint8_t lastByte = ((const uint8_t*)src)[srcSize - 1];
        const uint8_t remBits  = (uint8_t)(lastByte >> (8 - remNbBits));
        if (remBits) {
            if (ZL_Encoder_getCParam(eictx, ZL_CParam_formatVersion) >= 7) {
                header[1] = remBits;
                headerSize += 1;
            } else {
                ZL_RET_R_ERR(
                        GENERIC,
                        "Bitunpack support non-zero trailing bits starting at format version 7");
            }
        }
    }

    ZL_Encoder_sendCodecHeader(eictx, &header, headerSize);

    return ZL_returnValue(1);
}

ZL_NodeID ZL_Compressor_registerBitunpackNode(ZL_Compressor* cgraph, int nbBits)
{
    return ZL_CREATENODE_BITUNPACK(cgraph, nbBits);
}
