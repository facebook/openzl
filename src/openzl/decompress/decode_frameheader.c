// Copyright (c) Meta Platforms, Inc. and affiliates.

// Main decompression function

#include "openzl/decompress/decode_frameheader.h" // DFH_Interface

#include <stdint.h>

#include "openzl/codecs/bitpack/common_bitpack_kernel.h" // ZS_bitpackDecode32
#include "openzl/codecs/dispatch_by_tag/decode_dispatch_by_tag_kernel.h" // ZS_DispatchByTag_decode
#include "openzl/common/allocation.h" //ZL_malloc
#include "openzl/common/assertion.h"  // ZS_ASSERT_*
#include "openzl/common/cursor.h"     // ZL_RC
#include "openzl/common/limits.h"
#include "openzl/common/wire_format.h"  // ZL_StandardTransformID_numBits
#include "openzl/dict/dict_constants.h" // ZL_MAX_DICTS_PER_BUNDLE
#include "openzl/fse/fse.h"             // FSE_getErrorName
#include "openzl/fse/hist.h"            // HIST_count_simple
#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h"    // ZL_readLE32, etc.
#include "openzl/shared/xxhash.h" // XXH3_64bits
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h" // ZS2_* public methods
#include "openzl/zl_errors.h"     // ZL_Report
#include "openzl/zl_unique_id.h"
#include "openzl/zl_version.h"

// -------------------------------------------------
// General Frame Information, to start decompression
// -------------------------------------------------
struct ZL_FrameInfo {
    size_t formatVersion;
    ZL_FrameProperties properties;
    size_t nbOutputs;
    ZL_Type* types;
    uint64_t* decompressedSizes;
    uint64_t* numElts;
    size_t frameHeaderSize;
    void* comment;
    size_t commentSize;
    ZL_BundleID bundleID; // v24+: dict bundle ID
};

static ZL_Type decodeType(uint8_t et)
{
    ZL_ASSERT_LT(et, 4);
    switch (et) {
        case 0:
            return ZL_Type_serial;
        case 1:
            return ZL_Type_struct;
        case 2:
            return ZL_Type_numeric;
        case 3:
            return ZL_Type_string;
        default:
            ZL_ASSERT_FAIL("invalid type encoding");
            return ZL_Type_serial;
    }
}

static ZL_Report DFH_decodeNbOutputs(
        const void* cSrc,
        size_t cSize,
        size_t* consumedPtr,
        size_t formatVersion)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT_LE(*consumedPtr, cSize);
    if (formatVersion <= 14) {
        // single input only
        *consumedPtr += (formatVersion == 14);
        return ZL_returnValue(1);
    } else if (formatVersion < ZL_CHUNK_VERSION_MIN) {
        ZL_ERR_IF_LT(cSize, *consumedPtr + 1 + 4, srcSize_tooSmall);
        size_t nbOutputs =
                (size_t)(((const uint8_t*)cSrc)[*consumedPtr] >> 6) + 1;
        *consumedPtr += 1;
        if (nbOutputs == 4) {
            uint8_t token2 = ((const uint8_t*)cSrc)[*consumedPtr];
            nbOutputs      = (size_t)(token2 >> 4) + 4;
            *consumedPtr += 1;
        }
        if (nbOutputs == 19) {
            uint8_t token3 = ((const uint8_t*)cSrc)[*consumedPtr];
            nbOutputs      = (size_t)token3 + 19;
            *consumedPtr += 1;
        }
        if (nbOutputs == 274) {
            uint16_t token4 =
                    ZL_readLE16(((const uint8_t*)cSrc) + *consumedPtr);
            // Note(@Cyan): limited by format to 274+65535=65809 outputs
            // Encoder enforces a lower limit ZL_ENCODER_INPUT_LIMIT == 2048
            nbOutputs = (size_t)token4 + 274;
            *consumedPtr += 2;
        }
        return ZL_returnValue(nbOutputs);
    } else { // format >= ZL_CHUNK_VERSION_MIN
        ZL_ERR_IF_LT(cSize, *consumedPtr + 1 + 4, srcSize_tooSmall);
        uint8_t token1 = ((const uint8_t*)cSrc)[*consumedPtr];
        *consumedPtr += 1;
        size_t nbOutputs = (size_t)(token1 & 15);
        if (nbOutputs == 15) {
            uint8_t token2 = ((const uint8_t*)cSrc)[*consumedPtr];
            *consumedPtr += 1;
            nbOutputs = ((size_t)token2 << 4) + ((size_t)token1 >> 4) + 15;
        }
        return ZL_returnValue(nbOutputs);
    }
}

static ZL_Report DFH_decoderOutputTypes(
        ZL_Type* types,
        size_t nbOutputs,
        const void* cSrc,
        size_t cSize,
        size_t* consumedPtr,
        size_t formatVersion)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (formatVersion < 14) {
        // no type
        types[0] = ZL_Type_serial;
    } else if (formatVersion < ZL_CHUNK_VERSION_MIN) {
        size_t firstToken = ZL_MIN(nbOutputs, 3);
        ZL_ERR_IF_LT(cSize, 5, srcSize_tooSmall);
        for (size_t n = 0; n < firstToken; n++) {
            size_t shift = n * 2;
            types[n] = decodeType(((((const uint8_t*)cSrc)[4]) >> shift) & 3);
        }
        if (nbOutputs > 3) {
            size_t const limit = ZL_MIN(nbOutputs, 5);
            ZL_ERR_IF_LT(cSize, 6, srcSize_tooSmall);
            for (size_t n = 3; n < limit; n++) {
                size_t shift = (n - 3) * 2;
                types[n] =
                        decodeType(((((const uint8_t*)cSrc)[5]) >> shift) & 3);
            }
        }
        if (nbOutputs > 5) {
            size_t const neededBytes = ((nbOutputs - 5) + 3) / 4;
            ZL_ERR_IF_LT(cSize, *consumedPtr + neededBytes, srcSize_tooSmall);
            uint8_t token = 0;
            for (size_t n = 5; n < nbOutputs; n++) {
                size_t shift = ((n - 5) % 4) * 2;
                if (shift == 0) {
                    token = ((const uint8_t*)cSrc)[*consumedPtr];
                    *consumedPtr += 1;
                }
                types[n] = decodeType((token >> shift) & 3);
            }
        }
    } else { // format >= ZL_CHUNK_VERSION_MIN
        size_t done = 0;
        if (nbOutputs <= 14) {
            // First 2 output types stored in first byte
            // (the same byte that DFH_decodeNbOutputs already consumed)
            uint8_t const token1 = ((const uint8_t*)cSrc)[*consumedPtr - 1];
            size_t const max     = ZL_MIN(2, nbOutputs);
            for (size_t n = 0; n < max; n++) {
                size_t const shift = n * 2 + 4;
                types[n]           = decodeType((token1 >> shift) & 3);
            }
            if (nbOutputs <= 2)
                return ZL_returnSuccess();
            done = 2;
        }
        /* nbOutputs > 2 */
        size_t const neededBytes = (nbOutputs - done + 3) / 4;
        ZL_ERR_IF_LT(cSize, *consumedPtr + neededBytes, srcSize_tooSmall);
        uint8_t token = 0;
        for (size_t n = done; n < nbOutputs; n++) {
            size_t shift = ((n - done) % 4) * 2;
            if (shift == 0) {
                token = ((const uint8_t*)cSrc)[*consumedPtr];
                *consumedPtr += 1;
            }
            types[n] = decodeType((token >> shift) & 3);
        }
    }
    return ZL_returnSuccess();
}

