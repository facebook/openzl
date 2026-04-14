// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/partition/decode_partition_bitpack_fusion.h"

#include "openzl/codecs/bitpack/decode_bitpack_binding.h"
#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/codecs/partition/common_partition.h"
#include "openzl/common/wire_format.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/numeric_operations.h"
#include "openzl/zl_input.h"
#include "openzl/zl_output.h"

// ---------------------------------------------------------------------------
// LUT builders: expand per-bucket base/mask arrays into packed 2-element LUTs
// ---------------------------------------------------------------------------

/// Build expanded LUT for nbBits=4: 2^4=16 raw entries -> 2^8=256 expanded.
static void expandLUT4(const uint16_t* LUTx1, uint32_t LUTx2[256])
{
    for (size_t idx = 0; idx < 256; ++idx) {
        const size_t lo = idx & 0xF;
        const size_t hi = idx >> 4;
        LUTx2[idx]      = (uint32_t)LUTx1[lo] | ((uint32_t)LUTx1[hi] << 16);
    }
}

/// Build expanded LUT for nbBits=5: 2^5=32 raw entries -> 2^10=1024 expanded.
static void expandLUT5(const uint16_t* LUTx1, uint32_t LUTx2[1024])
{
    for (size_t idx = 0; idx < 1024; ++idx) {
        const size_t lo = idx & 31;
        const size_t hi = idx >> 5;
        LUTx2[idx]      = (uint32_t)LUTx1[lo] | ((uint32_t)LUTx1[hi] << 16);
    }
}

// ---------------------------------------------------------------------------
// computeLimit: safe iteration bound for fast-path loops
// ---------------------------------------------------------------------------
//
// The fast-path loop processes kEltsPerIter elements per iteration:
//
//     for (; i < limit; i += kEltsPerIter) { /* read kEltsPerIter elts */ }
//
// This function computes a safe `limit` such that every iteration reads
// within both the fixed and variable stream buffers.
//
// The variable stream bound is a worst-case estimate (assumes maxVarBits
// bits per element). Actual consumption may be lower, so callers recompute
// after the inner loop to squeeze out additional iterations:
//
//     for (;;) {
//         limit = computeLimit(...);
//         if (i >= limit) break;
//         for (; i < limit; i += kEltsPerIter) { ... }
//     }
//
static size_t computeLimit(
        size_t i,
        size_t numElts,
        size_t kEltsPerIter,
        size_t maxVarBits,
        const uint8_t* v,
        const uint8_t* vEnd,
        const uint8_t* f,
        const uint8_t* fEnd,
        size_t fixedBytesPerIter)
{
    ZL_ASSERT_LE(i, numElts);
    ZL_ASSERT_LE(v, vEnd);
    ZL_ASSERT_LE(f, fEnd);

    // Need at least one full iteration's worth of output elements.
    if (numElts < kEltsPerIter) {
        return i;
    }

    // We need to be able to load 8 bytes from the fixed and variable inputs.
    if (fEnd - f < 8 || vEnd - v < 8) {
        return i;
    }

    // Fixed stream: each iteration consumes exactly fixedBytesPerIter bytes,
    // but individual reads within the iteration are 8-byte LE loads that may
    // read ahead of what is consumed (e.g. decode5 reads 8 bytes, advances 5).
    // Reserve 7 bytes of margin so every 8-byte load stays in bounds.
    const size_t fSafeElts =
            (((size_t)(fEnd - f) - 7) / fixedBytesPerIter) * kEltsPerIter;

    size_t vSafeElts = numElts;
    if (maxVarBits != 0) {
        // Total bits in remaining bytes, minus 7 bytes so that every 8-byte
        // load stays in bounds, minus up to 7 residual bits from the sub-byte
        // bitsConsumed offset carried into this call.
        const size_t vSafeBits = (8 * ((size_t)(vEnd - v) - 7) - 7);
        vSafeElts              = vSafeBits / maxVarBits;
    }

    // Take the tighter of the two stream bounds.
    const size_t safeElts = ZL_MIN(fSafeElts, vSafeElts);
    if (safeElts < kEltsPerIter) {
        return i;
    }

    // An iteration at index j reads elements j..j+kEltsPerIter-1, so the
    // last valid start index is safeElts - kEltsPerIter + 1 from the current i.
    // Also cap at numElts - kEltsPerIter + 1 to stay within the output buffer.
    const size_t limit = ZL_MIN(numElts, i + safeElts) - kEltsPerIter + 1;
    return limit;
}

