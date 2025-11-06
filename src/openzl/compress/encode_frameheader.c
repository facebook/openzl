// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/encode_frameheader.h"

#include <limits.h> // INT_MAX
#include <stdint.h>

#include "openzl/codecs/bitpack/common_bitpack_kernel.h" // ZS_bitpackEncode32
#include "openzl/codecs/dispatch_by_tag/encode_dispatch_by_tag_kernel.h" // ZS_DispatchByTag_encode
#include "openzl/common/allocation.h"
#include "openzl/common/assertion.h"
#include "openzl/common/cursor.h" // ZL_WC
#include "openzl/common/limits.h"
#include "openzl/common/logging.h"     // ZL_LOG
#include "openzl/common/wire_format.h" // ZSTRONG_MAGIC_NUMBER, ZL_FrameProperties
#include "openzl/shared/mem.h"         // writeLE32
#include "openzl/shared/varint.h"      // ZL_varintEncode
#include "openzl/shared/xxhash.h"      // XXH3_64bits
#include "openzl/zl_data.h"            // ZL_Type
#include "openzl/zl_errors.h"
#include "openzl/zl_version.h"

// computeFHsize() :
// @return an upper bound estimation of frame header size.
// Note : this is seriously over-estimated,
//        could be tightened more
static ZL_Report computeFHBound(
        size_t numInputs,
        size_t nbTransforms,
        size_t nbBuffs,
        size_t nbRegens)
{
    ZL_RET_R_IF_GT(GENERIC, numInputs, ZL_ENCODER_INPUT_LIMIT);
    ZL_RET_R_IF_GT(
            GENERIC, nbTransforms, ZL_runtimeNodeLimit(ZL_MAX_FORMAT_VERSION));
    ZL_RET_R_IF_GT(
            GENERIC, nbBuffs, ZL_runtimeStreamLimit(ZL_MAX_FORMAT_VERSION));

    // Validate that this bound cannot overflow
    ZL_ASSERT_LT(
            ZL_ENCODER_INPUT_LIMIT
                    + ZL_runtimeStreamLimit(ZL_MAX_FORMAT_VERSION)
                    + ZL_runtimeStreamLimit(ZL_MAX_FORMAT_VERSION),
            (INT_MAX / 32));

    ZL_ASSERT_LE(
            ZL_varintSize(ZL_runtimeNodeInputLimit(ZL_MAX_FORMAT_VERSION)), 2);

    const size_t bound = 4 + (numInputs * 5) + ZL_varintSize(nbTransforms)
            + ZL_varintSize(nbBuffs - 1) + (nbBuffs * 4) + (nbTransforms * 22)
            + (nbRegens * 4) + 4 + 4;
    return ZL_returnValue(bound);
}

// Small helpers to bitpack encode into a WC
static size_t
ZL_WC_bitpackEncode8(ZL_WC* out, uint8_t const* src, size_t nbElts, int nbBits)
{
    size_t const size = ZS_bitpackEncode8(
            ZL_WC_ptr(out), ZL_WC_avail(out), src, nbElts, nbBits);
    ZL_WC_advance(out, size);
    return size;
}

static size_t ZL_WC_bitpackEncode32(
        ZL_WC* out,
        uint32_t const* src,
        size_t nbElts,
        int nbBits)
{
    size_t const size = ZS_bitpackEncode32(
            ZL_WC_ptr(out), ZL_WC_avail(out), src, nbElts, nbBits);
    ZL_WC_advance(out, size);
    return size;
}

/// Workspace contains 5 non-overlapping scratch regions of
/// nbTransforms elements each, for use during frame header
/// encoding.
typedef struct {
    uint32_t* buffScratch0;
    uint32_t* scratch0;
    uint32_t* scratch1;
    uint32_t* scratch2;
    uint8_t* scratch3;
    uint8_t* dst;
    size_t dstCapacity;
} EFH_Workspace;

