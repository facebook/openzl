// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "openzl/codecs/zl_bitpack.h"    // ZL_GRAPH_BITPACK
#include "openzl/codecs/zl_constant.h"   // ZL_GRAPH_CONSTANT
#include "openzl/codecs/zl_conversion.h" // ZL_NODE_INTERPRET_AS_LE32, etc.
#include "openzl/codecs/zl_delta.h"      // ZL_NODE_DELTA_INT
#include "openzl/codecs/zl_tokenize.h"   // ZL_Compressor_registerTokenizeGraph
#include "openzl/codecs/zl_transpose.h"  // ZL_NODE_TRANSPOSE_SPLIT
#include "openzl/codecs/zl_zigzag.h"     // ZL_NODE_ZIGZAG
#include "openzl/common/debug.h"         // ZL_REQUIRE
#include "openzl/common/wire_format.h"   // ZL_StandardTransformID_*
#include "openzl/compress/private_nodes.h" // ZL_NODE_BITPACK_INT
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
extern "C" {
#include "openzl/decompress/dctx2.h"
#include "openzl/decompress/decoder_fusion.h"
}
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_input.h"
#include "openzl/zl_output.h"
#include "openzl/zl_version.h"

namespace openzl::tests {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

template <typename T, void (*FreeFn)(T*)>
struct StaticFunctionDeleter {
    void operator()(T* p)
    {
        FreeFn(p);
    }
};

static size_t g_calledCount = 0;

// ---------------------------------------------------------------------------
// Fused zigzag+delta decoder
//
// During decompression the graph has zigzag_decoder feeding into delta_decoder.
// This fused decoder replaces both: it reads zigzag's stored input, reads
// delta's codec header (first value), and applies the combined zigzag-decode +
// prefix-sum in one pass, eliminating the intermediate buffer.
// ---------------------------------------------------------------------------

static ZL_Report fusedZigzagDeltaDecode(ZL_DecoderFusion* state) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->dctx);
    // codecIndex 0 = zigzag (child) — its inputs are the stored streams
    // codecIndex 1 = delta   (parent) — its inputs are [NULL] (fed by zigzag)
    ZL_RESULT_OF(ZL_CodecInputs)
    zigzagInputsResult = ZL_DecoderFusion_getCodecInputs(state, 0);
    ZL_ERR_IF(ZL_RES_isError(zigzagInputsResult), corruption);
    ZL_CodecInputs zigzagInputs = ZL_RES_value(zigzagInputsResult);
    ZL_ERR_IF_NE(zigzagInputs.singleton.numInputs, 1, corruption);

    const ZL_Input* const encodedStream = zigzagInputs.singleton.inputs[0];
    ZL_ERR_IF_NULL(encodedStream, corruption);
    ZL_ERR_IF_NE(ZL_Input_type(encodedStream), ZL_Type_numeric, corruption);

    size_t const eltWidth = ZL_Input_eltWidth(encodedStream);
    size_t const nbDeltas = ZL_Input_numElts(encodedStream);

    // Delta's codec header contains the first value
    ZL_RESULT_OF(ZL_RBuffer)
    deltaHeaderResult = ZL_DecoderFusion_getCodecHeader(state, 1);
    ZL_ERR_IF(ZL_RES_isError(deltaHeaderResult), corruption);
    ZL_RBuffer const deltaHeader = ZL_RES_value(deltaHeaderResult);

    size_t nbInts        = 0;
    const void* firstPtr = NULL;
    if (deltaHeader.size != 0) {
        ZL_ERR_IF_NE(deltaHeader.size, eltWidth, corruption);
        firstPtr = deltaHeader.start;
        nbInts   = nbDeltas + 1;
    } else {
        ZL_ERR_IF_NE(nbDeltas, (size_t)0, corruption);
    }

    ZL_Output* const out =
            ZL_DecoderFusion_createTypedStream(state, 0, nbInts, eltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    if (nbInts == 0) {
        ZL_ERR_IF_ERR(ZL_Output_commit(out, 0));
        return ZL_returnSuccess();
    }

    void* const dst           = ZL_Output_ptr(out);
    const void* const encoded = ZL_Input_ptr(encodedStream);

    // Fused zigzag decode + delta accumulation per element width
    switch (eltWidth) {
        case 1: {
            uint8_t* d       = (uint8_t*)dst;
            const uint8_t* e = (const uint8_t*)encoded;
            uint8_t first;
            memcpy(&first, firstPtr, 1);
            d[0] = first;
            for (size_t n = 1; n < nbInts; n++) {
                uint8_t z     = e[n - 1];
                uint8_t mask  = (uint8_t)(0U - (z & 0x1U));
                uint8_t delta = (uint8_t)((z >> 1) ^ mask);
                d[n]          = (uint8_t)(d[n - 1] + delta);
            }
        } break;

        case 2: {
            uint16_t* d       = (uint16_t*)dst;
            const uint16_t* e = (const uint16_t*)encoded;
            uint16_t first;
            memcpy(&first, firstPtr, 2);
            d[0] = first;
            for (size_t n = 1; n < nbInts; n++) {
                uint16_t z     = e[n - 1];
                uint16_t mask  = (uint16_t)(0U - (z & 0x1U));
                uint16_t delta = (uint16_t)((z >> 1) ^ mask);
                d[n]           = (uint16_t)(d[n - 1] + delta);
            }
        } break;

        case 4: {
            uint32_t* d       = (uint32_t*)dst;
            const uint32_t* e = (const uint32_t*)encoded;
            uint32_t first;
            memcpy(&first, firstPtr, 4);
            d[0] = first;
            for (size_t n = 1; n < nbInts; n++) {
                uint32_t z     = e[n - 1];
                uint32_t mask  = 0U - (z & 0x1U);
                uint32_t delta = (z >> 1) ^ mask;
                d[n]           = d[n - 1] + delta;
            }
        } break;

        case 8: {
            uint64_t* d       = (uint64_t*)dst;
            const uint64_t* e = (const uint64_t*)encoded;
            uint64_t first;
            memcpy(&first, firstPtr, 8);
            d[0] = first;
            for (size_t n = 1; n < nbInts; n++) {
                uint64_t z     = e[n - 1];
                uint64_t mask  = 0ULL - (z & 0x1ULL);
                uint64_t delta = (z >> 1) ^ mask;
                d[n]           = d[n - 1] + delta;
            }
        } break;

        default:
            ZL_ERR_IF(1 /* always fail */, corruption);
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbInts));

    g_calledCount++;

    return ZL_returnSuccess();
}

// ---------------------------------------------------------------------------
// Fused delta+bitpack+tokenize decoder
//
// During decompression the graph has:
//   delta_int feeding the sorted alphabet into tokenize_numeric port 0
//   bitpack_int feeding the packed indices into tokenize_numeric port 1
//
// This fused decoder replaces all three transforms:
//   codecIndex 0 = delta_int     (root child)  — numeric deltas input
//   codecIndex 1 = bitpack_int   (sibling child) — serial bitpacked input
//   codecIndex 2 = tokenize_numeric (parent) — both inputs NULL
//
// We only handle 4-byte output elements.
// ---------------------------------------------------------------------------