static ZL_Report DFH_decodeOutputSizes_v20(
        uint64_t* dSizes,
        uint64_t* numElts,
        const void* src,
        size_t cSize,
        size_t nbOutputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    (void)numElts; // not used for versions <= 20
    ZL_ERR_IF_LT(cSize, 4 * nbOutputs, srcSize_tooSmall);
    for (size_t n = 0; n < nbOutputs; n++) {
        dSizes[n] = ZL_readLE32((const char*)src + 4 * n);
    }
    return ZL_returnValue(4 * nbOutputs);
}

static ZL_Report DFH_decodeOutputSizes_v21(
        uint64_t* dSizes,
        uint64_t* numElts,
        const void* src,
        size_t cSize,
        const ZL_Type* types,
        size_t nbOutputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    const uint8_t* ptr = src;
    const uint8_t* end = ptr + cSize;

    ZL_ERR_IF_LT(cSize, 1, srcSize_tooSmall);
    {
        uint8_t const firstByte = ((const uint8_t*)src)[0];
        // 0 means "final output size(s) are unknown"
        ZL_ERR_IF_EQ(
                firstByte,
                0x0,
                temporaryLibraryLimitation,
                "doesn't support unknown size outputs for the time being");
    }

    for (size_t n = 0; n < nbOutputs; n++) {
        ZL_TRY_LET(uint64_t, v64, ZL_varintDecode(&ptr, end));
        ZL_ERR_IF_EQ(
                v64,
                0,
                temporaryLibraryLimitation,
                "does not support unknown decompressed size");
        dSizes[n] = v64 - 1;
    }

    // decode Nb strings
    for (size_t n = 0; n < nbOutputs; n++) {
        switch (types[n]) {
            case ZL_Type_struct:
            case ZL_Type_numeric:
                // no idea at this stage (unsupported)
                numElts[n] = 0;
                break;
            case ZL_Type_serial:
                numElts[n] = dSizes[n];
                break;
            case ZL_Type_string:
                ZL_TRY_SET(uint64_t, numElts[n], ZL_varintDecode(&ptr, end));
                break;
            default:
                ZL_ASSERT_FAIL("invalid type");
                break;
        }
    }
    return ZL_returnValue((size_t)(ptr - (const uint8_t*)src));
}

/* src: points where output sizes start
 * cSize: readable size in input buffer
 */
static ZL_Report DFH_decodeOutputSizes(
        uint64_t* dSizes,
        uint64_t* numElts,
        const void* src,
        size_t cSize,
        const ZL_Type* types,
        size_t nbOutputs,
        size_t formatVersion)
{
    ZL_DLOG(BLOCK, "DFH_decodeOutputSizes (nbOutputs = %zu)", nbOutputs);
    if (formatVersion < ZL_CHUNK_VERSION_MIN)
        return DFH_decodeOutputSizes_v20(
                dSizes, numElts, src, cSize, nbOutputs);
    return DFH_decodeOutputSizes_v21(
            dSizes, numElts, src, cSize, types, nbOutputs);
}

static ZL_Report DFH_FrameInfo_decodeFrameHeader(
        ZL_FrameInfo* zfi,
        const void* cSrc,
        size_t cSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_DLOG(BLOCK, "*****   DFH_FrameInfo_decodeFrameHeader   ***** \n");
    memset(zfi, 0, sizeof(*zfi));
    ZL_TRY_LET(
            size_t, formatVersion, ZL_getFormatVersionFromFrame(cSrc, cSize));
    ZL_ASSERT_NN(zfi);
    zfi->formatVersion = formatVersion;
    size_t consumed    = 4;
    const uint8_t* ptr = (const uint8_t*)cSrc;

    /* frame properties, such as checksums */
    if (zfi->formatVersion >= ZL_CHUNK_VERSION_MIN) {
        ZL_ERR_IF_LE(cSize, consumed, srcSize_tooSmall);
        uint8_t const flags                   = ptr[consumed++];
        zfi->properties.hasContentChecksum    = ((flags & (1 << 0)) != 0);
        zfi->properties.hasCompressedChecksum = ((flags & (1 << 1)) != 0);
        zfi->properties.hasComment            = ((flags & (1 << 2)) != 0);
        zfi->properties.hasBundleID =
                (formatVersion >= ZL_MATERIALIZED_DICT_VERSION_MIN)
                && ((flags & (1 << 3)) != 0);
    }

    // Decode dict bundle ID (v24+): 1-byte size + variable-length ID
    if (zfi->properties.hasBundleID) {
        ZL_ERR_IF_LE(cSize, consumed, corruption);
        uint8_t const encLen = ptr[consumed++];
        ZL_ERR_IF_EQ(
                encLen,
                0,
                corruption,
                "bundleID size byte is 0 but flag is set");
        ZL_ERR_IF_GT(
                encLen,
                ZL_UNIQUE_ID_SIZE,
                corruption,
                "bundleID size too large");
        ZL_ERR_IF_LT(cSize, consumed + encLen, corruption);
        memset(zfi->bundleID.id.bytes, 0, ZL_UNIQUE_ID_SIZE);
        memcpy(zfi->bundleID.id.bytes, ptr + consumed, encLen);
        consumed += encLen;
    }

    /* nb of outputs */
    ZL_TRY_SET(
            size_t,
            zfi->nbOutputs,
            DFH_decodeNbOutputs(cSrc, cSize, &consumed, formatVersion));
    ZL_DLOG(BLOCK,
            "frame format %u, hosts %u output streams",
            formatVersion,
            zfi->nbOutputs);
    ZL_ERR_IF_GT(
            zfi->nbOutputs,
            ZL_runtimeInputLimit((unsigned)formatVersion),
            outputs_tooNumerous,
            "Too many outputs for this format version");
    // @note for the time being, do not support 0 output
    // (which is different from an empty output).
    ZL_ERR_IF_EQ(zfi->nbOutputs, 0, GENERIC, "doesn't support 0 output");

    // Decode Output Types
    ALLOC_MALLOC_CHECKED(ZL_Type, types, zfi->nbOutputs);
    zfi->types = types;
    ZL_ERR_IF_ERR(DFH_decoderOutputTypes(
            types, zfi->nbOutputs, cSrc, cSize, &consumed, formatVersion));

    // Decode Output Sizes
    ZL_ASSERT_LE(consumed, cSize);
    ALLOC_MALLOC_CHECKED(uint64_t, dSizes, zfi->nbOutputs);
    zfi->decompressedSizes = dSizes;
    ALLOC_MALLOC_CHECKED(uint64_t, numElts, zfi->nbOutputs);
    zfi->numElts = numElts;
    ZL_TRY_LET(
            size_t,
            oss,
            DFH_decodeOutputSizes(
                    dSizes,
                    numElts,
                    ptr + consumed,
                    cSize - consumed,
                    types,
                    zfi->nbOutputs,
                    formatVersion));
    consumed += oss;
    ZL_DLOG(BLOCK,
            "DFH_FrameInfo_decodeFrameHeader consumed %zu bytes from header",
            consumed);

    // Decode Comment
    if (formatVersion >= ZL_COMMENT_VERSION_MIN && zfi->properties.hasComment) {
        const uint8_t* cSrcCur = ptr + consumed;
        ZL_TRY_LET_CONST(
                uint64_t, commentSize, ZL_varintDecode(&cSrcCur, ptr + cSize));
        ZL_ERR_IF_EQ(
                commentSize,
                0,
                corruption,
                "Invalid frame header: comment size cannot be 0 when flag is set.");
        ZL_ERR_IF_GT(
                commentSize,
                ZL_MAX_HEADER_COMMENT_SIZE_LIMIT,
                corruption,
                "Invalid frame header: frame max comment size exceeded.");
        zfi->commentSize = commentSize;
        consumed += (size_t)(cSrcCur - (ptr + consumed));
        zfi->comment = ZL_malloc(commentSize);
        ZL_ERR_IF_NULL(zfi->comment, allocation);
        ZL_ERR_IF_GT(consumed + commentSize, cSize, corruption);
        memcpy(zfi->comment, ptr + consumed, commentSize);
        consumed += commentSize;
    }

    if ((formatVersion >= ZL_CHUNK_VERSION_MIN)
        && zfi->properties.hasCompressedChecksum) {
        // Frame header checksum
        uint64_t const fhchk = XXH3_64bits(cSrc, consumed) & 255;
        ZL_ERR_IF_LT(cSize, consumed + 1, srcSize_tooSmall);
        uint64_t const rhchk = ((const uint8_t*)cSrc)[consumed++];
        ZL_ERR_IF_NE(fhchk, rhchk, corruption);
    }

    if (formatVersion >= ZL_CHUNK_VERSION_MIN) {
        /* for version < ZL_CHUNK_VERSION_MIN, there is no separation between
         * frame and block headers */
        zfi->frameHeaderSize = consumed;
    }

    return ZL_returnValue(consumed);
}