// Note(@Cyan): this whole "workspace" preallocation strategy should rather be
// replaced by an Arena strategy, which has proven more flexible.
static ZL_Report EFH_Workspace_init(
        EFH_Workspace* wksp,
        size_t numInputs,
        size_t nbTransforms,
        size_t nbRegens,
        size_t nbStoredBuffs,
        void* dst,
        size_t dstCapacity)
{
    ZL_TRY_LET_R(
            dstBound,
            computeFHBound(numInputs, nbTransforms, nbStoredBuffs, nbRegens));
    size_t const regenArr32 = sizeof(uint32_t) * nbRegens;
    size_t const trArrays =
            (2 * sizeof(uint32_t) + sizeof(uint8_t)) * nbTransforms;
    size_t const buffSize       = nbStoredBuffs * sizeof(*wksp->buffScratch0);
    size_t const dstSize        = dstCapacity < dstBound ? dstBound : 0;
    size_t const allocationSize = buffSize + regenArr32 + trArrays + dstSize;

    uint32_t* const alloc = ZL_malloc(allocationSize);
    ZL_RET_R_IF_NULL(allocation, alloc);

    wksp->buffScratch0 = alloc;
    wksp->scratch0     = wksp->buffScratch0 + nbStoredBuffs;
    wksp->scratch1     = wksp->scratch0 + nbRegens;
    wksp->scratch2     = wksp->scratch1 + nbTransforms;
    wksp->scratch3     = (uint8_t*)(wksp->scratch2 + nbTransforms);
    wksp->dst          = wksp->scratch3 + nbTransforms;
    wksp->dstCapacity  = dstBound;

    ZL_ASSERT_EQ(wksp->dst + dstSize, (uint8_t*)alloc + allocationSize);

    if (dstCapacity >= dstBound) {
        // Write directly into the dst buffer
        wksp->dst         = dst;
        wksp->dstCapacity = dstCapacity;
    }

    return ZL_returnSuccess();
}
static void EFH_Workspace_destroy(EFH_Workspace* wksp)
{
    ZL_free(wksp->buffScratch0);
}

// Compress Transform Types
// Simple bit packing, 1 bit per transform
// Ideas or the future : consider different scenarios,
// Typically : 0: all transforms are "standard", and 1: 1 bit flag per transform
// (1 bit header) And then later, possibly : unbalanced (25<->75) repartition
// between 0 & 1 (2 bits header)
static void compressTrt(ZL_WC* out, const uint8_t flags[], size_t nbFlags)
{
    ZL_WC_bitpackEncode8(out, flags, nbFlags, 1);
}

// Compress Transform ID:
// separate standard ID from custom ID
//    - use bit-packing for standard transformID
//    - use varint for custom transformID
// Ideas for the future :
// 1) use range-packing for standard transformID
// 2) statistic model for standard trID
//    - some transforms are more common than others
// 3) dict-compress trID
//    - successions of transforms can be common
static void compressTrID(
        ZL_WC* out,
        const uint32_t trid[],
        size_t nbTransforms,
        const uint8_t ctrFlags[],
        uint32_t* snodeidsScratch,
        uint32_t* cnodeidsScratch)
{
    if (!nbTransforms)
        return;

    // separate standard TrIDs from custom ones
    // encode them separately
    // standard TrIDs use bitpack
    // custom TrIDs use varint
    uint32_t* snodeids = snodeidsScratch;
    uint32_t* cnodeids = cnodeidsScratch;
    void* niPtr[2]     = { snodeids, cnodeids };
    ZS_DispatchByTag_encode(niPtr, 2, trid, nbTransforms, 4, ctrFlags);
    size_t const nbSNodeIds =
            MEM_ptrDistance(snodeids, niPtr[0]) / sizeof(uint32_t);
    size_t const nbCNodeIds =
            MEM_ptrDistance(cnodeids, niPtr[1]) / sizeof(uint32_t);
    // use bitPacking for standard nodes
    int const nbBits = ZL_nextPow2(ZL_StandardTransformID_end);
    ZL_ASSERT_GE(ZL_WC_avail(out), ((nbTransforms * (size_t)nbBits) + 7) / 8);

    ZL_WC_bitpackEncode32(out, snodeids, nbSNodeIds, nbBits);

    // use varint for custom nodes
    for (size_t u = 0; u < nbCNodeIds; u++) {
        ZL_WC_pushVarint(out, cnodeids[u]);
    }
}

// Compress Transform's Private Header sizes
// - Bitmap encode zero vs non-zero private header sizes
// - varint encode remaining non-zero sizes
//
// Ideas for the future :
// 1) model private header size for standard trID
//    - some standard transforms have guaranteed transform's header sizes
static void compressTrHSize(
        ZL_WC* out,
        const uint32_t trhs[],
        size_t nbTransforms,
        uint32_t* wksp32)
{
    // detect 0-sizes
    for (size_t n = 0; n < nbTransforms; n++) {
        wksp32[n] = (trhs[n] > 0);
    }

    // bitpack 0-sizes flags
    ZL_ASSERT_GT(ZL_WC_avail(out), (nbTransforms + 7) / 8);
    ZL_WC_bitpackEncode32(out, wksp32, nbTransforms, 1);

    // collect nb-sizes
    size_t nbnzSizes = 0;
    for (size_t n = 0; n < nbTransforms; n++) {
        if (trhs[n] > 0) {
            wksp32[nbnzSizes] = trhs[n] - 1;
            nbnzSizes++;
        }
    }

    // varint-encode them
    for (size_t u = 0; u < nbnzSizes; u++) {
        ZL_WC_pushVarint(out, wksp32[u]);
    }
}