static ZL_Report fusedDeltaBitpackTokenizeDecode(
        ZL_DecoderFusion* state) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->dctx);
    // --- Delta's input (codec index 0) ---
    ZL_RESULT_OF(ZL_CodecInputs)
    deltaInputsResult = ZL_DecoderFusion_getCodecInputs(state, 0);
    ZL_ERR_IF(ZL_RES_isError(deltaInputsResult), corruption);
    ZL_CodecInputs deltaInputs = ZL_RES_value(deltaInputsResult);
    ZL_ERR_IF_NE(deltaInputs.singleton.numInputs, 1, corruption);

    const ZL_Input* const deltaStream = deltaInputs.singleton.inputs[0];
    ZL_ERR_IF_NULL(deltaStream, corruption);
    ZL_ERR_IF_NE(ZL_Input_type(deltaStream), ZL_Type_numeric, corruption);
    size_t const eltWidth = ZL_Input_eltWidth(deltaStream);
    ZL_ERR_IF_NE(eltWidth, 4, corruption); // only handle 4-byte
    size_t const nbDeltas = ZL_Input_numElts(deltaStream);

    // --- Delta's codec header (first value, codec index 0) ---
    ZL_RESULT_OF(ZL_RBuffer)
    deltaHeaderResult = ZL_DecoderFusion_getCodecHeader(state, 0);
    ZL_ERR_IF(ZL_RES_isError(deltaHeaderResult), corruption);
    ZL_RBuffer const deltaHeader = ZL_RES_value(deltaHeaderResult);
    size_t alphabetSize          = 0;
    const void* firstPtr         = NULL;
    if (deltaHeader.size != 0) {
        ZL_ERR_IF_NE(deltaHeader.size, eltWidth, corruption);
        firstPtr     = deltaHeader.start;
        alphabetSize = nbDeltas + 1;
    } else {
        ZL_ERR_IF_NE(nbDeltas, (size_t)0, corruption);
    }

    // --- Bitpack's input (codec index 1) ---
    ZL_RESULT_OF(ZL_CodecInputs)
    bitpackInputsResult = ZL_DecoderFusion_getCodecInputs(state, 1);
    ZL_ERR_IF(ZL_RES_isError(bitpackInputsResult), corruption);
    ZL_CodecInputs bitpackInputs = ZL_RES_value(bitpackInputsResult);
    ZL_ERR_IF_NE(bitpackInputs.singleton.numInputs, 1, corruption);

    const ZL_Input* const packedStream = bitpackInputs.singleton.inputs[0];
    ZL_ERR_IF_NULL(packedStream, corruption);
    ZL_ERR_IF_NE(ZL_Input_type(packedStream), ZL_Type_serial, corruption);
    const uint8_t* packedSrc = (const uint8_t*)ZL_Input_ptr(packedStream);
    size_t packedSize        = ZL_Input_numElts(packedStream);

    // --- Bitpack's codec header (codec index 1) ---
    ZL_RESULT_OF(ZL_RBuffer)
    bpHeaderResult = ZL_DecoderFusion_getCodecHeader(state, 1);
    ZL_ERR_IF(ZL_RES_isError(bpHeaderResult), corruption);
    ZL_RBuffer const bpHeader = ZL_RES_value(bpHeaderResult);
    ZL_ERR_IF(bpHeader.size == 0, corruption);
    ZL_ERR_IF(bpHeader.size > 2, corruption);
    uint8_t const hdrByte = *(const uint8_t*)bpHeader.start;
    size_t const idxWidth = (size_t)1 << ((hdrByte >> 6) & 0x3);
    int const nbBits      = 1 + (hdrByte & 0x3F);

    size_t numElts;
    if (bpHeader.size > 1) {
        size_t const maxNbElts    = (packedSize * 8) / (size_t)nbBits;
        uint8_t const nbExtraElts = ((const uint8_t*)bpHeader.start)[1];
        ZL_ERR_IF(nbExtraElts > maxNbElts, corruption);
        numElts = maxNbElts - nbExtraElts;
    } else {
        numElts = (packedSize * 8) / (size_t)nbBits;
    }

    // --- Tokenize parent (codec index 2) ---
    // Both inputs are NULL (both children are fused). No codec header.
    ZL_RESULT_OF(ZL_CodecInputs)
    tokenizeInputsResult = ZL_DecoderFusion_getCodecInputs(state, 2);
    ZL_ERR_IF(ZL_RES_isError(tokenizeInputsResult), corruption);
    ZL_CodecInputs tokenizeInputs = ZL_RES_value(tokenizeInputsResult);
    ZL_ERR_IF_NE(tokenizeInputs.singleton.numInputs, 2, corruption);
    ZL_ERR_IF(tokenizeInputs.singleton.inputs[0] != NULL, corruption);
    ZL_ERR_IF(tokenizeInputs.singleton.inputs[1] != NULL, corruption);

    // --- Reconstruct alphabet via delta decoding ---
    uint32_t* alphabet = NULL;
    if (alphabetSize > 0) {
        alphabet = (uint32_t*)ZL_DecoderFusion_getScratchSpace(
                state, alphabetSize * sizeof(uint32_t));
        ZL_ERR_IF_NULL(alphabet, allocation);

        uint32_t first;
        memcpy(&first, firstPtr, 4);
        alphabet[0] = first;

        const uint32_t* deltas = (const uint32_t*)ZL_Input_ptr(deltaStream);
        for (size_t i = 1; i < alphabetSize; i++) {
            alphabet[i] = alphabet[i - 1] + deltas[i - 1];
        }
    }

    // --- Unpack bitpacked indices ---
    uint32_t* indices = NULL;
    if (numElts > 0) {
        indices = (uint32_t*)ZL_DecoderFusion_getScratchSpace(
                state, numElts * sizeof(uint32_t));
        ZL_ERR_IF_NULL(indices, allocation);

        size_t bitPos           = 0;
        size_t const bitsPerElt = (size_t)nbBits;
        size_t const maxVal =
                idxWidth == 8 ? SIZE_MAX : ((size_t)1 << (idxWidth * 8)) - 1;
        (void)maxVal;
        for (size_t i = 0; i < numElts; i++) {
            uint32_t val = 0;
            for (size_t b = 0; b < bitsPerElt; b++) {
                size_t byteIdx = bitPos / 8;
                size_t bitIdx  = bitPos % 8;
                val |= (uint32_t)(((packedSrc[byteIdx] >> bitIdx) & 1) << b);
                bitPos++;
            }
            indices[i] = val;
        }
        ZL_ERR_IF_NE((bitPos + 7) / 8, packedSize, corruption);
    }

    // --- Create output and apply tokenize decode ---
    ZL_Output* const out =
            ZL_DecoderFusion_createTypedStream(state, 0, numElts, eltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    if (numElts == 0) {
        ZL_ERR_IF_ERR(ZL_Output_commit(out, 0));
        g_calledCount++;
        return ZL_returnSuccess();
    }

    uint32_t* dst = (uint32_t*)ZL_Output_ptr(out);
    for (size_t i = 0; i < numElts; i++) {
        uint32_t idx = indices[i];
        ZL_ERR_IF(idx >= alphabetSize, corruption);
        dst[i] = alphabet[idx];
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(out, numElts));

    g_calledCount++;

    return ZL_returnSuccess();
}

// ---------------------------------------------------------------------------
// Fused constant+transpose_split decoder
//
// During decompression the graph has two constant_serial decoders feeding
// byte lanes into transpose_split's VO inputs (one per byte lane, width=2).
//
// This fused decoder replaces all three transforms:
//   codecIndex 0 = constant_serial (root child, lane 0)
//   codecIndex 1 = constant_serial (sibling child, lane 1)
//   codecIndex 2 = transpose_split (parent, both VO inputs NULL)
//
// We only handle width=2 (2 byte lanes).
// ---------------------------------------------------------------------------

static ZL_Report fusedConstantTransposeSplitDecode(
        ZL_DecoderFusion* state) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->dctx);
    // --- Decode each constant_serial child ---
    uint8_t laneValues[2];
    size_t laneCounts[2];

    for (size_t ci = 0; ci < 2; ci++) {
        // Read stored input (1-element serial stream)
        ZL_RESULT_OF(ZL_CodecInputs)
        inputsResult = ZL_DecoderFusion_getCodecInputs(state, ci);
        ZL_ERR_IF(ZL_RES_isError(inputsResult), corruption);
        ZL_CodecInputs inputs = ZL_RES_value(inputsResult);
        ZL_ERR_IF_NE(inputs.singleton.numInputs, 1, corruption);

        const ZL_Input* const stream = inputs.singleton.inputs[0];
        ZL_ERR_IF_NULL(stream, corruption);
        ZL_ERR_IF_NE(ZL_Input_type(stream), ZL_Type_serial, corruption);
        ZL_ERR_IF_NE(ZL_Input_numElts(stream), 1, corruption);
        laneValues[ci] = *(const uint8_t*)ZL_Input_ptr(stream);

        // Codec header contains a varint encoding the original element count
        ZL_RESULT_OF(ZL_RBuffer)
        hdrResult = ZL_DecoderFusion_getCodecHeader(state, ci);
        ZL_ERR_IF(ZL_RES_isError(hdrResult), corruption);
        ZL_RBuffer const hdr = ZL_RES_value(hdrResult);
        ZL_ERR_IF(hdr.size == 0, corruption);
        const uint8_t* hdrBytes = (const uint8_t*)hdr.start;
        size_t count            = 0;
        size_t shift            = 0;
        size_t pos              = 0;
        for (;;) {
            ZL_ERR_IF(pos >= hdr.size, corruption);
            uint8_t byte = hdrBytes[pos++];
            count |= (size_t)(byte & 0x7F) << shift;
            shift += 7;
            if (!(byte & 0x80))
                break;
        }
        ZL_ERR_IF(count == 0, corruption);
        ZL_ERR_IF_NE(pos, hdr.size, corruption);
        laneCounts[ci] = count;
    }

    // Both lanes must have the same element count
    size_t const count0 = laneCounts[0];
    size_t const count1 = laneCounts[1];
    ZL_ERR_IF_NE(count0, count1, corruption);
    size_t const nbElts = count0;

    // Verify parent has all-NULL inputs (both children fused)
    ZL_RESULT_OF(ZL_CodecInputs)
    parentInputsResult = ZL_DecoderFusion_getCodecInputs(state, 2);
    ZL_ERR_IF(ZL_RES_isError(parentInputsResult), corruption);
    ZL_CodecInputs parentInputs = ZL_RES_value(parentInputsResult);
    ZL_ERR_IF_NE(parentInputs.variable.numInputs, 2, corruption);
    ZL_ERR_IF(parentInputs.variable.inputs[0] != NULL, corruption);
    ZL_ERR_IF(parentInputs.variable.inputs[1] != NULL, corruption);

    // Create output: struct stream, width=2
    ZL_Output* const out =
            ZL_DecoderFusion_createTypedStream(state, 0, nbElts, 2);
    ZL_ERR_IF_NULL(out, allocation);

    uint8_t* dst = (uint8_t*)ZL_Output_ptr(out);
    for (size_t i = 0; i < nbElts; i++) {
        dst[i * 2]     = laneValues[0];
        dst[i * 2 + 1] = laneValues[1];
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbElts));

    g_calledCount++;

    return ZL_returnSuccess();
}

