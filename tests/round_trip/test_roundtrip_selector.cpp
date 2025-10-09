// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// standard C
#include <stdint.h> // uint_X,
#include <stdio.h>  // printf
#include <string.h> // memcpy

// Zstrong
#include "openzl/common/debug.h" // ZL_REQUIRE
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_*
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_selector.h"   // ZL_SelectorDesc

namespace {

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

/* ------   create custom transforms   -------- */

// N/A

/* ------   create custom parser   -------- */

// N/A

/* ------   create custom selectors   -------- */

static ZL_GraphID simple_selector_serial(
        const void* src,
        size_t srcSize,
        const ZL_GraphID* csg,
        size_t nbCsg) // unused, must be == 0
        noexcept
{
    assert(src != nullptr);
    (void)src;
    assert(nbCsg == 0);
    (void)nbCsg;
    (void)srcSize;
    (void)csg;
    return ZL_GRAPH_ZSTD;
}

const ZL_SerialSelectorDesc simple_selector_serial_desc = {
    .selector_f = simple_selector_serial,
};

static ZL_GraphID typed_selector_zstd(
        const ZL_Selector* selCtx,
        const ZL_Input* ins,
        const ZL_GraphID* csg,
        size_t nbCsg) // unused, must be == 0
        noexcept
{
    (void)selCtx;
    (void)ins;
    (void)csg;
    (void)nbCsg;
    return ZL_GRAPH_ZSTD;
}

/* this selector is declared supporting both fixed_size and serialized types.
 * If input is type numeric, it should be implicitly converted to fixed-size.
 * If input is fixed_size, it shall remain fixed_size. */
static ZL_GraphID selector_check_fixed(
        const ZL_Selector* selCtx,
        const ZL_Input* ins,
        const ZL_GraphID* csg,
        size_t nbCsg) // unused, must be == 0
        noexcept
{
    (void)selCtx;
    assert(ins != nullptr);
    ZL_Type const st = ZL_Input_type(ins);
    printf("input Stream type: %u == %u ZL_Type_struct \n", st, ZL_Type_struct);
    EXPECT_EQ(st, ZL_Type_struct);
    (void)csg;
    (void)nbCsg;
    return ZL_GRAPH_STORE;
}

const ZL_SelectorDesc selector_check_fixed_desc = {
    .selector_f   = selector_check_fixed,
    .inStreamType = (ZL_Type)(ZL_Type_serial | ZL_Type_struct),
};

/* this Selector will return an invalid Successor.
 * the Graph Engine must survive such a case,
 * either returning an error, or using backup mode if allowed */
static ZL_GraphID selector_invalidSuccessor(
        const ZL_Selector* selCtx,
        const ZL_Input* ins,
        const ZL_GraphID* csg,
        size_t nbCsg) // unused, must be == 0
        noexcept
{
    (void)selCtx;
    (void)ins;
    (void)csg;
    (void)nbCsg;
    printf("Selector is providing an invalid successor graph \n");
    return ZL_GRAPH_ILLEGAL;
}

/* ------   create custom graph   -------- */

static ZL_GraphID selectorGraph_withSimpleDesc(
        ZL_Compressor* cgraph,
        const ZL_SerialSelectorDesc* csd)
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    return ZL_Compressor_registerSerialSelectorGraph(cgraph, csd);
}

static ZL_GraphID selectorGraph_simpleSerial(ZL_Compressor* cgraph) noexcept
{
    return selectorGraph_withSimpleDesc(cgraph, &simple_selector_serial_desc);
}

static ZL_GraphID selectorGraph_withTypedDesc(
        ZL_Compressor* cgraph,
        const ZL_SelectorDesc* csd) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    return ZL_Compressor_registerSelectorGraph(cgraph, csd);
}

static ZL_GraphID tselGraph_serial(ZL_Compressor* cgraph) noexcept
{
    const ZL_SelectorDesc tsel = {
        .selector_f   = typed_selector_zstd,
        .inStreamType = ZL_Type_serial,
    };
    return selectorGraph_withTypedDesc(cgraph, &tsel);
}

static ZL_GraphID tselGraph_serial_numeric(ZL_Compressor* cgraph) noexcept
{
    const ZL_SelectorDesc tsel = {
        .selector_f   = typed_selector_zstd,
        .inStreamType = (ZL_Type)(ZL_Type_numeric | ZL_Type_serial),
    };
    return selectorGraph_withTypedDesc(cgraph, &tsel);
}

static ZL_GraphID tselGraph_numeric(ZL_Compressor* cgraph) noexcept
{
    const ZL_SelectorDesc tsel = {
        .selector_f   = typed_selector_zstd,
        .inStreamType = ZL_Type_numeric,
    };
    return selectorGraph_withTypedDesc(cgraph, &tsel);
}