ZL_FrameInfo* ZL_FrameInfo_create(const void* cSrc, size_t cSize)
{
    ZL_FrameInfo* const zfi = ZL_malloc(sizeof(*zfi));
    if (!zfi)
        return NULL;
    if (ZL_isError(DFH_FrameInfo_decodeFrameHeader(zfi, cSrc, cSize))) {
        ZL_FrameInfo_free(zfi);
        return NULL;
    }
    return zfi;
}

void ZL_FrameInfo_free(ZL_FrameInfo* zfi)
{
    if (zfi == NULL)
        return;
    ZL_free(zfi->types);
    ZL_free(zfi->decompressedSizes);
    ZL_free(zfi->numElts);
    ZL_free(zfi->comment);
    ZL_free(zfi);
}

ZL_Report ZL_FrameInfo_getFormatVersion(const ZL_FrameInfo* zfi)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_NULL(zfi, GENERIC);
    return ZL_returnValue(zfi->formatVersion);
}

ZL_Report ZL_FrameInfo_getNumOutputs(const ZL_FrameInfo* zfi)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_NULL(zfi, GENERIC);
    ZL_ASSERT_GT(zfi->nbOutputs, 0);
    return ZL_returnValue(zfi->nbOutputs);
}

ZL_Report ZL_FrameInfo_getOutputType(const ZL_FrameInfo* zfi, int outputID)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_DLOG(SEQ, "ZL_FrameInfo_getOutputType (outputID:%i)", outputID);
    ZL_ASSERT_NN(zfi);
    ZL_ERR_IF_GE(
            outputID,
            (int)zfi->nbOutputs,
            outputID_invalid,
            "This frame only contains %zu outputs",
            zfi->nbOutputs);
    return ZL_returnValue((size_t)zfi->types[outputID]);
}

ZL_Report ZL_FrameInfo_getDecompressedSize(
        const ZL_FrameInfo* zfi,
        int outputID)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT_NN(zfi);
    ZL_ERR_IF_GE(
            outputID,
            (int)zfi->nbOutputs,
            outputID_invalid,
            "This frame only contains %zu outputs",
            zfi->nbOutputs);
    ZL_ASSERT_NN(zfi->decompressedSizes);
    return ZL_returnValue(zfi->decompressedSizes[outputID]);
}

ZL_Report ZL_FrameInfo_getNumElts(const ZL_FrameInfo* zfi, int outputID)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT_NN(zfi);
    ZL_ERR_IF_GE(
            outputID,
            (int)zfi->nbOutputs,
            outputID_invalid,
            "This frame only contains %zu outputs",
            zfi->nbOutputs);
    ZL_ERR_IF_LT(
            zfi->formatVersion,
            ZL_CHUNK_VERSION_MIN,
            GENERIC,
            "This method only works on frames with version >= %zu",
            ZL_CHUNK_VERSION_MIN);

    // Currently only supports string & serial
    ZL_ASSERT_NN(zfi->types);
    ZL_Type const outType = zfi->types[outputID];
    ZL_ERR_IF_EQ(
            outType,
            ZL_Type_struct,
            temporaryLibraryLimitation,
            "this method doesn't support Struct type yet");
    ZL_ERR_IF_EQ(
            outType,
            ZL_Type_numeric,
            temporaryLibraryLimitation,
            "this method doesn't support Numeric type yet");

    ZL_ASSERT_NN(zfi->numElts);
    return ZL_returnValue(zfi->numElts[outputID]);
}

ZL_RESULT_OF(ZL_Comment) ZL_FrameInfo_getComment(const ZL_FrameInfo* zfi)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_Comment, NULL);
    ZL_Comment comment;
    ZL_ASSERT_NN(zfi);
    ZL_ERR_IF_LT(
            zfi->formatVersion,
            ZL_COMMENT_VERSION_MIN,
            GENERIC,
            "This method only works on frames with version >= %zu",
            ZL_COMMENT_VERSION_MIN);
    comment.data = zfi->comment;
    comment.size = zfi->commentSize;
    return ZL_WRAP_VALUE(comment);
}

const ZL_BundleID* ZL_FrameInfo_getBundleID(const ZL_FrameInfo* zfi)
{
    ZL_ASSERT_NN(zfi);
    return ZL_UniqueID_isValid(&zfi->bundleID.id) ? &zfi->bundleID : NULL;
}

// --------------------------
// Header parsing
// --------------------------

typedef struct DFH_Interface_s {
    /// @returns The decompressed size of the frame.
    ZL_Report (*getDecompressedSize)(
            struct DFH_Interface_s const*,
            void const*,
            size_t);

    /// @returns The compressed size of the frame.
    ZL_Report (*getCompressedSize)(
            struct DFH_Interface_s const*,
            void const*,
            size_t);

    /// @returns The size of the frame header for debugging.
    ZL_Report (
            *getHeaderSize)(struct DFH_Interface_s const*, void const*, size_t);

    /**
     * Decode the frame header from the source into the DFH_Struct.
     * @returns The size of the frame header on success, or an error code.
     */
    ZL_Report (*decodeFrameHeader)(
            DFH_Struct*,
            void const*,
            size_t,
            uint32_t formatVersion);

    /**
     * Decode the frame header from the source into the DFH_Struct.
     * @returns The size of the frame header on success, or an error code.
     */
    ZL_Report (*decodeChunkHeader)(
            struct DFH_Interface_s const*,
            DFH_Struct*,
            void const*,
            size_t);

    uint32_t formatVersion;
} DFH_Interface;

/**
 * @pre ZL_isFormatVersionSupported(formatVersion)
 * @returns The decoder interface that can decode the given format version.
 */
DFH_Interface DFH_getFrameHeaderDecoder(uint32_t formatVersion);

