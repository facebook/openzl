// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitpack/common_bitpack_kernel.h"
#include "openzl/codecs/conversion/common_endianness_kernel.h"
#include "openzl/codecs/entropy/decode_huffman_kernel.h"
#include "openzl/codecs/entropy/deprecated/common_entropy.h"
#include "openzl/codecs/entropy/deprecated/common_huf_avx2.h"
#include "openzl/common/assertion.h"
#include "openzl/common/cursor.h"
#include "openzl/fse/fse.h"
#include "openzl/fse/huf.h"
#include "openzl/shared/bits.h"
#include "openzl/zl_errors.h"

ZS_Entropy_DecodeParameters ZS_Entropy_DecodeParameters_default(void)
{
    ZS_Entropy_DecodeParameters params = {
        .allowedTypes = ZS_Entropy_TypeMask_all,
        .tableManager = NULL,
        .fseNbStates  = ZS_ENTROPY_DEFAULT_FSE_NBSTATES
    };
    return params;
}

ZL_Report ZS_Entropy_decodeDefault(
        void* dst,
        size_t dstCapacity,
        ZL_RC* src,
        size_t elementSize)
{
    ZS_Entropy_DecodeParameters const params =
            ZS_Entropy_DecodeParameters_default();
    return ZS_Entropy_decode(dst, dstCapacity, src, elementSize, &params);
}

typedef struct {
    uint64_t encodedSize;
    uint64_t decodedSize;
    uint32_t tableMode;
    int format;
} ZS_HufAndFse_Header_t;

static ZL_Report ZS_HufAndFse_getHeader(
        ZS_HufAndFse_Header_t* header,
        ZL_RC* src);

typedef struct {
    uint64_t decodedSize;
} ZS_RawAndConstant_Header_t;

static ZL_Report ZS_RawAndConstant_getHeader(
        ZS_RawAndConstant_Header_t* header,
        ZL_RC* src);

typedef struct {
    uint64_t numBlocks;
} ZS_Multi_Header_t;

static ZL_Report ZS_Multi_getHeader(ZS_Multi_Header_t* header, ZL_RC* src);

typedef struct {
    uint32_t numBits;
    uint64_t decodedSize;
} ZS_Bit_Header_t;

static ZL_Report ZS_Bit_getHeader(ZS_Bit_Header_t* header, ZL_RC* src);

ZL_Report ZS_Entropy_getType(void const* src, size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (srcSize == 0) {
        ZL_DLOG(ERROR, "Source is empty");
        ZL_ERR(GENERIC);
    }
    uint8_t const header         = *(uint8_t const*)src;
    ZS_Entropy_Type_e const type = header & 0x7;
    ZL_STATIC_ASSERT(
            ZS_Entropy_Type_reserved1 > ZS_Entropy_Type_reserved0,
            "Assumption");
    if (type >= ZS_Entropy_Type_reserved0) {
        ZL_DLOG(V1, "Bad type");
        ZL_ERR(GENERIC);
    }
    ZL_DLOG(V1, "Type = %d", type);
    return ZL_returnValue(type);
}