// Compress Nb of Variable Outputs
// - Bitmap encode zero vs non-zero nbVOs
// - 1 byte encode remaining non-zero sizes
//
// Ideas for future encoding scheme:
// 1) model nbVOs depending on transform's description.
//    This would allow to state "0" for all transforms without VOs
//
// Limitation : NbVOs per transform <= 256
static void compressNbVOs(
        ZL_WC* out,
        const uint32_t nbvos[],
        size_t nbTransforms,
        uint32_t* wksp32,
        unsigned formatVersion)
{
    // detect 0-sizes
    for (size_t n = 0; n < nbTransforms; n++) {
        wksp32[n] = (nbvos[n] > 0);
    }

    // bitpack 0-sizes flags
    ZL_ASSERT_GT(ZL_WC_avail(out), (nbTransforms + 7) / 8);
    ZL_WC_bitpackEncode32(out, wksp32, nbTransforms, 1);

    // collect nb-sizes
    size_t nbnzVos = 0;
    for (size_t n = 0; n < nbTransforms; n++) {
        if (nbvos[n] > 0) {
            wksp32[nbnzVos] = nbvos[n] - 1;
            nbnzVos++;
        }
    }

    // Varint encode non-zero nbVOs
    // Format versions older than 9 used a single-byte, but the max value was
    // < 128, so it is equivalent to varint.
    ZL_ASSERT_LT(ZL_transformOutStreamsLimit(8), 128);
    // Assertion for the frame header bound correctness, can be lifted without
    // impacting the decoder.
    ZL_ASSERT_LT(ZL_transformOutStreamsLimit(formatVersion), 1u << 21);
    for (size_t u = 0; u < nbnzVos; u++) {
        ZL_ASSERT_LE(wksp32[u], ZL_transformOutStreamsLimit(formatVersion));
        ZL_WC_pushVarint(out, wksp32[u]);
    }
}

// Compress Nb of Inputs
// - Bitmap encode 1 vs non-1 numInputs
// - varint encode remaining non-1 sizes
//
// Ideas for future encoding scheme:
// 1) model numInputs depending on transform's description.
//    This would skip encoding this value for Transforms with known numInputs
//
// Limitation : ZL_runtimeNodeInputLimit() per transform (2048 in v16)
static void compressNumInputs(
        ZL_WC* out,
        const uint32_t numInputs[],
        size_t nbTransforms,
        uint32_t* wksp32,
        unsigned formatVersion)
{
    ZL_ASSERT_GE(formatVersion, 16);
    (void)formatVersion;
    ZL_DLOG(SEQ, "compressNumInputs (%zu transforms)", nbTransforms);

    // detect 1 input (most common)
    for (size_t n = 0; n < nbTransforms; n++) {
        ZL_ASSERT_GE(numInputs[n], 1);
        wksp32[n] = (numInputs[n] > 1);
    }

    // bitpack 1-sizes
    ZL_ASSERT_GT(ZL_WC_avail(out), (nbTransforms + 7) / 8);
    ZL_WC_bitpackEncode32(out, wksp32, nbTransforms, 1);

    // collect numInputs > 1
    size_t nbMIs = 0;
    for (size_t n = 0; n < nbTransforms; n++) {
        if (numInputs[n] > 1) {
            wksp32[nbMIs++] = numInputs[n] - 2;
        }
    }

    // Varint encode numInputs > 1
    for (size_t u = 0; u < nbMIs; u++) {
        ZL_ASSERT_LE(wksp32[u] + 2, ZL_runtimeNodeInputLimit(formatVersion));
        ZL_WC_pushVarint(out, wksp32[u]);
    }
}

// Compress Stream Distances information
// Values are bitpacked. nbBits is determined by Graph's size (bound)
// Ideas for the future (@Cyan):
// 1) use range coding
// 2) Range could be shrinking as it progresses towards the end
// 3) rebuild, or emulate the graph building process for faster shrinking
static void compressStreamDistances(
        ZL_WC* out,
        const uint32_t distances[],
        size_t nbConsumedStreams,
        size_t nbStoredStreams)
{
    // Distances can never be > (nbConsumedStreams + nbStoredStreams)
    // So let's use that to restrict the range of possible values.
    // Note : this is a very safe bound, but ultimately a wasteful one.
    //        actual jump values are likely much smaller.
    //        This can likely be exploited for further improvement
    size_t const maxStrIdx = nbConsumedStreams + nbStoredStreams;
    int const nbBits       = ZL_nextPow2(maxStrIdx);
    ZL_ASSERT_GE(
            ZL_WC_avail(out), ((nbConsumedStreams * (size_t)nbBits) + 7) / 8);
    size_t const r =
            ZL_WC_bitpackEncode32(out, distances, nbConsumedStreams, nbBits);
    ZL_DLOG(BLOCK,
            "compressStrJ : use %zu bytes, for %i bits per %zu entries",
            r,
            nbBits,
            nbConsumedStreams);
}

