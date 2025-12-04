// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/entropy/encode_entropy_binding.h"

#define FSE_STATIC_LINKING_ONLY
#define HUF_STATIC_LINKING_ONLY

#include "openzl/codecs/constant/encode_constant_binding.h"
#include "openzl/codecs/entropy/deprecated/common_entropy.h"
#include "openzl/codecs/entropy/encode_entropy_selector.h"
#include "openzl/codecs/entropy/encode_huffman_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h" // ZS2_RET_IF*
#include "openzl/compress/enc_interface.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/fse/fse.h"
#include "openzl/fse/huf.h"
#include "openzl/shared/data_stats.h"
#include "openzl/shared/histogram.h"
#include "openzl/shared/mem.h" // writeLE32
#include "openzl/shared/varint.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_graph_api.h"

#define ENTROPY_HISTORAM_PID 246

static ZL_Histogram const* getHistogram(ZL_Encoder* eictx, const ZL_Input* in)
{
    ZL_RefParam param = ZL_Encoder_getLocalParam(eictx, ENTROPY_HISTORAM_PID);
    if (param.paramRef != NULL) {
        return (ZL_Histogram const*)param.paramRef;
    }
    size_t const eltWidth = ZL_Input_eltWidth(in);
    if (eltWidth > 2) {
        return NULL;
    }
    ZL_Histogram* histogram = (ZL_Histogram*)ZL_Encoder_getScratchSpace(
            eictx,
            eltWidth == 1 ? sizeof(ZL_Histogram8) : sizeof(ZL_Histogram16));
    if (histogram == NULL) {
        return NULL;
    }
    ZL_Histogram_init(histogram, eltWidth == 1 ? 255 : 65535);
    ZL_Histogram_build(
            histogram, ZL_Input_ptr(in), ZL_Input_numElts(in), eltWidth);
    return histogram;
}

ZL_Report EI_fse_v2(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT(ZL_Input_type(in) == ZL_Type_serial);
    uint8_t const* src            = ZL_Input_ptr(in);
    size_t const srcSize          = ZL_Input_numElts(in);
    ZL_Histogram const* histogram = getHistogram(eictx, in);

    ZL_RET_R_IF_LT(
            node_invalid_input,
            srcSize,
            2,
            "Must not use FSE for 0 or 1 element (should be impossible for users to trigger)");
    ZL_RET_R_IF_EQ(
            node_invalid_input,
            histogram->count[histogram->maxSymbol],
            histogram->total,
            "Must not use FSE on constant data (should be impossible for users to trigger)");

    // 1. Decide on number of states & send header
    unsigned const nbStates = srcSize < 1000 ? 2 : 4;
    {
        size_t const nbBytes = (size_t)(ZL_nextPow2(srcSize + 1) + 7) / 8;
        uint8_t header[sizeof(uint64_t) + 1];
        header[0] = (uint8_t)nbStates;
        ZL_writeLE64_N(header + 1, srcSize, nbBytes);
        ZL_ASSERT_EQ(ZL_readLE64_N(header + 1, nbBytes), srcSize);
        ZL_Encoder_sendCodecHeader(eictx, header, nbBytes + 1);
    }

    // 2. Build table
    FSE_CTable* ctable;
    {
        size_t const normSize = histogram->maxSymbol + 1;
        ZL_Output* const normStream =
                ZL_Encoder_createTypedStream(eictx, 0, normSize, 2);
        ZL_RET_R_IF_NULL(allocation, normStream);
        int16_t* const normCount = ZL_Output_ptr(normStream);

        unsigned const tableLog = FSE_optimalTableLog(
                FSE_DEFAULT_TABLELOG, srcSize, histogram->maxSymbol);
        ctable = ZL_Encoder_getScratchSpace(
                eictx, FSE_CTABLE_SIZE(tableLog, histogram->maxSymbol));
        ZL_RET_R_IF_NULL(allocation, ctable);

        ZL_RET_R_IF(
                GENERIC,
                FSE_isError(FSE_normalizeCount(
                        normCount,
                        tableLog,
                        histogram->count,
                        srcSize,
                        histogram->maxSymbol,
                        true)));

        ZL_RET_R_IF(
                GENERIC,
                FSE_isError(FSE_buildCTable(
                        ctable, normCount, histogram->maxSymbol, tableLog)));

        ZL_RET_R_IF_ERR(ZL_Output_setIntMetadata(normStream, 0, (int)tableLog));
        ZL_RET_R_IF_ERR(ZL_Output_commit(normStream, normSize));
    }

    // 3. Encode
    size_t const bitCapacity = FSE_compressBound(srcSize);
    ZL_Output* bitStream =
            ZL_Encoder_createTypedStream(eictx, 1, bitCapacity, 1);
    ZL_RET_R_IF_NULL(allocation, bitStream);

    size_t const bitSize = FSE_compress_usingCTable(
            ZL_Output_ptr(bitStream),
            bitCapacity,
            src,
            srcSize,
            ctable,
            nbStates);
    ZL_RET_R_IF(node_invalid_input, FSE_isError(bitSize));
    ZL_RET_R_IF_EQ(
            node_invalid_input,
            bitSize,
            0,
            "FSE source is not compressible (should be impossible to trigger for user)");
    ZL_RET_R_IF_ERR(ZL_Output_commit(bitStream, bitSize));

    return ZL_returnSuccess();
}

