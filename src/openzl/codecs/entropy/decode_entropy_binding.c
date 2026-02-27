// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/entropy/decode_entropy_binding.h"

#define FSE_STATIC_LINKING_ONLY
#define HUF_STATIC_LINKING_ONLY

#include "openzl/codecs/entropy/decode_huffman_kernel.h"
#include "openzl/codecs/entropy/deprecated/common_entropy.h"
#include "openzl/common/assertion.h"
#include "openzl/decompress/dictx.h"
#include "openzl/fse/fse.h"
#include "openzl/fse/huf.h"
#include "openzl/shared/mem.h" // srcSize
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_errors.h"

#if ZL_HAS_SSSE3
#    include <emmintrin.h>
#endif

static FSE_DTable const*
buildFSEDTable(ZL_Decoder* dictx, int16_t const* norm, size_t nbSymbols)
{
    if (nbSymbols < 2) {
        // Can't encode empty or constant data
        return NULL;
    }
    if (nbSymbols > 256) {
        // Only supports serialized data
        return NULL;
    }

    bool invalid = false;
    unsigned sum = 0;
    for (size_t i = 0; i < nbSymbols; ++i) {
        invalid |= norm[i] < -1;
        sum += norm[i] == -1 ? 1 : (unsigned)norm[i];
    }
    if (invalid) {
        return NULL;
    }
    if (!(sum > 0 && ZL_isPow2(sum))) {
        return NULL;
    }
    int const tableLog = ZL_highbit32(sum);
    if (tableLog < FSE_MIN_TABLELOG || tableLog > FSE_MAX_TABLELOG) {
        return NULL;
    }
    FSE_DTable* const dtable =
            ZL_Decoder_getScratchSpace(dictx, FSE_DTABLE_SIZE(tableLog));
    if (dtable == NULL) {
        return NULL;
    }
    size_t const ret = FSE_buildDTable(
            dtable, norm, (unsigned)nbSymbols - 1, (unsigned)tableLog);
    if (FSE_isError(ret)) {
        return NULL;
    }
    return dtable;
}

ZL_Report DI_fse_v2(ZL_Decoder* dictx, const ZL_Input* in[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_Input const* const normStream = in[0];
    ZL_Input const* const bitsStream = in[1];

    ZL_ASSERT_EQ(ZL_Input_type(normStream), ZL_Type_numeric);
    ZL_ASSERT_EQ(ZL_Input_type(bitsStream), ZL_Type_serial);

    ZL_ERR_IF_NE(ZL_Input_eltWidth(normStream), 2, corruption);

    FSE_DTable const* dtable = buildFSEDTable(
            dictx,
            (int16_t const*)ZL_Input_ptr(normStream),
            ZL_Input_numElts(normStream));
    ZL_ERR_IF_NULL(dtable, corruption);

    unsigned nbStates;
    size_t dstSize;
    {
        ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dictx);
        ZL_ERR_IF_LT(header.size, 2, corruption, "Min size = 2 bytes");
        ZL_ERR_IF_GT(header.size, 9, corruption, "Max size = 9 bytes");
        uint8_t const* ptr = (uint8_t const*)header.start;
        nbStates           = *ptr++;
        ZL_ERR_IF_NOT(
                nbStates == 2 || nbStates == 4,
                corruption,
                "Unsupported number of states");
        dstSize = ZL_readLE64_N(ptr, header.size - 1);
        ZL_ERR_IF_LT(dstSize, 2, corruption, "Must have at least 2 elements");
    }

    ZL_Output* const outStream = ZL_Decoder_create1OutStream(dictx, dstSize, 1);
    ZL_ERR_IF_NULL(outStream, allocation);

    size_t const ret = FSE_decompress_usingDTable(
            ZL_Output_ptr(outStream),
            dstSize,
            ZL_Input_ptr(bitsStream),
            ZL_Input_numElts(bitsStream),
            dtable,
            nbStates);
    ZL_ERR_IF(
            FSE_isError(ret),
            corruption,
            "FSE decoding failed: %s",
            FSE_getErrorName(ret));
    ZL_ERR_IF_NE(ret, dstSize, corruption);

    ZL_ERR_IF_ERR(ZL_Output_commit(outStream, dstSize));

    return ZL_returnSuccess();
}