// ---------------------------------------------------------------------------
// runCodec variants for zigzag+delta fusion
//
// These test ZL_DecoderFusion_runCodec() in three configurations:
//   1. runCodec on child (zigzag) only, parent (delta) implemented directly
//   2. runCodec on parent (delta) only, child (zigzag) implemented directly
//   3. runCodec on both child and parent
// ---------------------------------------------------------------------------

// Case 1: runCodec for zigzag child, implement delta prefix-sum directly
static ZL_Report fusedZigzagDelta_runCodecChild(
        ZL_DecoderFusion* state) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->dctx);
    // Run zigzag decoder via runCodec
    ZL_RESULT_OF(ZL_InputArray)
    zigzagOutputResult = ZL_DecoderFusion_runCodec(state, 0);
    ZL_ERR_IF(ZL_RES_isError(zigzagOutputResult), corruption);
    ZL_InputArray zigzagOutputs = ZL_RES_value(zigzagOutputResult);
    ZL_ERR_IF_NE(zigzagOutputs.numInputs, 1, corruption);

    const ZL_Input* const decodedDeltas = zigzagOutputs.inputs[0];
    ZL_ERR_IF_NULL(decodedDeltas, corruption);
    ZL_ERR_IF_NE(ZL_Input_type(decodedDeltas), ZL_Type_numeric, corruption);

    size_t const eltWidth = ZL_Input_eltWidth(decodedDeltas);
    size_t const nbDeltas = ZL_Input_numElts(decodedDeltas);

    // Read delta's codec header (first value)
    ZL_RESULT_OF(ZL_RBuffer)
    deltaHeaderResult = ZL_DecoderFusion_getCodecHeader(state, 1);
    ZL_ERR_IF(ZL_RES_isError(deltaHeaderResult), corruption);
    ZL_RBuffer const deltaHeader = ZL_RES_value(deltaHeaderResult);

    size_t nbInts        = 0;
    const void* firstPtr = NULL;
    if (deltaHeader.size != 0) {
        ZL_ERR_IF_NE(deltaHeader.size, eltWidth, corruption);
        firstPtr = deltaHeader.start;
        nbInts   = nbDeltas + 1;
    } else {
        ZL_ERR_IF_NE(nbDeltas, (size_t)0, corruption);
    }

    ZL_Output* const out =
            ZL_DecoderFusion_createTypedStream(state, 0, nbInts, eltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    if (nbInts == 0) {
        ZL_ERR_IF_ERR(ZL_Output_commit(out, 0));
        return ZL_returnSuccess();
    }

    // Apply prefix-sum (delta decode) on the zigzag-decoded deltas
    // Only implement 4-byte case for simplicity (tests use 4-byte)
    ZL_ERR_IF_NE(eltWidth, 4, corruption);
    uint32_t* dst          = (uint32_t*)ZL_Output_ptr(out);
    const uint32_t* deltas = (const uint32_t*)ZL_Input_ptr(decodedDeltas);
    uint32_t first;
    memcpy(&first, firstPtr, 4);
    dst[0] = first;
    for (size_t n = 1; n < nbInts; n++) {
        dst[n] = dst[n - 1] + deltas[n - 1];
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbInts));
    g_calledCount++;
    return ZL_returnSuccess();
}

// Case 2: implement zigzag directly, then runCodec for delta parent
static ZL_Report fusedZigzagDelta_runCodecParent(
        ZL_DecoderFusion* state) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->dctx);
    // Get zigzag's stored input
    ZL_RESULT_OF(ZL_CodecInputs)
    zigzagInputsResult = ZL_DecoderFusion_getCodecInputs(state, 0);
    ZL_ERR_IF(ZL_RES_isError(zigzagInputsResult), corruption);
    ZL_CodecInputs zigzagInputs = ZL_RES_value(zigzagInputsResult);
    ZL_ERR_IF_NE(zigzagInputs.singleton.numInputs, 1, corruption);

    const ZL_Input* const encodedStream = zigzagInputs.singleton.inputs[0];
    ZL_ERR_IF_NULL(encodedStream, corruption);
    ZL_ERR_IF_NE(ZL_Input_type(encodedStream), ZL_Type_numeric, corruption);

    size_t const eltWidth = ZL_Input_eltWidth(encodedStream);
    size_t const nbElts   = ZL_Input_numElts(encodedStream);

    // Compute the regen stream ID for zigzag's output so we can store it
    // in the DCtx where delta's standard decoder expects to find it.
    const DFH_NodeInfo* childNodeInfo = state->nodeInfos[0];
    const ZL_IDType inputEndIdx =
            childNodeInfo->inputStreamBaseIdx + childNodeInfo->numInputStreams;
    const ZL_IDType regenIdx = inputEndIdx + childNodeInfo->regenDistances[0];

    // Create the output stream at zigzag's regen slot
    ZL_Data* zigzagOut = DCTX_newStream(
            state->dctx, regenIdx, ZL_Type_numeric, eltWidth, nbElts);
    ZL_ERR_IF_NULL(zigzagOut, allocation);

    // Apply zigzag decode (only 4-byte for simplicity)
    ZL_ERR_IF_NE(eltWidth, 4, corruption);
    uint32_t* dst           = (uint32_t*)ZL_Data_wPtr(zigzagOut);
    const uint32_t* encoded = (const uint32_t*)ZL_Input_ptr(encodedStream);
    for (size_t i = 0; i < nbElts; i++) {
        uint32_t z     = encoded[i];
        uint32_t mask  = 0U - (z & 0x1U);
        uint32_t delta = (z >> 1) ^ mask;
        dst[i]         = delta;
    }
    ZL_ERR_IF_ERR(ZL_Data_commit(zigzagOut, nbElts));

    // Now run delta's standard decoder via runCodec
    ZL_RESULT_OF(ZL_InputArray)
    deltaOutputResult = ZL_DecoderFusion_runCodec(state, 1);
    ZL_ERR_IF(ZL_RES_isError(deltaOutputResult), corruption);

    g_calledCount++;
    return ZL_returnSuccess();
}

// Case 3: runCodec on both child (zigzag) and parent (delta)
static ZL_Report fusedZigzagDelta_runCodecBoth(ZL_DecoderFusion* state) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->dctx);
    // Run zigzag child via runCodec — this stores its output in the DCtx
    ZL_RESULT_OF(ZL_InputArray)
    zigzagOutputResult = ZL_DecoderFusion_runCodec(state, 0);
    ZL_ERR_IF(ZL_RES_isError(zigzagOutputResult), corruption);

    // Run delta parent via runCodec — finds zigzag output in DCtx, produces
    // the final output
    ZL_RESULT_OF(ZL_InputArray)
    deltaOutputResult = ZL_DecoderFusion_runCodec(state, 1);
    ZL_ERR_IF(ZL_RES_isError(deltaOutputResult), corruption);

    g_calledCount++;
    return ZL_returnSuccess();
}

// ---------------------------------------------------------------------------
// runCodec partial variant for delta+bitpack+tokenize fusion
//
// 3 codecs: delta_int (0), bitpack_int (1), tokenize_numeric (2)
// Manually decode delta_int (child 0), runCodec for bitpack_int (child 1)
// and tokenize_numeric (parent).
// ---------------------------------------------------------------------------