// ---------------------------------------------------------------------------
// DecodeTail: Decode the tail of the stream with a scalar safe path.
// ---------------------------------------------------------------------------
static ZL_Report decodeTail(
        uint16_t* out,
        size_t i,
        size_t numElts,
        const uint8_t* fixed,
        const uint8_t* fixedEnd,
        size_t fixedBitsPerElt,
        const uint8_t* var,
        const uint8_t* varEnd,
        size_t varBitsConsumed,
        const uint64_t* bases,
        const uint8_t* bits,
        size_t numPartitions)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);

    ZS_BitDStreamFF fixedBits =
            ZS_BitDStreamFF_init(fixed, (size_t)(fixedEnd - fixed));
    ZS_BitDStreamFF varBits = ZS_BitDStreamFF_init(var, (size_t)(varEnd - var));
    ZS_BitDStreamFF_skip(&varBits, varBitsConsumed);
    for (; i < numElts; ++i) {
        size_t bucket = ZS_BitDStreamFF_read(&fixedBits, fixedBitsPerElt);
        ZS_BitDStreamFF_reload(&fixedBits);
        ZL_ERR_IF_GE(bucket, numPartitions, corruption, "Bucket ID OOB");
        uint64_t offset = ZS_BitDStreamFF_read(&varBits, bits[bucket]);
        ZS_BitDStreamFF_reload(&varBits);
        out[i] = (uint16_t)(bases[bucket] + offset);
    }
    ZL_ERR_IF_ERR(ZS_BitDStreamFF_finish(&fixedBits));
    ZL_ERR_IF_ERR(ZS_BitDStreamFF_finish(&varBits));
    return ZL_returnSuccess();
}

// ---------------------------------------------------------------------------
// Decode4: 2 bucket IDs packed per byte (4 bits each), 16 elts per iter
// ---------------------------------------------------------------------------
static ZL_Report decode4(
        uint16_t* out,
        size_t numElts,
        const uint8_t* fixed,
        size_t fixedSize,
        const uint8_t* var,
        size_t varSize,
        const uint64_t* bases,
        const uint8_t* bits,
        size_t numPartitions,
        size_t maxVarBits,
        uint32_t baseLUTx2[256],
        uint32_t maskLUTx2[256])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT_LE(numPartitions, 16);

    {
        // Need to copy bases to local storage for when numPartitions < 16.
        uint16_t baseLUTx1[16] = { 0 };
        uint16_t maskLUTx1[16] = { 0 };
        for (size_t p = 0; p < numPartitions; ++p) {
            baseLUTx1[p] = (uint16_t)bases[p];
            maskLUTx1[p] = (uint16_t)((1u << bits[p]) - 1);
        }
        expandLUT4(baseLUTx1, baseLUTx2);
        expandLUT4(maskLUTx1, maskLUTx2);
    }

    const uint8_t* f             = fixed;
    const uint8_t* const fEnd    = fixed + fixedSize;
    const uint8_t* v             = var;
    const uint8_t* const vEnd    = var + varSize;
    const size_t kEltsPerIter    = 16;
    const size_t kFixedBytesIter = 8; // 16 elts * 4 bits / 8 = 8 bytes
    size_t i                     = 0;
    size_t bitsConsumed          = 0;

    for (;;) {
        const size_t limit = computeLimit(
                i,
                numElts,
                kEltsPerIter,
                maxVarBits,
                v,
                vEnd,
                f,
                fEnd,
                kFixedBytesIter);
        if (i >= limit) {
            break;
        }
        for (; i < limit; i += kEltsPerIter) {
            for (size_t u = 0; u < kEltsPerIter; u += 4) {
                const uint64_t base = (uint64_t)baseLUTx2[f[0]]
                        | ((uint64_t)baseLUTx2[f[1]] << 32);
                const uint64_t mask = (uint64_t)maskLUTx2[f[0]]
                        | ((uint64_t)maskLUTx2[f[1]] << 32);
                f += 2;
                const uint64_t vData  = ZL_readLE64(v) >> bitsConsumed;
                const uint64_t offset = ZL_bitDeposit64(vData, mask);
                ZL_writeLE64(out + i + u, base + offset);
                bitsConsumed += (size_t)ZL_popcount64(mask);
                v += bitsConsumed >> 3;
                bitsConsumed &= 7;
            }
        }
    }

    return decodeTail(
            out,
            i,
            numElts,
            f,
            fEnd,
            4,
            v,
            vEnd,
            bitsConsumed,
            bases,
            bits,
            numPartitions);
}