static ZL_GraphID tselGraph_fixed_numeric(ZL_Compressor* cgraph) noexcept
{
    const ZL_SelectorDesc tsel = {
        .selector_f   = typed_selector_zstd,
        .inStreamType = (ZL_Type)(ZL_Type_numeric | ZL_Type_struct),
    };
    return selectorGraph_withTypedDesc(cgraph, &tsel);
}

static ZL_GraphID tselGraph_any(ZL_Compressor* cgraph) noexcept
{
    const ZL_SelectorDesc tsel = {
        .selector_f   = typed_selector_zstd,
        .inStreamType = ZL_Type_any,
    };
    return selectorGraph_withTypedDesc(cgraph, &tsel);
}

static ZL_GraphID tselGraph_invalidSuccessor(ZL_Compressor* cgraph) noexcept
{
    const ZL_SelectorDesc tsel = {
        .selector_f   = selector_invalidSuccessor,
        .inStreamType = ZL_Type_any,
    };
    return selectorGraph_withTypedDesc(cgraph, &tsel);
}

static ZL_GraphID numSelGraph(ZL_Compressor* cgraph) noexcept
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE32,
            selectorGraph_withTypedDesc(cgraph, &selector_check_fixed_desc));
}

static ZL_GraphID fixedSelGraph(ZL_Compressor* cgraph) noexcept
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN4,
            selectorGraph_withTypedDesc(cgraph, &selector_check_fixed_desc));
}

static ZL_GraphID compressGraph(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    return ZL_GRAPH_COMPRESS_GENERIC;
}

static ZL_GraphID compressGraph_fixedSize(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN4,
            ZL_GRAPH_COMPRESS_GENERIC);
}

static ZL_GraphID compressGraph_numeric(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_INTERPRET_AS_LE32, ZL_GRAPH_COMPRESS_GENERIC);
}

static ZL_SetStringLensInstructions parse_1Bigfield(
        ZL_SetStringLensState* state,
        const ZL_Input* in)
{
    assert(in != nullptr);
    size_t const totalSize     = ZL_Input_contentSize(in);
    uint32_t* const fieldSizes = (uint32_t*)ZL_SetStringLensState_malloc(
            state, 1 * sizeof(*fieldSizes));
    assert(fieldSizes != nullptr);
    fieldSizes[0]                         = (uint32_t)totalSize;
    ZL_SetStringLensInstructions const si = { fieldSizes, 1 };
    return si;
}

static ZL_GraphID compressGraph_vsf(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_Compressor_registerConvertSerialToStringNode(
                    cgraph, parse_1Bigfield, nullptr),
            ZL_GRAPH_COMPRESS_GENERIC);
}

/* ------   compress, using provided graph function   -------- */

static size_t compress(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_GraphFn graphf)
{
    ZL_REQUIRE_GE(dstCapacity, ZL_compressBound(srcSize));

    ZL_CCtx* const cctx = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx);
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    ZL_GraphID const sgid       = graphf(cgraph);
    ZL_Report const gssr = ZL_Compressor_selectStartingGraphID(cgraph, sgid);
    EXPECT_EQ(ZL_isError(gssr), 0) << "selection of starting graphid failed\n";
    ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx, cgraph);
    EXPECT_EQ(ZL_isError(rcgr), 0) << "CGraph reference failed\n";
    ZL_Report const r = ZL_CCtx_compress(cctx, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 0) << "compression failed \n";

    ZL_Compressor_free(cgraph);
    ZL_CCtx_free(cctx);
    return ZL_validResult(r);
}

/* ------ define custom decoder transforms ------- */

// N/A

/* ------   decompress   -------- */

static size_t
decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    // Check buffer size
    ZL_Report const dr = ZL_getDecompressedSize(src, srcSize);
    ZL_REQUIRE(!ZL_isError(dr));
    size_t const dstSize = ZL_validResult(dr);
    ZL_REQUIRE_GE(dstCapacity, dstSize);

    // Create a single decompression state, to store the custom decoder(s)
    // The decompression state will be re-employed
    static ZL_DCtx* const dctx = ZL_DCtx_create();
    ZL_REQUIRE_NN(dctx);

    // register custom decoders
    // N/A

    // Decompress, using custom decoder(s)
    ZL_Report const r =
            ZL_DCtx_decompress(dctx, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 0) << "decompression failed \n";

    return ZL_validResult(r);
}

/* ------   round trip test   ------ */