void DFH_init(DFH_Struct* dfh)
{
    memset(dfh, 0, sizeof(DFH_Struct));
    VECTOR_INIT(
            dfh->storedStreamSizes,
            ZL_runtimeStreamLimit(ZL_MAX_FORMAT_VERSION));
    VECTOR_INIT(dfh->nodes, ZL_runtimeNodeLimit(ZL_MAX_FORMAT_VERSION));
    VECTOR_INIT(
            dfh->regenDistances, ZL_runtimeStreamLimit(ZL_MAX_FORMAT_VERSION));
}

void DFH_destroy(DFH_Struct* dfh)
{
    if (dfh == NULL)
        return;
    VECTOR_DESTROY(dfh->storedStreamSizes);
    VECTOR_DESTROY(dfh->nodes);
    VECTOR_DESTROY(dfh->regenDistances);
    ZL_FrameInfo_free(dfh->frameinfo);
    dfh->frameinfo = NULL;
}

// Public Symbol
ZL_Report ZL_getDecompressedSize(const void* cSrc, size_t cSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_TRY_LET(
            size_t, formatVersion, ZL_getFormatVersionFromFrame(cSrc, cSize));
    DFH_Interface const decoder =
            DFH_getFrameHeaderDecoder((uint32_t)formatVersion);
    return decoder.getDecompressedSize(&decoder, cSrc, cSize);
}

ZL_Report ZL_getNumOutputs(const void* cSrc, size_t cSize)
{
    ZL_FrameInfo* fi = ZL_FrameInfo_create(cSrc, cSize);
    ZL_Report ret    = ZL_FrameInfo_getNumOutputs(fi);
    ZL_FrameInfo_free(fi);
    return ret;
}

// @note (@cyan): this method duplicates header parsing logic, which is fragile.
// The goal is likely to avoid paying the cost of a full header decoding,
// but it creates an additional sync burden when wire format evolves.
ZL_Report ZL_getOutputType(ZL_Type* typePtr, const void* cSrc, size_t cSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_TRY_LET(
            size_t, formatVersion, ZL_getFormatVersionFromFrame(cSrc, cSize));
    if (formatVersion <= 13) {
        *typePtr = ZL_Type_serial;
    } else if (formatVersion < ZL_CHUNK_VERSION_MIN) {
        ZL_ERR_IF_LT(cSize, 5, srcSize_tooSmall);
        uint8_t const typeEncoded = ((const uint8_t*)cSrc)[4];
        ZL_ERR_IF_GT(typeEncoded, 3, invalidRequest_singleOutputFrameOnly);
        *typePtr = decodeType(typeEncoded);
    } else { // formatVersion >= ZL_CHUNK_VERSION_MIN
        ZL_ERR_IF_LT(cSize, 6, srcSize_tooSmall);
        uint8_t const flags = ((const uint8_t*)cSrc)[4];
        int const hasBundleID =
                (formatVersion >= ZL_MATERIALIZED_DICT_VERSION_MIN)
                && ((flags & (1 << 3)) != 0);
        // Skip: flags(1) + [sizebyte(1) + bundleID(sizeByte)] if present
        size_t bundleIDFieldLen = 0;
        if (hasBundleID) {
            uint8_t const idLen = ((const uint8_t*)cSrc)[5];
            bundleIDFieldLen    = 1 + idLen;
        }
        size_t const tokenOffset = 5 + bundleIDFieldLen;
        ZL_ERR_IF_LT(cSize, tokenOffset + 1, srcSize_tooSmall);
        uint8_t const typeEncoded = ((const uint8_t*)cSrc)[tokenOffset];
        ZL_ERR_IF_NE(typeEncoded & 15, 1, invalidRequest_singleOutputFrameOnly);
        *typePtr = decodeType((typeEncoded >> 4) & 3);
    }
    return ZL_returnSuccess();
}

ZL_Report ZL_getCompressedSize(const void* src, size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_DLOG(SEQ, "ZL_getCompressedSize");
    ZL_TRY_LET(
            size_t, formatVersion, ZL_getFormatVersionFromFrame(src, srcSize));
    DFH_Interface const decoder =
            DFH_getFrameHeaderDecoder((uint32_t)formatVersion);
    return decoder.getCompressedSize(&decoder, src, srcSize);
}

ZL_Report ZL_getHeaderSize(const void* src, size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_TRY_LET(
            size_t, formatVersion, ZL_getFormatVersionFromFrame(src, srcSize));
    DFH_Interface const decoder =
            DFH_getFrameHeaderDecoder((uint32_t)formatVersion);
    return decoder.getHeaderSize(&decoder, src, srcSize);
}

int FrameInfo_hasContentChecksum(const ZL_FrameInfo* fi)
{
    ZL_ASSERT_NN(fi);
    return fi->properties.hasContentChecksum;
}

int FrameInfo_hasCompressedChecksum(const ZL_FrameInfo* fi)
{
    ZL_ASSERT_NN(fi);
    return fi->properties.hasCompressedChecksum;
}

size_t FrameInfo_frameHeaderSize(const ZL_FrameInfo* fi)
{
    ZL_ASSERT_NN(fi);
    return fi->frameHeaderSize;
}

static ZL_Report
checkedBitpackDecode8(uint8_t* dst, size_t nbElts, ZL_RC* src, int nbBits)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    // Prevalidated
    ZL_ASSERT_GE(nbBits, 0);

    ZL_ERR_IF_GT(nbBits, 8, GENERIC, "corruption");
    ZL_ERR_IF_GT(
            (nbElts * (size_t)nbBits + 7) / 8,
            ZL_RC_avail(src),
            internalBuffer_tooSmall);
    size_t const srcSize = ZS_bitpackDecode8(
            dst, nbElts, ZL_RC_ptr(src), ZL_RC_avail(src), nbBits);
    ZL_RC_advance(src, srcSize);
    return ZL_returnValue(srcSize);
}

static ZL_Report
checkedBitpackDecode32(uint32_t* dst, size_t nbElts, ZL_RC* src, int nbBits)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    // Prevalidated
    ZL_ASSERT_GE(nbBits, 0);

    ZL_ERR_IF_GT(nbBits, 32, GENERIC, "corruption");
    ZL_ERR_IF_GT(
            (nbElts * (size_t)nbBits + 7) / 8,
            ZL_RC_avail(src),
            internalBuffer_tooSmall);
    size_t const srcSize = ZS_bitpackDecode32(
            dst, nbElts, ZL_RC_ptr(src), ZL_RC_avail(src), nbBits);
    ZL_RC_advance(src, srcSize);
    return ZL_returnValue(srcSize);
}

// Decompress Decoder Types
// Simple bit packing, 1 bit per transform (for the time being)
static ZL_Report decompressTrt(uint8_t array8[], size_t nbFlags, ZL_RC* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_TRY_LET(size_t, r, checkedBitpackDecode8(array8, nbFlags, src, 1));
    ZL_DLOG(BLOCK, "Decoding %zu codec types, using %zu bytes", nbFlags, r);
    return ZL_returnSuccess();
}