static ZL_Report fusedDeltaBitpackTokenize_runCodecPartial(
        ZL_DecoderFusion* state) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->dctx);
    // --- Manually decode delta_int (codec index 0) ---
    ZL_RESULT_OF(ZL_CodecInputs)
    deltaInputsResult = ZL_DecoderFusion_getCodecInputs(state, 0);
    ZL_ERR_IF(ZL_RES_isError(deltaInputsResult), corruption);
    ZL_CodecInputs deltaInputs = ZL_RES_value(deltaInputsResult);
    ZL_ERR_IF_NE(deltaInputs.singleton.numInputs, 1, corruption);

    const ZL_Input* const deltaStream = deltaInputs.singleton.inputs[0];
    ZL_ERR_IF_NULL(deltaStream, corruption);
    ZL_ERR_IF_NE(ZL_Input_type(deltaStream), ZL_Type_numeric, corruption);
    size_t const eltWidth = ZL_Input_eltWidth(deltaStream);
    ZL_ERR_IF_NE(eltWidth, 4, corruption);
    size_t const nbDeltas = ZL_Input_numElts(deltaStream);

    // Delta codec header (first value)
    ZL_RESULT_OF(ZL_RBuffer)
    deltaHeaderResult = ZL_DecoderFusion_getCodecHeader(state, 0);
    ZL_ERR_IF(ZL_RES_isError(deltaHeaderResult), corruption);
    ZL_RBuffer const deltaHeader = ZL_RES_value(deltaHeaderResult);

    size_t alphabetSize  = 0;
    const void* firstPtr = NULL;
    if (deltaHeader.size != 0) {
        ZL_ERR_IF_NE(deltaHeader.size, eltWidth, corruption);
        firstPtr     = deltaHeader.start;
        alphabetSize = nbDeltas + 1;
    } else {
        ZL_ERR_IF_NE(nbDeltas, (size_t)0, corruption);
    }

    // Compute delta's regen stream ID and create output in DCtx
    const DFH_NodeInfo* deltaNodeInfo = state->nodeInfos[0];
    const ZL_IDType deltaInputEnd =
            deltaNodeInfo->inputStreamBaseIdx + deltaNodeInfo->numInputStreams;
    const ZL_IDType deltaRegenIdx =
            deltaInputEnd + deltaNodeInfo->regenDistances[0];

    ZL_Data* deltaOut = DCTX_newStream(
            state->dctx,
            deltaRegenIdx,
            ZL_Type_numeric,
            eltWidth,
            alphabetSize);
    ZL_ERR_IF_NULL(deltaOut, allocation);

    if (alphabetSize > 0) {
        uint32_t* dst          = (uint32_t*)ZL_Data_wPtr(deltaOut);
        const uint32_t* deltas = (const uint32_t*)ZL_Input_ptr(deltaStream);
        uint32_t first;
        memcpy(&first, firstPtr, 4);
        dst[0] = first;
        for (size_t i = 1; i < alphabetSize; i++) {
            dst[i] = dst[i - 1] + deltas[i - 1];
        }
    }
    ZL_ERR_IF_ERR(ZL_Data_commit(deltaOut, alphabetSize));

    // --- runCodec for bitpack_int (codec index 1) ---
    ZL_RESULT_OF(ZL_InputArray)
    bitpackResult = ZL_DecoderFusion_runCodec(state, 1);
    ZL_ERR_IF(ZL_RES_isError(bitpackResult), corruption);

    // --- runCodec for tokenize_numeric parent (codec index 2) ---
    ZL_RESULT_OF(ZL_InputArray)
    tokenizeResult = ZL_DecoderFusion_runCodec(state, 2);
    ZL_ERR_IF(ZL_RES_isError(tokenizeResult), corruption);

    g_calledCount++;
    return ZL_returnSuccess();
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class CodecFusionTest : public ::testing::Test {
   protected:
    void SetUp() override
    {
        compressor_   = Compressor();
        dctx_         = DCtx();
        g_calledCount = 0;
    }

    void setTestParams(CCtx& cctx)
    {
        cctx.setParameter(CParam::MinStreamSize, -1);
        cctx.setParameter(CParam::StoreOnExpansion, ZL_TernaryParam_disable);
    }

    /** Build a pipeline: interpret_LE -> delta -> zigzag -> num_to_serial ->
     * store */
    ZL_GraphID buildDeltaZigzagGraph(size_t eltWidth)
    {
        EXPECT_FALSE(ZL_isError(ZL_Compressor_setParameter(
                compressor_.get(),
                ZL_CParam_formatVersion,
                ZL_MAX_FORMAT_VERSION)));

        ZL_NodeID interpretNode;
        switch (eltWidth) {
            case 1:
                interpretNode = ZL_NODE_INTERPRET_AS_LE8;
                break;
            case 2:
                interpretNode = ZL_NODE_INTERPRET_AS_LE16;
                break;
            case 4:
                interpretNode = ZL_NODE_INTERPRET_AS_LE32;
                break;
            case 8:
                interpretNode = ZL_NODE_INTERPRET_AS_LE64;
                break;
            default:
                ADD_FAILURE();
                return {};
        }

        ZL_GraphID const toSerial =
                ZL_Compressor_registerStaticGraph_fromNode1o(
                        compressor_.get(),
                        ZL_NODE_CONVERT_NUM_TO_SERIAL,
                        ZL_GRAPH_STORE);
        ZL_GraphID const zigzag = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_.get(), ZL_NODE_ZIGZAG, toSerial);
        ZL_GraphID const delta = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_.get(), ZL_NODE_DELTA_INT, zigzag);
        ZL_GraphID const graph = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_.get(), interpretNode, delta);
        return graph;
    }

    /** Register the fused zigzag+delta decoder on the DCtx */
    void registerFusion()
    {
        uint32_t parentIdx0[]            = { 0 };
        ZL_DecoderFusionChild children[] = {
            { .codec         = ZL_StandardTransformID_zigzag,
              .numRegens     = 1,
              .parentIndices = parentIdx0 },
        };
        ZL_DecoderFusionDesc desc = {
            .pattern = {
                .parentCodec = ZL_StandardTransformID_delta_int,
                .numChildren = 1,
                .children    = children,
            },
            .fusionFn = fusedZigzagDeltaDecode,
        };
        ZL_Report r = DCTX_registerDecoderFusion(dctx_.get(), &desc);
        ASSERT_FALSE(ZL_isError(r)) << "Failed to register codec fusion";
    }

    /** Register a fusion with a custom fusionFn for zigzag+delta */
    void registerFusionWith(ZL_DecoderFusionFn fn)
    {
        uint32_t parentIdx0[]            = { 0 };
        ZL_DecoderFusionChild children[] = {
            { .codec         = ZL_StandardTransformID_zigzag,
              .numRegens     = 1,
              .parentIndices = parentIdx0 },
        };
        ZL_DecoderFusionDesc desc = {
            .pattern = {
                .parentCodec = ZL_StandardTransformID_delta_int,
                .numChildren = 1,
                .children    = children,
            },
            .fusionFn = fn,
        };
        ZL_Report r = DCTX_registerDecoderFusion(dctx_.get(), &desc);
        ASSERT_FALSE(ZL_isError(r)) << "Failed to register codec fusion";
    }

    /** Compress -> decompress -> verify round trip */
    void roundTrip(const void* input, size_t inputSize, ZL_GraphID graph)
    {
        ASSERT_FALSE(ZL_isError(
                ZL_Compressor_selectStartingGraphID(compressor_.get(), graph)));

        std::string compressed(ZL_compressBound(inputSize), '\0');
        CCtx cctx;
        setTestParams(cctx);
        ASSERT_FALSE(ZL_isError(
                ZL_CCtx_refCompressor(cctx.get(), compressor_.get())));
        ZL_Report const cSize = ZL_CCtx_compress(
                cctx.get(),
                compressed.data(),
                compressed.size(),
                input,
                inputSize);
        ASSERT_FALSE(ZL_isError(cSize))
                << "Compression failed: "
                << ZL_CCtx_getErrorContextString(cctx.get(), cSize);
        compressed.resize(ZL_validResult(cSize));

        std::string decompressed(inputSize, '\0');
        ZL_Report const dSize = ZL_DCtx_decompress(
                dctx_.get(),
                decompressed.data(),
                decompressed.size(),
                compressed.data(),
                compressed.size());
        ASSERT_FALSE(ZL_isError(dSize))
                << "Decompression failed: "
                << ZL_DCtx_getErrorContextString(dctx_.get(), dSize);
        ASSERT_EQ(ZL_validResult(dSize), inputSize);
        if (inputSize > 0) {
            ASSERT_EQ(memcmp(input, decompressed.data(), inputSize), 0)
                    << "Round-trip data mismatch";
        }
    }

    void testWithEltWidth(size_t eltWidth)
    {
        EXPECT_EQ(g_calledCount, 0);
        ZL_GraphID graph = buildDeltaZigzagGraph(eltWidth);
        registerFusion();

        // Test with sequentially increasing integers
        std::vector<uint8_t> data(78 * eltWidth);
        for (size_t i = 0; i < 78; i++) {
            uint64_t val = i;
            memcpy(data.data() + i * eltWidth, &val, eltWidth);
        }
        roundTrip(data.data(), data.size(), graph);
        EXPECT_EQ(g_calledCount, 1);

        // Test with empty input
        roundTrip(nullptr, 0, graph);
        EXPECT_EQ(g_calledCount, 1);

        // Test with a single element
        uint64_t single = 42;
        roundTrip(&single, eltWidth, graph);
        EXPECT_EQ(g_calledCount, 2);

        // Test with alternating positive/negative pattern (good for zigzag)
        std::vector<uint8_t> altData(64 * eltWidth);
        for (size_t i = 0; i < 64; i++) {
            int64_t val = (int64_t)(i & 1 ? -(int64_t)(i + 1) : (int64_t)i);
            uint64_t uval;
            memcpy(&uval, &val, sizeof(val));
            memcpy(altData.data() + i * eltWidth, &uval, eltWidth);
        }
        roundTrip(altData.data(), altData.size(), graph);
        EXPECT_EQ(g_calledCount, 3);
    }

    /** Test round trip WITHOUT fusion (baseline correctness check) */
    void testWithoutFusion(size_t eltWidth)
    {
        ZL_GraphID graph = buildDeltaZigzagGraph(eltWidth);
        // Do NOT register fusion — standard decoders used

        std::vector<uint8_t> data(78 * eltWidth);
        for (size_t i = 0; i < 78; i++) {
            uint64_t val = i;
            memcpy(data.data() + i * eltWidth, &val, eltWidth);
        }
        roundTrip(data.data(), data.size(), graph);
        EXPECT_EQ(g_calledCount, 0);
    }

    /** Build a pipeline:
     * interpret_LE32 -> tokenize_numeric(sorted)
     *   -> [alphabet] -> delta -> num_to_serial -> store
     *   -> [indices]  -> bitpack -> store
     */
    ZL_GraphID buildTokenizeDeltaBitpackGraph()
    {
        EXPECT_FALSE(ZL_isError(ZL_Compressor_setParameter(
                compressor_.get(),
                ZL_CParam_formatVersion,
                ZL_MAX_FORMAT_VERSION)));

        // Output 0 (alphabet, numeric): delta -> num_to_serial -> store
        ZL_GraphID const toSerial =
                ZL_Compressor_registerStaticGraph_fromNode1o(
                        compressor_.get(),
                        ZL_NODE_CONVERT_NUM_TO_SERIAL,
                        ZL_GRAPH_STORE);
        ZL_GraphID const alphabetGraph =
                ZL_Compressor_registerStaticGraph_fromNode1o(
                        compressor_.get(), ZL_NODE_DELTA_INT, toSerial);

        // Output 1 (indices, numeric): bitpack -> store
        ZL_GraphID const indicesGraph = ZL_GRAPH_BITPACK;

        // Tokenize graph (sorted)
        ZL_GraphID const tokenizeGraph = ZL_Compressor_registerTokenizeGraph(
                compressor_.get(),
                ZL_Type_numeric,
                true, // sorted
                alphabetGraph,
                indicesGraph);
        EXPECT_NE(tokenizeGraph.gid, ZL_GRAPH_ILLEGAL.gid);

        // Wrap with interpret_as_le32
        ZL_GraphID const graph = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_.get(), ZL_NODE_INTERPRET_AS_LE32, tokenizeGraph);
        return graph;
    }

    /** Register the fused delta+bitpack+tokenize decoder on the DCtx */
    void registerTokenizeDeltaBitpackFusion()
    {
        registerTokenizeDeltaBitpackFusionWith(fusedDeltaBitpackTokenizeDecode);
    }

    void registerTokenizeDeltaBitpackFusionWith(ZL_DecoderFusionFn fn)
    {
        uint32_t parentIdx0[]            = { 0 };
        uint32_t parentIdx1[]            = { 1 };
        ZL_DecoderFusionChild children[] = {
            { .codec         = ZL_StandardTransformID_delta_int,
              .numRegens     = 1,
              .parentIndices = parentIdx0 },
            { .codec         = ZL_StandardTransformID_bitpack_int,
              .numRegens     = 1,
              .parentIndices = parentIdx1 },
        };
        ZL_DecoderFusionDesc desc = {
            .pattern = {
                .parentCodec = ZL_StandardTransformID_tokenize_numeric,
                .numChildren = 2,
                .children    = children,
            },
            .fusionFn = fn,
        };
        ZL_Report r = DCTX_registerDecoderFusion(dctx_.get(), &desc);
        ASSERT_FALSE(ZL_isError(r))
                << "Failed to register delta+bitpack+tokenize fusion";
    }

    /** Build a pipeline:
     * serial_to_struct2 -> transpose_split -> [each lane] -> constant -> store
     */
    ZL_GraphID buildTransposeSplitConstantGraph()
    {
        EXPECT_FALSE(ZL_isError(ZL_Compressor_setParameter(
                compressor_.get(),
                ZL_CParam_formatVersion,
                ZL_MAX_FORMAT_VERSION)));

        // Each byte lane goes through constant -> store
        ZL_GraphID const laneGraph = ZL_GRAPH_CONSTANT;

        // transpose_split with constant successor for all VO outputs
        ZL_GraphID const transposeGraph =
                ZL_Compressor_registerTransposeSplitGraph(
                        compressor_.get(), laneGraph);

        // Interpret raw bytes as 2-byte structs
        ZL_GraphID const graph = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_.get(),
                ZL_NODE_CONVERT_SERIAL_TO_STRUCT2,
                transposeGraph);
        return graph;
    }

    /** Register the fused constant+transpose_split decoder on the DCtx */
    void registerTransposeSplitConstantFusion()
    {
        uint32_t parentIdx0[]            = { 0 };
        uint32_t parentIdx1[]            = { 1 };
        ZL_DecoderFusionChild children[] = {
            { .codec         = ZL_StandardTransformID_constant_serial,
              .numRegens     = 1,
              .parentIndices = parentIdx0 },
            { .codec         = ZL_StandardTransformID_constant_serial,
              .numRegens     = 1,
              .parentIndices = parentIdx1 },
        };
        ZL_DecoderFusionDesc desc = {
            .pattern = {
                .parentCodec = ZL_StandardTransformID_transpose_split,
                .numChildren = 2,
                .children    = children,
            },
            .fusionFn = fusedConstantTransposeSplitDecode,
        };
        ZL_Report r = DCTX_registerDecoderFusion(dctx_.get(), &desc);
        ASSERT_FALSE(ZL_isError(r))
                << "Failed to register constant+transpose_split fusion";
    }

    Compressor compressor_;
    DCtx dctx_;
};