ZL_Report DI_fse_ncount(ZL_Decoder* dictx, const ZL_Input* in[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_Input const* const srcStream = in[0];

    ZL_Output* ncountStream =
            ZL_Decoder_create1OutStream(dictx, 256, sizeof(short));
    ZL_ERR_IF_NULL(ncountStream, allocation);

    unsigned maxSymbolValue = 255;
    unsigned tableLog       = FSE_MAX_TABLELOG;
    size_t const ncountSize = FSE_readNCount(
            ZL_Output_ptr(ncountStream),
            &maxSymbolValue,
            &tableLog,
            ZL_Input_ptr(srcStream),
            ZL_Input_numElts(srcStream));
    ZL_ERR_IF(
            FSE_isError(ncountSize),
            corruption,
            "Failed to read nCount: %s",
            FSE_getErrorName(ncountSize));
    ZL_ERR_IF_NE(ncountSize, ZL_Input_numElts(srcStream), corruption);

    ZL_ERR_IF_ERR(ZL_Output_commit(ncountStream, 1 + maxSymbolValue));

    return ZL_returnSuccess();
}

ZL_FORCE_INLINE void countWeights(
        uint32_t weightCounts[HUF_TABLELOG_MAX + 1],
        uint8_t const* weights,
        size_t nbWeights)
{
    ZL_ASSERT_LE(nbWeights, 256);
    ZL_STATIC_ASSERT(HUF_TABLELOG_MAX < 16, "Assumption");
#if ZL_HAS_SSSE3
    // Optimized counting because we expect a lot of collisions in the Histogram
    __m128i const iota =
            _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    __m128i count = _mm_setzero_si128();
    for (size_t i = 0; i < nbWeights; ++i) {
        __m128i const inc =
                _mm_cmpeq_epi8(_mm_set1_epi8((char)weights[i]), iota);
        count = _mm_sub_epi8(count, inc);
    }
    bool const everyCountIsZero =
            _mm_movemask_epi8(_mm_cmpeq_epi8(count, _mm_setzero_si128()))
            == 0xFFFF;
    // Check for overflow
    if (nbWeights == 256 && everyCountIsZero) {
        memset(weightCounts, 0, sizeof(*weightCounts) * (HUF_TABLELOG_MAX + 1));
        weightCounts[weights[0]] = 256;
        return;
    }
    uint8_t counts[16];
    _mm_storeu_si128((__m128i_u*)counts, count);
    for (size_t i = 0; i <= HUF_TABLELOG_MAX; ++i) {
        weightCounts[i] = counts[i];
    }
#else
    uint8_t weightCounts0[HUF_TABLELOG_MAX + 1] = { 0 };
    uint8_t weightCounts1[HUF_TABLELOG_MAX + 1] = { 0 };
    uint8_t weightCounts2[HUF_TABLELOG_MAX + 1] = { 0 };
    uint8_t weightCounts3[HUF_TABLELOG_MAX + 1] = { 0 };

    size_t const prefix = nbWeights % 4;
    for (size_t i = 0; i < prefix; ++i) {
        weightCounts0[weights[i]] = (uint8_t)(weightCounts0[weights[i]] + 1);
    }
    for (size_t i = prefix; i < nbWeights; i += 4) {
        weightCounts0[weights[i + 0]] =
                (uint8_t)(weightCounts0[weights[i + 0]] + 1);
        weightCounts1[weights[i + 1]] =
                (uint8_t)(weightCounts1[weights[i + 1]] + 1);
        weightCounts2[weights[i + 2]] =
                (uint8_t)(weightCounts2[weights[i + 2]] + 1);
        weightCounts3[weights[i + 3]] =
                (uint8_t)(weightCounts3[weights[i + 3]] + 1);
    }
    for (size_t i = 0; i < HUF_TABLELOG_MAX + 1; ++i) {
        weightCounts[i] = (uint32_t)weightCounts0[i]
                + (uint32_t)weightCounts1[i] + (uint32_t)weightCounts2[i]
                + (uint32_t)weightCounts3[i];
    }
#endif
}

static HUF_DTable const* buildHUFDTable(
        ZL_Decoder* dictx,
        uint8_t const* weights,
        size_t nbWeights,
        bool x2)
{
    if (nbWeights < 2 || nbWeights > 256) {
        ZL_LOG(ERROR, "Invalid nbWeights: %zu", nbWeights);
        return NULL;
    }

    bool invalid = false;
    unsigned sum = 0;
    for (size_t i = 0; i < nbWeights; ++i) {
        uint8_t const w = weights[i];
        invalid |= (w > HUF_TABLELOG_MAX);
        sum += (1u << (w & 31)) >> 1;
    }
    if (invalid) {
        ZL_LOG(ERROR, "Invalid weight > %u", HUF_TABLELOG_MAX);
        return NULL;
    }
    if (!(sum > 0 && ZL_isPow2(sum))) {
        ZL_LOG(ERROR, "Invalid sum: %u is not pow2", sum);
        return NULL;
    }
    unsigned const tableLog = (unsigned)ZL_highbit32(sum);
    if (tableLog > HUF_TABLELOG_MAX) {
        ZL_LOG(ERROR,
               "Table log too large: %u > %u",
               tableLog,
               HUF_TABLELOG_MAX);
        return NULL;
    }

    unsigned const maxTableLog = ZL_MAX(tableLog, HUF_TABLELOG_DEFAULT);
    HUF_DTable* const dtable   = ZL_Decoder_getScratchSpace(
            dictx,
            (size_t)HUF_DTABLE_SIZE(x2 ? maxTableLog : maxTableLog - 1)
                    * sizeof(HUF_DTable));
    if (dtable == NULL) {
        return NULL;
    }
    dtable[0] = (unsigned)maxTableLog * 0x01000001;
    ZL_ASSERT_EQ(*((uint8_t*)dtable), maxTableLog);

    uint8_t mutWeights[256];
    memcpy(mutWeights, weights, nbWeights);
    uint32_t weightCounts[HUF_TABLELOG_MAX + 1];
    countWeights(weightCounts, weights, nbWeights);

    const size_t nbZeroWeights    = weightCounts[0];
    const size_t nbNonZeroWeights = nbWeights - nbZeroWeights;
    if (nbNonZeroWeights < 2) {
        ZL_LOG(ERROR, "Must have at least 2 non-zero weights");
        return NULL;
    }

    size_t const ret = x2 ? HUF_buildDTableX2(
                                    dtable,
                                    mutWeights,
                                    (uint32_t)nbWeights,
                                    weightCounts,
                                    tableLog)
                          : HUF_buildDTableX1(
                                    dtable,
                                    mutWeights,
                                    (uint32_t)nbWeights,
                                    weightCounts,
                                    tableLog);
    if (HUF_isError(ret)) {
        ZL_LOG(ERROR,
               "HUF_buildDTable failed: x2=%u: %s",
               (unsigned)x2,
               HUF_getErrorName(ret));
        return NULL;
    }

    return dtable;
}

ZL_Report DI_huffman_v2(ZL_Decoder* dictx, const ZL_Input* in[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_Input const* const weightsStream = in[0];
    ZL_Input const* const bitsStream    = in[1];

    // TODO(terrelln): Should this be serialized? It is more efficient, but also
    // a less accurate representation
    ZL_ASSERT_EQ(ZL_Input_type(weightsStream), ZL_Type_numeric);
    ZL_ASSERT_EQ(ZL_Input_type(bitsStream), ZL_Type_serial);

    ZL_ERR_IF_NE(ZL_Input_eltWidth(weightsStream), 1, corruption);

    bool x4;
    size_t dstSize;
    {
        ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dictx);
        ZL_ERR_IF_LT(header.size, 2, corruption, "Min size = 2 bytes");
        ZL_ERR_IF_GT(header.size, 9, corruption, "Max size = 9 bytes");
        uint8_t const* ptr = (uint8_t const*)header.start;
        x4                 = (*ptr & 0x1) != 0;
        ++ptr;
        dstSize = ZL_readLE64_N(ptr, header.size - 1);
        ZL_ERR_IF_LT(dstSize, 2, corruption, "Must have at least 2 elements");
    }

    bool const x2 = x4
            ? HUF_selectDecoder(dstSize, ZL_Input_numElts(bitsStream))
            : false;

    HUF_DTable const* dtable = buildHUFDTable(
            dictx,
            (uint8_t const*)ZL_Input_ptr(weightsStream),
            ZL_Input_numElts(weightsStream),
            x2);
    ZL_ERR_IF_NULL(dtable, corruption);

    ZL_Output* const outStream = ZL_Decoder_create1OutStream(dictx, dstSize, 1);
    ZL_ERR_IF_NULL(outStream, allocation);

    {
        void* dst            = ZL_Output_ptr(outStream);
        void const* src      = ZL_Input_ptr(bitsStream);
        size_t const srcSize = ZL_Input_numElts(bitsStream);
        size_t (*decode)(
                void*, size_t, void const*, size_t, HUF_DTable const*) = x4
                ? (x2 ? HUF_decompress4X2_usingDTable
                      : HUF_decompress4X1_usingDTable)
                : HUF_decompress1X1_usingDTable;
        ZL_ASSERT_EQ(!x4 && x2, false);
        size_t const ret = decode(dst, dstSize, src, srcSize, dtable);
        ZL_ERR_IF(
                HUF_isError(ret),
                corruption,
                "HUF decoding failed: %s",
                HUF_getErrorName(ret));
        ZL_ERR_IF_NE(ret, dstSize, corruption);
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(outStream, dstSize));

    return ZL_returnSuccess();
}

static ZS_Huf16DElt const* buildHUF16DTable(
        ZL_Decoder* dictx,
        uint8_t const* weights,
        size_t nbWeights,
        int* tableLogPtr)
{
    if (nbWeights < 2 || nbWeights > 65536) {
        return NULL;
    }

    // Ensure there are at least 2 non-zero weights
    size_t firstNonZeroWeight = 0;
    for (; firstNonZeroWeight < nbWeights; ++firstNonZeroWeight) {
        if (weights[firstNonZeroWeight] != 0) {
            break;
        }
    }
    {
        size_t secondNonZeroWeight = firstNonZeroWeight + 1;
        for (; secondNonZeroWeight < nbWeights; ++secondNonZeroWeight) {
            if (weights[secondNonZeroWeight] != 0) {
                break;
            }
        }
        if (secondNonZeroWeight >= nbWeights) {
            ZL_LOG(ERROR, "Must have at least 2 non-zero weights");
            return NULL;
        }
    }

    bool invalid = false;
    uint64_t sum = 0;
    for (size_t i = firstNonZeroWeight; i < nbWeights; ++i) {
        uint8_t const w = weights[i];
        invalid |= (w > ZS_kLargeHuffmanMaxNbBits);
        sum += (((uint64_t)1) << (w & 63)) >> 1;
    }
    if (invalid) {
        ZL_LOG(ERROR, "Invalid weight > %u", ZS_kLargeHuffmanMaxNbBits);
        return NULL;
    }
    if (!ZL_isPow2(sum)) {
        ZL_LOG(ERROR, "Invalid sum: %u is not pow2", sum);
        return NULL;
    }
    ZL_ASSERT_NE(sum, 0);
    int const tableLog = ZL_highbit64(sum);
    if (tableLog > ZS_kLargeHuffmanMaxNbBits) {
        ZL_LOG(ERROR,
               "Table log too large: %u > %u",
               tableLog,
               ZS_kLargeHuffmanMaxNbBits);
        return NULL;
    }

    ZS_Huf16DElt* const dtable = ZL_Decoder_getScratchSpace(
            dictx, ((size_t)1 << tableLog) * sizeof(ZS_Huf16DElt));
    if (dtable == NULL) {
        return NULL;
    }

    ZS_largeHuffmanBuildDTable(dtable, weights, nbWeights, tableLog);
    *tableLogPtr = tableLog;
    return dtable;
}

ZL_Report DI_huffman_struct_v2(ZL_Decoder* dictx, const ZL_Input* in[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_Input const* const weightsStream = in[0];
    ZL_Input const* const bitsStream    = in[1];

    // TODO(terrelln): Should this be serialized? It is more efficient, but
    // also a less accurate representation
    ZL_ASSERT_EQ(ZL_Input_type(weightsStream), ZL_Type_numeric);
    ZL_ASSERT_EQ(ZL_Input_type(bitsStream), ZL_Type_serial);

    ZL_ERR_IF_NE(ZL_Input_eltWidth(weightsStream), 1, corruption);

    bool x4;
    size_t dstSize;
    {
        ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dictx);
        ZL_ERR_IF_LT(header.size, 2, corruption, "Min size = 2 bytes");
        ZL_ERR_IF_GT(header.size, 9, corruption, "Max size = 9 bytes");
        uint8_t const* ptr = (uint8_t const*)header.start;
        x4                 = (*ptr & 0x1) != 0;
        ++ptr;
        dstSize = ZL_readLE64_N(ptr, header.size - 1);
        ZL_ERR_IF_LT(dstSize, 2, corruption, "Must have at least 2 elements");
    }

    int tableLog;
    ZS_Huf16DElt const* dtable = buildHUF16DTable(
            dictx,
            (uint8_t const*)ZL_Input_ptr(weightsStream),
            ZL_Input_numElts(weightsStream),
            &tableLog);
    ZL_ERR_IF_NULL(dtable, corruption);

    ZL_Output* const outStream = ZL_Decoder_create1OutStream(dictx, dstSize, 2);
    ZL_ERR_IF_NULL(outStream, allocation);

    {
        ZL_RC src = ZL_RC_wrap(
                ZL_Input_ptr(bitsStream), ZL_Input_numElts(bitsStream));
        uint16_t* dst    = ZL_Output_ptr(outStream);
        ZL_Report report = x4 ? ZS_largeHuffmanDecodeUsingDTableX4(
                                        dst, dstSize, &src, dtable, tableLog)
                              : ZS_largeHuffmanDecodeUsingDTable(
                                        dst, dstSize, &src, dtable, tableLog);
        ZL_ERR_IF_ERR(report);
        ZL_ERR_IF_NE(ZL_validResult(report), dstSize, corruption);
        ZL_ERR_IF_NE(ZL_RC_avail(&src), 0, corruption);
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(outStream, dstSize));

    return ZL_returnSuccess();
}

static ZL_Report
DI_entropyDstBound(const void* src, size_t srcSize, size_t eltWidth)
{
    if (srcSize == 0) {
        return ZL_returnValue(0);
    }
    ZL_Report const ret = ZS_Entropy_getDecodedSize(src, srcSize, eltWidth);
    ZL_RET_R_IF(
            GENERIC,
            ZL_isError(ret),
            "corruption: ZS_Entropy_getDecodedSize failed");
    return ret;
}

static ZL_Report DI_entropyDecode(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        size_t eltWidth,
        ZS_Entropy_DecodeParameters const* optionalParams)
{
    if (srcSize == 0) {
        return ZL_returnValue(0);
    }
    ZL_ReadCursor rc = ZL_RC_wrap((const uint8_t*)src, srcSize);
    ZS_Entropy_DecodeParameters const deafultParams =
            ZS_Entropy_DecodeParameters_default();
    ZL_Report const ret = ZS_Entropy_decode(
            dst,
            dstCapacity,
            &rc,
            eltWidth,
            optionalParams ? optionalParams : &deafultParams);
    ZL_RET_R_IF(corruption, ZL_isError(ret), "ZS_Entropy_decodeDefault failed");
    ZL_RET_R_IF_NE(corruption, ZL_RC_avail(&rc), 0, "Not all input consumed");
    return ret;
}

// ZL_TypedEncoderFn
static ZL_Report
DI_huffman_typed(ZL_Decoder* dictx, const ZL_Input* ins[], bool hasHeader)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_ASSERT_EQ(ZL_Input_eltWidth(in), 1);
    const uint8_t* src = (uint8_t const*)ZL_Input_ptr(in);
    size_t srcSize     = ZL_Input_numElts(in);

    //> The actual dst nbElts/eltWidth.
    size_t dstNbElts;
    size_t dstEltWidth;

    //> The nbElts/eltWidth that we told the entropy compressor.
    size_t entropyEltWidth;
    size_t entropyNbElts;

    //> Read the header if present
    //> Determine the dst/entropy nbElts/eltWidth.
    //> They are different when we are dealing with transposed streams.
    if (hasHeader) {
        ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dictx);
        uint8_t const* hdr      = (uint8_t const*)header.start;
        uint8_t const* hdrEnd   = hdr + header.size;
        ZL_ERR_IF_LT(header.size, 2, header_unknown);

        bool const isTransposed        = *hdr++ != 0;
        ZL_RESULT_OF(uint64_t) const r = ZL_varintDecode(&hdr, hdrEnd);
        ZL_ERR_IF(ZL_RES_isError(r), header_unknown);
        ZL_ERR_IF_NE(hdr, hdrEnd, header_unknown);
        uint64_t const eltWidth = ZL_RES_value(r);

        ZL_ERR_IF_EQ(eltWidth, 0, header_unknown, "Invalid element width!");

        entropyEltWidth = isTransposed ? 1 : eltWidth;
        ZL_TRY_LET_R(nbElts, DI_entropyDstBound(src, srcSize, entropyEltWidth));
        entropyNbElts = nbElts;

        dstEltWidth = eltWidth;
        dstNbElts   = entropyNbElts / (dstEltWidth / entropyEltWidth);
    } else {
        entropyEltWidth = 1;
        ZL_TRY_LET_R(nbElts, DI_entropyDstBound(src, srcSize, 1));
        entropyNbElts = nbElts;
        dstEltWidth   = 1;
        dstNbElts     = entropyNbElts;
    }
    ZL_ERR_IF_NE(
            entropyNbElts * entropyEltWidth,
            dstNbElts * dstEltWidth,
            header_unknown,
            "Overflow computing element widths");

    if (DI_getFrameFormatVersion(dictx) >= 11) {
        ZL_ERR_IF_GT(
                dstEltWidth,
                2,
                corruption,
                "eltWidth > 2 is not supported in version 11 or newer.");
    }

    //> Create the output stream.
    ZL_Output* const out =
            ZL_Decoder_create1OutStream(dictx, dstNbElts, dstEltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    //> Decode & tell how much we wrote to the output buffer.
    ZL_TRY_LET_R(
            dSize,
            DI_entropyDecode(
                    ZL_Output_ptr(out),
                    entropyNbElts,
                    src,
                    srcSize,
                    entropyEltWidth,
                    NULL));
    ZL_ERR_IF_NE(dSize, entropyNbElts, corruption, "Entropy decoding failed");
    ZL_ERR_IF_ERR(ZL_Output_commit(out, dstNbElts));

    //> Return the number of output streams.
    return ZL_returnValue(1);
}