// Compress Stream Sizes
// Simple varint encoding
// Ideas for the future :
// Note : this field is one of the most difficult ones to predict
// Idea 1: employ a "regular" size-field compression graph
// Idea 2: prediction depends on decompressedSize (when present)
// Idea 3: prediction depends on origin's transform
static void
compressStrSizes(ZL_WC* out, const uint32_t strs[], size_t nbStreams)
{
    for (size_t u = 0; u < nbStreams; u++) {
        ZL_WC_pushVarint(out, strs[u]);
    }
}

static uint8_t encodeType(ZL_Type type)
{
    switch (type) {
        case ZL_Type_serial:
            return 0;
        case ZL_Type_struct:
            return 1;
        case ZL_Type_numeric:
            return 2;
        case ZL_Type_string:
            return 3;
        default:
            ZL_ASSERT_FAIL("invalid type");
            return 0;
    }
}

static ZL_Report
EFH_encodeInputSizes_v20(ZL_WC* out, const InputDesc* inDesc, size_t numInputs)
{
    ZL_RET_R_IF_LT(dstCapacity_tooSmall, ZL_WC_avail(out), numInputs * 4);
    for (size_t n = 0; n < numInputs; n++) {
        ZL_RET_R_IF_GE(srcSize_tooLarge, inDesc[n].byteSize, UINT_MAX);
        ZL_WC_pushLE32(out, (uint32_t)inDesc[n].byteSize);
    }
    return ZL_returnValue(numInputs * 4);
}

static ZL_Report EFH_writeVarint(ZL_WC* out, uint64_t num)
{
    ZL_RET_R_IF_LT(
            dstCapacity_tooSmall,
            ZL_WC_avail(out),
            ZL_VARINT_FAST_OVERWRITE_64);
    size_t sWritten = ZL_varintEncode64Fast(num, ZL_WC_ptr(out));
    ZL_WC_ASSERT_HAS(out, sWritten);
    ZL_WC_advance(out, sWritten);
    return ZL_returnSuccess();
}

static ZL_Report
EFH_encodeInputSizes_v21(ZL_WC* out, const InputDesc* inDesc, size_t numInputs)
{
    size_t start = ZL_WC_size(out);
    for (size_t n = 0; n < numInputs; n++) {
        ZL_RET_R_IF_ERR(EFH_writeVarint(out, (uint64_t)inDesc[n].byteSize + 1));
    }
    // add nbStrings
    for (size_t n = 0; n < numInputs; n++) {
        if (inDesc[n].type == ZL_Type_string) {
            ZL_RET_R_IF_ERR(EFH_writeVarint(out, inDesc[n].numElts));
        }
    }

    ZL_ASSERT_GE(ZL_WC_size(out), start);
    return ZL_returnValue(ZL_WC_size(out) - start);
}

static ZL_Report EFH_encodeInputSizes(
        ZL_WC* out,
        const InputDesc* inDesc,
        size_t numInputs,
        uint32_t formatVersion)
{
    if (formatVersion <= 20)
        return EFH_encodeInputSizes_v20(out, inDesc, numInputs);
    // formatVersion>=21
    return EFH_encodeInputSizes_v21(out, inDesc, numInputs);
}