// ---------------------------------------------------------------------------
// Decode5: 8 bucket IDs packed into 5 bytes (5 bits each), 16 elts per iter
// ---------------------------------------------------------------------------
static ZL_Report decode5(
        uint16_t* out,
        size_t numElts,
        const uint8_t* fixed,
        size_t fixedSize,
        const uint8_t* var,
        size_t varSize,
        const uint64_t* bases,
        const uint8_t* bits,
        size_t numPartitions,
        size_t maxVarBits,
        uint32_t baseLUTx2[1024],
        uint32_t maskLUTx2[1024])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT_LE(numPartitions, 32);

    {
        // Need to copy bases to local storage for when numPartitions < 32.
        uint16_t baseLUTx1[32] = { 0 };
        uint16_t maskLUTx1[32] = { 0 };
        for (size_t p = 0; p < numPartitions; ++p) {
            baseLUTx1[p] = (uint16_t)bases[p];
            maskLUTx1[p] = (uint16_t)((1u << bits[p]) - 1);
        }
        expandLUT5(baseLUTx1, baseLUTx2);
        expandLUT5(maskLUTx1, maskLUTx2);
    }

    uint16_t* o                  = out;
    const uint8_t* f             = fixed;
    const uint8_t* const fEnd    = fixed + fixedSize;
    const uint8_t* v             = var;
    const uint8_t* const vEnd    = var + varSize;
    const size_t kEltsPerIter    = 16;
    const size_t kFixedBytesIter = 10; // 16 elts * 5 bits / 8 = 10 bytes
    size_t i                     = 0;
    size_t bitsConsumed          = 0;

    for (;;) {
        const size_t limit = computeLimit(
                i,
                numElts,
                kEltsPerIter,
                maxVarBits,
                v,
                vEnd,
                f,
                fEnd,
                kFixedBytesIter);
        if (i >= limit) {
            break;
        }
        for (; i < limit; i += kEltsPerIter) {
            for (size_t u = 0; u < kEltsPerIter; u += 8) {
                const uint64_t F     = ZL_readLE64(f);
                const uint64_t fs[4] = {
                    (F >> 0) & 1023,
                    (F >> 10) & 1023,
                    (F >> 20) & 1023,
                    (F >> 30) & 1023,
                };
                f += 5;
                for (size_t k = 0; k < 4; k += 2) {
                    const uint64_t base = (uint64_t)baseLUTx2[fs[k]]
                            | ((uint64_t)baseLUTx2[fs[k + 1]] << 32);
                    const uint64_t mask = (uint64_t)maskLUTx2[fs[k]]
                            | ((uint64_t)maskLUTx2[fs[k + 1]] << 32);
                    const uint64_t vData  = ZL_readLE64(v) >> bitsConsumed;
                    const uint64_t offset = ZL_bitDeposit64(vData, mask);
                    ZL_writeLE64(o, base + offset);
                    o += 4;
                    bitsConsumed += (size_t)ZL_popcount64(mask);
                    v += bitsConsumed >> 3;
                    bitsConsumed &= 7;
                }
            }
        }
    }

    return decodeTail(
            out,
            i,
            numElts,
            f,
            fEnd,
            5,
            v,
            vEnd,
            bitsConsumed,
            bases,
            bits,
            numPartitions);
}

static ZL_Report ZL_partitionBitpackFusedDecode_fallback(
        ZL_DecoderFusion* state)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->dctx);
    ZL_ERR_IF_ERR(ZL_DecoderFusion_runCodec(state, 0));
    ZL_ERR_IF_ERR(ZL_DecoderFusion_runCodec(state, 1));
    return ZL_returnSuccess();
}