ZL_Report EI_fse_ncount(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT(ZL_Input_type(in) == ZL_Type_numeric);
    ZL_RET_R_IF_NE(node_invalid_input, ZL_Input_eltWidth(in), 2);

    short const* const ncount = ZL_Input_ptr(in);
    size_t const nbCounts     = ZL_Input_numElts(in);

    ZL_RET_R_IF_EQ(node_invalid_input, nbCounts, 0);
    ZL_RET_R_IF_GT(node_invalid_input, nbCounts, 256);
    ZL_RET_R_IF_EQ(node_invalid_input, ncount[nbCounts - 1], 0);

    bool invalid = false;
    uint64_t sum = 0;
    for (size_t i = 0; i < nbCounts; ++i) {
        sum += ncount[i] == -1 ? (uint64_t)1 : (uint64_t)ncount[i];
        invalid |= ncount[i] < -1;
    }
    ZL_RET_R_IF(node_invalid_input, invalid, "Ncount must not be less than -1");
    ZL_RET_R_IF_NOT(node_invalid_input, ZL_isPow2(sum));
    ZL_RET_R_IF_EQ(node_invalid_input, sum, 0);
    unsigned const tableLog = (unsigned)ZL_highbit64(sum);

    ZL_RET_R_IF_LT(node_invalid_input, tableLog, FSE_MIN_TABLELOG);
    ZL_RET_R_IF_GT(node_invalid_input, tableLog, FSE_MAX_TABLELOG);

    ZL_Output* const dstStream =
            ZL_Encoder_createTypedStream(eictx, 0, FSE_NCOUNTBOUND, 1);
    ZL_RET_R_IF_NULL(allocation, dstStream);

    size_t const ncountSize = FSE_writeNCount(
            ZL_Output_ptr(dstStream),
            FSE_NCOUNTBOUND,
            ncount,
            (unsigned)nbCounts - 1,
            tableLog);
    ZL_RET_R_IF(
            GENERIC,
            FSE_isError(ncountSize),
            "%s",
            FSE_getErrorName(ncountSize));

    ZL_RET_R_IF_ERR(ZL_Output_commit(dstStream, ncountSize));

    return ZL_returnSuccess();
}