// ---------------------------------------------------------------------------
// Tests — baseline (no fusion)
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, BaselineNoFusion32)
{
    testWithoutFusion(4);
}

// ---------------------------------------------------------------------------
// Tests — with fused zigzag+delta decoder
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, FusedZigzagDelta8)
{
    testWithEltWidth(1);
}

TEST_F(CodecFusionTest, FusedZigzagDelta16)
{
    testWithEltWidth(2);
}

TEST_F(CodecFusionTest, FusedZigzagDelta32)
{
    testWithEltWidth(4);
}

TEST_F(CodecFusionTest, FusedZigzagDelta64)
{
    testWithEltWidth(8);
}

// ---------------------------------------------------------------------------
// Test — larger data with random-ish content
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, FusedZigzagDeltaLargeData)
{
    ZL_GraphID graph = buildDeltaZigzagGraph(4);
    registerFusion();

    // Generate 4096 integers with a pattern that exercises delta+zigzag well
    constexpr size_t N = 4096;
    std::vector<uint32_t> data(N);
    uint32_t val = 1000;
    for (size_t i = 0; i < N; i++) {
        // Pseudo-random walk: the deltas oscillate around zero
        val += (uint32_t)((int32_t)(i * 7 + 13) % 31 - 15);
        data[i] = val;
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    ASSERT_EQ(g_calledCount, 1);
}

// ---------------------------------------------------------------------------
// Tests — fused delta+bitpack+tokenize decoder (parent + 2 children)
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, BaselineTokenizeNoFusion)
{
    ZL_GraphID graph = buildTokenizeDeltaBitpackGraph();
    // Do NOT register fusion — standard decoders used

    // Data with repeated values (good for tokenization)
    constexpr size_t N = 128;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i % 10) * 100;
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 0);
}

TEST_F(CodecFusionTest, FusedDeltaBitpackTokenize)
{
    ZL_GraphID graph = buildTokenizeDeltaBitpackGraph();
    registerTokenizeDeltaBitpackFusion();

    // Data with repeated values from a small sorted vocabulary
    constexpr size_t N = 128;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i % 10) * 100;
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 1);
}

TEST_F(CodecFusionTest, FusedDeltaBitpackTokenizeAllSame)
{
    ZL_GraphID graph = buildTokenizeDeltaBitpackGraph();
    registerTokenizeDeltaBitpackFusion();

    // All identical values: alphabet has 1 entry, all indices are 0
    constexpr size_t N = 64;
    std::vector<uint32_t> data(N, 42);
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 1);
}

TEST_F(CodecFusionTest, FusedDeltaBitpackTokenizeLargeAlphabet)
{
    ZL_GraphID graph = buildTokenizeDeltaBitpackGraph();
    registerTokenizeDeltaBitpackFusion();

    // Many unique values: alphabet is large
    constexpr size_t N = 512;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i * 7);
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 1);
}

TEST_F(CodecFusionTest, FusedDeltaBitpackTokenizeSingleElement)
{
    ZL_GraphID graph = buildTokenizeDeltaBitpackGraph();
    registerTokenizeDeltaBitpackFusion();

    uint32_t single = 999;
    roundTrip(&single, sizeof(single), graph);
    EXPECT_EQ(g_calledCount, 1);
}