// ---------------------------------------------------------------------------
// Main fusion entry point
// ---------------------------------------------------------------------------
ZL_Report ZL_partitionBitpackFusedDecode(ZL_DecoderFusion* state)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->dctx);

    // Codec index 0 = child (bitpack_int), codec index 1 = parent (partition)
    const size_t bitpackIdx   = 0;
    const size_t partitionIdx = 1;

    // --- Get bitpack input (packed serial stream) ---
    ZL_TRY_LET(
            ZL_CodecInputs,
            bpInputs,
            ZL_DecoderFusion_getCodecInputs(state, bitpackIdx));
    ZL_ERR_IF_NE(bpInputs.singleton.numInputs, 1, corruption);
    const ZL_Input* packedIn = bpInputs.singleton.inputs[0];
    ZL_ERR_IF_NULL(packedIn, corruption);
    const uint8_t* packedData = (const uint8_t*)ZL_Input_ptr(packedIn);
    const size_t packedSize   = ZL_Input_numElts(packedIn);

    // --- Parse bitpack header ---
    ZL_TRY_LET(
            ZL_RBuffer,
            bpHeaderBuf,
            ZL_DecoderFusion_getCodecHeader(state, bitpackIdx));
    ZL_BitpackHeader bpHeader;
    ZL_ERR_IF_ERR(ZL_BitpackHeader_parse(
            &bpHeader, bpHeaderBuf.start, bpHeaderBuf.size, packedSize));
    ZL_ERR_IF_NE(
            bpHeader.eltWidth,
            1,
            corruption,
            "Fused bitpack must produce bytes");
    const size_t nbBits  = bpHeader.nbBits;
    const size_t numElts = bpHeader.numElts;

    // --- Parse partition header ---
    ZL_TRY_LET(
            ZL_RBuffer,
            partHeader,
            ZL_DecoderFusion_getCodecHeader(state, partitionIdx));

    uint64_t* partitionSizesBuffer =
            (uint64_t*)ZL_DecoderFusion_getScratchSpace(
                    state, sizeof(uint64_t) * ZL_PARTITION_MAX_PARTITIONS);
    ZL_ERR_IF_NULL(partitionSizesBuffer, allocation);
    ZL_PartitionParams params = { 0 };
    size_t outEltWidth        = 0;
    ZL_ERR_IF_ERR(ZL_PartitionParams_parseHeader(
            &params,
            &outEltWidth,
            (const uint8_t*)partHeader.start,
            partHeader.size,
            partitionSizesBuffer));

    // --- Fallback for non-uint16_t output ---
    if (outEltWidth != 2 || nbBits < 4 || nbBits > 5
        || params.numPartitions > (1u << nbBits)
        || ZL_PartitionParams_getLargestPartitionSize(&params)
                > ZL_PARTITION_MAX_PARTITION_SIZE_FOR_UNROLL4) {
        return ZL_partitionBitpackFusedDecode_fallback(state);
    }

    // --- Get partition offsets input (input 1, not fused) ---
    ZL_TRY_LET(
            ZL_CodecInputs,
            partInputs,
            ZL_DecoderFusion_getCodecInputs(state, partitionIdx));
    // Input 0 is fused (NULL), input 1 is the offsets serial stream
    ZL_ERR_IF_NE(partInputs.singleton.numInputs, 2, corruption);
    ZL_ASSERT_NULL(partInputs.singleton.inputs[0]); // fused
    const ZL_Input* offsetsIn = partInputs.singleton.inputs[1];
    ZL_ERR_IF_NULL(offsetsIn, corruption);
    const uint8_t* offsetsData = (const uint8_t*)ZL_Input_ptr(offsetsIn);
    const size_t offsetsSize   = ZL_Input_numElts(offsetsIn);

    // --- Fast path: uint16_t output ---
    const size_t numPartitions = params.numPartitions;

    // Compute uint16_t bases and uint8_t bits
    uint64_t* basesU64 = (uint64_t*)ZL_DecoderFusion_getScratchSpace(
            state, sizeof(uint64_t) * ZL_PARTITION_MAX_PARTITIONS);
    ZL_ERR_IF_NULL(basesU64, allocation);
    uint8_t* varBits = (uint8_t*)ZL_DecoderFusion_getScratchSpace(
            state, sizeof(uint8_t) * ZL_PARTITION_MAX_PARTITIONS);
    ZL_ERR_IF_NULL(varBits, allocation);
    ZL_PartitionParams_computeBasesU64(&params, basesU64);
    ZL_PartitionParams_computeBits(&params, varBits);

    const size_t maxVarBits = NUMOP_findMaxU8(varBits, numPartitions);
    ZL_ASSERT_LE(maxVarBits, 14, "Already validated");

    // Allocate LUT scratch space (decode5 needs 1024 entries, decode4 needs
    // 256)
    const size_t lutSize = (nbBits == 5) ? 1024 : 256;
    uint32_t* baseLUTx2  = (uint32_t*)ZL_DecoderFusion_getScratchSpace(
            state, sizeof(uint32_t) * lutSize);
    ZL_ERR_IF_NULL(baseLUTx2, allocation);
    uint32_t* maskLUTx2 = (uint32_t*)ZL_DecoderFusion_getScratchSpace(
            state, sizeof(uint32_t) * lutSize);
    ZL_ERR_IF_NULL(maskLUTx2, allocation);

    // Create output stream
    ZL_Output* const out =
            ZL_DecoderFusion_createTypedStream(state, 0, numElts, outEltWidth);
    ZL_ERR_IF_NULL(out, allocation);
    uint16_t* outPtr = (uint16_t*)ZL_Output_ptr(out);

    // Dispatch to specialized decoder
    ZL_Report ret;
    switch (nbBits) {
        case 4:
            ret =
                    decode4(outPtr,
                            numElts,
                            packedData,
                            packedSize,
                            offsetsData,
                            offsetsSize,
                            basesU64,
                            varBits,
                            numPartitions,
                            maxVarBits,
                            baseLUTx2,
                            maskLUTx2);
            break;
        case 5:
            ret =
                    decode5(outPtr,
                            numElts,
                            packedData,
                            packedSize,
                            offsetsData,
                            offsetsSize,
                            basesU64,
                            varBits,
                            numPartitions,
                            maxVarBits,
                            baseLUTx2,
                            maskLUTx2);
            break;
        default:
            ZL_ASSERT_FAIL("Unreachable");
            break;
    }
    ZL_ERR_IF_ERR(ret);

    ZL_ERR_IF_ERR(ZL_Output_commit(out, numElts));

    return ZL_returnSuccess();
}