ZL_Report EI_huffman_v2(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT(ZL_Input_type(in) == ZL_Type_serial);
    uint8_t const* src            = ZL_Input_ptr(in);
    size_t const srcSize          = ZL_Input_numElts(in);
    ZL_Histogram const* histogram = getHistogram(eictx, in);

    ZL_RET_R_IF_LT(
            node_invalid_input,
            srcSize,
            2,
            "Must not use Huffman for 0 or 1 element (should be impossible for users to trigger)");
    ZL_RET_R_IF_EQ(
            node_invalid_input,
            histogram->count[histogram->maxSymbol],
            histogram->total,
            "Must not use Huffman on constant data (should be impossible for users to trigger)");

    // 1. Build table
    HUF_CElt* ctable;
    {
        size_t const weightsSize = histogram->maxSymbol + 1;
        ZL_Output* const weightsStream =
                ZL_Encoder_createTypedStream(eictx, 0, weightsSize, 1);
        ZL_RET_R_IF_NULL(allocation, weightsStream);
        uint8_t* const weights = ZL_Output_ptr(weightsStream);

        size_t tableLog = HUF_optimalTableLog(
                HUF_TABLELOG_DEFAULT, srcSize, histogram->maxSymbol);
        ctable = ZL_Encoder_getScratchSpace(
                eictx, HUF_CTABLE_SIZE(histogram->maxSymbol));
        ZL_RET_R_IF_NULL(allocation, ctable);
        tableLog = HUF_buildCTable(
                ctable,
                histogram->count,
                histogram->maxSymbol,
                (unsigned)tableLog);
        ZL_RET_R_IF(GENERIC, HUF_isError(tableLog));

        HUF_CElt const* ct = ctable + 1;
        for (size_t i = 0; i < weightsSize; ++i) {
            size_t const length = HUF_getNbBits(ct[i]);
            ZL_ASSERT_EQ(length, HUF_getNbBitsFromCTable(ctable, (uint8_t)i));
            weights[i] = (uint8_t)(length == 0 ? 0 : tableLog + 1 - length);
            ZL_ASSERT_EQ(weights[i] == 0, histogram->count[i] == 0);
        }

        ZL_RET_R_IF_ERR(
                ZL_Output_setIntMetadata(weightsStream, 0, (int)tableLog));
        ZL_RET_R_IF_ERR(ZL_Output_commit(weightsStream, weightsSize));
    }

    // 2. Decide on 4x streams & send header
    bool const x4 = srcSize > 1000;
    {
        size_t const nbBytes = (size_t)(ZL_nextPow2(srcSize + 1) + 7) / 8;
        uint8_t header[sizeof(uint64_t) + 1];
        header[0] = (uint8_t)(x4 ? 1 : 0);
        ZL_writeLE64_N(header + 1, srcSize, nbBytes);
        ZL_ASSERT_EQ(ZL_readLE64_N(header + 1, nbBytes), srcSize);
        ZL_Encoder_sendCodecHeader(eictx, header, nbBytes + 1);
    }

    // 3. Encode
    size_t const bitCapacity = HUF_compressBound(srcSize);
    ZL_Output* bitStream =
            ZL_Encoder_createTypedStream(eictx, 1, bitCapacity, 1);
    ZL_RET_R_IF_NULL(allocation, bitStream);

    size_t const bitSize = x4 ? HUF_compress4X_usingCTable(
                                        ZL_Output_ptr(bitStream),
                                        bitCapacity,
                                        src,
                                        srcSize,
                                        ctable)
                              : HUF_compress1X_usingCTable(
                                        ZL_Output_ptr(bitStream),
                                        bitCapacity,
                                        src,
                                        srcSize,
                                        ctable);
    ZL_RET_R_IF(node_invalid_input, HUF_isError(bitSize));
    ZL_RET_R_IF_EQ(
            node_invalid_input,
            bitSize,
            0,
            "Huffman source is not compressible (should be impossible to trigger for user)");
    ZL_RET_R_IF_ERR(ZL_Output_commit(bitStream, bitSize));

    return ZL_returnSuccess();
}

ZL_Report
EI_huffman_struct_v2(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_RET_R_IF_NE(node_invalid_input, ZL_Input_eltWidth(in), 2);

    ZL_ASSERT(ZL_Input_type(in) == ZL_Type_struct);
    uint16_t const* src           = ZL_Input_ptr(in);
    size_t const srcSize          = ZL_Input_numElts(in);
    ZL_Histogram const* histogram = getHistogram(eictx, in);

    ZL_RET_R_IF_LT(
            node_invalid_input,
            srcSize,
            2,
            "Must not use Huffman for 0 or 1 element (should be impossible for users to trigger)");
    ZL_RET_R_IF_EQ(
            node_invalid_input,
            histogram->count[histogram->maxSymbol],
            histogram->total,
            "Must not use Huffman on constant data (should be impossible for users to trigger)");

    // 1. Build table
    ZS_Huf16CElt* ctable;
    int tableLog;
    {
        size_t const weightsSize = histogram->maxSymbol + 1;
        ZL_Output* const weightsStream =
                ZL_Encoder_createTypedStream(eictx, 0, weightsSize, 1);
        ZL_RET_R_IF_NULL(allocation, weightsStream);
        uint8_t* const weights = ZL_Output_ptr(weightsStream);

        ctable = ZL_Encoder_getScratchSpace(
                eictx, sizeof(ZS_Huf16CElt) * weightsSize);
        ZL_RET_R_IF_NULL(allocation, ctable);
        ZL_Report tableLogRet = ZS_largeHuffmanBuildCTable(
                ctable, histogram->count, (uint16_t)histogram->maxSymbol, 0);
        ZL_RET_R_IF_ERR(tableLogRet);
        tableLog = (int)ZL_validResult(tableLogRet);

        for (size_t i = 0; i < weightsSize; ++i) {
            int const length = ctable[i].nbBits;
            weights[i] = (uint8_t)(length == 0 ? 0 : tableLog + 1 - length);
            ZL_ASSERT_EQ(weights[i] == 0, histogram->count[i] == 0);
        }

        ZL_RET_R_IF_ERR(
                ZL_Output_setIntMetadata(weightsStream, 0, (int)tableLog));
        ZL_RET_R_IF_ERR(ZL_Output_commit(weightsStream, weightsSize));
    }

    // 2. Decide on 4x streams & send header
    bool const x4 = srcSize > 1000;
    {
        size_t const nbBytes = (size_t)(ZL_nextPow2(srcSize + 1) + 7) / 8;
        uint8_t header[sizeof(uint64_t) + 1];
        header[0] = (uint8_t)(x4 ? 1 : 0);
        ZL_writeLE64_N(header + 1, srcSize, nbBytes);
        ZL_ASSERT_EQ(ZL_readLE64_N(header + 1, nbBytes), srcSize);
        ZL_Encoder_sendCodecHeader(eictx, header, nbBytes + 1);
    }

    // 3. Encode
    size_t const bitCapacity = 2 * srcSize + 32;
    ZL_Output* bitStream =
            ZL_Encoder_createTypedStream(eictx, 1, bitCapacity, 1);
    ZL_RET_R_IF_NULL(allocation, bitStream);

    ZL_WC bits             = ZL_WC_wrap(ZL_Output_ptr(bitStream), bitCapacity);
    ZL_Report const report = x4
            ? ZS_largeHuffmanEncodeUsingCTableX4(
                      &bits, src, srcSize, ctable, tableLog)
            : ZS_largeHuffmanEncodeUsingCTable(
                      &bits, src, srcSize, ctable, tableLog);
    ZL_RET_R_IF_ERR(report);
    ZL_ASSERT_LE(ZL_WC_size(&bits), bitCapacity);
    ZL_RET_R_IF_ERR(ZL_Output_commit(bitStream, ZL_WC_size(&bits)));

    return ZL_returnSuccess();
}