// Decompress TransformID
static ZL_Report decompressTrID(
        uint32_t transformIDs[],
        size_t nbTransforms,
        ZL_RC* src,
        const uint8_t trt8[],
        uint32_t scratch[],
        unsigned formatVersion)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (!nbTransforms)
        return ZL_returnSuccess();

    // separate standard TrIDs from custom ones
    // encode them separately
    // standard TrIDs use bitpack
    // custom TrIDs use varint

    unsigned nbTrs[2];
    unsigned maxTrtValue = 1;
    unsigned cardinality = 0;
    size_t const r       = HIST_count_simple(
            nbTrs, &maxTrtValue, &cardinality, trt8, nbTransforms);
    ZL_ERR_IF(
            HIST_isError(r),
            corruption,
            "histogram error: %s",
            FSE_getErrorName(r));
    ZL_ASSERT_LE(maxTrtValue, 1);
    ZL_ASSERT_EQ(nbTrs[0] + nbTrs[1], nbTransforms);

    // start decoding standard nodes
    uint32_t* snodeids = scratch;
    int const nbBits   = ZL_StandardTransformID_numBits(formatVersion);
    ZL_ERR_IF_ERR(checkedBitpackDecode32(snodeids, nbTrs[0], src, nbBits));

    // then decode custom nodes
    uint32_t* cnodeids = scratch + nbTrs[0];
    for (size_t u = 0; u < nbTrs[1]; u++) {
        ZL_RESULT_OF(uint64_t) const res = ZL_RC_popVarint(src);
        ZL_ERR_IF_ERR(res);
        uint64_t const trid64 = ZL_RES_value(res);
        ZL_ERR_IF_GT(trid64, UINT32_MAX, corruption, "Transform ID too large");
        cnodeids[u] = (uint32_t)trid64;
    }

    // then combine
    const void* srcs[2]    = { snodeids, cnodeids };
    const size_t nbElts[2] = { nbTrs[0], nbTrs[1] };
    ZL_ASSERT_EQ(nbElts[0] + nbElts[1], nbTransforms);
    ZS_DispatchByTag_decode(
            transformIDs,
            nbTransforms * sizeof(transformIDs[0]),
            srcs,
            nbElts,
            2,
            4,
            trt8);

    // return distance consumed from @src
    return ZL_returnSuccess();
}

// Decompress Transform's Private Header Sizes
// 0-sizes are bitpacked
// non-zero sizes are varint decoded
static ZL_Report
decompressTrHSize(uint32_t trhSizes[], size_t nbTransforms, ZL_RC* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    // Store 0-size flags in trhSizes temporarily
    // 1s will be replaced by the header size
    ZL_ERR_IF_ERR(checkedBitpackDecode32(trhSizes, nbTransforms, src, 1));

    // decode non-zero private header sizes
    for (size_t u = 0; u < nbTransforms; u++) {
        if (trhSizes[u] != 0) {
            // varint decoded
            ZL_RESULT_OF(uint64_t) const res = ZL_RC_popVarint(src);
            ZL_ERR_IF_ERR(res);
            uint64_t const trhSize64 = ZL_RES_value(res);
            ZL_ERR_IF_GE(
                    trhSize64,
                    UINT32_MAX - 1,
                    corruption,
                    "Transform header size too large");
            trhSizes[u] = (uint32_t)trhSize64 + 1;
        }
    }

    return ZL_returnSuccess();
}

// Decompress Transform's nb of Variable Outputs
// 0-sizes are bitpacked
// non-zero sizes are shifted by -1, then varint encoded
static ZL_Report
decompressNbVOs(uint32_t nbVOs[], size_t nbTransforms, ZL_RC* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    // Store 0-size flags in nbVOs temporarily
    // 1s will be replaced later by actual nbVOs
    ZL_ERR_IF_ERR(checkedBitpackDecode32(nbVOs, nbTransforms, src, 1));

    // decode non-zero nbVOs (8-bit encoded, shifted by 1)
    for (size_t u = 0; u < nbTransforms; u++) {
        if (nbVOs[u] != 0) {
            // Format versions < 9 used byte encoding, but the maximum valid
            // value was < 128. In this case varint is exactly equivalent to
            // byte encoding.
            ZL_ASSERT_LT(ZL_transformOutStreamsLimit(8), 128);
            ZL_TRY_LET(uint64_t, nbVOsMinus1, ZL_RC_popVarint(src));
            nbVOs[u] = (uint32_t)nbVOsMinus1 + 1;
        }
    }

    return ZL_returnSuccess();
}

// Decompress Transform's nb of Regenerated Streams
// 1-sizes are bitpacked
// non-1 sizes are shifted by -2, then varint encoded
static ZL_Report decompressNbRegens(
        uint32_t nbRegens[],
        size_t nbTransforms,
        ZL_RC* src,
        unsigned formatVersion)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_DLOG(SEQ, "decompressNbRegens (nbTransforms = %zu)", nbTransforms);
    ZL_ASSERT_GE(formatVersion, 16);
    // Store 1-size flags in nbRegens temporarily
    // 1s will be replaced later by actual nbRegens
    ZL_ERR_IF_ERR(checkedBitpackDecode32(nbRegens, nbTransforms, src, 1));

    // decode non-1 nbRegens (varint encoded, shifted by 2)
    for (size_t u = 0; u < nbTransforms; u++) {
        if (nbRegens[u] != 0) {
            ZL_TRY_LET(uint64_t, nbRegensMinus2, ZL_RC_popVarint(src));
            nbRegens[u] = (uint32_t)nbRegensMinus2 + 2;
            ZL_ERR_IF_GT(
                    nbRegens[u],
                    ZL_runtimeNodeInputLimit(formatVersion),
                    corruption);
        } else {
            nbRegens[u] = 1;
        }
    }

    return ZL_returnSuccess();
}

// Decompress per-codec dict bundle offsets
// (v24+)
// has-dict flags are bitpacked
// non-zero dict indices are bitpacked
static ZL_Report decompressDictIdxs(
        uint32_t dictIdxs[],
        size_t nbTransforms,
        ZL_RC* src,
        uint32_t* valuesWksp)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    // Decode bitmap into dictIdxs (0 = no dict, 1 = has dict)
    ZL_ERR_IF_ERR(checkedBitpackDecode32(dictIdxs, nbTransforms, src, 1));

    // Count non-zero entries
    size_t nbNonZero = 0;
    for (size_t u = 0; u < nbTransforms; u++) {
        if (dictIdxs[u] != 0)
            nbNonZero++;
    }

    if (nbNonZero > 0) {
        // Read nbBits
        ZL_ERR_IF_LT(ZL_RC_avail(src), 1, srcSize_tooSmall);
        uint8_t const nbBits = ZL_RC_pop(src);
        ZL_ERR_IF_GT(nbBits, 16, corruption, "dictIdx nbBits too large");

        // Decode bitpacked values into separate workspace
        ZL_ERR_IF_ERR(
                checkedBitpackDecode32(valuesWksp, nbNonZero, src, nbBits));

        // Merge: walk forward, overwriting bitmap flags with actual values
        size_t idx = 0;
        for (size_t u = 0; u < nbTransforms; u++) {
            if (dictIdxs[u]) {
                ZL_ERR_IF_GT(
                        valuesWksp[idx],
                        ZL_MAX_DICTS_PER_BUNDLE,
                        corruption,
                        "dictIdx value above allowed range");
                dictIdxs[u] = valuesWksp[idx++];
            } else {
                dictIdxs[u] = ZL_DICT_INDEX_NONE;
            }
        }
    } else {
        for (size_t u = 0; u < nbTransforms; u++) {
            dictIdxs[u] = ZL_DICT_INDEX_NONE;
        }
    }

    return ZL_returnSuccess();
}