static ZL_Report ZS_Entropy_getEncodedSize_internal(
        void const* src,
        size_t srcSize,
        size_t elementSize,
        size_t maxDepth)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_RC rc      = ZL_RC_wrap((uint8_t const*)src, srcSize);
    ZL_Report ret = ZS_Entropy_getType(src, srcSize);
    ZL_ERR_IF_ERR(ret);
    size_t extraSize;
    switch (ZL_validResult(ret)) {
        case ZS_Entropy_Type_fse:
        case ZS_Entropy_Type_huf: {
            ZS_HufAndFse_Header_t header;
            ZL_ERR_IF_ERR(ZS_HufAndFse_getHeader(&header, &rc));
            extraSize = header.encodedSize;
            break;
        }
        case ZS_Entropy_Type_raw: {
            ZS_RawAndConstant_Header_t header;
            ZL_ERR_IF_ERR(ZS_RawAndConstant_getHeader(&header, &rc));
            extraSize = header.decodedSize * elementSize;
            break;
        }
        case ZS_Entropy_Type_constant: {
            ZS_RawAndConstant_Header_t header;
            ZL_ERR_IF_ERR(ZS_RawAndConstant_getHeader(&header, &rc));
            extraSize = elementSize;
            break;
        }
        // TODO: Add these modes
        case ZS_Entropy_Type_bit: {
            ZS_Bit_Header_t header;
            ZL_ERR_IF_ERR(ZS_Bit_getHeader(&header, &rc));
            extraSize = (header.decodedSize * header.numBits + 7) / 8;
            break;
        }
        case ZS_Entropy_Type_multi: {
            if (maxDepth == 0) {
                ZL_ERR(GENERIC);
            }
            ZS_Multi_Header_t header;
            ZL_ERR_IF_ERR(ZS_Multi_getHeader(&header, &rc));
            for (uint64_t block = 0; block < header.numBlocks; ++block) {
                ZL_Report const blockSize = ZS_Entropy_getEncodedSize_internal(
                        ZL_RC_ptr(&rc),
                        ZL_RC_avail(&rc),
                        elementSize,
                        maxDepth - 1);
                ZL_ERR_IF_ERR(blockSize);
                if (ZL_validResult(blockSize) > ZL_RC_avail(&rc)) {
                    ZL_ERR(GENERIC);
                }
                ZL_RC_advance(&rc, ZL_validResult(blockSize));
            }
            extraSize = 0;
            break;
        }
        default:
            ZL_ERR(GENERIC);
    }
    if (ZL_RC_avail(&rc) < extraSize) {
        ZL_ERR(GENERIC);
    }
    size_t const consumed = srcSize - ZL_RC_avail(&rc);
    ZL_ASSERT_NE(consumed, 0);
    ZL_ASSERT_GE(consumed + extraSize, consumed);
    return ZL_returnValue(consumed + extraSize);
}

ZL_Report
ZS_Entropy_getEncodedSize(void const* src, size_t srcSize, size_t elementSize)
{
    // Add a maximum depth for the multi encoding.
    // Set it to be very deep, so we don't reject any valid frames, but disallow
    // unlimited recursion.
    size_t const kMaxDepth = 64;
    return ZS_Entropy_getEncodedSize_internal(
            src, srcSize, elementSize, kMaxDepth);
}

static ZL_Report ZS_Entropy_getDecodedSize_internal(
        void const* src,
        size_t srcSize,
        size_t elementSize,
        size_t maxDepth)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_RC rc      = ZL_RC_wrap((uint8_t const*)src, srcSize);
    ZL_Report ret = ZS_Entropy_getType(src, srcSize);
    ZL_ERR_IF_ERR(ret);
    switch (ZL_validResult(ret)) {
        case ZS_Entropy_Type_fse:
        case ZS_Entropy_Type_huf: {
            ZS_HufAndFse_Header_t header;
            ZL_ERR_IF_ERR(ZS_HufAndFse_getHeader(&header, &rc));
            return ZL_returnValue(header.decodedSize);
        }
        case ZS_Entropy_Type_raw:
        case ZS_Entropy_Type_constant: {
            ZS_RawAndConstant_Header_t header;
            ZL_ERR_IF_ERR(ZS_RawAndConstant_getHeader(&header, &rc));
            return ZL_returnValue(header.decodedSize);
        }
        // TODO: Add these modes
        case ZS_Entropy_Type_bit: {
            ZS_Bit_Header_t header;
            ZL_ERR_IF_ERR(ZS_Bit_getHeader(&header, &rc));
            return ZL_returnValue(header.decodedSize);
        }
        case ZS_Entropy_Type_multi: {
            if (maxDepth == 0) {
                ZL_ERR(GENERIC);
            }
            ZS_Multi_Header_t header;
            uint64_t decodedSize = 0;
            ZL_ERR_IF_ERR(ZS_Multi_getHeader(&header, &rc));
            ZL_ERR_IF_GT(header.numBlocks, ZL_RC_avail(&rc), corruption);
            for (uint64_t block = 0; block < header.numBlocks; ++block) {
                ZL_Report const blockEncodedSize =
                        ZS_Entropy_getEncodedSize_internal(
                                ZL_RC_ptr(&rc),
                                ZL_RC_avail(&rc),
                                elementSize,
                                maxDepth - 1);
                ZL_Report const blockDecodedSize =
                        ZS_Entropy_getDecodedSize_internal(
                                ZL_RC_ptr(&rc),
                                ZL_RC_avail(&rc),
                                elementSize,
                                maxDepth - 1);
                ZL_ERR_IF_ERR(blockEncodedSize);
                ZL_ERR_IF_ERR(blockDecodedSize);
                // Disallow zero sized blocks because it makes no sense to
                // generate them, and the fuzzer generates a bunch of them and
                // times out.
                ZL_ERR_IF_EQ(ZL_validResult(blockDecodedSize), 0, corruption);
                if (ZL_validResult(blockEncodedSize) > ZL_RC_avail(&rc)) {
                    ZL_ERR(GENERIC);
                }
                ZL_RC_advance(&rc, ZL_validResult(blockEncodedSize));
                decodedSize += ZL_validResult(blockDecodedSize);
            }
            return ZL_returnValue(decodedSize);
        }
        default:
            ZL_ERR(GENERIC);
    }
}