// ZL_TypedEncoderFn
ZL_Report EI_fse_typed(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);
    ZL_ASSERT(
            ZL_Input_type(in) == ZL_Type_serial
            || ZL_Input_type(in) == ZL_Type_struct);
    ZL_RET_R_IF_NE(GENERIC, ZL_Input_eltWidth(in), 1);
    const void* const src = ZL_Input_ptr(in);
    size_t const srcSize  = ZL_Input_numElts(in);
    size_t const dstCapacity =
            ZS_Entropy_encodedSizeBound(srcSize, /* elementSize */ 1);
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eictx, 0, dstCapacity, 1);
    ZL_RET_R_IF_NULL(allocation, out);
    // Starting version 5 we can support more than two states and we send the
    // number of states in the header, otherwise we conform to older versions
    // that only support 2 states.
    uint8_t nbStates = 2;
    if (ZL_Encoder_getCParam(eictx, ZL_CParam_formatVersion) >= 5) {
        nbStates               = 4;
        const uint8_t header[] = { nbStates };
        ZL_Encoder_sendCodecHeader(eictx, header, sizeof(header));
    }
    // TODO(@Cyan): ZS_Entropy_encodeFse() uses an old (deprecated)
    // ZL_WriteCursor API, it should be updated to no longer depends on this
    // abstraction
    ZL_WriteCursor wc = ZL_WC_wrap(ZL_Output_ptr(out), dstCapacity);
    ZL_RET_R_IF(
            GENERIC,
            ZL_isError(ZS_Entropy_encodeFse(
                    &wc, src, srcSize, /* elementSize */ 1, nbStates)));
    ZL_RET_R_IF_ERR(ZL_Output_commit(out, ZL_WC_size(&wc)));
    return ZL_returnValue(1);
}

static void EI_huffman_header(ZL_Encoder* eictx, ZL_Input const* in)
{
    bool const isTransposed = false; // Support removed in version 11
    // Starting in format 4 we no longer send a header for ZL_Type_serial,
    // because we can infer that information from the type of the transform. We
    // still need to send the header in version 3 and earlier for compatibility.
    if (ZL_Encoder_getCParam(eictx, ZL_CParam_formatVersion) >= 4
        && ZL_Input_type(in) == ZL_Type_serial) {
        return;
    }
    size_t const eltWidth = ZL_Input_eltWidth(in);
    uint8_t header[1 + ZL_VARINT_LENGTH_64];
    header[0]         = isTransposed ? 1 : 0;
    size_t varintSize = ZL_varintEncode((uint64_t)eltWidth, header + 1);
    ZL_Encoder_sendCodecHeader(eictx, header, 1 + varintSize);
}