static int roundTripTest(
        ZL_GraphFn graphf,
        const void* input,
        size_t inputSize,
        const char* name)
{
    printf("\n=========================== \n");
    printf(" %s \n", name);
    printf("--------------------------- \n");
    // Generate test input
    size_t const compressedBound = ZL_compressBound(inputSize);
    void* const compressed       = malloc(compressedBound);
    ZL_REQUIRE_NN(compressed);

    size_t const compressedSize =
            compress(compressed, compressedBound, input, inputSize, graphf);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           inputSize,
           compressedSize);

    void* const decompressed = malloc(inputSize);
    ZL_REQUIRE_NN(decompressed);

    size_t const decompressedSize =
            decompress(decompressed, inputSize, compressed, compressedSize);
    printf("decompressed %zu input bytes into %zu original bytes \n",
           compressedSize,
           decompressedSize);

    // round-trip check
    EXPECT_EQ(decompressedSize, inputSize)
            << "Error : decompressed size != original size \n";
    if (inputSize) {
        EXPECT_EQ(memcmp(input, decompressed, inputSize), 0)
                << "Error : decompressed content differs from original (corruption issue) !!!  \n";
    }

    printf("round-trip success \n");
    free(decompressed);
    free(compressed);
    return 0;
}

static int roundTripIntegers(ZL_GraphFn graphf, const char* name)
{
    // Generate test input
#define NB_INTS 84
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    return roundTripTest(graphf, input, sizeof(input), name);
}

static int cFailTest(ZL_GraphFn graphf, const char* testName)
{
    printf("\n=========================== \n");
    printf(" %s \n", testName);
    printf("--------------------------- \n");
    // Generate test input => too short, will fail
    char input[40];
    for (int i = 0; i < 40; i++)
        input[i] = (char)i;

#define COMPRESSED_BOUND ZL_COMPRESSBOUND(sizeof(input))
    char compressed[COMPRESSED_BOUND] = { 0 };

    ZL_Report const r = ZL_compress_usingGraphFn(
            compressed, COMPRESSED_BOUND, input, sizeof(input), graphf);
    EXPECT_EQ(ZL_isError(r), 1) << "compression should have failed \n";

    printf("Compression failure observed as expected : %s \n",
           ZL_ErrorCode_toString(r._code));
    return 0;
}

/* ------   published tests   ------ */

TEST(selectorGraph, basic_simpleSerialSelector)
{
    (void)roundTripIntegers(
            selectorGraph_simpleSerial, "Basic selector for serial input");
}

TEST(selectorGraph, typedSelector_serial)
{
    (void)roundTripIntegers(
            tselGraph_serial, "Typed selector for serial input");
}

TEST(selectorGraph, typedSelector_serial_and_numeric)
{
    (void)roundTripIntegers(
            tselGraph_serial_numeric,
            "Typed selector supporting both serial and numeric input (valid)");
}

TEST(selectorGraph, typedSelector_wrongInput)
{
    cFailTest(
            tselGraph_numeric,
            "Typed selector only accepts numeric input, but input is serial"
            " => failure expected");
}

TEST(selectorGraph, typedSelector_fixed_numeric)
{
    cFailTest(
            tselGraph_fixed_numeric,
            "Typed selector accepts both numeric and fixed-size input, "
            "but input is serial => failure expected");
}

TEST(selectorGraph, typedSelector_any)
{
    (void)roundTripIntegers(
            tselGraph_any, "Typed selector allowing any input type");
}

TEST(selectorGraph, select_invalid_successor)
{
    cFailTest(
            tselGraph_invalidSuccessor,
            "Selector provides an invalid Successor "
            "=> failure expected");
}

TEST(selectorGraph, MTSelector_fromNumeric)
{
    (void)roundTripIntegers(
            numSelGraph,
            "Check implicit conversion : numeric -> fixed_size "
            "shall be preferred to numeric -> serialized ");
}

TEST(selectorGraph, MTSelector_fromFixed)
{
    (void)roundTripIntegers(
            fixedSelGraph,
            "fixed_size input must remain fixed_size "
            "for a Selector supporting both fixed_size and serialized");
}

TEST(genericGraph, compress_fromSerial)
{
    (void)roundTripIntegers(
            compressGraph, "Invoke generic compression on serial input");
}

TEST(genericGraph, compress_fromFixedSizeTokens)
{
    (void)roundTripIntegers(
            compressGraph_fixedSize,
            "Invoke generic compression on fixed size tokens");
}

TEST(genericGraph, compress_fromNumeric)
{
    (void)roundTripIntegers(
            compressGraph_numeric,
            "Invoke generic compression on an array of numeric values");
}

TEST(genericGraph, compress_fromVariableSizeTokens)
{
    (void)roundTripIntegers(
            compressGraph_vsf,
            "Invoke generic compression on variable size tokens");
}

} // namespace