ZL_Report
ZS_Entropy_getDecodedSize(void const* src, size_t srcSize, size_t elementSize)
{
    // Add a maximum depth for the multi encoding.
    // Set it to be very deep, so we don't reject any valid frames, but disallow
    // unlimited recursion.
    size_t const kMaxDepth = 64;
    return ZS_Entropy_getDecodedSize_internal(
            src, srcSize, elementSize, kMaxDepth);
}

ZL_Report ZS_Entropy_getHeaderSize(void const* src, size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_RC rc      = ZL_RC_wrap((uint8_t const*)src, srcSize);
    ZL_Report ret = ZS_Entropy_getType(src, srcSize);
    ZL_ERR_IF_ERR(ret);
    switch (ZL_validResult(ret)) {
        case ZS_Entropy_Type_fse:
        case ZS_Entropy_Type_huf: {
            ZS_HufAndFse_Header_t header;
            ZL_ERR_IF_ERR(ZS_HufAndFse_getHeader(&header, &rc));
            break;
        }
        case ZS_Entropy_Type_raw:
        case ZS_Entropy_Type_constant: {
            ZS_RawAndConstant_Header_t header;
            ZL_ERR_IF_ERR(ZS_RawAndConstant_getHeader(&header, &rc));
            break;
        }
        // TODO: Add these modes
        case ZS_Entropy_Type_bit: {
            ZS_Bit_Header_t header;
            ZL_ERR_IF_ERR(ZS_Bit_getHeader(&header, &rc));
            break;
        }
        case ZS_Entropy_Type_multi: {
            ZS_Multi_Header_t header;
            ZL_ERR_IF_ERR(ZS_Multi_getHeader(&header, &rc));
            break;
        }
        default:
            ZL_ERR(GENERIC);
    }
    return ZL_returnValue(srcSize - ZL_RC_avail(&rc));
}

static ZL_Report ZS_Huf_decodeImpl(
        void* dst,
        size_t dstSize,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        bool avx2,
        ZS_Entropy_DecodeParameters const* params);