// ZL_TypedEncoderFn
ZL_Report
EI_huffman_typed(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);

    //> Determine the nbElts/eltWidth for entropy coding.
    //> If isTransposed: nbElts = in-nbElts * in-eltWidth, eltWidth = 1.
    //> Else: nbElts = in-nbElts, eltWidth = in-eltWidth.
    const void* const src = ZL_Input_ptr(in);
    size_t const eltWidth = ZL_Input_eltWidth(in);
    size_t const nbElts   = ZL_Input_numElts(in);

    ZL_RET_R_IF_GT(
            node_invalid_input,
            eltWidth,
            2,
            "eltWidth > 2 is no longer supported for encoding.");

    ZL_ASSERT(
            ZL_Input_type(in) == ZL_Type_serial
            || ZL_Input_type(in) == ZL_Type_struct);
    ZL_RET_R_IF_GT(GENERIC, eltWidth, 2);

    //> Tell the entropy compressor to use Huffman, or a raw-bits mode,
    //> and allow block splitting.
    ZS_Entropy_TypeMask_e const allowedTypes = ZS_Entropy_TypeMask_huf
            | ZS_Entropy_TypeMask_raw | ZS_Entropy_TypeMask_constant
            | ZS_Entropy_TypeMask_bit | ZS_Entropy_TypeMask_multi;
    ZS_Entropy_EncodeParameters params =
            ZS_Entropy_EncodeParameters_fromAllowedTypes(allowedTypes);

    //> Allocate our output buffer with space for our header + entropy.
    size_t const dstCapacity = ZS_Entropy_encodedSizeBound(nbElts, eltWidth);
    ZL_Output* const out     = ZL_Encoder_createTypedStream(
            eictx, 0, dstCapacity, /* eltWidth */ 1);
    ZL_RET_R_IF_NULL(allocation, out);
    ZL_WriteCursor wc = ZL_WC_wrap(ZL_Output_ptr(out), dstCapacity);

    //> Write our header & encode
    EI_huffman_header(eictx, in);
    if (nbElts > 0) {
        ZL_RET_R_IF(
                GENERIC,
                ZL_isError(ZS_Entropy_encode(
                        &wc, src, nbElts, eltWidth, &params)));
    }

    //> Tell how large the output stream is.
    ZL_RET_R_IF_ERR(ZL_Output_commit(out, ZL_WC_size(&wc)));

    //> Return the number of output streams.
    return ZL_returnValue(1);
}

/**
 * Splits the input into chunks to entropy compress independently.
 *
 * TODO: This currently only splits into fixed size chunks. We should
 * do intelligent block splitting at higher compression levels.
 */
static ZL_RESULT_OF(ZL_EdgeList)
        chunkInputStream(ZL_Graph* gctx, ZL_Edge** sctx)
{
    ZL_Input const* input = ZL_Edge_getData(*sctx);
    size_t const nbElts   = ZL_Input_numElts(input);
    ZL_ASSERT_NE(ZL_Input_type(input) & (ZL_Type_serial | ZL_Type_struct), 0);

    // TODO: These are taken directly from the entropy compression library to
    // match behavior. We should look into tuning these.
    size_t const kChunkSize      = 1 << 15;
    size_t const kMinSizeToChunk = 100000;
    if (nbElts < kMinSizeToChunk) {
        ZL_EdgeList out = { .edges = sctx, .nbEdges = 1 };
        return ZL_RESULT_WRAP_VALUE(ZL_EdgeList, out);
    }

    size_t const nbChunks = (nbElts + kChunkSize - 1) / kChunkSize;
    size_t* chunkSizes =
            ZL_Graph_getScratchSpace(gctx, sizeof(size_t) * nbChunks);
    ZL_RET_T_IF_NULL(ZL_EdgeList, allocation, chunkSizes);

    ZL_ASSERT_GE(nbChunks, 1);
    for (size_t i = 0; i < nbChunks - 1; ++i) {
        chunkSizes[i] = kChunkSize;
    }
    chunkSizes[nbChunks - 1] = nbElts % kChunkSize;

    return ZL_Edge_runSplitNode(sctx[0], chunkSizes, nbChunks);
}

typedef enum {
    EBM_huf,
    EBM_fse,
    EBM_any,
} EntropyBackendMode;

static EntropyBackendMode resolveMode(
        DataStatsU8* stats,
        EntropyBackendMode mode)
{
    // TODO: Better selection between Huffman & FSE
    // Take decompression speed into account
    if (mode == EBM_any) {
        size_t const nbElts = DataStatsU8_totalElements(stats);
        size_t const fseSize =
                (size_t)(DataStatsU8_getEntropy(stats) * (double)nbElts + 7)
                / 8;
        size_t const hufSize =
                DataStatsU8_estimateHuffmanSizeFast(stats, /* delta */ false);
        size_t const minGain = nbElts / 32;
        if (fseSize + minGain < hufSize) {
            return EBM_fse;
        } else {
            return EBM_huf;
        }
    }
    return mode;
}

static ZL_RESULT_OF(ZL_EdgeList) runNode_wHistogram(
        ZL_Edge* sctx,
        ZL_NodeID node,
        ZL_Histogram const* histogram)
{
    ZL_RefParam const param = {
        .paramId  = ENTROPY_HISTORAM_PID,
        .paramRef = histogram,
    };
    ZL_LocalParams params = {
        .refParams = {
                .refParams = &param,
                .nbRefParams = 1,
        },
    };
    return ZL_Edge_runNode_withParams(sctx, node, &params);
}

