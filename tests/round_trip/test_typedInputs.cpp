// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// standard C
#include <cstdio>  // printf
#include <cstring> // memcpy

// Zstrong
#include "openzl/common/debug.h" // ZL_REQUIRE
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_*, ZL_Compressor_registerStaticGraph_fromNode1o
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_errors.h"
#include "openzl/zl_version.h"

namespace {

static int g_formatVersion_forTests = ZL_MAX_FORMAT_VERSION;

#if 0 // for debug only
void printHexa(const void* p, size_t size)
{
    const unsigned char* const b = (const unsigned char*)p;
    for (size_t n = 0; n < size; n++) {
        printf(" %02X ", b[n]);
    }
    printf("\n");
}
#endif

/* ------   custom transforms   -------- */
#define CT_STRINGCOPY_ID 1

// Just copy String input to output
// Ensures that last decompression stage is not a reference
static ZL_Report stringCopy_ct(
        ZL_Encoder* eictx, // To create output stream
        const ZL_Input* in) noexcept
{
    assert(in != nullptr);
    size_t const nbStrings = ZL_Input_numElts(in);
    printf("copy %zu strings\n", nbStrings);
    assert(ZL_Input_type(in) == ZL_Type_string);
    size_t stringsTotalSize = ZL_Input_contentSize(in);
    ZL_Output* const out    = ZL_Encoder_createStringStream(
            eictx, 0, nbStrings, stringsTotalSize);
    ZL_RET_R_IF_NULL(allocation, out); // control allocation success

    memcpy(ZL_Output_ptr(out), ZL_Input_ptr(in), stringsTotalSize);
    memcpy(ZL_Output_stringLens(out), ZL_Input_stringLens(in), 4 * nbStrings);

    ZL_RET_R_IF_ERR(ZL_Output_commit(out, nbStrings));

    return ZL_returnValue(1); // nb Out Streams
}
// Use a #define, to employ as initializer in static const declarations.
#define STRINGCOPY_GDESC                                          \
    (ZL_TypedGraphDesc)                                           \
    {                                                             \
        .CTid = CT_STRINGCOPY_ID, .inStreamType = ZL_Type_string, \
        .outStreamTypes = (const ZL_Type[]){ ZL_Type_string },    \
        .nbOutStreams   = 1                                       \
    }
static ZL_TypedEncoderDesc const stringCopy_CDesc = {
    .gd          = STRINGCOPY_GDESC,
    .transform_f = stringCopy_ct,
};

/* ------   custom graphs   -------- */

// Currently, zstrong requires setting up a CGraph to start compression.
// The below (simple) graph is a work-around this limitation.
// This may be removed in the future, once default graphs are a thing.

static ZL_GraphID basicGenericGraph(ZL_Compressor* cgraph) noexcept
{
    (void)cgraph;
    return ZL_GRAPH_COMPRESS_GENERIC;
}

static ZL_GraphID stringCopyGraph(ZL_Compressor* cgraph) noexcept
{
    ZL_NodeID const node_copyString =
            ZL_Compressor_registerTypedEncoder(cgraph, &stringCopy_CDesc);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_copyString, ZL_GRAPH_COMPRESS_GENERIC);
}

/* ------   compress, specify Type & CGraph   -------- */

uint32_t* g_strLens = NULL;

static ZL_TypedRef* initInput(const void* src, size_t srcSize, ZL_Type type)
{
    switch (type) {
        case ZL_Type_serial:
            return ZL_TypedRef_createSerial(src, srcSize);
        case ZL_Type_struct:
            // 32-bit only
            assert(srcSize % 4 == 0);
            return ZL_TypedRef_createStruct(src, 4, srcSize / 4);
        case ZL_Type_numeric:
            // 32-bit only
            assert(srcSize % 4 == 0);
            return ZL_TypedRef_createNumeric(src, 4, srcSize / 4);
        case ZL_Type_string:
            // we will pretend that all string sizes are 4 bytes,
            // except the last one
            {
                free(g_strLens);
                size_t nbStrings = srcSize / 4;
                assert(nbStrings >= 1);
                g_strLens = (uint32_t*)calloc(nbStrings, sizeof(*g_strLens));
                assert(g_strLens);
                for (size_t n = 0; n < nbStrings; n++) {
                    g_strLens[n] = 4;
                }
                g_strLens[nbStrings - 1] += (uint32_t)(srcSize % 4);
                return ZL_TypedRef_createString(
                        src, srcSize, g_strLens, nbStrings);
            }

        default:
            return NULL;
    }
}