static ZL_Report ZS_Entropy_decode_internal(
        void* dst,
        size_t dstCapacity,
        ZL_RC* src,
        size_t elementSize,
        ZS_Entropy_DecodeParameters const* params,
        size_t maxDepth)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_DLOG(V1,
            "ZS_Entropy_decode(dstCapacity = %zu, ZL_RC_avail(src) = %zu, elementSize = %zu",
            dstCapacity,
            ZL_RC_avail(src),
            elementSize);
    ZL_Report ret = ZS_Entropy_getType(ZL_RC_ptr(src), ZL_RC_avail(src));
    ZL_ERR_IF_ERR(ret);
    if (!(params->allowedTypes & (1 << ZL_validResult(ret)))) {
        ZL_DLOG(ERROR, "Type not allowed!");
        ZL_ERR(GENERIC);
    }

    switch (ZL_validResult(ret)) {
        case ZS_Entropy_Type_fse: {
            ZS_HufAndFse_Header_t header;
            ZL_ERR_IF_ERR(ZS_HufAndFse_getHeader(&header, src));
            if (ZL_RC_avail(src) < header.encodedSize) {
                ZL_DLOG(ERROR, "Source size too small");
                ZL_ERR(GENERIC);
            }
            if (dstCapacity < header.decodedSize) {
                ZL_DLOG(ERROR, "Dst size too small");
                ZL_ERR(GENERIC);
            }
            ZL_ERR_IF_ERR(ZS_Fse_decode(
                    dst,
                    header.decodedSize,
                    ZL_RC_ptr(src),
                    header.encodedSize,
                    elementSize,
                    params));
            ZL_RC_advance(src, header.encodedSize);
            return ZL_returnValue(header.decodedSize);
        }
        case ZS_Entropy_Type_huf: {
            ZS_HufAndFse_Header_t header;
            ZL_ERR_IF_ERR(ZS_HufAndFse_getHeader(&header, src));
            if (ZL_RC_avail(src) < header.encodedSize) {
                ZL_DLOG(ERROR, "Src size too small");
                ZL_ERR(GENERIC);
            }
            if (dstCapacity < header.decodedSize) {
                ZL_DLOG(ERROR, "Dst size too small");
                ZL_ERR(GENERIC);
            }
            ZL_ERR_IF_ERR(ZS_Huf_decodeImpl(
                    dst,
                    header.decodedSize,
                    ZL_RC_ptr(src),
                    header.encodedSize,
                    elementSize,
                    header.format,
                    params));
            ZL_DLOG(V1, "HUF decoded");
            ZL_RC_advance(src, header.encodedSize);
            return ZL_returnValue(header.decodedSize);
        }
        case ZS_Entropy_Type_raw: {
            ZS_RawAndConstant_Header_t header;
            ZL_ERR_IF_ERR(ZS_RawAndConstant_getHeader(&header, src));
            size_t const srcSize = header.decodedSize * elementSize;
            if (ZL_RC_avail(src) < srcSize) {
                ZL_DLOG(ERROR,
                        "Source size too small: %zu < %zu",
                        ZL_RC_avail(src),
                        srcSize);
                ZL_ERR(GENERIC);
            }
            if (dstCapacity < header.decodedSize) {
                ZL_DLOG(ERROR, "Dst size too small");
                ZL_ERR(GENERIC);
            }
            ZL_ERR_IF_ERR(ZS_Raw_decode(
                    dst,
                    header.decodedSize,
                    ZL_RC_ptr(src),
                    srcSize,
                    elementSize));
            ZL_RC_advance(src, srcSize);
            ZL_DLOG(V1,
                    "returning decoded size = %zu",
                    (size_t)(header.decodedSize));
            return ZL_returnValue(header.decodedSize);
        }
        case ZS_Entropy_Type_constant: {
            ZS_RawAndConstant_Header_t header;
            ZL_ERR_IF_ERR(ZS_RawAndConstant_getHeader(&header, src));
            size_t const srcSize = elementSize;
            if (ZL_RC_avail(src) < srcSize
                || dstCapacity < header.decodedSize) {
                ZL_ERR(GENERIC);
            }
            ZL_ERR_IF_ERR(ZS_Constant_decode(
                    dst,
                    header.decodedSize,
                    ZL_RC_ptr(src),
                    srcSize,
                    elementSize));
            ZL_RC_advance(src, elementSize);
            return ZL_returnValue(header.decodedSize);
        }
        case ZS_Entropy_Type_bit: {
            ZS_Bit_Header_t header;
            ZL_ERR_IF_ERR(ZS_Bit_getHeader(&header, src));
            size_t const srcSize =
                    (header.decodedSize * header.numBits + 7) / 8;
            if (ZL_RC_avail(src) < srcSize) {
                ZL_DLOG(ERROR, "src size too small");
                ZL_ERR(GENERIC);
            }
            if (dstCapacity < header.decodedSize) {
                ZL_DLOG(ERROR, "dst size too small");
                ZL_ERR(GENERIC);
            }
            ZL_ERR_IF_ERR(ZS_Bit_decode(
                    dst,
                    header.decodedSize,
                    ZL_RC_ptr(src),
                    srcSize,
                    elementSize,
                    header.numBits));
            ZL_RC_advance(src, srcSize);
            return ZL_returnValue(header.decodedSize);
        }
        case ZS_Entropy_Type_multi: {
            ZL_DLOG(V1, "MULTI decode");
            if (maxDepth == 0) {
                ZL_ERR(GENERIC);
            }
            ZS_Multi_Header_t header;
            ZL_ERR_IF_ERR(ZS_Multi_getHeader(&header, src));
            ZL_DLOG(V1, "NBlocks = %llu", (unsigned long long)header.numBlocks);
            size_t dstSize = 0;
            for (size_t i = 0; i < header.numBlocks; ++i) {
                ZL_DLOG(V1, "block = %zu", i);
                size_t const avail1    = ZL_RC_avail(src);
                uint8_t* const op      = (uint8_t*)dst + dstSize * elementSize;
                ZL_Report blockSizeRet = ZS_Entropy_decode_internal(
                        op,
                        dstCapacity - dstSize,
                        src,
                        elementSize,
                        params,
                        maxDepth - 1);
                ZL_ERR_IF_ERR(blockSizeRet);
                size_t const blockSize = ZL_validResult(blockSizeRet);
                dstSize += blockSize;
                size_t const avail2 = ZL_RC_avail(src);
                ZL_DLOG(V1, "block size = %zu", avail1 - avail2);
            }
            ZL_ASSERT_LE(dstSize, dstCapacity);
            return ZL_returnValue(dstSize);
        }
        default:
            ZL_ERR(GENERIC);
    }
}