// writeFrameHeader_internal() :
// Note : @dstCapacity must be large enough to write the header,
//        otherwise the function will return an error
// @return : nb of bytes written into @dst (necessarily <= @dstCapacity)
static ZL_Report writeFrameHeader_internal(
        const EFH_Interface* encoder,
        void* dst,
        size_t dstCapacity,
        const EFH_FrameInfo* fip)
{
    ZL_TRY_LET_R(hsBound, computeFHBound(fip->numInputs, 0, 0, 0));
    // Add comment bytes relaxing header bound
    hsBound += fip->comment.size ? 4 + fip->comment.size : 0;
    ZL_DLOG(FRAME,
            "writeFrameHeader_internal (nbInputs=%zu, maxBound=%zu bytes)",
            fip->numInputs,
            hsBound);
    ZL_RET_R_IF_LT(dstCapacity_tooSmall, dstCapacity, hsBound);

    ZL_WC out = ZL_WC_wrap(dst, dstCapacity);

    ZL_ASSERT_GE(encoder->formatVersion, 3);

    ZL_writeMagicNumber(dst, dstCapacity, encoder->formatVersion);
    ZL_WC_advance(&out, 4);

    // Frame properties
    if (encoder->formatVersion >= ZL_CHUNK_VERSION_MIN) {
        uint8_t flags = 0;
        if (fip->fprop->hasContentChecksum)
            flags |= 1 << 0;
        if (fip->fprop->hasCompressedChecksum)
            flags |= 1 << 1;
        if (encoder->formatVersion >= ZL_COMMENT_VERSION_MIN
            && fip->fprop->hasComment)
            flags |= 1 << 2;
        ZL_WC_push(&out, flags);
    }

    // Nb of Inputs and their types
    ZL_ASSERT_GE(fip->numInputs, 1);
    if (encoder->formatVersion >= ZL_CHUNK_VERSION_MIN) {
        // Multiple typed inputs, multiple blocks
        if (fip->numInputs < 15) {
            // short format
            uint8_t token = (uint8_t)fip->numInputs;
            size_t max2   = ZL_MIN(2, fip->numInputs);
            for (size_t n = 0; n < max2; n++) {
                size_t const shift = (2 * n) + 4;
                token |=
                        (uint8_t)(encodeType(fip->inputDescs[n].type) << shift);
            }
            ZL_WC_push(&out, token);

            // write types if n > 2
            uint8_t* const hPtr = ZL_WC_ptr(&out);
            uint64_t inTypes    = 0;
            for (size_t n = 2; n < fip->numInputs; n++) {
                size_t const shift = (2 * (n - 2));
                inTypes |= (uint64_t)encodeType(fip->inputDescs[n].type)
                        << shift;
            }
            // must be written using Little Endian convention
            ZL_writeLE64(hPtr, inTypes);
            size_t const bytesNeeded = (fip->numInputs - 2 + 3) / 4;
            ZL_WC_advance(&out, bytesNeeded);

        } else { // nbInputs >= 15

            // write nb inputs
            ZL_ASSERT_LT(fip->numInputs, 4110);
            uint8_t token1 = (uint8_t)(((fip->numInputs - 15) << 4) | 15);
            ZL_WC_push(&out, token1);
            uint8_t token2 = (uint8_t)((fip->numInputs - 15) >> 4);
            ZL_WC_push(&out, token2);
            // write types
            uint8_t* hPtr = ZL_WC_ptr(&out);
            for (size_t n = 0; n < fip->numInputs;) {
                uint64_t inTypes   = 0;
                size_t const limit = ZL_MIN(n + 32, fip->numInputs);
                for (; n < limit; n++) {
                    size_t const shift = (2 * n) % 64;
                    inTypes |= (uint64_t)encodeType(fip->inputDescs[n].type)
                            << shift;
                }
                // must be written using Little Endian convention
                ZL_writeLE64(hPtr, inTypes);
                hPtr += 8;
            }
            size_t const bytesNeeded = (fip->numInputs + 3) / 4;
            ZL_WC_advance(&out, bytesNeeded);
        }

    } else if (encoder->formatVersion >= 15) {
        // Multiple typed inputs, single block
        {
            uint8_t first3 =
                    (fip->numInputs < 4) ? (uint8_t)(fip->numInputs - 1) : 3;
            uint8_t inTypes    = (uint8_t)(first3 << 6);
            size_t maxToEncode = ZL_MIN(fip->numInputs, 3);
            for (size_t n = 0; n < maxToEncode; n++) {
                inTypes |= (uint8_t)(encodeType(fip->inputDescs[n].type)
                                     << (2 * n));
            }
            ZL_WC_push(&out, inTypes);
        }
        if (fip->numInputs > 3) {
            uint8_t token2     = (fip->numInputs < 19)
                        ? (uint8_t)((fip->numInputs - 4) << 4)
                        : (uint8_t)15 << 4;
            size_t const limit = ZL_MIN(fip->numInputs, 5);
            for (size_t n = 3; n < limit; n++) {
                token2 |= (uint8_t)(encodeType(fip->inputDescs[n].type)
                                    << (2 * (n - 3)));
            }
            ZL_WC_push(&out, token2);
        }
        if (fip->numInputs > 18) {
            uint8_t token3 = (fip->numInputs > 273)
                    ? 255
                    : (uint8_t)(fip->numInputs - 19);
            ZL_WC_push(&out, token3);
        }
        if (fip->numInputs > 273) {
            ZL_RET_R_IF_GT(
                    userBuffers_invalidNum,
                    fip->numInputs,
                    ZL_ENCODER_INPUT_LIMIT);
            ZL_WC_pushLE16(&out, (uint16_t)(fip->numInputs - 274));
        }
        if (fip->numInputs > 5) {
            uint8_t* hPtr = ZL_WC_ptr(&out);
            for (size_t n = 5; n < fip->numInputs;) {
                uint64_t inTypes   = 0;
                size_t const limit = ZL_MIN(n + 32, fip->numInputs);
                for (; n < limit; n++) {
                    size_t const shift = (2 * (n - 5)) % 64;
                    inTypes |= (uint64_t)encodeType(fip->inputDescs[n].type)
                            << shift;
                }
                // must be written using Little Endian convention
                ZL_writeLE64(hPtr, inTypes);
                hPtr += 8;
            }
            size_t const bytesNeeded = ((fip->numInputs - 5) + 3) / 4;
            ZL_WC_advance(&out, bytesNeeded);
        }
    } else if (encoder->formatVersion == 14) {
        // Support for Single Typed Input
        ZL_RET_R_IF_GT(
                graph_invalidNumInputs,
                fip->numInputs,
                1,
                "Format version 14 only supports 1 Typed Input");
        ZL_WC_push(&out, encodeType(fip->inputDescs[0].type));
    } else {
        // formatVersion <= 13 : single serial input, no type header
        ZL_RET_R_IF_GT(
                graph_invalidNumInputs,
                fip->numInputs,
                1,
                "Format version %u only supports 1 Serial Input",
                encoder->formatVersion);
        ZL_RET_R_IF_NE(
                streamType_incorrect,
                encodeType(fip->inputDescs[0].type),
                0,
                "Format version %u only supports 1 Serial Input",
                encoder->formatVersion);
    }

    // Store Sizes of Inputs
    // @note (@cyan): currently, input size is presumed always known
    ZL_RET_R_IF_ERR(EFH_encodeInputSizes(
            &out, fip->inputDescs, fip->numInputs, encoder->formatVersion));

    // Store variable-length comment
    if (encoder->formatVersion >= ZL_COMMENT_VERSION_MIN
        && fip->fprop->hasComment) {
        ZL_RET_R_IF_GT(
                graph_invalid,
                fip->comment.size,
                ZL_MAX_HEADER_COMMENT_SIZE_LIMIT);
        ZL_WC_pushVarint(&out, fip->comment.size);
        ZL_WC_shove(&out, (const uint8_t*)fip->comment.data, fip->comment.size);
    }

    if ((encoder->formatVersion >= ZL_CHUNK_VERSION_MIN)
        && fip->fprop->hasCompressedChecksum) {
        // Frame header checksum
        uint64_t const fhchk =
                XXH3_64bits(ZL_WC_begin(&out), ZL_WC_size(&out)) & 255;
        ZL_WC_push(&out, (uint8_t)fhchk);
    }

    ZL_DLOG(BLOCK, "frame header size: %zu bytes", ZL_WC_size(&out));
    ZL_ASSERT_LE(ZL_WC_size(&out), hsBound);
    return ZL_returnValue(ZL_WC_size(&out));
}