static ZL_Histogram* getHistogram8(ZL_Graph* gctx, DataStatsU8* stats)
{
    ZL_Histogram* histogram = (ZL_Histogram*)ZL_Graph_getScratchSpace(
            gctx, sizeof(ZL_Histogram8));
    memcpy(histogram->count,
           DataStatsU8_getHistogram(stats),
           256 * sizeof(uint32_t));
    histogram->total        = (unsigned)DataStatsU8_totalElements(stats);
    histogram->maxSymbol    = DataStatsU8_getMaxElt(stats);
    histogram->elementSize  = 1;
    histogram->largestCount = 0;
    for (size_t i = 0; i < histogram->maxSymbol + 1; ++i) {
        histogram->largestCount =
                ZL_MAX(histogram->count[i], histogram->largestCount);
    }
    return histogram;
}

static ZL_Report runBitpack(ZL_Edge* input)
{
    ZL_TRY_LET_T(
            ZL_EdgeList,
            streams,
            ZL_Edge_runNode(input, ZL_NODE_INTERPRET_TOKEN_AS_LE));
    ZL_ASSERT_EQ(streams.nbEdges, 1);
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(streams.edges[0], ZL_GRAPH_BITPACK));
    return ZL_returnSuccess();
}

/**
 * Entropy compresses a single chunk by selecting the most efficient backend
 * allowed by the mode.
 */