// ---------------------------------------------------------------------------
// Tests — fused constant+transpose_split decoder (VO parent + 2 children)
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, BaselineTransposeSplitNoFusion)
{
    ZL_GraphID graph = buildTransposeSplitConstantGraph();
    // Do NOT register fusion — standard decoders used

    // All-identical 2-byte elements (required for constant codec)
    constexpr size_t N = 128;
    uint16_t val       = 0x1234;
    std::vector<uint16_t> data(N, val);
    roundTrip(data.data(), data.size() * sizeof(uint16_t), graph);
    EXPECT_EQ(g_calledCount, 0);
}

TEST_F(CodecFusionTest, FusedConstantTransposeSplit)
{
    ZL_GraphID graph = buildTransposeSplitConstantGraph();
    registerTransposeSplitConstantFusion();

    // All-identical 2-byte elements
    constexpr size_t N = 128;
    uint16_t val       = 0xABCD;
    std::vector<uint16_t> data(N, val);
    roundTrip(data.data(), data.size() * sizeof(uint16_t), graph);
    EXPECT_EQ(g_calledCount, 1);
}

TEST_F(CodecFusionTest, FusedConstantTransposeSplitZeroValue)
{
    ZL_GraphID graph = buildTransposeSplitConstantGraph();
    registerTransposeSplitConstantFusion();

    // All zeros
    constexpr size_t N = 64;
    std::vector<uint16_t> data(N, 0);
    roundTrip(data.data(), data.size() * sizeof(uint16_t), graph);
    EXPECT_EQ(g_calledCount, 1);
}

TEST_F(CodecFusionTest, FusedConstantTransposeSplitLargeData)
{
    ZL_GraphID graph = buildTransposeSplitConstantGraph();
    registerTransposeSplitConstantFusion();

    // Large run of identical 2-byte elements
    constexpr size_t N = 8192;
    uint16_t val       = 0xFF01;
    std::vector<uint16_t> data(N, val);
    roundTrip(data.data(), data.size() * sizeof(uint16_t), graph);
    EXPECT_EQ(g_calledCount, 1);
}

TEST_F(CodecFusionTest, FusedConstantTransposeSplitSingleElement)
{
    ZL_GraphID graph = buildTransposeSplitConstantGraph();
    registerTransposeSplitConstantFusion();

    uint16_t single = 0x4242;
    roundTrip(&single, sizeof(single), graph);
    EXPECT_EQ(g_calledCount, 1);
}

// ---------------------------------------------------------------------------
// Tests — runCodec variants (zigzag+delta with runCodec delegation)
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, RunCodecChildOnly)
{
    ZL_GraphID graph = buildDeltaZigzagGraph(4);
    registerFusionWith(fusedZigzagDelta_runCodecChild);

    constexpr size_t N = 128;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i * 3 + 7);
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 1);
}

TEST_F(CodecFusionTest, RunCodecParentOnly)
{
    ZL_GraphID graph = buildDeltaZigzagGraph(4);
    registerFusionWith(fusedZigzagDelta_runCodecParent);

    constexpr size_t N = 128;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i * 3 + 7);
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 1);
}

TEST_F(CodecFusionTest, RunCodecBoth)
{
    ZL_GraphID graph = buildDeltaZigzagGraph(4);
    registerFusionWith(fusedZigzagDelta_runCodecBoth);

    constexpr size_t N = 128;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i * 3 + 7);
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 1);
}

// ---------------------------------------------------------------------------
// Test — runCodec partial (2-child fusion: manual delta, runCodec
// bitpack+tokenize)
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, RunCodecPartialTwoChildren)
{
    ZL_GraphID graph = buildTokenizeDeltaBitpackGraph();
    registerTokenizeDeltaBitpackFusionWith(
            fusedDeltaBitpackTokenize_runCodecPartial);

    constexpr size_t N = 128;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i % 10) * 100;
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 1);
}

// ---------------------------------------------------------------------------
// Test — multiple fusions with different parents (exercises bug 5 fix)
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, MultipleFusionsDifferentParents)
{
    // Register both zigzag+delta and delta+bitpack+tokenize fusions on the same
    // DCtx
    registerFusion();
    registerTokenizeDeltaBitpackFusion();

    // Round trip with delta+zigzag graph
    {
        ZL_GraphID graph   = buildDeltaZigzagGraph(4);
        constexpr size_t N = 64;
        std::vector<uint32_t> data(N);
        for (size_t i = 0; i < N; i++) {
            data[i] = (uint32_t)(i * 3 + 7);
        }
        roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
        EXPECT_EQ(g_calledCount, 1);
    }

    // Round trip with tokenize+delta+bitpack graph (same DCtx, both fusions
    // registered)
    {
        ZL_GraphID graph   = buildTokenizeDeltaBitpackGraph();
        constexpr size_t N = 64;
        std::vector<uint32_t> data(N);
        for (size_t i = 0; i < N; i++) {
            data[i] = (uint32_t)(i * 503 % 0x10000);
        }
        roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
        EXPECT_EQ(g_calledCount, 2);
    }
}

// ---------------------------------------------------------------------------
// Test — multiple fusions with the same parent codec ID (exercises bug 5 fix)
// ---------------------------------------------------------------------------

// A dummy fusion function — should never be called
static ZL_Report dummyFusionFn(ZL_DecoderFusion* state) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->dctx);
    ZL_ERR(logicError);
    return ZL_returnSuccess();
}

TEST_F(CodecFusionTest, MultipleFusionsSameParent)
{
    // Register a fake zigzag+delta fusion that expects 2 children (won't match)
    {
        uint32_t parentIdx0[]            = { 0 };
        uint32_t parentIdx1[]            = { 1 };
        ZL_DecoderFusionChild children[] = {
            { .codec         = ZL_StandardTransformID_zigzag,
              .numRegens     = 1,
              .parentIndices = parentIdx0 },
            { .codec         = ZL_StandardTransformID_bitpack_int,
              .numRegens     = 1,
              .parentIndices = parentIdx1 },
        };
        ZL_DecoderFusionDesc desc = {
            .pattern = {
                .parentCodec = ZL_StandardTransformID_delta_int,
                .numChildren = 2,
                .children    = children,
            },
            .fusionFn = dummyFusionFn,
        };
        ZL_Report r = DCTX_registerDecoderFusion(dctx_.get(), &desc);
        ASSERT_FALSE(ZL_isError(r));
    }

    // Register the real zigzag+delta fusion (1 child, matches)
    registerFusion();

    ZL_GraphID graph = buildDeltaZigzagGraph(4);

    constexpr size_t N = 64;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i * 3 + 7);
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 1);
}

// ---------------------------------------------------------------------------
// Test — fusion registered but pattern doesn't match graph
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, FusionPatternMismatch)
{
    // Register zigzag+delta fusion
    registerFusion();

    // But compress with a graph that uses delta WITHOUT zigzag as its child.
    // Build: interpret_LE32 -> delta -> num_to_serial -> store
    EXPECT_FALSE(ZL_isError(ZL_Compressor_setParameter(
            compressor_.get(),
            ZL_CParam_formatVersion,
            ZL_MAX_FORMAT_VERSION)));

    ZL_GraphID const toSerial = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_CONVERT_NUM_TO_SERIAL, ZL_GRAPH_STORE);
    ZL_GraphID const delta = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_DELTA_INT, toSerial);
    ZL_GraphID const graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_INTERPRET_AS_LE32, delta);

    constexpr size_t N = 64;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i * 5 + 10);
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    // Fusion should not fire — child codec doesn't match
    EXPECT_EQ(g_calledCount, 0);
}

// ---------------------------------------------------------------------------
// Test — fusion registered for unrelated codec (map lookup returns NULL)
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, FusionForUnrelatedCodec)
{
    // Register delta+bitpack+tokenize fusion, but decompress a delta+zigzag
    // graph
    registerTokenizeDeltaBitpackFusion();

    ZL_GraphID graph = buildDeltaZigzagGraph(4);

    constexpr size_t N = 64;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i * 3 + 7);
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    // Fusion parent codec ID doesn't appear in the graph, so no fusion
    EXPECT_EQ(g_calledCount, 0);
}

// ---------------------------------------------------------------------------
// Test — clearDecoderFusions disables fusion
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, ClearFusionsDisablesFusion)
{
    ZL_GraphID graph = buildDeltaZigzagGraph(4);
    registerFusion();

    constexpr size_t N = 64;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i * 3 + 7);
    }

    // First round trip — fusion should fire
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 1);

    // Clear fusions
    DCTX_clearDecoderFusions(dctx_.get());

    // Second round trip — standard decode, no fusion
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 1);
}