static ZL_Report writeFrameHeaderV3orMore(
        const EFH_Interface* encoder,
        void* const dst,
        size_t dstCapacity,
        const EFH_FrameInfo* fip)
{
    EFH_Workspace wksp = { 0 };
    ZL_RET_R_IF_ERR(EFH_Workspace_init(
            &wksp, fip->numInputs, 0, 0, 0, dst, dstCapacity));
    ZL_Report ret =
            writeFrameHeader_internal(encoder, wksp.dst, wksp.dstCapacity, fip);
    if (!ZL_isError(ret) && wksp.dst != dst) {
        if (dstCapacity >= ZL_validResult(ret)) {
            memcpy(dst, wksp.dst, ZL_validResult(ret));
        } else {
            ret = ZL_REPORT_ERROR(
                    dstCapacity_tooSmall,
                    "Frame header requires exactly %zu bytes, but dstCapacity is %zu bytes.",
                    ZL_validResult(ret),
                    dstCapacity);
        }
    }
    EFH_Workspace_destroy(&wksp);
    return ret;
}

// writeChunkHeaderV8_internal() :
// Note : @dstCapacity must be large enough to write the chunk header,
//        otherwise the function will return an error
// @return : nb of bytes written into @dst (necessarily <= @dstCapacity)
static ZL_Report writeChunkHeaderV8_internal(
        EFH_Interface const* encoder,
        void* const dst,
        size_t dstCapacity,
        const ZL_FrameProperties* fprop,
        const GraphInfo* gip,
        EFH_Workspace* wksp)
{
    /* @note (@cyan): the bound could be tightened a bit */
    ZL_TRY_LET_R(
            hsBound,
            computeFHBound(
                    gip->nbSessionInputs,
                    gip->nbTransforms,
                    gip->nbStoredBuffs,
                    gip->nbDistances));
    ZL_DLOG(FRAME,
            "writeChunkHeaderV8_internal (nbInputs=%zu, maxBound=%zu bytes)",
            gip->nbSessionInputs,
            hsBound);
    ZL_RET_R_IF_LT(internalBuffer_tooSmall, dstCapacity, hsBound);
    ZL_ASSERT_GE(encoder->formatVersion, 8);

    ZL_WC out = ZL_WC_wrap(dst, dstCapacity);

    size_t const nbCodecs = gip->nbTransforms;
    size_t const nbBuffs  = gip->nbStoredBuffs;

    ZL_ASSERT_GE(nbBuffs, 1);
    ZL_RET_R_IF_GE(
            corruption, nbCodecs, ZL_runtimeNodeLimit(encoder->formatVersion));
    ZL_RET_R_IF_GE(
            corruption,
            nbBuffs - 1,
            ZL_runtimeStreamLimit(encoder->formatVersion));
    ZL_RET_R_IF_EQ(corruption, nbBuffs, 0);

    if (encoder->formatVersion < 9) {
        ZL_ASSERT_LT(nbCodecs, 256);
        ZL_WC_push(&out, (uint8_t)nbCodecs);

        ZL_ASSERT_LE(nbBuffs, 256);
        ZL_WC_push(&out, (uint8_t)(nbBuffs - 1));
    } else {
        ZL_WC_pushVarint(
                &out,
                nbCodecs + (encoder->formatVersion >= ZL_CHUNK_VERSION_MIN));
        ZL_WC_pushVarint(&out, nbBuffs - 1);
    }

    // Version 4 added content & compressed checksums,
    // adding a header byte to determine if checksumming is enabled.
    // This is moved to Frame Header in v21
    ZL_ASSERT_GT(encoder->formatVersion, 4);
    if (4 <= encoder->formatVersion
        && encoder->formatVersion < ZL_CHUNK_VERSION_MIN) {
        uint8_t flags = 0;
        if (fprop->hasContentChecksum)
            flags |= 1 << 0;
        if (fprop->hasCompressedChecksum)
            flags |= 1 << 1;
        ZL_WC_push(&out, flags);
    }

    /* Encode Transform's formatIDs */
    {
        uint8_t* const trt = wksp->scratch3;
        for (size_t u = 0; u < nbCodecs; u++) {
            ZL_DLOG(FRAME,
                    "transform %u has ID %u (type:%u, jumps:%i)",
                    (unsigned)u,
                    gip->trInfo[u].trid,
                    gip->trInfo[u].trt,
                    gip->distances[u]);
            trt[u] = (uint8_t)gip->trInfo[u].trt;
        }
        compressTrt(&out, trt, nbCodecs);

        uint32_t* const array32 = wksp->scratch0;
        for (size_t u = 0; u < nbCodecs; u++) {
            array32[u] = (uint32_t)gip->trInfo[u].trid;
        }
        compressTrID(
                &out, array32, nbCodecs, trt, wksp->scratch1, wksp->scratch2);
    }

    /* Encode Transform's private header's sizes */
    {
        uint32_t* const array32 = wksp->scratch0;
        for (size_t u = 0; u < nbCodecs; u++) {
            array32[u] = (uint32_t)gip->trHSizes[u];
        }
        compressTrHSize(&out, array32, nbCodecs, wksp->scratch1);
    }

    /* Encode nb of Variable Outputs (v8+ only) */
    if (encoder->formatVersion >= 8) {
        uint32_t* const array32 = wksp->scratch0;
        for (size_t u = 0; u < nbCodecs; u++) {
            array32[u] = (uint32_t)gip->nbVOs[u];
        }
        compressNbVOs(
                &out,
                array32,
                nbCodecs,
                wksp->scratch1,
                encoder->formatVersion);
    }

    /* Encode nb of Inputs (v16+ only) */
    size_t totalNbRegens = 0;
    if (encoder->formatVersion >= 16) {
        uint32_t* const array32 = wksp->scratch0;
        for (size_t u = 0; u < nbCodecs; u++) {
            array32[u] = (uint32_t)gip->nbTrInputs[u];
            ZL_ASSERT_GE(array32[u], 1);
            totalNbRegens += array32[u];
        }
        ZL_ASSERT_EQ(totalNbRegens, gip->nbDistances);
        compressNumInputs(
                &out,
                array32,
                nbCodecs,
                wksp->scratch1,
                encoder->formatVersion);
    } else {
        // v15-: MI Transform's input count must be always 1
        totalNbRegens = nbCodecs;
        for (size_t u = 0; u < nbCodecs; u++) {
            ZL_RET_R_IF_NE(
                    node_versionMismatch,
                    gip->nbTrInputs[u],
                    1,
                    "Version %u encoding format does not support Transforms featuring 2+ inputs",
                    encoder->formatVersion);
        }
    }

    /* Encode Regen Distances */
    {
        uint32_t* const array32 = wksp->scratch0;
        for (size_t u = 0; u < totalNbRegens; u++) {
            /* distances are necessarily >= 1,
             * so let's reduce the range by 1 */
            ZL_ASSERT_GE((uint32_t)gip->distances[u], 1);
            array32[u] = (uint32_t)gip->distances[u] - 1;
        }
        compressStreamDistances(&out, array32, totalNbRegens, nbBuffs - 1);
    }

    /* Encode Stream's buffer sizes */
    {
        uint32_t* const buffSizes = wksp->buffScratch0;
        for (size_t u = 1; u < nbBuffs; u++) {
            ZL_ASSERT_LT(gip->storedBuffs[u].size, UINT_MAX);
            buffSizes[u - 1] = (uint32_t)gip->storedBuffs[u].size;
        }
        ZL_ASSERT_GE(nbBuffs, 1);
        compressStrSizes(&out, buffSizes, nbBuffs - 1);
    }

    ZL_ASSERT_LE(ZL_WC_size(&out), hsBound);

    ZL_DLOG(BLOCK, "chunk header size: %zu bytes", ZL_WC_size(&out));
    return ZL_returnValue(ZL_WC_size(&out));
}