ZL_Report ZS_Entropy_decode(
        void* dst,
        size_t dstCapacity,
        ZL_RC* src,
        size_t elementSize,
        ZS_Entropy_DecodeParameters const* params)
{
    // Add a maximum depth for the multi encoding.
    // Set it to be very deep, so we don't reject any valid frames, but disallow
    // unlimited recursion.
    size_t const kMaxDepth = 64;
    return ZS_Entropy_decode_internal(
            dst, dstCapacity, src, elementSize, params, kMaxDepth);
}

static ZL_Report ZS_HufAndFse_getHeader(
        ZS_HufAndFse_Header_t* header,
        ZL_RC* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (ZL_RC_avail(src) < 2) {
        ZL_DLOG(ERROR, "Source too small");
        ZL_ERR(GENERIC);
    }
    size_t const avail    = ZL_RC_avail(src);
    uint16_t const hdr    = ZL_RC_popCE16(src);
    header->tableMode     = (hdr >> 3) & 0x3;
    header->format        = (int)(hdr >> 5) & 0x1;
    bool const hasVarints = (hdr >> 6) & 0x1;
    header->decodedSize   = (hdr >> 7) & 0x1F;
    header->encodedSize   = (hdr >> 12) & 0x0F;
    if (hasVarints) {
        ZL_DLOG(V1, "varint 1...");
        ZL_TRY_LET_CONST(uint64_t, decodedSizeVarint, ZL_RC_popVarint(src));
        header->decodedSize |= decodedSizeVarint << 5;
        ZL_DLOG(V1, "varint 2...");
        ZL_TRY_LET_CONST(uint64_t, encodedSizeVarint, ZL_RC_popVarint(src));
        header->encodedSize |= encodedSizeVarint << 4;
    }
    size_t const avail2 = ZL_RC_avail(src);
    ZL_DLOG(V1,
            "tableMode = %u | encodedSize = %llu | decodedSize = %llu (headerSize = %zu)",
            header->tableMode,
            (unsigned long long)header->encodedSize,
            (unsigned long long)header->decodedSize,
            avail - avail2);
    return ZL_returnSuccess();
}