// Decompress Stream's Nb Jumps
// bitpacking
static ZL_Report decompress_regenStreamID_distances(
        uint32_t distances[],
        size_t nbGenStreams,
        ZL_RC* src,
        size_t nbStoredStreams)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    // Note : distance can never be > (nbDistances + nbStreams)
    size_t const maxDistance = nbGenStreams + nbStoredStreams;
    int const nbBits         = ZL_nextPow2(maxDistance);
    ZL_TRY_LET(
            size_t,
            r,
            checkedBitpackDecode32(distances, nbGenStreams, src, nbBits));
    ZL_DLOG(BLOCK,
            "decompress_regenStreamID_distances : read %zu bytes, using %i bits per %zu entries",
            r,
            nbBits,
            nbGenStreams);
    return ZL_returnSuccess();
}

// Decompress Streams' sizes
// varint decode
static ZL_Report
decompressStrSizes(size_t streamSizes[], size_t nbStreams, ZL_RC* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_LT(
            ZL_RC_avail(src),
            nbStreams * 1,
            corruption,
            "Stream sizes header smaller than minimum size");
    for (unsigned u = 0; u < nbStreams; u++) {
        ZL_RESULT_OF(uint64_t) const res = ZL_RC_popVarint(src);
        ZL_ERR_IF_ERR(res);
        uint64_t const streamSize64 = ZL_RES_value(res);
        ZL_ERR_IF_GE(
                streamSize64, UINT32_MAX, corruption, "Stream size too large");
        streamSizes[u] = (size_t)streamSize64;
        ZL_DLOG(FRAME, "stream %u => %u bytes", u, streamSizes[u]);
    }
    return ZL_returnSuccess();
}

/// Workspace contains 3 non-overlapping scratch regions
/// of size nbTransforms elements each,
/// for use during frame header decoding.
typedef struct {
    uint32_t* scratch0;
    uint32_t* scratch1;
    uint8_t* scratch2;
} DFH_Workspace;

static size_t DFH_scratchSize(unsigned nbTransforms)
{
    size_t const unitSize = sizeof(((DFH_Workspace*)NULL)->scratch0)
            + sizeof(((DFH_Workspace*)NULL)->scratch1)
            + sizeof(((DFH_Workspace*)NULL)->scratch2);
    size_t const eltSize = sizeof(uint32_t);
    size_t const allocationSize =
            (nbTransforms * unitSize + eltSize - 1) / eltSize;
    return allocationSize;
}

static ZL_Report DFH_Workspace_init(
        DFH_Workspace* wksp,
        unsigned nbTransforms,
        VECTOR(uint32_t) * scratch)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    size_t const allocationSize = DFH_scratchSize(nbTransforms);
    ZL_ASSERT_EQ(VECTOR_MAX_CAPACITY(*scratch), 0);
    VECTOR_INIT(*scratch, allocationSize);
    ZL_ERR_IF_NE(
            allocationSize,
            VECTOR_RESIZE(*scratch, allocationSize),
            allocation);

    wksp->scratch0 = VECTOR_DATA(*scratch);
    wksp->scratch1 =
            nbTransforms ? wksp->scratch0 + nbTransforms : wksp->scratch0;
    wksp->scratch2 = nbTransforms ? (uint8_t*)(wksp->scratch1 + nbTransforms)
                                  : (uint8_t*)wksp->scratch1;

    if (nbTransforms) {
        ZL_ASSERT_LE(
                wksp->scratch2 + nbTransforms,
                (uint8_t*)(VECTOR_DATA(*scratch) + VECTOR_SIZE(*scratch)));
    }

    return ZL_returnSuccess();
}

static ZL_Report DFH_decodeFrameHeader_V3orMore(
        DFH_Struct* dfh,
        const void* src,
        size_t srcSize,
        unsigned formatVersion)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_DLOG(FRAME, "decodeFrameHeader (srcSize = %zu)", srcSize);
    ZL_ERR_IF_LT(srcSize, FRAME_HEADER_SIZE_MIN, srcSize_tooSmall);

    ZL_ASSERT_GE(formatVersion, 3);
    dfh->formatVersion = formatVersion;

    if (dfh->frameinfo)
        ZL_FrameInfo_free(dfh->frameinfo);
    dfh->frameinfo = ZL_malloc(sizeof(*(dfh->frameinfo)));
    ZL_ERR_IF_NULL(dfh->frameinfo, allocation);
    return DFH_FrameInfo_decodeFrameHeader(dfh->frameinfo, src, srcSize);
}