static ZL_Report writeChunkHeaderV8orMore(
        const EFH_Interface* encoder,
        void* const dst,
        size_t dstCapacity,
        const ZL_FrameProperties* info,
        const GraphInfo* gip)
{
    ZL_DLOG(SEQ, "writeChunkHeaderV8orMore");
    EFH_Workspace wksp = { 0 };
    ZL_RET_R_IF_ERR(EFH_Workspace_init(
            &wksp,
            gip->nbSessionInputs,
            gip->nbTransforms,
            gip->nbDistances,
            gip->nbStoredBuffs,
            dst,
            dstCapacity));
    ZL_Report ret = writeChunkHeaderV8_internal(
            encoder, wksp.dst, wksp.dstCapacity, info, gip, &wksp);
    if (!ZL_isError(ret) && wksp.dst != dst) {
        if (dstCapacity >= ZL_validResult(ret)) {
            memcpy(dst, wksp.dst, ZL_validResult(ret));
        } else {
            ret = ZL_REPORT_ERROR(
                    dstCapacity_tooSmall,
                    "Chunk header requires exactly %zu bytes, but dstCapacity is %zu bytes.",
                    ZL_validResult(ret),
                    dstCapacity);
        }
    }
    EFH_Workspace_destroy(&wksp);
    if (ZL_isError(ret)) {
        ZL_DLOG(ERROR, "writeChunkHeaderV8orMore() error");
    }
    return ret;
}