// ---------------------------------------------------------------------------
// Test — DCtx reuse across different graphs
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, DCtxReuseAcrossGraphs)
{
    // Register only zigzag+delta fusion
    registerFusion();

    // First round trip: delta+zigzag graph — fusion fires
    {
        ZL_GraphID graph   = buildDeltaZigzagGraph(4);
        constexpr size_t N = 64;
        std::vector<uint32_t> data(N);
        for (size_t i = 0; i < N; i++) {
            data[i] = (uint32_t)(i * 3 + 7);
        }
        roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
        EXPECT_EQ(g_calledCount, 1);
    }

    // Second round trip: tokenize+delta+bitpack graph — no matching fusion
    {
        ZL_GraphID graph   = buildTokenizeDeltaBitpackGraph();
        constexpr size_t N = 64;
        std::vector<uint32_t> data(N);
        for (size_t i = 0; i < N; i++) {
            data[i] = (uint32_t)(i * 503 % 0x10000);
        }
        roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
        EXPECT_EQ(g_calledCount, 1); // no additional fusion call
    }
}

// ---------------------------------------------------------------------------
// Test — runCodec with empty data
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, RunCodecEmptyData)
{
    ZL_GraphID graph = buildDeltaZigzagGraph(4);
    registerFusionWith(fusedZigzagDelta_runCodecBoth);

    // Empty input — 0 bytes
    roundTrip(nullptr, 0, graph);
    // Empty data doesn't go through the fusion (no elements to decode)
    // Just verify no crash
}

// ---------------------------------------------------------------------------
// Test — multiple decompressions with fusion (DCtx reuse, same graph)
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, FusionMultipleDecompressions)
{
    ZL_GraphID graph = buildDeltaZigzagGraph(4);
    registerFusion();

    for (size_t iter = 0; iter < 3; iter++) {
        constexpr size_t N = 64;
        std::vector<uint32_t> data(N);
        for (size_t i = 0; i < N; i++) {
            data[i] = (uint32_t)(i * (iter + 1) * 3 + 7 + iter);
        }
        roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
        EXPECT_EQ(g_calledCount, iter + 1);
    }
}

// ---------------------------------------------------------------------------
// Test — fusion child's input is stored in frame (ZL_PRODUCER_STORE)
//
// In the tokenize+delta+bitpack graph, the bitpack_int decoder reads its
// input directly from the frame (ZL_PRODUCER_STORE). Register a fake fusion
// with parent = bitpack_int and a child expecting to produce input 0.
// Since that input comes from store (not a decoder node),
// getProducerNodeIdx returns ZL_PRODUCER_STORE and the fusion must not fire.
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, FusionChildInputStoredInFrame)
{
    // Register a fusion where child maps to bitpack_int's input 0,
    // which comes from store (ZL_PRODUCER_STORE).
    uint32_t parentIdx0[]            = { 0 };
    ZL_DecoderFusionChild children[] = {
        { .codec         = ZL_StandardTransformID_constant_serial,
          .numRegens     = 1,
          .parentIndices = parentIdx0 },
    };
    ZL_DecoderFusionDesc desc = {
        .pattern = {
            .parentCodec = ZL_StandardTransformID_bitpack_int,
            .numChildren = 1,
            .children    = children,
        },
        .fusionFn = dummyFusionFn,
    };
    ZL_Report r = DCTX_registerDecoderFusion(dctx_.get(), &desc);
    ASSERT_FALSE(ZL_isError(r));

    ZL_GraphID graph = buildTokenizeDeltaBitpackGraph();

    constexpr size_t N = 64;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i * 503 % 0x10000);
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    // dummyFusionFn would fail if called, so successful round trip
    // proves the fusion did not fire.
    EXPECT_EQ(g_calledCount, 0);
}

// ---------------------------------------------------------------------------
// Test — partial child match in multi-child fusion
//
// Register the 2-child delta+bitpack+tokenize fusion, but build a graph
// where tokenize's indices output goes through zigzag instead of bitpack.
// Child 0 (delta) matches, but child 1 (expects bitpack, finds zigzag)
// does not. Fusion must not fire.
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, FusionPartialChildMatch)
{
    // Register the delta+bitpack+tokenize fusion (expects both children)
    registerTokenizeDeltaBitpackFusion();

    // Build a tokenize graph where indices go through zigzag instead of bitpack
    EXPECT_FALSE(ZL_isError(ZL_Compressor_setParameter(
            compressor_.get(),
            ZL_CParam_formatVersion,
            ZL_MAX_FORMAT_VERSION)));

    // Output 0 (alphabet, numeric): delta -> num_to_serial -> store
    ZL_GraphID const toSerial = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_CONVERT_NUM_TO_SERIAL, ZL_GRAPH_STORE);
    ZL_GraphID const alphabetGraph =
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    compressor_.get(), ZL_NODE_DELTA_INT, toSerial);

    // Output 1 (indices, numeric): zigzag -> num_to_serial -> store
    // (NOT bitpack, so child 1 won't match the fusion pattern)
    ZL_GraphID const toSerial2 = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_CONVERT_NUM_TO_SERIAL, ZL_GRAPH_STORE);
    ZL_GraphID const indicesGraph =
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    compressor_.get(), ZL_NODE_ZIGZAG, toSerial2);

    ZL_GraphID const tokenizeGraph = ZL_Compressor_registerTokenizeGraph(
            compressor_.get(),
            ZL_Type_numeric,
            true, // sorted
            alphabetGraph,
            indicesGraph);
    EXPECT_NE(tokenizeGraph.gid, ZL_GRAPH_ILLEGAL.gid);

    ZL_GraphID const graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_INTERPRET_AS_LE32, tokenizeGraph);

    // Data with repeated values (good for tokenization)
    constexpr size_t N = 128;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i % 10) * 100;
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    // Fusion should not fire — child 1 codec mismatch (zigzag != bitpack)
    EXPECT_EQ(g_calledCount, 0);
}

// ---------------------------------------------------------------------------
// Test — child codec produces wrong number of regens
//
// Register a fusion expecting the child (zigzag) to produce 2 regens, but
// zigzag only produces 1. The nbRegens check in fuseIfMatches must reject.
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, FusionChildWrongNumRegens)
{
    // Register a fusion with numRegens=2 for zigzag (which only produces 1)
    uint32_t parentIndices[]         = { 0, 1 };
    ZL_DecoderFusionChild children[] = {
        { .codec         = ZL_StandardTransformID_zigzag,
          .numRegens     = 2,
          .parentIndices = parentIndices },
    };
    ZL_DecoderFusionDesc desc = {
        .pattern = {
            .parentCodec = ZL_StandardTransformID_delta_int,
            .numChildren = 1,
            .children    = children,
        },
        .fusionFn = dummyFusionFn,
    };
    ZL_Report r = DCTX_registerDecoderFusion(dctx_.get(), &desc);
    ASSERT_FALSE(ZL_isError(r));

    ZL_GraphID graph = buildDeltaZigzagGraph(4);

    constexpr size_t N = 64;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i * 3 + 7);
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    // dummyFusionFn would fail if called — successful round trip proves
    // the fusion did not fire due to nbRegens mismatch.
    EXPECT_EQ(g_calledCount, 0);
}

// ---------------------------------------------------------------------------
// Test — multiple copies of the fused decoder in the same graph
//
// Build a tokenize graph where both outputs (alphabet and indices) go
// through delta -> zigzag. Register the zigzag+delta fusion. Both
// delta nodes should be independently fused, so g_calledCount == 2.
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, MultipleFusedDecodersSameGraph)
{
    registerFusion();

    EXPECT_FALSE(ZL_isError(ZL_Compressor_setParameter(
            compressor_.get(),
            ZL_CParam_formatVersion,
            ZL_MAX_FORMAT_VERSION)));

    // Both alphabet and indices go through delta -> zigzag -> store
    auto buildDeltaZigzagSubgraph = [&]() {
        ZL_GraphID const toSerial =
                ZL_Compressor_registerStaticGraph_fromNode1o(
                        compressor_.get(),
                        ZL_NODE_CONVERT_NUM_TO_SERIAL,
                        ZL_GRAPH_STORE);
        ZL_GraphID const zigzag = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_.get(), ZL_NODE_ZIGZAG, toSerial);
        return ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_.get(), ZL_NODE_DELTA_INT, zigzag);
    };

    ZL_GraphID const alphabetGraph = buildDeltaZigzagSubgraph();
    ZL_GraphID const indicesGraph  = buildDeltaZigzagSubgraph();

    ZL_GraphID const tokenizeGraph = ZL_Compressor_registerTokenizeGraph(
            compressor_.get(),
            ZL_Type_numeric,
            true, // sorted
            alphabetGraph,
            indicesGraph);
    EXPECT_NE(tokenizeGraph.gid, ZL_GRAPH_ILLEGAL.gid);

    ZL_GraphID const graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_INTERPRET_AS_LE32, tokenizeGraph);

    // Data with repeated values
    constexpr size_t N = 128;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i % 10) * 100;
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    // Both delta+zigzag pairs should be fused independently
    EXPECT_EQ(g_calledCount, 2);
}