ZL_Report DI_huffman_serialized(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    return DI_huffman_typed(dictx, ins, false);
}

ZL_Report DI_huffman_fixed(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    return DI_huffman_typed(dictx, ins, true);
}

ZL_Report DI_fse_typed(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_ASSERT_EQ(ZL_Input_eltWidth(in), 1);
    const uint8_t* src = (uint8_t const*)ZL_Input_ptr(in);
    size_t srcSize     = ZL_Input_numElts(in);

    // Read the header if present and set the number of states used, if no
    // header use default of 2 states which matches older versions.
    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dictx);
    uint8_t nbStates        = 2;
    if (header.size) {
        // Header should be only 1 bytes, anything else is probably a
        // corruption
        ZL_ERR_IF_NE(
                header.size,
                1,
                corruption,
                "FSE header size should be at most 1, got unexpected header size - %d",
                header.size);
        nbStates = *(uint8_t const*)header.start;
        // We support only 2 or 4 states, anything else is probably a
        // corruption
        ZL_ERR_IF(
                nbStates != 2 && nbStates != 4,
                corruption,
                "FSE supports only 2 or 4 states, got unexpected number of states - %d",
                nbStates);
    }

    ZL_TRY_LET_R(nbElts, DI_entropyDstBound(src, srcSize, 1));

    // Create the output stream.
    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, nbElts, 1);
    ZL_ERR_IF_NULL(out, allocation);

    // Decode & tell how much we wrote to the output buffer.
    ZS_Entropy_DecodeParameters params = ZS_Entropy_DecodeParameters_default();
    params.fseNbStates                 = nbStates;
    ZL_TRY_LET_R(
            dSize,
            DI_entropyDecode(
                    ZL_Output_ptr(out), nbElts, src, srcSize, 1, &params));
    ZL_ERR_IF_NE(dSize, nbElts, corruption, "FSE decoding failed");
    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbElts));

    // Return the number of output streams.
    return ZL_returnValue(1);
}