static ZL_Report ZS_RawAndConstant_getHeader(
        ZS_RawAndConstant_Header_t* header,
        ZL_RC* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (ZL_RC_avail(src) < 1) {
        ZL_ERR(GENERIC);
    }
    uint8_t const hdr    = ZL_RC_pop(src);
    bool const hasVarint = hdr & 0x80;
    header->decodedSize  = (hdr >> 3) & 0xF;
    if (hasVarint) {
        ZL_DLOG(V1, "grabbing varint");
        ZL_TRY_LET_CONST(uint64_t, decodedSizeVarint, ZL_RC_popVarint(src));
        header->decodedSize |= decodedSizeVarint << 4;
    }
    ZL_DLOG(V1, "decodedSize = %llu", (unsigned long long)header->decodedSize);
    return ZL_returnSuccess();
}

static ZL_Report ZS_Multi_getHeader(ZS_Multi_Header_t* header, ZL_RC* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (ZL_RC_avail(src) < 1) {
        ZL_ERR(GENERIC);
    }
    uint8_t const hdr    = ZL_RC_pop(src);
    bool const hasVarint = hdr & 0x80;
    header->numBlocks    = (hdr >> 3) & 0xF;
    if (hasVarint) {
        ZL_DLOG(V1, "have varint");
        ZL_TRY_LET_CONST(uint64_t, numBlocksVarint, ZL_RC_popVarint(src));
        header->numBlocks |= numBlocksVarint << 4;
    }
    return ZL_returnSuccess();
}

static ZL_Report ZS_Bit_getHeader(ZS_Bit_Header_t* header, ZL_RC* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (ZL_RC_avail(src) < 1) {
        ZL_ERR(GENERIC);
    }
    uint8_t const hdr = ZL_RC_pop(src);
    ZL_ASSERT_EQ(hdr & 0x7, ZS_Entropy_Type_bit);
    header->numBits = hdr >> 3;
    ZL_TRY_LET_CONST(uint64_t, decodedSizeVarint, ZL_RC_popVarint(src));
    header->decodedSize = decodedSizeVarint;
    return ZL_returnSuccess();
}

ZL_Report ZS_Fse_decode(
        void* dst,
        size_t dstSize,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        ZS_Entropy_DecodeParameters const* params)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_DLOG(V1, "FSE decode");
    (void)params;
    if (elementSize != 1) {
        ZL_ERR(GENERIC);
    }
    // TODO: Customize the format for e.g. vectorization, for now just use FSE.
    size_t const fseDSize =
            FSE_decompress2(dst, dstSize, src, srcSize, 0, params->fseNbStates);
    if (FSE_isError(fseDSize) || fseDSize != dstSize) {
        ZL_ERR(GENERIC);
    }
    return ZL_returnSuccess();
}

static ZL_Report ZS_Huf_decodeImpl(
        void* dst,
        size_t dstSize,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        bool avx2,
        ZS_Entropy_DecodeParameters const* params)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_DLOG(V1, "HUF decode");
    (void)params;
    if (elementSize < 1 || elementSize > 2) {
        ZL_ERR(GENERIC);
    }
    // TODO: Currently large alphabet huffman has its own header with the
    // encoded/decoded size. Fix that by absorbing the header into this layer.
    if (elementSize == 2) {
        ZL_DLOG(V1, "LA HUF");
        if (avx2) {
            size_t const dsize =
                    ZS_Huf16Avx2_decode(dst, dstSize, src, srcSize);
            if (dsize != dstSize) {
                ZL_ERR(GENERIC);
            }
        } else {
            ZL_RC rc = ZL_RC_wrap(src, srcSize);
            ZL_Report hufDSize =
                    ZS_largeHuffmanDecode((uint16_t*)dst, dstSize, &rc);
            ZL_ERR_IF_ERR(hufDSize);
            if (ZL_validResult(hufDSize) != dstSize || ZL_RC_avail(&rc) > 0) {
                ZL_ERR(GENERIC);
            }
        }
        return ZL_returnSuccess();
    }
    ZL_DLOG(V1, "HUF");

    ZL_DLOG(V1, "ds = %zu | ss = %zu", dstSize, srcSize);
    // TODO: Support vectorized huffman and rewrite the Huffman format.
    if (avx2) {
        size_t const ret = ZS_HufAvx2_decode(dst, dstSize, src, srcSize);
        if (ret != dstSize) {
            ZL_ERR(GENERIC);
        }
    } else {
        if (HUF_isError(HUF_decompress(dst, dstSize, src, srcSize))) {
            ZL_DLOG(ERROR, "Huff error");
            ZL_ERR(GENERIC);
        }
    }
    return ZL_returnSuccess();
}