// ---------------------------------------------------------------------------
// Test — chained fusions: parent of one fusion is child of another
//
// Graph: interpret_LE32 -> tokenize -> [alphabet: delta -> zigzag -> store]
//                                   -> [indices: bitpack -> store]
//
// Register two fusions:
//   Fusion A: parent=delta, child=zigzag  (fires first in graph order)
//   Fusion B: parent=tokenize, children=[delta, bitpack]
//
// In graph processing order (leaves first):
//   - delta's child is zigzag: fusion A matches, both marked fused
//   - tokenize tries fusion B: delta is already fused, so fusion B fails
// Only fusion A fires. Standard decode handles tokenize.
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, ChainedFusionParentIsChildOfAnother)
{
    // Register fusion A: zigzag+delta
    registerFusion();
    // Register fusion B: delta+bitpack+tokenize
    registerTokenizeDeltaBitpackFusion();

    // Build the tokenize graph with delta+zigzag for alphabet, bitpack for
    // indices (same as buildTokenizeDeltaBitpackGraph but with zigzag added)
    EXPECT_FALSE(ZL_isError(ZL_Compressor_setParameter(
            compressor_.get(),
            ZL_CParam_formatVersion,
            ZL_MAX_FORMAT_VERSION)));

    // alphabet: delta -> zigzag -> num_to_serial -> store
    ZL_GraphID const toSerial = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_CONVERT_NUM_TO_SERIAL, ZL_GRAPH_STORE);
    ZL_GraphID const zigzag = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_ZIGZAG, toSerial);
    ZL_GraphID const alphabetGraph =
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    compressor_.get(), ZL_NODE_DELTA_INT, zigzag);

    // indices: bitpack -> store
    ZL_GraphID const indicesGraph = ZL_GRAPH_BITPACK;

    ZL_GraphID const tokenizeGraph = ZL_Compressor_registerTokenizeGraph(
            compressor_.get(),
            ZL_Type_numeric,
            true, // sorted
            alphabetGraph,
            indicesGraph);
    EXPECT_NE(tokenizeGraph.gid, ZL_GRAPH_ILLEGAL.gid);

    ZL_GraphID const graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), ZL_NODE_INTERPRET_AS_LE32, tokenizeGraph);

    constexpr size_t N = 128;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i % 10) * 100;
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    // Only the zigzag+delta fusion (A) should fire.
    // The tokenize fusion (B) should not fire because delta is already fused.
    EXPECT_EQ(g_calledCount, 1);
}

// ---------------------------------------------------------------------------
// Test — clear fusions, re-register, verify fusion works again
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, ClearAndReRegisterFusion)
{
    ZL_GraphID graph = buildDeltaZigzagGraph(4);

    constexpr size_t N = 64;
    std::vector<uint32_t> data(N);
    for (size_t i = 0; i < N; i++) {
        data[i] = (uint32_t)(i * 3 + 7);
    }

    // Register fusion, verify it fires
    registerFusion();
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 1);

    // Clear fusions, verify it no longer fires
    DCTX_clearDecoderFusions(dctx_.get());
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 1);

    // Re-register fusion, verify it fires again
    registerFusion();
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    EXPECT_EQ(g_calledCount, 2);
}

// ---------------------------------------------------------------------------
// Tests — ZL_DParam_enableCodecFusion
// ---------------------------------------------------------------------------

TEST_F(CodecFusionTest, EnableCodecFusionParam_SetAndGet)
{
    // Default is 0 (unset), resolves to enabled via applyDefaults
    EXPECT_EQ(
            ZL_DCtx_getParameter(dctx_.get(), ZL_DParam_enableCodecFusion), 0);

    ASSERT_FALSE(ZL_isError(ZL_DCtx_setParameter(
            dctx_.get(),
            ZL_DParam_enableCodecFusion,
            ZL_TernaryParam_disable)));
    EXPECT_EQ(
            ZL_DCtx_getParameter(dctx_.get(), ZL_DParam_enableCodecFusion),
            ZL_TernaryParam_disable);

    ASSERT_FALSE(ZL_isError(ZL_DCtx_setParameter(
            dctx_.get(), ZL_DParam_enableCodecFusion, ZL_TernaryParam_enable)));
    EXPECT_EQ(
            ZL_DCtx_getParameter(dctx_.get(), ZL_DParam_enableCodecFusion),
            ZL_TernaryParam_enable);
}

TEST_F(CodecFusionTest, EnableCodecFusionParam_ResetAfterDecompress)
{
    ZL_GraphID graph = buildDeltaZigzagGraph(4);
    registerFusion();

    ASSERT_FALSE(ZL_isError(ZL_DCtx_setParameter(
            dctx_.get(),
            ZL_DParam_enableCodecFusion,
            ZL_TernaryParam_disable)));

    std::vector<uint32_t> data(64);
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = (uint32_t)(i * 3 + 7);
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);

    // Parameter resets to 0 (default/unset) after decompression
    EXPECT_EQ(
            ZL_DCtx_getParameter(dctx_.get(), ZL_DParam_enableCodecFusion), 0);
}

TEST_F(CodecFusionTest, EnableCodecFusionParam_StickyAcrossDecompress)
{
    ZL_GraphID graph = buildDeltaZigzagGraph(4);
    registerFusion();

    ASSERT_FALSE(ZL_isError(
            ZL_DCtx_setParameter(dctx_.get(), ZL_DParam_stickyParameters, 1)));
    ASSERT_FALSE(ZL_isError(ZL_DCtx_setParameter(
            dctx_.get(),
            ZL_DParam_enableCodecFusion,
            ZL_TernaryParam_disable)));

    std::vector<uint32_t> data(64);
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = (uint32_t)(i * 3 + 7);
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);

    EXPECT_EQ(
            ZL_DCtx_getParameter(dctx_.get(), ZL_DParam_enableCodecFusion),
            ZL_TernaryParam_disable);
}

TEST_F(CodecFusionTest, EnableCodecFusionParam_DisablePreventsCodecFusion)
{
    ZL_GraphID graph = buildDeltaZigzagGraph(4);
    registerFusion();

    ASSERT_FALSE(ZL_isError(ZL_DCtx_setParameter(
            dctx_.get(),
            ZL_DParam_enableCodecFusion,
            ZL_TernaryParam_disable)));

    std::vector<uint32_t> data(64);
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = (uint32_t)(i * 3 + 7);
    }
    roundTrip(data.data(), data.size() * sizeof(uint32_t), graph);
    // Fusion registered but param disables it — standard decoders used
    EXPECT_EQ(g_calledCount, 0);
}

TEST_F(CodecFusionTest, EnableCodecFusionParam_EnabledAndDisabledMatch)
{
    ZL_GraphID graph = buildDeltaZigzagGraph(4);
    registerFusion();

    std::vector<uint32_t> data(128);
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = (uint32_t)(i * 3 + 7);
    }

    // Compress once
    ASSERT_FALSE(ZL_isError(
            ZL_Compressor_selectStartingGraphID(compressor_.get(), graph)));
    std::string compressed(
            ZL_compressBound(data.size() * sizeof(uint32_t)), '\0');
    CCtx cctx;
    setTestParams(cctx);
    ASSERT_FALSE(
            ZL_isError(ZL_CCtx_refCompressor(cctx.get(), compressor_.get())));
    ZL_Report cSize = ZL_CCtx_compress(
            cctx.get(),
            compressed.data(),
            compressed.size(),
            data.data(),
            data.size() * sizeof(uint32_t));
    ASSERT_FALSE(ZL_isError(cSize));
    compressed.resize(ZL_validResult(cSize));

    // Decompress with fusion enabled (default)
    std::string decompFused(data.size() * sizeof(uint32_t), '\0');
    ZL_Report dSize = ZL_DCtx_decompress(
            dctx_.get(),
            decompFused.data(),
            decompFused.size(),
            compressed.data(),
            compressed.size());
    ASSERT_FALSE(ZL_isError(dSize));
    EXPECT_EQ(g_calledCount, 1);

    // Decompress with fusion disabled
    ASSERT_FALSE(ZL_isError(ZL_DCtx_setParameter(
            dctx_.get(),
            ZL_DParam_enableCodecFusion,
            ZL_TernaryParam_disable)));
    std::string decompUnfused(data.size() * sizeof(uint32_t), '\0');
    dSize = ZL_DCtx_decompress(
            dctx_.get(),
            decompUnfused.data(),
            decompUnfused.size(),
            compressed.data(),
            compressed.size());
    ASSERT_FALSE(ZL_isError(dSize));
    EXPECT_EQ(g_calledCount, 1); // fusion did not fire

    EXPECT_EQ(decompFused, decompUnfused);
    EXPECT_EQ(
            memcmp(data.data(),
                   decompFused.data(),
                   data.size() * sizeof(uint32_t)),
            0);
}

} // namespace
} // namespace openzl::tests