/* src is expected to start at beginning of chunk header */
static ZL_Report decodeChunkHeader_internal(
        DFH_Struct* dfh,
        const void* src,
        size_t srcSize,
        const DFH_Interface* decoder,
        VECTOR(uint32_t) * scratch)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_DLOG(FRAME, "decodeChunkHeader_internal (srcSize = %zu)", srcSize);
    ZL_ERR_IF_LT(srcSize, CHUNK_HEADER_SIZE_MIN, srcSize_tooSmall);

    ZL_ASSERT_GE(decoder->formatVersion, 3);

    ZL_RC in = ZL_RC_wrap(src, srcSize);

    uint64_t nbDecoders;
    uint64_t nbStoredStreams;
    if (decoder->formatVersion < 9) {
        nbDecoders      = ZL_RC_pop(&in);
        nbStoredStreams = ZL_RC_pop(&in);
    } else {
        ZL_RESULT_OF(uint64_t) res = ZL_RC_popVarint(&in);
        ZL_ERR_IF_ERR(res);
        nbDecoders = ZL_RES_value(res);

        if (decoder->formatVersion >= ZL_CHUNK_VERSION_MIN) {
            ZL_ERR_IF_EQ(nbDecoders, 0, corruption, "invalid field value");
            nbDecoders--;
        }

        res = ZL_RC_popVarint(&in);
        ZL_ERR_IF_ERR(res);
        nbStoredStreams = ZL_RES_value(res);
    }
    ZL_DLOG(FRAME,
            "nbDecoders = %u | nbStoredStreams = %u",
            nbDecoders,
            nbStoredStreams);
    ZL_ERR_IF_GE(
            nbDecoders,
            ZL_runtimeNodeLimit(decoder->formatVersion),
            temporaryLibraryLimitation,
            "OpenZL refuses to process graphs with this many nodes");
    ZL_ERR_IF_GE(
            nbStoredStreams,
            ZL_runtimeStreamLimit(decoder->formatVersion),
            temporaryLibraryLimitation,
            "OpenZL refuses to process graphs with this many streams");

    dfh->nbDTransforms   = (size_t)nbDecoders;
    dfh->nbStoredStreams = (size_t)nbStoredStreams;

    ZL_ERR_IF_NE(nbDecoders, VECTOR_RESIZE(dfh->nodes, nbDecoders), allocation);
    ZL_ERR_IF_NE(
            nbStoredStreams,
            VECTOR_RESIZE(dfh->storedStreamSizes, nbStoredStreams),
            allocation);

    DFH_Workspace wksp = { 0 };
    ZL_ERR_IF_ERR(DFH_Workspace_init(&wksp, (unsigned)nbDecoders, scratch));

    DFH_NodeInfo* const nodes = VECTOR_DATA(dfh->nodes);

    // Checksum properties are positioned in the frame header for versions >=
    // ZL_CHUNK_VERSION_MIN
    if (4 <= decoder->formatVersion
        && decoder->formatVersion < ZL_CHUNK_VERSION_MIN) {
        ZL_ERR_IF_LT(ZL_RC_avail(&in), 1, srcSize_tooSmall);
        uint8_t const flags = ZL_RC_pop(&in);
        dfh->frameinfo->properties.hasContentChecksum =
                ((flags & (1 << 0)) != 0);
        dfh->frameinfo->properties.hasCompressedChecksum =
                ((flags & (1 << 1)) != 0);
    }

    {
        // Collect list of decoders
        uint8_t* trt8 = wksp.scratch2;

        ZL_ERR_IF_ERR(decompressTrt(trt8, nbDecoders, &in));
        for (unsigned u = 0; u < nbDecoders; u++) {
            TransformType_e const trt = trt8[u];
            nodes[u].trpid.trt        = trt;
            ZL_ASSERT(trt == trt_custom || trt == trt_standard);
        }

        uint32_t* trIDs = wksp.scratch0;
        ZL_ERR_IF_ERR(decompressTrID(
                trIDs,
                nbDecoders,
                &in,
                trt8,
                wksp.scratch1,
                decoder->formatVersion));
        for (unsigned u = 0; u < nbDecoders; u++) {
            if (nodes[u].trpid.trt == trt_standard
                && trIDs[u] >= ZL_StandardTransformID_end) {
                ZL_ERR(invalidTransform,
                       "Standard Codec ID %u too large, must be <= %u",
                       trIDs[u],
                       ZL_StandardTransformID_end);
            }
            nodes[u].trpid.trid = trIDs[u];
        }
    }

    // Decode private header size of each Transform node
    {
        uint32_t* const trHeaderSizes = wksp.scratch0;
        size_t totalTHSize            = 0;
        ZL_ERR_IF_ERR(decompressTrHSize(trHeaderSizes, nbDecoders, &in));
        for (unsigned u = 0; u < nbDecoders; u++) {
            nodes[u].trhSize  = trHeaderSizes[u];
            nodes[u].trhStart = totalTHSize;
            totalTHSize += trHeaderSizes[u];
        }
        dfh->totalTHSize = totalTHSize;
    }

    // Decode nbVOs per Transform
    if (decoder->formatVersion >= 8) {
        uint32_t* const nbVOs = wksp.scratch0;
        ZL_ERR_IF_ERR(decompressNbVOs(nbVOs, nbDecoders, &in));
        for (unsigned u = 0; u < nbDecoders; u++) {
            nodes[u].nbVOs = nbVOs[u];
        }
    } else {
        // No VOs for older format
        for (unsigned u = 0; u < nbDecoders; u++) {
            nodes[u].nbVOs = 0;
        }
    }

    // Decode nbRegens per Transform
    size_t totalNbRegens = 0;
    if (decoder->formatVersion >= 16) {
        uint32_t* const nbRegens = wksp.scratch0;
        ZL_ERR_IF_ERR(decompressNbRegens(
                nbRegens, nbDecoders, &in, decoder->formatVersion));
        for (unsigned u = 0; u < nbDecoders; u++) {
            nodes[u].nbRegens = nbRegens[u];
            totalNbRegens += nbRegens[u];
        }
    } else {
        // Older format: only single-regen Transforms
        for (unsigned u = 0; u < nbDecoders; u++) {
            nodes[u].nbRegens = 1;
        }
        totalNbRegens = nbDecoders;
    }

    // Decode per-codec dict indices if a bundle is declared
    // (v24+ only)
    if (decoder->formatVersion >= ZL_MATERIALIZED_DICT_VERSION_MIN
        && dfh->frameinfo->properties.hasBundleID) {
        uint32_t* const dictIdxs = wksp.scratch0;
        ZL_ERR_IF_ERR(
                decompressDictIdxs(dictIdxs, nbDecoders, &in, wksp.scratch1));
        for (unsigned u = 0; u < nbDecoders; u++) {
            nodes[u].dictIdx = dictIdxs[u];
        }
    } else {
        for (unsigned u = 0; u < nbDecoders; u++) {
            nodes[u].dictIdx = ZL_DICT_INDEX_NONE;
        }
    }

    // Decode regen stream id distance (1 per Transform)
    ZL_DLOG(SEQ, "totalNbRegens = %zu", totalNbRegens);
    ZL_ERR_IF_NE(
            totalNbRegens,
            VECTOR_RESIZE(dfh->regenDistances, totalNbRegens),
            allocation);
    dfh->nbRegens = totalNbRegens;
    /* warning: do NOT reallocate or update VECTOR distances after that point,
     * there are pointers referencing its content */
    /* Note: this array should rather be hosted within a session-level arena */
    {
        uint32_t* const distances = VECTOR_DATA(dfh->regenDistances);
        ZL_ERR_IF_ERR(decompress_regenStreamID_distances(
                distances, totalNbRegens, &in, nbStoredStreams));
        for (unsigned t = 0, d = 0; t < nbDecoders; t++) {
            nodes[t].regenDistances = distances + d;
            d += (unsigned)nodes[t].nbRegens;
            ZL_DLOG(FRAME,
                    "stage %u : trid=%u (type:%u, nbRegens:%i), trhsize=%zu",
                    t,
                    nodes[t].trpid.trid,
                    nodes[t].trpid.trt,
                    nodes[t].nbRegens,
                    nodes[t].trhSize);
            if (t == nbDecoders - 1)
                ZL_ASSERT_EQ(d, totalNbRegens);
        }
    }

    ZL_DLOG(FRAME, "%u streams stored in the chunk", nbStoredStreams);
    ZL_ASSERT_LE(
            nbStoredStreams, ZL_runtimeStreamLimit(decoder->formatVersion));

    ZL_ERR_IF_ERR(decompressStrSizes(
            VECTOR_DATA(dfh->storedStreamSizes), nbStoredStreams, &in));

    ZL_DLOG(SEQ,
            "Chunk header size : %zu",
            MEM_ptrDistance(src, ZL_RC_ptr(&in)));
    return ZL_returnValue(MEM_ptrDistance(src, ZL_RC_ptr(&in)));
}

static ZL_Report decodeChunkHeaderV3orMore(
        DFH_Interface const* decoder,
        DFH_Struct* dfh,
        const void* src,
        size_t srcSize)
{
    // The vector lives here so it can be destroyed after
    // `decodeFrameHeader_internal` finishes. However, it's only initialized and
    // used in `decodeFrameHeader_internal`. For that reason we assign a max
    // capacity of 0 here to enforce the fact that it's re-initialized before
    // used.
    VECTOR(uint32_t) scratch = VECTOR_EMPTY(0);
    ZL_Report const ret =
            decodeChunkHeader_internal(dfh, src, srcSize, decoder, &scratch);
    VECTOR_DESTROY(scratch);
    return ret;
}

/* @note only works for frames with a single output
 * @note (@cyan): duplicates header parsing logic, this is fragile */