ZL_Report ZS_Huf_decode(
        void* dst,
        size_t dstSize,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        ZS_Entropy_DecodeParameters const* params)
{
    return ZS_Huf_decodeImpl(
            dst, dstSize, src, srcSize, elementSize, false, params);
}

ZL_Report ZS_Raw_decode(
        void* const dst,
        size_t const dstSize,
        void const* const src,
        size_t const srcSize,
        size_t const elementSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_DLOG(V1, "RAW decode");
    if (!ZL_isPow2(elementSize) || elementSize == 0 || elementSize > 8) {
        ZL_ERR(GENERIC);
    }
    if (dstSize * elementSize != srcSize) {
        ZL_ERR(GENERIC);
    }
    if (srcSize == 0) {
        return ZL_returnSuccess();
    }

    ZL_WC dstWC = ZL_WC_wrap(dst, dstSize * elementSize);
    ZL_RC srcRC = ZL_RC_wrap(src, srcSize);
    ZS_Endianness_transform(
            &dstWC,
            &srcRC,
            ZL_Endianness_host(),
            ZL_Endianness_canonical,
            elementSize);
    ZL_ASSERT_EQ(ZL_WC_avail(&dstWC), 0);
    ZL_ASSERT_EQ(ZL_RC_avail(&srcRC), 0);
    return ZL_returnSuccess();
}

ZL_Report ZS_Constant_decode(
        void* dst,
        size_t dstSize,
        void const* src,
        size_t srcSize,
        size_t elementSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_DLOG(V1, "Constant decode");
    if (!ZL_isPow2(elementSize) || elementSize == 0 || elementSize > 8) {
        ZL_ERR(GENERIC);
    }
    if (srcSize != elementSize) {
        ZL_ERR(GENERIC);
    }
    if (elementSize == 1) {
        uint8_t const value = *(uint8_t const*)src;
        memset(dst, value, dstSize);
        return ZL_returnSuccess();
    }
    // TODO: Optimize multi-byte elements
    switch (elementSize) {
        case 2: {
            uint16_t const value  = ZL_readCE16(src);
            uint16_t* const dst16 = (uint16_t*)dst;
            for (size_t i = 0; i < dstSize; ++i) {
                dst16[i] = value;
            }
            break;
        }
        case 4: {
            uint32_t const value  = ZL_readCE32(src);
            uint32_t* const dst32 = (uint32_t*)dst;
            for (size_t i = 0; i < dstSize; ++i) {
                dst32[i] = value;
            }
            break;
        }
        case 8: {
            uint64_t const value  = ZL_readCE64(src);
            uint64_t* const dst64 = (uint64_t*)dst;
            for (size_t i = 0; i < dstSize; ++i) {
                dst64[i] = value;
            }
            break;
        }
        default: {
            ZL_ASSERT(false);
            break;
        }
    }
    return ZL_returnSuccess();
}

ZL_Report ZS_Bit_decode(
        void* dst,
        size_t dstSize,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        size_t numBits)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (elementSize > 2) {
        ZL_DLOG(ERROR, "Not supported yet.");
        ZL_ERR(GENERIC);
    }
    if (numBits >= 8 * elementSize) {
        ZL_ERR(GENERIC);
    }
    size_t const expectedSrcSize = (dstSize * numBits + 7) / 8;
    if (srcSize != expectedSrcSize) {
        ZL_DLOG(ERROR, "Corruption!");
        ZL_ERR(GENERIC);
    }

    ZS_bitpackDecode(dst, dstSize, elementSize, src, srcSize, (int)numBits);
    return ZL_returnSuccess();
}