static ZL_Report
entropyCompressChunk(ZL_Graph* gctx, ZL_Edge* chunk, EntropyBackendMode mode)
{
    ZL_Input const* input = ZL_Edge_getData(chunk);
    size_t const nbElts   = ZL_Input_numElts(input);
    size_t const eltWidth = ZL_Input_eltWidth(input);

    if (nbElts <= 1) {
        return ZL_Edge_setDestination(chunk, ZL_GRAPH_STORE);
    }

    if (ZL_Input_type(input) != ZL_Type_serial) {
        ZL_ASSERT_EQ(eltWidth, 2, "Already converted to serial");
        ZL_ASSERT_EQ(ZL_Input_type(input), ZL_Type_struct);
        ZL_Histogram* histogram = (ZL_Histogram*)ZL_Graph_getScratchSpace(
                gctx, sizeof(ZL_Histogram16));
        ZL_RET_R_IF_NULL(allocation, histogram);
        ZL_Histogram_init(histogram, 65535);
        ZL_Histogram_build(histogram, ZL_Input_ptr(input), nbElts, eltWidth);

        if (histogram->total == histogram->count[histogram->maxSymbol]) {
            ZL_ASSERT(ZL_Graph_isConstantSupported(gctx));
            return ZL_Edge_setDestination(chunk, ZL_GRAPH_CONSTANT);
        }

        // Get the huffman size estimate
        double entropy = ZL_calculateEntropy(
                histogram->count, histogram->maxSymbol, histogram->total);
        entropy                  = ZL_MAX(1, entropy);
        size_t const entropySize = (size_t)(entropy * (double)nbElts + 7) / 8;
        size_t const headerSizeEstimate = ZL_MAX(100, histogram->maxSymbol / 4);
        size_t const huffSize           = entropySize + headerSizeEstimate;
        size_t const storeSize          = 2 * nbElts;

        // Check if we should use bitpack
        ZL_ASSERT_NE(histogram->maxSymbol, 0);
        ZL_ASSERT(ZL_isLittleEndian(), "Only supports LE currently");
        size_t const nbBits = (size_t)ZL_nextPow2(histogram->maxSymbol + 1);
        size_t const bitpackSize = ((nbElts * nbBits + 7) / 8) + /* header */ 2;

        if (bitpackSize <= huffSize && bitpackSize < storeSize) {
            return runBitpack(chunk);
        }

        // Check if we can simply store the data
        if (entropy > 15 || huffSize >= storeSize) {
            return ZL_Edge_setDestination(chunk, ZL_GRAPH_STORE);
        }

        // Check if we can use tokenization
        if (histogram->cardinality < 256) {
            ZL_TRY_LET_T(
                    ZL_EdgeList,
                    streams,
                    ZL_Edge_runNode(chunk, ZL_NODE_TOKENIZE));
            ZL_ASSERT_EQ(streams.nbEdges, 2);
            // Bitpack the values stream if possible
            ZL_RET_R_IF_ERR(
                    nbBits < 16 ? runBitpack(streams.edges[0])
                                : ZL_Edge_setDestination(
                                          streams.edges[0], ZL_GRAPH_STORE));
            // Huffman compress the tokenized stream
            ZL_RET_R_IF_ERR(
                    ZL_Edge_setDestination(streams.edges[1], ZL_GRAPH_HUFFMAN));
            return ZL_returnSuccess();
        }

        // TODO: Allow tokenization
        ZL_TRY_LET_T(
                ZL_EdgeList,
                streams,
                runNode_wHistogram(
                        chunk,
                        (ZL_NodeID){
                                ZL_PrivateStandardNodeID_huffman_struct_v2 },
                        histogram));
        ZL_ASSERT_EQ(streams.nbEdges, 2);
        ZL_RET_R_IF_ERR(ZL_Edge_setDestination(streams.edges[0], ZL_GRAPH_FSE));
        ZL_RET_R_IF_ERR(
                ZL_Edge_setDestination(streams.edges[1], ZL_GRAPH_STORE));
        return ZL_returnSuccess();
    }

    DataStatsU8 stats;
    DataStatsU8_init(&stats, ZL_Input_ptr(input), nbElts);

    if (DataStatsU8_getCardinality(&stats) == 1) {
        ZL_ASSERT(ZL_Graph_isConstantSupported(gctx));
        return ZL_Edge_setDestination(chunk, ZL_GRAPH_CONSTANT);
    }

    // TODO: At higher compression levels use a better estimate
    size_t const entropySize = mode == EBM_huf
            ? DataStatsU8_estimateHuffmanSizeFast(&stats, /* delta */ false)
            : (size_t)(DataStatsU8_getEntropy(&stats) * (double)nbElts + 7) / 8;

    size_t const headerSizeEstimate =
            ZL_MAX(10, DataStatsU8_getCardinality(&stats) / 4);

    size_t const baselineSize =
            ZL_MIN(entropySize + headerSizeEstimate, nbElts);

    size_t const flatpackedSize = DataStatsU8_getFlatpackedSize(&stats);
    size_t const bitpackedSize  = DataStatsU8_getBitpackedSize(&stats);

    if (flatpackedSize < bitpackedSize) {
        if (flatpackedSize < baselineSize) {
            return ZL_Edge_setDestination(chunk, ZL_GRAPH_FLATPACK);
        }
    } else {
        if (bitpackedSize < baselineSize) {
            return ZL_Edge_setDestination(chunk, ZL_GRAPH_BITPACK);
        }
    }

    if (nbElts <= baselineSize) {
        return ZL_Edge_setDestination(chunk, ZL_GRAPH_STORE);
    }

    // Select between FSE & Huffman
    mode = resolveMode(&stats, mode);

    ZL_Histogram* histogram = getHistogram8(gctx, &stats);
    if (mode == EBM_huf) {
        ZL_TRY_LET_T(
                ZL_EdgeList,
                streams,
                runNode_wHistogram(
                        chunk,
                        (ZL_NodeID){ ZL_PrivateStandardNodeID_huffman_v2 },
                        histogram));
        ZL_ASSERT_EQ(streams.nbEdges, 2);
        ZL_RET_R_IF_ERR(ZL_Edge_setDestination(streams.edges[0], ZL_GRAPH_FSE));
        ZL_RET_R_IF_ERR(
                ZL_Edge_setDestination(streams.edges[1], ZL_GRAPH_STORE));
        return ZL_returnSuccess();
    } else {
        ZL_TRY_LET_T(
                ZL_EdgeList,
                streams,
                runNode_wHistogram(
                        chunk,
                        (ZL_NodeID){ ZL_PrivateStandardNodeID_fse_v2 },
                        histogram));
        ZL_ASSERT_EQ(streams.nbEdges, 2);
        ZL_RET_R_IF_ERR(ZL_Edge_setDestination(
                streams.edges[0],
                (ZL_GraphID){ ZL_PrivateStandardGraphID_fse_ncount }));
        ZL_RET_R_IF_ERR(
                ZL_Edge_setDestination(streams.edges[1], ZL_GRAPH_STORE));
        return ZL_returnSuccess();
    }
}

static ZL_Report
entropyDynamicGraph(ZL_Graph* gctx, ZL_Edge* sctx, EntropyBackendMode mode)
{
    ZL_TRY_LET_T(ZL_EdgeList, chunks, chunkInputStream(gctx, &sctx));
    for (size_t i = 0; i < chunks.nbEdges; ++i) {
        ZL_RET_R_IF_ERR(entropyCompressChunk(gctx, chunks.edges[i], mode));
    }
    return ZL_returnSuccess();
}