static size_t compress(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_Type type,
        ZL_GraphFn graphf)
{
    ZL_REQUIRE_GE(dstCapacity, ZL_compressBound(srcSize));

    ZL_CCtx* const cctx = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx);

    ZL_TypedRef* const tref = initInput(src, srcSize, type);
    ZL_REQUIRE_NN(tref);

    // CGraph setup
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    ZL_Report const gssr = ZL_Compressor_initUsingGraphFn(cgraph, graphf);
    EXPECT_EQ(ZL_isError(gssr), 0) << "selection of starting graphid failed\n";
    ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx, cgraph);
    EXPECT_EQ(ZL_isError(rcgr), 0) << "CGraph reference failed\n";
    // Parameter setup
    ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
            cctx, ZL_CParam_formatVersion, g_formatVersion_forTests));

    ZL_Report const r = ZL_CCtx_compressTypedRef(cctx, dst, dstCapacity, tref);
    EXPECT_EQ(ZL_isError(r), 0) << "compression failed \n";

    ZL_Compressor_free(cgraph);
    ZL_TypedRef_free(tref);
    ZL_CCtx_free(cctx);
    return ZL_validResult(r);
}

/* ------ define custom decoder transforms ------- */

// custom decoder transform description
static ZL_Report stringCopy_decode(
        ZL_Decoder* eictx,
        const ZL_Input* ins[]) noexcept
{
    assert(ins != nullptr);
    const ZL_Input* const in = ins[0];
    assert(in != nullptr);
    assert(ZL_Input_type(in) == ZL_Type_string);
    size_t const nbStrings = ZL_Input_numElts(in);

    printf("copy %zu strings\n", nbStrings);
    size_t stringsTotalSize = ZL_Input_contentSize(in);
    ZL_Output* const out =
            ZL_Decoder_create1OutStream(eictx, stringsTotalSize, 1);
    ZL_RET_R_IF_NULL(allocation, out); // control allocation success
    uint32_t* stringLengths = ZL_Output_reserveStringLens(out, nbStrings);
    ZL_RET_R_IF_NULL(allocation, stringLengths); // control allocation success

    memcpy(ZL_Output_ptr(out), ZL_Input_ptr(in), stringsTotalSize);
    memcpy(stringLengths, ZL_Input_stringLens(in), 4 * nbStrings);

    ZL_RET_R_IF_ERR(ZL_Output_commit(out, nbStrings));

    return ZL_returnValue(1); // nb Out Streams
}
static ZL_TypedDecoderDesc const stringCopy_DDesc = {
    .gd          = STRINGCOPY_GDESC,
    .transform_f = stringCopy_decode,
};

/* ------   decompress   -------- */