static ZL_Report getDecompressedSizeV3orMore(
        const DFH_Interface* decoder,
        const void* src,
        size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_DLOG(FRAME, "getDecompressedSizeV3orMore (from srcSize=%zu)", srcSize);
    ZL_ERR_IF_LT(srcSize, 5, srcSize_tooSmall);
    uint8_t const flags = ((const uint8_t*)src)[4];
    int const hasBundleID =
            (decoder->formatVersion >= ZL_MATERIALIZED_DICT_VERSION_MIN)
            && ((flags & (1 << 3)) != 0);
    size_t bundleIDFieldLen = 0;
    if (hasBundleID) {
        ZL_ERR_IF_LT(srcSize, 6, srcSize_tooSmall);
        uint8_t const idLen = ((const uint8_t*)src)[5];
        bundleIDFieldLen    = 1 + idLen;
    }
    size_t const hSize = (size_t)4 + (decoder->formatVersion > 13)
            + (decoder->formatVersion >= ZL_CHUNK_VERSION_MIN)
            + bundleIDFieldLen;
    ZL_ERR_IF_LT(srcSize, hSize + 4, srcSize_tooSmall);
    if (decoder->formatVersion > 14) {
        uint8_t token1 = ((const uint8_t*)src)
                [4ul + (decoder->formatVersion >= ZL_CHUNK_VERSION_MIN)
                 + bundleIDFieldLen];
        ZL_ERR_IF_GE(
                token1,
                64,
                invalidRequest_singleOutputFrameOnly,
                "getDecompressedSize is only meaningful for single-output frames");
    }
    if (decoder->formatVersion < ZL_CHUNK_VERSION_MIN) {
        // limited to 32-bit values, i.e. < 4 GB
        ZL_DLOG(FRAME,
                "decompressed size == %u",
                ZL_readLE32((const char*)src + hSize));
        return ZL_returnValue(ZL_readLE32((const char*)src + hSize));
    }
    // formatVersion >= ZL_CHUNK_VERSION_MIN supports huge sizes > 4 GB
    uint64_t oSize;
    ZL_ASSERT_LE(hSize, srcSize);
    const uint8_t* ptr = (const uint8_t*)src + hSize;
    const uint8_t* end = (const uint8_t*)src + srcSize;
    ZL_TRY_SET(uint64_t, oSize, ZL_varintDecode(&ptr, end));
    ZL_ERR_IF_EQ(
            oSize,
            0,
            temporaryLibraryLimitation,
            "size must be registered in the frame header"); // 0 means "unknown"
    ZL_DLOG(BLOCK, "1 stream, of decompressed size %llu bytes", oSize - 1);
    ZL_ERR_IF_GE(
            oSize,
            (uint64_t)SIZE_MAX,
            GENERIC,
            "large size (%llu): unsupported on current system");
    return ZL_returnValue((size_t)oSize - 1);
}

// @note (@cyan): how useful is this method ?
// I see it used in one assert() so far,
// though it requires duplicating the frame scanning logic here
// so it's easy to get the duplicated part wrong.
static ZL_Report getCompressedSizeV3orMore_inner(
        const DFH_Interface* decoder,
        const void* src,
        size_t srcSize,
        DFH_Struct* dfh)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_Report fhSize = decoder->decodeFrameHeader(
            dfh, src, srcSize, decoder->formatVersion);
    size_t frameSize = 0;
    if (ZL_isError(fhSize)) {
        return fhSize;
    }

    frameSize += ZL_RES_value(fhSize); // Add header size.

    int oneMoreBlock = 1;

    while (oneMoreBlock) {
        if (dfh->formatVersion >= ZL_CHUNK_VERSION_MIN) {
            ZL_ERR_IF_GE(
                    frameSize,
                    srcSize,
                    srcSize_tooSmall); // need at least one byte
            if (((const char*)src)[frameSize] == 0) {
                // frame footer
                frameSize++;
                break;
            }
        }
        ZL_TRY_LET(
                size_t,
                chhSize,
                decoder->decodeChunkHeader(
                        decoder,
                        dfh,
                        (const char*)src + frameSize,
                        srcSize - frameSize));
        frameSize += chhSize;

        frameSize += dfh->totalTHSize;

        for (uint32_t streamNb = 0; streamNb < dfh->nbStoredStreams;
             streamNb++) {
            frameSize += VECTOR_AT(dfh->storedStreamSizes, streamNb);
        }

        frameSize += dfh->frameinfo->properties.hasContentChecksum ? 4 : 0;
        frameSize += dfh->frameinfo->properties.hasCompressedChecksum ? 4 : 0;

        if (dfh->formatVersion < ZL_CHUNK_VERSION_MIN)
            break; // single block for v20-
    }
    ZL_ERR_IF_GT(frameSize, srcSize, srcSize_tooSmall);

    return ZL_returnValue(frameSize);
}

static ZL_Report getCompressedSizeV3orMore(
        const DFH_Interface* decoder,
        const void* src,
        size_t srcSize)
{
    ZL_DLOG(SEQ, "getCompressedSizeV3orMore (srcSize=%zu)", srcSize);
    DFH_Struct dfh = { 0 };
    DFH_init(&dfh);
    ZL_Report report =
            getCompressedSizeV3orMore_inner(decoder, src, srcSize, &dfh);
    DFH_destroy(&dfh);
    return report;
}

static ZL_Report getHeaderSizeV3orV4(
        DFH_Interface const* decoder,
        const void* src,
        size_t srcSize)
{
    DFH_Struct dfh_unused = { 0 };
    DFH_init(&dfh_unused);
    ZL_Report ret = decoder->decodeFrameHeader(
            &dfh_unused, src, srcSize, decoder->formatVersion);
    DFH_destroy(&dfh_unused);
    return ret;
}

static DFH_Interface const dfhV3 = {
    .getDecompressedSize = getDecompressedSizeV3orMore,
    .getCompressedSize   = getCompressedSizeV3orMore,
    .getHeaderSize       = getHeaderSizeV3orV4,
    .decodeFrameHeader   = DFH_decodeFrameHeader_V3orMore,
    .decodeChunkHeader   = decodeChunkHeaderV3orMore,
};

DFH_Interface DFH_getFrameHeaderDecoder(uint32_t formatVersion)
{
    ZL_ASSERT(ZL_isFormatVersionSupported(formatVersion));
    DFH_Interface decoder;
    if (formatVersion >= 3) {
        decoder = dfhV3;
    } else {
        ZL_ASSERT_FAIL("Format version is supposed to be validated.");
    }
    decoder.formatVersion = formatVersion;
    return decoder;
}

ZL_Report
DFH_decodeFrameHeader(DFH_Struct* dfh, const void* src, size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_TRY_LET(
            size_t, formatVersion, ZL_getFormatVersionFromFrame(src, srcSize));
    DFH_Interface const decoder =
            DFH_getFrameHeaderDecoder((uint32_t)formatVersion);
    return decoder.decodeFrameHeader(dfh, src, srcSize, decoder.formatVersion);
}

static DFH_Interface DFH_getChunkHeaderDecoder(uint32_t formatVersion)
{
    ZL_ASSERT(ZL_isFormatVersionSupported(formatVersion));
    DFH_Interface decoder;
    if (formatVersion >= 3) {
        decoder = dfhV3;
    } else {
        ZL_ASSERT_FAIL("Format version is supposed to be validated.");
    }
    decoder.formatVersion = formatVersion;
    return decoder;
}

ZL_Report
DFH_decodeChunkHeader(DFH_Struct* dfh, const void* src, size_t srcSize)
{
    ZL_ASSERT_NN(dfh);
    uint32_t const formatVersion = dfh->formatVersion;
    DFH_Interface const decoder =
            DFH_getChunkHeaderDecoder((uint32_t)formatVersion);
    return decoder.decodeChunkHeader(&decoder, dfh, src, srcSize);
}