static ZL_Report doEntropyConversion(ZL_Graph* gctx, ZL_Edge** sctx)
{
    (void)gctx;

    ZL_Input const* const input = ZL_Edge_getData(*sctx);
    ZL_Type const type          = ZL_Input_type(input);
    size_t const eltWidth       = ZL_Input_eltWidth(input);
    ZL_ASSERT_NE(type & (ZL_Type_serial | ZL_Type_struct | ZL_Type_numeric), 0);
    if (eltWidth == 1) {
        if (type != ZL_Type_serial) {
            // Convert eltWidth=1 to serial at the top level for efficiency &
            // simplicity
            ZL_NodeID const conversion = type == ZL_Type_numeric
                    ? ZL_NODE_CONVERT_NUM_TO_SERIAL
                    : ZL_NODE_CONVERT_TOKEN_TO_SERIAL;
            ZL_TRY_LET_T(
                    ZL_EdgeList, serial, ZL_Edge_runNode(*sctx, conversion));
            *sctx = serial.edges[0];
        }
    } else {
        ZL_ASSERT_GT(eltWidth, 1);
        ZL_RET_R_IF_NE(node_invalid_input, eltWidth, 2);

        if (type == ZL_Type_numeric) {
            // Accept numeric inputs so we don't get a conversion from
            // numeric -> struct -> serial for eltWidth 1 data. Then convert
            // to struct for eltWidth 2.
            ZL_TRY_LET_T(
                    ZL_EdgeList,
                    structs,
                    ZL_Edge_runNode(*sctx, ZL_NODE_CONVERT_NUM_TO_TOKEN));
            ZL_ASSERT_EQ(structs.nbEdges, 1);
            *sctx = structs.edges[0];
        }
    }

#ifndef NDEBUG
    // Check that we've correctly converted the stream
    ZL_Input const* const newInput = ZL_Edge_getData(*sctx);
    ZL_Type const newType          = ZL_Input_type(newInput);
    size_t const newEltWidth       = ZL_Input_eltWidth(newInput);
    ZL_ASSERT(
            (newType == ZL_Type_serial && newEltWidth == 1)
            || (newType == ZL_Type_struct && newEltWidth == 2));
#endif

    return ZL_returnSuccess();
}

ZL_Report EI_fseDynamicGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns)
{
    ZL_RET_R_IF(graph_invalidNumInputs, nbIns != 1);
    ZL_Edge* input = inputs[0];
    ZL_RET_R_IF_ERR(doEntropyConversion(gctx, &input));
    if (ZL_Graph_getCParam(gctx, ZL_CParam_formatVersion) < 15) {
        ZL_TRY_LET_T(
                ZL_EdgeList,
                streams,
                ZL_Edge_runNode(
                        input,
                        (ZL_NodeID){
                                ZL_PrivateStandardNodeID_fse_deprecated }));
        ZL_ASSERT_EQ(streams.nbEdges, 1);
        return ZL_Edge_setDestination(streams.edges[0], ZL_GRAPH_STORE);
    }
    return entropyDynamicGraph(gctx, input, EBM_fse);
}

ZL_Report
EI_huffmanDynamicGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns)
{
    ZL_RET_R_IF(graph_invalidNumInputs, nbIns != 1);
    ZL_Edge* input = inputs[0];
    ZL_RET_R_IF_ERR(doEntropyConversion(gctx, &input));
    if (ZL_Graph_getCParam(gctx, ZL_CParam_formatVersion) < 15) {
        ZL_NodeID const node =
                ZL_Input_type(ZL_Edge_getData(input)) == ZL_Type_serial
                ? (ZL_NodeID){ ZL_PrivateStandardNodeID_huffman_deprecated }
                : (ZL_NodeID){
                      ZL_PrivateStandardNodeID_huffman_fixed_deprecated
                  };
        ZL_TRY_LET_T(ZL_EdgeList, streams, ZL_Edge_runNode(input, node));
        ZL_ASSERT_EQ(streams.nbEdges, 1);
        return ZL_Edge_setDestination(streams.edges[0], ZL_GRAPH_STORE);
    }
    return entropyDynamicGraph(gctx, input, EBM_huf);
}

ZL_Report
EI_entropyDynamicGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns)
{
    ZL_RET_R_IF(graph_invalidNumInputs, nbIns != 1);
    ZL_Edge* input = inputs[0];
    ZL_RET_R_IF_ERR(doEntropyConversion(gctx, &input));
    if (ZL_Graph_getCParam(gctx, ZL_CParam_formatVersion) < 15) {
        ZL_Input const* stream = ZL_Edge_getData(input);
        if (ZL_Input_type(stream) != ZL_Type_serial) {
            return EI_huffmanDynamicGraph(gctx, inputs, nbIns);
        }
        ZL_GraphID const graph = EI_selector_entropy(gctx, input);
        return ZL_Edge_setDestination(input, graph);
    }
    return entropyDynamicGraph(gctx, input, EBM_any);
}