static size_t decompress(
        void* dst,
        size_t dstCapacity,
        ZL_Type type,
        unsigned fixedWidth,
        const void* compressed,
        size_t cSize)
{
    // Collect Frame info
    ZL_FrameInfo* const fi = ZL_FrameInfo_create(compressed, cSize);
    ZL_REQUIRE_NN(fi);

    size_t const nbOutputs = ZL_validResult(ZL_FrameInfo_getNumOutputs(fi));
    ZL_REQUIRE_EQ(nbOutputs, 1);

    ZL_Type const outputType =
            (ZL_Type)ZL_validResult(ZL_FrameInfo_getOutputType(fi, 0));
    ZL_REQUIRE_EQ((int)type, (int)outputType);

    size_t const dstSize =
            ZL_validResult(ZL_FrameInfo_getDecompressedSize(fi, 0));
    ZL_REQUIRE_GE(dstCapacity, dstSize);

    // shorter way to extract same information (without FrameInfo state)
    ZL_Type outputTypeDirect;
    ZL_Report otr = ZL_getOutputType(&outputTypeDirect, compressed, cSize);
    EXPECT_EQ(ZL_isError(otr), 0);
    EXPECT_EQ(outputType, outputTypeDirect);

    ZL_Report const dsdr = ZL_getDecompressedSize(compressed, cSize);
    EXPECT_EQ(ZL_isError(dsdr), 0);
    EXPECT_EQ((int)dstSize, (int)ZL_validResult(dsdr));

    // Create a static decompression state, to store the custom decoder(s)
    // The decompression state will be re-employed
    static ZL_DCtx* const dctx = ZL_DCtx_create();
    ZL_REQUIRE_NN(dctx);

    // register custom decoders
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(dctx, &stringCopy_DDesc));

    // Decompress (single buffer) - incompatible with String Type
    ZL_OutputInfo outInfo = {};
    if (outputType != ZL_Type_string) {
        ZL_Report const rsb = ZL_DCtx_decompressTyped(
                dctx, &outInfo, dst, dstCapacity, compressed, cSize);
        EXPECT_EQ(ZL_isError(rsb), 0) << "decompression failed \n";
        EXPECT_EQ(outInfo.type, type);
        EXPECT_EQ((int)outInfo.decompressedByteSize, (int)ZL_validResult(rsb));
        EXPECT_GT((int)outInfo.fixedWidth, 0);
        EXPECT_EQ(outInfo.fixedWidth, fixedWidth);
        ZL_DLOG(SEQ, "outInfo.numElts = %zu", outInfo.numElts);
        ZL_DLOG(SEQ, "outInfo.fixedWidth = %zu", outInfo.fixedWidth);
        EXPECT_EQ(
                (int)(outInfo.numElts * outInfo.fixedWidth),
                (int)outInfo.decompressedByteSize);
    }

    // Decompress (Typed buffer)
    ZL_Report result;
    {
        ZL_TypedBuffer* const tbuf = ZL_TypedBuffer_create();
        assert(tbuf);
        ZL_Report const rtb =
                ZL_DCtx_decompressTBuffer(dctx, tbuf, compressed, cSize);
        EXPECT_EQ(ZL_isError(rtb), 0) << "decompression failed \n";
        EXPECT_EQ(ZL_TypedBuffer_type(tbuf), outputType);
        EXPECT_EQ((int)ZL_validResult(rtb), (int)dstSize);
        EXPECT_EQ((int)ZL_TypedBuffer_byteSize(tbuf), (int)dstSize);
        memcpy(dst, ZL_TypedBuffer_rPtr(tbuf), dstSize);
        if (outputType == ZL_Type_string) {
            EXPECT_TRUE(ZL_TypedBuffer_rStringLens(tbuf));
        } else {
            EXPECT_EQ(
                    (int)outInfo.fixedWidth,
                    (int)ZL_TypedBuffer_eltWidth(tbuf));
            EXPECT_EQ((int)outInfo.numElts, (int)ZL_TypedBuffer_numElts(tbuf));
        }
        ZL_TypedBuffer_free(tbuf); // note: TypedBuffer are not re-usable
        result = rtb;
    }

    // Decompress (Pre-allocated Typed buffer)
    // @note: pre-allocation not possible for type string and version <
    // ZL_CHUNK_VERSION_MIN
    if ((g_formatVersion_forTests >= ZL_CHUNK_VERSION_MIN)
        || (outputType == ZL_Type_string)) {
        size_t maxNumStrings = dstCapacity / 4;
        uint32_t* const lenBuffer =
                (uint32_t*)calloc(maxNumStrings, sizeof(*lenBuffer));
        ZL_TypedBuffer* tb = NULL;
        switch (outputType) {
            case ZL_Type_serial:
                tb = ZL_TypedBuffer_createWrapSerial(dst, dstCapacity);
                break;
            case ZL_Type_struct:
                tb = ZL_TypedBuffer_createWrapStruct(
                        dst, fixedWidth, dstCapacity / fixedWidth);
                break;
            case ZL_Type_numeric:
                tb = ZL_TypedBuffer_createWrapNumeric(
                        dst, fixedWidth, dstCapacity / fixedWidth);
                break;
            case ZL_Type_string:
                tb = ZL_TypedBuffer_createWrapString(
                        dst, dstCapacity, lenBuffer, maxNumStrings);
                break;
            default:
                assert(0); // invalid type, should not happen
        }
        assert(tb != NULL);
        ZL_Report const rtb =
                ZL_DCtx_decompressTBuffer(dctx, tb, compressed, cSize);
        EXPECT_EQ(ZL_isError(rtb), 0) << "decompression failed \n";
        EXPECT_EQ(ZL_TypedBuffer_type(tb), outputType);
        EXPECT_EQ((int)ZL_validResult(rtb), (int)dstSize);
        EXPECT_EQ((int)ZL_TypedBuffer_byteSize(tb), (int)dstSize);
        if (outputType == ZL_Type_string) {
            EXPECT_TRUE(ZL_TypedBuffer_rStringLens(tb));
            size_t const declaredNumStrings =
                    ZL_validResult(ZL_FrameInfo_getNumElts(fi, 0));
            EXPECT_EQ(ZL_TypedBuffer_numElts(tb), declaredNumStrings);
        } else {
            EXPECT_EQ(
                    (int)outInfo.fixedWidth, (int)ZL_TypedBuffer_eltWidth(tb));
            EXPECT_EQ((int)outInfo.numElts, (int)ZL_TypedBuffer_numElts(tb));
        }
        ZL_TypedBuffer_free(tb); // note: TypedBuffer are not re-usable
        free(lenBuffer);
        result = rtb;
    }

    ZL_FrameInfo_free(fi);
    return ZL_validResult(result);
}