static EFH_Interface const efhV8orMore = { .writeFrameHeader =
                                                   writeFrameHeaderV3orMore,
                                           .writeChunkHeader =
                                                   writeChunkHeaderV8orMore };

EFH_Interface EFH_getFrameHeaderEncoder(uint32_t formatVersion)
{
    ZL_ASSERT(ZL_isFormatVersionSupported(formatVersion));
    EFH_Interface encoder;
    if (formatVersion >= 8) {
        encoder = efhV8orMore;
    } else {
        ZL_ASSERT_FAIL("Format version is supposed to be validated.");
    }
    encoder.formatVersion = formatVersion;
    return encoder;
}

ZL_Report EFH_writeFrameHeader(
        void* dst,
        size_t dstCapacity,
        const EFH_FrameInfo* fip,
        uint32_t version)
{
    EFH_Interface const encoder = EFH_getFrameHeaderEncoder(version);
    return encoder.writeFrameHeader(&encoder, dst, dstCapacity, fip);
}

ZL_Report EFH_writeChunkHeader(
        void* const dst,
        size_t dstCapacity,
        const ZL_FrameProperties* info,
        const GraphInfo* gip,
        uint32_t version)
{
    EFH_Interface const encoder = EFH_getFrameHeaderEncoder(version);
    return encoder.writeChunkHeader(&encoder, dst, dstCapacity, info, gip);
}