/* ------   round trip test   ------ */

static int roundTripTest(
        ZL_GraphFn graphf,
        const void* input,
        size_t inputSize,
        ZL_Type inputType,
        const char* name)
{
    printf("\n=========================== \n");
    printf(" %s \n", name);
    printf("--------------------------- \n");
    // Generate test input
    size_t const compressedBound = ZL_compressBound(inputSize);
    void* const compressed       = malloc(compressedBound);
    ZL_REQUIRE_NN(compressed);

    size_t const compressedSize = compress(
            compressed, compressedBound, input, inputSize, inputType, graphf);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           inputSize,
           compressedSize);

    /* =======   Decompression   ======= */

    /* check frame header information (before decompression) */
    ZL_FrameInfo* const fi = ZL_FrameInfo_create(compressed, compressedSize);
    assert(fi != NULL);
    EXPECT_EQ(ZL_validResult(ZL_FrameInfo_getNumOutputs(fi)), 1);
    EXPECT_EQ(ZL_validResult(ZL_FrameInfo_getOutputType(fi, 0)), inputType);
    EXPECT_EQ(
            ZL_validResult(ZL_FrameInfo_getDecompressedSize(fi, 0)), inputSize);
    if (g_formatVersion_forTests >= ZL_CHUNK_VERSION_MIN) {
        // ZL_FrameInfo_getNumElts() is only valid for frames with version >=
        // ZL_CHUNK_VERSION_MIN
        if (inputType == ZL_Type_serial) {
            EXPECT_EQ(
                    ZL_validResult(ZL_FrameInfo_getNumElts(fi, 0)), inputSize);
        }
        if (inputType == ZL_Type_string) {
            EXPECT_EQ(
                    ZL_validResult(ZL_FrameInfo_getNumElts(fi, 0)),
                    inputSize / 4);
        }
        // ZL_FrameInfo_getNumElts() doesn't work on struct or numeric Outputs
        // yet
    }
    ZL_FrameInfo_free(fi);

    void* const decompressed = malloc(inputSize);
    ZL_REQUIRE_NN(decompressed);

    unsigned const width          = (inputType == ZL_Type_serial) ? 1 : 4;
    size_t const decompressedSize = decompress(
            decompressed,
            inputSize,
            inputType,
            width,
            compressed,
            compressedSize);
    printf("decompressed %zu input bytes into %zu original bytes \n",
           compressedSize,
           decompressedSize);

    // round-trip check
    EXPECT_EQ((int)decompressedSize, (int)inputSize)
            << "Error : decompressed size != original size \n";
    if (inputSize) {
        printf("checking that round-trip regenerates the same content \n");
        EXPECT_EQ(memcmp(input, decompressed, inputSize), 0)
                << "Error : decompressed content differs from original (corruption issue) !!!  \n";
    }

    printf("round-trip success \n");
    free(decompressed);
    free(compressed);
    return 0;
}

static int roundTripIntegers(ZL_GraphFn graphf, ZL_Type type, const char* name)
{
    // Generate test input
#define NB_INTS 84
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    return roundTripTest(graphf, input, sizeof(input), type, name);
}

/* ------   exposed tests   ------ */

TEST(TypedInput, serial)
{
    (void)roundTripIntegers(
            basicGenericGraph,
            ZL_Type_serial,
            "Typed Compression, using Serial TypedRef");
}

TEST(TypedInput, struct)
{
    (void)roundTripIntegers(
            basicGenericGraph,
            ZL_Type_struct,
            "Typed Compression, using Struct TypedRef");
}

TEST(TypedInput, numeric)
{
    (void)roundTripIntegers(
            basicGenericGraph,
            ZL_Type_numeric,
            "Typed Compression, using Numeric TypedRef");
}

TEST(TypedInput, string)
{
    (void)roundTripIntegers(
            basicGenericGraph,
            ZL_Type_string,
            "Typed Compression, using String TypedRef");
}

TEST(TypedInput, stringCopy)
{
    (void)roundTripIntegers(
            stringCopyGraph,
            ZL_Type_string,
            "String Compression, ensure no reference as last decoding operation");
}

/* ============================= */
/* ------   error tests   ------ */
/* ============================= */

/* ------   unaligned numeric input   ------ */

/* this test is expected to fail predictably */
static int cFailInitTest(
        ZL_GraphFn graphf,
        const char* testName,
        ZL_Type type,
        const void* src,
        size_t srcSize)
{
    printf("\n=========================== \n");
    printf(" %s \n", testName);
    printf("--------------------------- \n");

    ZL_CCtx* const cctx = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx);

    // fail: incorrect size, not a multiple
    ZL_TypedRef* const tref = initInput(src, srcSize, type);
    EXPECT_EQ(tref, nullptr);

    (void)graphf;

    ZL_CCtx_free(cctx);
    printf("Compression initialization failure observed as expected \n");
    return 0;
}

static int cUnaligned(ZL_GraphFn graphf, ZL_Type type, const char* name)
{
    // Generate test input
#define NB_INTS 84
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    return cFailInitTest(
            graphf, name, type, ((char*)input) + 1, sizeof(input) - 4);
}

TEST(TypedInput, numeric_cUnaligned)
{
    (void)cUnaligned(
            basicGenericGraph,
            ZL_Type_numeric,
            "Typed Compression of Numeric: Buffer is not aligned correctly");
}

/* ------   unaligned buffer for numeric output   ------ */

static void decompress_fail(
        void* dst,
        size_t dstCapacity,
        ZL_Type type,
        unsigned fixedWidth,
        const void* compressed,
        size_t cSize)
{
    // more direct way to extract same information (no need for FrameInfo state)
    ZL_Type outputTypeDirect;
    ZL_Report otr = ZL_getOutputType(&outputTypeDirect, compressed, cSize);
    EXPECT_EQ(ZL_isError(otr), 0);
    EXPECT_EQ(outputTypeDirect, type);

    ZL_Report const dsdr = ZL_getDecompressedSize(compressed, cSize);
    EXPECT_EQ(ZL_isError(dsdr), 0);
    EXPECT_GE((int)dstCapacity, (int)ZL_validResult(dsdr));

    // Create a static decompression state, to store the custom decoder(s)
    // The decompression state will be re-employed
    static ZL_DCtx* const dctx = ZL_DCtx_create();
    ZL_REQUIRE_NN(dctx);

    // Decompress
    ZL_OutputInfo outInfo;
    ZL_Report const r = ZL_DCtx_decompressTyped(
            dctx, &outInfo, dst, dstCapacity, compressed, cSize);
    EXPECT_EQ(ZL_isError(r), 1) << "decompression should have failed \n";
    (void)fixedWidth;
}

static int RTFail(
        ZL_GraphFn graphf,
        const char* testTitle,
        void* compressed,
        size_t cCapacity,
        void* decompressed,
        size_t dCapacity,
        const void* input,
        size_t inputSize,
        ZL_Type inputType)
{
    printf("\n=========================== \n");
    printf(" %s \n", testTitle);
    printf("--------------------------- \n");

    size_t const compressedSize = compress(
            compressed, cCapacity, input, inputSize, inputType, graphf);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           inputSize,
           compressedSize);

    assert(dCapacity >= inputSize);
    unsigned const width = (inputType == ZL_Type_serial) ? 1 : 4;
    decompress_fail(
            decompressed,
            dCapacity,
            inputType,
            width,
            compressed,
            compressedSize);
    printf("decompression failed as expected \n");

    return 0;
}

static void dUnaligned(ZL_GraphFn graphf, const char* testTitle)
{
    // Generate test input
#define NB_INTS 84
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;
    size_t const inputSize = sizeof(input);

    // Generate test input
    size_t const compressedBound = ZL_compressBound(inputSize);
    void* const compressed       = malloc(compressedBound);
    ZL_REQUIRE_NN(compressed);

    size_t const dCapacity   = inputSize + 1;
    void* const decompressed = malloc(dCapacity + 1);
    ZL_REQUIRE_NN(decompressed);
    void* const decStart = (char*)decompressed + 1;

    (void)RTFail(
            graphf,
            testTitle,
            compressed,
            compressedBound,
            decStart,
            dCapacity,
            input,
            sizeof(input),
            ZL_Type_numeric);

    free(decompressed);
    free(compressed);
}

TEST(TypedInput, numeric_dUnaligned)
{
    (void)dUnaligned(
            basicGenericGraph,
            "Typed Decompression of Numeric: Buffer is not aligned correctly");
}

} // namespace
