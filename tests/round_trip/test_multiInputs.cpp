// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// standard C
#include <cstdio>  // printf
#include <cstring> // memcpy

// C++
#include <optional>
#include <vector>

// Zstrong
#include "openzl/common/debug.h"  // ZL_REQUIRE
#include "openzl/common/limits.h" // ZL_ENCODER_INPUT_LIMIT
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_*, ZL_Compressor_registerStaticGraph_fromNode1o
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_graph_api.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_segmenter.h"
#include "openzl/zl_version.h"
#include "tests/utils.h"

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

static int g_formatVersion_forTests = ZL_MAX_FORMAT_VERSION;

/* ------   custom graphs   -------- */

// Currently, zstrong requires setting up a CGraph to start compression.
// The below (simple) graph is a work-around this limitation.
// This may be removed in the future, once default graphs are a thing.

static ZL_GraphID basicGenericGraph(ZL_Compressor* cgraph) noexcept
{
    (void)cgraph;
    return ZL_GRAPH_COMPRESS_GENERIC; // Supports multiple Inputs
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
            // we will pretend that all string sizes are 4 bytes, except the
            // last one
            {
                size_t nbStrings = srcSize / 4;
                assert(nbStrings >= 1);
                // Note: for this test, we are sharing the same stringLens array
                // across all Inputs
                if (g_strLens == NULL) {
                    g_strLens =
                            (uint32_t*)calloc(nbStrings, sizeof(*g_strLens));
                    assert(g_strLens);
                    for (size_t n = 0; n < nbStrings; n++) {
                        g_strLens[n] = 4;
                    }
                    g_strLens[nbStrings - 1] += (uint32_t)(srcSize % 4);
                }
                return ZL_TypedRef_createString(
                        src, srcSize, g_strLens, nbStrings);
            }

        default:
            assert(false); // this should never happen
            return NULL;
    }
}

/* Implementation note: test cases target an eltSize of 4 */
static ZL_TypedBuffer* initOutput(void* src, size_t srcSize, ZL_Type type)
{
    switch (type) {
        case ZL_Type_serial:
            return ZL_TypedBuffer_createWrapSerial(src, srcSize);
        case ZL_Type_struct:
            // 32-bit only
            assert(srcSize % 4 == 0);
            return ZL_TypedBuffer_createWrapStruct(src, 4, srcSize / 4);
        case ZL_Type_numeric:
            // 32-bit only
            assert(srcSize % 4 == 0);
            return ZL_TypedBuffer_createWrapNumeric(src, 4, srcSize / 4);
        case ZL_Type_string:
            return ZL_TypedBuffer_create();
        default:
            assert(false); // this should never happen
            return NULL;
    }
}

static ZL_Report compress(
        void* dst,
        size_t dstCapacity,
        const ZL_TypedRef* inputs[],
        size_t nbInputs,
        ZL_Compressor* cgraph)
{
    ZL_CCtx* const cctx = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx);

    // CGraph setup
    ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx, cgraph);
    EXPECT_EQ(ZL_isError(rcgr), 0) << "CGraph reference failed\n";
    // Parameter setup
    ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
            cctx, ZL_CParam_formatVersion, g_formatVersion_forTests));

    ZL_Report const r = ZL_CCtx_compressMultiTypedRef(
            cctx, dst, dstCapacity, inputs, nbInputs);

    ZL_CCtx_free(cctx);
    return r;
}

/* ------ define custom decoder transforms ------- */

/* ------   decompress   -------- */
static ZL_Report decompress(
        ZL_TypedBuffer* outputs[],
        size_t nbOuts,
        const void* compressed,
        size_t cSize)
{
    // Collect Frame info
    ZL_FrameInfo* const fi = ZL_FrameInfo_create(compressed, cSize);
    ZL_REQUIRE_NN(fi);

    size_t const nbOutputs = ZL_validResult(ZL_FrameInfo_getNumOutputs(fi));

    std::vector<ZL_Type> outputTypes;
    outputTypes.resize(nbOutputs);
    for (size_t n = 0; n < nbOutputs; n++) {
        outputTypes[n] =
                (ZL_Type)ZL_validResult(ZL_FrameInfo_getOutputType(fi, (int)n));
    }

    std::vector<size_t> outputSizes;
    outputSizes.resize(nbOutputs);
    for (size_t n = 0; n < nbOutputs; n++) {
        outputSizes[n] = (ZL_Type)ZL_validResult(
                ZL_FrameInfo_getDecompressedSize(fi, (int)n));
    }

    ZL_FrameInfo_free(fi);

    // Create a static decompression state, to store the custom decoder(s)
    // The decompression state will be re-employed
    static ZL_DCtx* const dctx = ZL_DCtx_create();
    ZL_REQUIRE_NN(dctx);

    // register custom decoders
    // none

    // Decompress (typed buffer)
    ZL_Report const rtb = ZL_DCtx_decompressMultiTBuffer(
            dctx, outputs, nbOuts, compressed, cSize);
    if (!ZL_isError(rtb)) {
        EXPECT_EQ((int)nbOuts, (int)nbOutputs);
        EXPECT_EQ((int)ZL_validResult(rtb), (int)nbOutputs);
        for (size_t n = 0; n < nbOutputs; n++) {
            EXPECT_EQ(
                    (int)ZL_TypedBuffer_byteSize(outputs[n]),
                    (int)outputSizes[n]);
            EXPECT_EQ(ZL_TypedBuffer_type(outputs[n]), outputTypes[n]);
            if (ZL_TypedBuffer_type(outputs[n]) == ZL_Type_string) {
                EXPECT_TRUE(ZL_TypedBuffer_rStringLens(outputs[n]));
            } else {
                int const fixedWidth =
                        (outputTypes[n] == ZL_Type_serial) ? 1 : 4;
                EXPECT_EQ((int)ZL_TypedBuffer_eltWidth(outputs[n]), fixedWidth);
                EXPECT_EQ(
                        (int)ZL_TypedBuffer_numElts(outputs[n]),
                        (int)outputSizes[n] / fixedWidth);
            }
        }
    }

    // clean and return
    return rtb;
}

/* ------   round trip test   ------ */

typedef struct {
    const void* start;
    size_t size;
    ZL_Type type;
} InputDesc;

static int roundTripSuccessTestBase(
        ZL_Compressor* cgraph,
        const InputDesc* inputs,
        size_t nbInputs,
        const char* testName,
        std::optional<size_t> allocation_offset = std::nullopt)
{
    printf("\n=========================== \n");
    printf(" %s (%zu inputs)\n", testName, nbInputs);
    printf("--------------------------- \n");

    // Create Inputs
    size_t totalSrcSize = 0;
    for (size_t n = 0; n < nbInputs; n++) {
        totalSrcSize += inputs[n].size;
    }
    size_t const compressedBound = ZL_compressBound(totalSrcSize);
    void* const compressed       = malloc(compressedBound);
    ZL_REQUIRE_NN(compressed);

    ZL_TypedRef** typedInputs =
            (ZL_TypedRef**)malloc(nbInputs * sizeof(typedInputs[0]));
    ZL_REQUIRE_NN(typedInputs);

    for (size_t n = 0; n < nbInputs; n++) {
        typedInputs[n] =
                initInput(inputs[n].start, inputs[n].size, inputs[n].type);
        ZL_REQUIRE_NN(typedInputs[n]);
    }

    // just for type casting
    const ZL_TypedRef** readOnly =
            (const ZL_TypedRef**)malloc(nbInputs * sizeof(typedInputs[0]));
    ZL_REQUIRE_NN(readOnly);
    memcpy(readOnly, typedInputs, nbInputs * sizeof(typedInputs[0]));

    ZL_Report const compressionReport =
            compress(compressed, compressedBound, readOnly, nbInputs, cgraph);
    EXPECT_ZS_VALID(compressionReport);
    size_t const compressedSize = ZL_validResult(compressionReport);

    printf("compressed %zu input bytes from %zu inputs into %zu compressed bytes \n",
           totalSrcSize,
           nbInputs,
           compressedSize);

    size_t const nbOutputs = nbInputs;
    ZL_TypedBuffer** outputs =
            (ZL_TypedBuffer**)malloc(nbOutputs * sizeof(ZL_TypedBuffer*));
    std::vector<std::vector<uint8_t>> bufs{ nbOutputs };
    for (size_t n = 0; n < nbOutputs; n++) {
        if (allocation_offset.has_value()) {
            bufs[n] = std::vector<uint8_t>(
                    inputs[n].size + allocation_offset.value());
            outputs[n] =
                    initOutput(bufs[n].data(), bufs[n].size(), inputs[n].type);
        } else {
            outputs[n] = ZL_TypedBuffer_create();
        }
        assert(outputs[n]);
    }
    ZL_Report const decompressionReport =
            decompress(outputs, nbOutputs, compressed, compressedSize);
    EXPECT_ZS_VALID(decompressionReport);
    size_t nbOuts = ZL_validResult(decompressionReport);
    printf("decompressed %zu compressed bytes into %zu outputs \n",
           compressedSize,
           nbOuts);
    EXPECT_EQ((int)nbOuts, (int)nbOutputs);

    // round-trip check
    for (size_t n = 0; n < nbOutputs; n++) {
        EXPECT_EQ((int)ZL_TypedBuffer_byteSize(outputs[n]), (int)inputs[n].size)
                << "Error : decompressed size != original size \n";

        EXPECT_EQ((int)ZL_TypedBuffer_type(outputs[n]), (int)inputs[n].type)
                << "Error : decompressed type != original type \n";

        if (inputs[n].size) {
            EXPECT_EQ(
                    memcmp(inputs[n].start,
                           ZL_TypedBuffer_rPtr(outputs[n]),
                           inputs[n].size),
                    0)
                    << "Error : decompressed content differs from original (corruption issue) !!!  \n";
        }

        if (allocation_offset.has_value()) {
            EXPECT_EQ(ZL_TypedBuffer_rPtr(outputs[n]), bufs[n].data());
        }
    }

    printf("round-trip success \n");

    // clean
    for (size_t n = 0; n < nbOutputs; n++) {
        ZL_TypedBuffer_free(outputs[n]);
    }
    free(outputs);
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_TypedRef_free(typedInputs[n]);
    }
    free(readOnly);
    free(typedInputs);
    free(compressed);
    return 0;
}

static int roundTripSuccessTest(
        ZL_Compressor* cgraph,
        const InputDesc* inputs,
        size_t nbInputs,
        const char* testName)
{
    return roundTripSuccessTestBase(
            cgraph, inputs, nbInputs, testName, std::nullopt);
}

static int roundTripAllocateOutputsTest(
        ZL_Compressor* cgraph,
        const InputDesc* inputs,
        size_t nbInputs,
        const char* testName)
{
    return roundTripSuccessTestBase(cgraph, inputs, nbInputs, testName, 0);
}

static int roundTripAllocateBiggerOutputsTest(
        ZL_Compressor* cgraph,
        const InputDesc* inputs,
        size_t nbInputs,
        const char* testName)
{
    return roundTripSuccessTestBase(cgraph, inputs, nbInputs, testName, 32);
}

typedef int (*RunScenario)(
        ZL_Compressor* cgraph,
        const InputDesc* inputs,
        size_t nbInputs,
        const char* testName);

static int genInt32Data(
        ZL_Compressor* cgraph,
        const ZL_Type inputTypes[],
        size_t nbInputs,
        const char* testName,
        RunScenario run_f)
{
    // Generate test input
#define NB_INTS 134
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    std::vector<InputDesc> inDesc;
    inDesc.resize(nbInputs);
    for (size_t n = 0; n < nbInputs; n++) {
        inDesc[n] = (InputDesc){ input, sizeof(input), inputTypes[n] };
    }

    return run_f(cgraph, inDesc.data(), nbInputs, testName);
}

static int genInt32Data(
        ZL_FunctionGraphFn graphf,
        const ZL_Type inputTypes[],
        size_t nbInputs,
        const char* testName,
        RunScenario run_f)
{
    ZL_Compressor* const cgraph     = ZL_Compressor_create();
    ZL_FunctionGraphDesc const migd = {
        .name           = "storeGraph",
        .graph_f        = graphf,
        .inputTypeMasks = inputTypes,
        .nbInputs       = nbInputs,
    };
    auto graph = ZL_Compressor_registerFunctionGraph(cgraph, &migd);
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph, graph));
    auto ret = genInt32Data(cgraph, inputTypes, nbInputs, testName, run_f);
    ZL_Compressor_free(cgraph);
    return ret;
}

static int genInt32Data(
        ZL_GraphFn graphf,
        const ZL_Type inputTypes[],
        size_t nbInputs,
        const char* testName,
        RunScenario run_f)
{
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    ZL_Report const gssr = ZL_Compressor_initUsingGraphFn(cgraph, graphf);
    EXPECT_ZS_VALID(gssr);
    auto ret = genInt32Data(cgraph, inputTypes, nbInputs, testName, run_f);
    ZL_Compressor_free(cgraph);
    return ret;
}

/* ------   error tests   ------ */

static int cFailTest(
        ZL_Compressor* cgraph,
        const InputDesc* inputs,
        size_t nbInputs,
        const char* testName)
{
    printf("\n=========================== \n");
    printf(" %s (%zu inputs)\n", testName, nbInputs);
    printf("--------------------------- \n");

    // Create Inputs
    size_t totalSrcSize = 0;
    for (size_t n = 0; n < nbInputs; n++) {
        totalSrcSize += inputs[n].size;
    }
    size_t const compressedBound = ZL_compressBound(totalSrcSize);
    void* const compressed       = malloc(compressedBound);
    ZL_REQUIRE_NN(compressed);

    ZL_TypedRef** typedInputs =
            (ZL_TypedRef**)malloc(nbInputs * sizeof(typedInputs[0]));
    ZL_REQUIRE_NN(typedInputs);

    for (size_t n = 0; n < nbInputs; n++) {
        typedInputs[n] =
                initInput(inputs[n].start, inputs[n].size, inputs[n].type);
        ZL_REQUIRE_NN(typedInputs[n]);
    }

    // just for type casting
    const ZL_TypedRef** readOnly =
            (const ZL_TypedRef**)malloc(nbInputs * sizeof(typedInputs[0]));
    ZL_REQUIRE_NN(readOnly);
    memcpy(readOnly, typedInputs, nbInputs * sizeof(typedInputs[0]));

    ZL_Report const compressionReport =
            compress(compressed, compressedBound, readOnly, nbInputs, cgraph);
    EXPECT_EQ(ZL_isError(compressionReport), 1)
            << "compression should have failed \n";

    printf("compression failed as expected \n");

    // clean
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_TypedRef_free(typedInputs[n]);
    }
    free(readOnly);
    free(typedInputs);
    free(compressed);
    return 0;
}

static int gOutputDeviation = 0;

static int dFailTestBase(
        ZL_Compressor* cgraph,
        const InputDesc* inputs,
        size_t nbInputs,
        const char* testName,
        std::optional<int> allocation_offset = std::nullopt)
{
    printf("\n=========================== \n");
    printf(" %s (%zu inputs)\n", testName, nbInputs);
    printf("--------------------------- \n");

    // Create Inputs
    size_t totalSrcSize = 0;
    for (size_t n = 0; n < nbInputs; n++) {
        totalSrcSize += inputs[n].size;
    }
    size_t const compressedBound = ZL_compressBound(totalSrcSize);
    void* const compressed       = malloc(compressedBound);
    ZL_REQUIRE_NN(compressed);

    ZL_TypedRef** typedInputs =
            (ZL_TypedRef**)malloc(nbInputs * sizeof(typedInputs[0]));
    ZL_REQUIRE_NN(typedInputs);

    for (size_t n = 0; n < nbInputs; n++) {
        typedInputs[n] =
                initInput(inputs[n].start, inputs[n].size, inputs[n].type);
        ZL_REQUIRE_NN(typedInputs[n]);
    }

    // just for type casting
    const ZL_TypedRef** readOnly =
            (const ZL_TypedRef**)malloc(nbInputs * sizeof(typedInputs[0]));
    ZL_REQUIRE_NN(readOnly);
    memcpy(readOnly, typedInputs, nbInputs * sizeof(typedInputs[0]));

    ZL_Report const compressionReport =
            compress(compressed, compressedBound, readOnly, nbInputs, cgraph);
    EXPECT_EQ(ZL_isError(compressionReport), 0) << "compression failed \n";
    size_t const compressedSize = ZL_validResult(compressionReport);

    size_t const nbOutputs = (size_t)((int)nbInputs + gOutputDeviation);
    ZL_TypedBuffer** outputs =
            (ZL_TypedBuffer**)malloc(nbOutputs * sizeof(ZL_TypedBuffer*));
    std::vector<std::vector<uint8_t>> bufs{ nbOutputs };
    for (size_t n = 0; n < nbOutputs; n++) {
        if (allocation_offset.has_value()) {
            auto allocate = std::max(
                    (int)inputs[n].size + allocation_offset.value(), 0);
            bufs[n] = std::vector<uint8_t>(allocate);
            outputs[n] =
                    initOutput(bufs[n].data(), bufs[n].size(), inputs[n].type);
        } else {
            outputs[n] = ZL_TypedBuffer_create();
        }
        assert(outputs[n]);
    }
    ZL_Report const decompressionReport =
            decompress(outputs, nbOutputs, compressed, compressedSize);
    EXPECT_EQ(ZL_isError(decompressionReport), 1)
            << "decompression should have failed \n";

    printf("decompression failed as expected \n");

    // clean
    for (size_t n = 0; n < nbOutputs; n++) {
        ZL_TypedBuffer_free(outputs[n]);
    }
    free(outputs);
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_TypedRef_free(typedInputs[n]);
    }
    free(readOnly);
    free(typedInputs);
    free(compressed);
    return 0;
}

static int dFailTest(
        ZL_Compressor* cgraph,
        const InputDesc* inputs,
        size_t nbInputs,
        const char* testName)
{
    return dFailTestBase(cgraph, inputs, nbInputs, testName);
}

static int dFailTestAllocateSmallOutput(
        ZL_Compressor* cgraph,
        const InputDesc* inputs,
        size_t nbInputs,
        const char* testName)
{
    return dFailTestBase(cgraph, inputs, nbInputs, testName, -32);
}

/* ------   exposed tests   ------ */

#define ARRAY_SIZE(_arr) (sizeof(_arr) / sizeof(_arr[0]))

TEST(MultiInput, serial_1)
{
    const ZL_Type types[] = { ZL_Type_serial };

    (void)genInt32Data(
            basicGenericGraph,
            types,
            ARRAY_SIZE(types),
            "Multi-Input compression, just 1 serial input",
            roundTripSuccessTest);
}

TEST(MultiInput, serial_2)
{
    const ZL_Type types[] = { ZL_Type_serial, ZL_Type_serial };

    (void)genInt32Data(
            basicGenericGraph,
            types,
            ARRAY_SIZE(types),
            "Multi-Input compression, 2 serial inputs",
            roundTripSuccessTest);
}

std::vector<ZL_Type> createTypeArray(
        size_t size,
        std::optional<std::vector<ZL_Type>> opts = std::nullopt)
{
    std::vector<ZL_Type> types;
    types.reserve(size);
    std::vector<ZL_Type> vals;
    if (!opts.has_value()) {
        vals = {
            ZL_Type_serial, ZL_Type_struct, ZL_Type_numeric, ZL_Type_string
        };
    } else {
        vals = opts.value();
    }

    for (size_t i = 0; i < size; ++i) {
        types.push_back(vals[i % vals.size()]); // Cycle through the array
    }
    return types;
}

void roundTripTest(size_t nbInputs)
{
    std::vector<ZL_Type> types    = createTypeArray(nbInputs);
    const ZL_Type* const typesPtr = types.data();

    (void)genInt32Data(
            basicGenericGraph,
            typesPtr,
            types.size(),
            "Compression of multiple Inputs of various Types",
            roundTripSuccessTest);
}

void roundTripAllocateOutputs(size_t nbInputs)
{
    std::vector<ZL_Type> opts = {
        ZL_Type_serial,
        ZL_Type_struct,
        ZL_Type_numeric,
    };
    std::vector<ZL_Type> types    = createTypeArray(nbInputs, opts);
    const ZL_Type* const typesPtr = types.data();

    (void)genInt32Data(
            basicGenericGraph,
            typesPtr,
            types.size(),
            "Compression of multiple Inputs of various Types",
            roundTripAllocateOutputsTest);
}

void roundTripAllocateBiggerOutputs(size_t nbInputs)
{
    std::vector<ZL_Type> opts = {
        ZL_Type_serial,
        ZL_Type_struct,
        ZL_Type_numeric,
    };
    std::vector<ZL_Type> types    = createTypeArray(nbInputs, opts);
    const ZL_Type* const typesPtr = types.data();

    (void)genInt32Data(
            basicGenericGraph,
            typesPtr,
            types.size(),
            "Compression of multiple Inputs of various Types",
            roundTripAllocateBiggerOutputsTest);
}

TEST(MultiInput, _1_types)
{
    roundTripTest(1);
    roundTripAllocateOutputs(1);
}

TEST(MultiInput, _2_types)
{
    roundTripTest(2);
    roundTripAllocateOutputs(2);
}

TEST(MultiInput, _4_types)
{
    roundTripTest(4);
    roundTripAllocateOutputs(4);
}

TEST(MultiInput, _5_types)
{
    roundTripTest(5);
    roundTripAllocateOutputs(5);
}

TEST(MultiInput, _6_types)
{
    roundTripTest(6);
    roundTripAllocateOutputs(6);
}

TEST(MultiInput, _8_types)
{
    roundTripTest(8);
    roundTripAllocateOutputs(8);
}

TEST(MultiInput, _18_types)
{
    roundTripTest(18);
    roundTripAllocateOutputs(18);
}

TEST(MultiInput, _19_types)
{
    roundTripTest(19);
    roundTripAllocateOutputs(19);
}

TEST(MultiInput, _20_types)
{
    roundTripTest(20);
    roundTripAllocateOutputs(20);
}

TEST(MultiInput, _37_types)
{
    roundTripTest(37);
    roundTripAllocateOutputs(37);
}

TEST(MultiInput, _38_types)
{
    roundTripTest(38);
    roundTripAllocateOutputs(38);
}

TEST(MultiInput, _39_types)
{
    roundTripTest(39);
    roundTripAllocateOutputs(39);
}

TEST(MultiInput, _273_types)
{
    roundTripTest(273);
    roundTripAllocateOutputs(273);
}

TEST(MultiInput, _274_types)
{
    roundTripTest(274);
    roundTripAllocateOutputs(274);
}

TEST(MultiInput, _2047_types)
{
    roundTripTest(2047);
    roundTripAllocateOutputs(2047);
}

TEST(MultiInput, maxNbInputs)
{
    roundTripTest(ZL_ENCODER_INPUT_LIMIT);
    roundTripAllocateOutputs(ZL_ENCODER_INPUT_LIMIT);
}

TEST(MultiInput, maxNbInputsNumeric)
{
    std::vector<ZL_Type> types = { ZL_Type_numeric };

    (void)genInt32Data(
            basicGenericGraph,
            types.data(),
            types.size(),
            "Compression of multiple numeric inputs",
            roundTripSuccessTest);
}

TEST(MultiInput, allocateBiggerOutputsThanNeeded)
{
    roundTripAllocateBiggerOutputs(2);
    roundTripAllocateBiggerOutputs(5);
    roundTripAllocateBiggerOutputs(20);
}

/* failure scenarios */

TEST(MultiInput, _too_many_Inputs_failure)
{
    std::vector<ZL_Type> types    = createTypeArray(ZL_ENCODER_INPUT_LIMIT + 1);
    const ZL_Type* const typesPtr = types.data();

    char buffer[100] = { 0 };
    int written      = std::snprintf(
            buffer,
            sizeof(buffer),
            "Multi-Input compression failure: too many Typed Inputs");
    assert(written > 0);
    (void)written;
    std::string formattedString(buffer); // Initialize directly from buffer

    (void)genInt32Data(
            basicGenericGraph,
            typesPtr,
            types.size(),
            formattedString.c_str(),
            cFailTest);
}

static void tooManyOutputs(size_t nbOutputs, int deviation)
{
    size_t nbInputs               = nbOutputs;
    std::vector<ZL_Type> types    = createTypeArray(nbInputs);
    const ZL_Type* const typesPtr = types.data();
    gOutputDeviation              = deviation;

    (void)genInt32Data(
            basicGenericGraph,
            typesPtr,
            types.size(),
            "Decompression of incorrect nb of Outputs",
            dFailTest);
}

TEST(MultiInput, tooManyDecompressionOutputs)
{
    tooManyOutputs(16, 1);
}

TEST(MultiInput, notEnoughDecompressionOutputs)
{
    tooManyOutputs(16, -1);
}

static void smallOutputs(size_t nbOutputs)
{
    std::vector<ZL_Type> opts = {
        ZL_Type_serial,
        ZL_Type_struct,
        ZL_Type_numeric,
    };
    std::vector<ZL_Type> types    = createTypeArray(nbOutputs, opts);
    const ZL_Type* const typesPtr = types.data();

    (void)genInt32Data(
            basicGenericGraph,
            typesPtr,
            types.size(),
            "Compression of multiple Inputs of various Types",
            dFailTestAllocateSmallOutput);
}

TEST(MultiInput, smallOutputs)
{
    smallOutputs(1);
    smallOutputs(5);
    smallOutputs(20);
}

template <size_t N = 1>
static ZL_Report
runStoreGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs) noexcept
{
    for (size_t n = 0; n < nbInputs; n++) {
        auto sctx = inputs[n];
        if (n % N == 0) {
            ZL_RET_R_IF_ERR(ZL_Edge_setDestination(sctx, ZL_GRAPH_STORE));
        } else {
            ZL_RET_R_IF_ERR(
                    ZL_Edge_setDestination(sctx, ZL_GRAPH_COMPRESS_GENERIC));
        }
    }
    return ZL_returnSuccess();
}

TEST(MultiInput, store3SerialInputs)
{
    std::vector<ZL_Type> types = { ZL_Type_serial,
                                   ZL_Type_serial,
                                   ZL_Type_serial };

    (void)genInt32Data(
            runStoreGraph,
            types.data(),
            types.size(),
            "Compression of multiple Inputs of various Types",
            roundTripSuccessTest);
}

TEST(MultiInput, store3NumInputs)
{
    constexpr std::array<ZL_Type, 3> types = { ZL_Type_numeric,
                                               ZL_Type_numeric,
                                               ZL_Type_numeric };
    (void)genInt32Data(
            runStoreGraph,
            types.data(),
            types.size(),
            "Compression of multiple Inputs of various Types",
            roundTripSuccessTest);
}

TEST(MultiInput, storeAndCompress5SerialInputs)
{
    std::vector<ZL_Type> types = { ZL_Type_serial,
                                   ZL_Type_serial,
                                   ZL_Type_serial,
                                   ZL_Type_serial,
                                   ZL_Type_serial };

    (void)genInt32Data(
            runStoreGraph<2>,
            types.data(),
            types.size(),
            "Compression of multiple Inputs of various Types",
            roundTripSuccessTest);
}

TEST(MultiInput, storeAndCompress5NumericInputs)
{
    std::vector<ZL_Type> types = { ZL_Type_numeric,
                                   ZL_Type_numeric,
                                   ZL_Type_numeric,
                                   ZL_Type_numeric,
                                   ZL_Type_numeric };

    (void)genInt32Data(
            runStoreGraph<2>,
            types.data(),
            types.size(),
            "Compression of multiple Inputs of various Types",
            roundTripSuccessTest);
}

TEST(MultiInput, storeAndCompress5MixedInputs)
{
    std::vector<ZL_Type> types = { ZL_Type_numeric,
                                   ZL_Type_serial,
                                   ZL_Type_struct,
                                   ZL_Type_numeric,
                                   ZL_Type_serial };

    (void)genInt32Data(
            runStoreGraph<2>,
            types.data(),
            types.size(),
            "Compression of multiple Inputs of various Types",
            roundTripSuccessTest);
}

/* =======   multi-inputs and segmenter   ======== */

#define MIN(a, b) ((a) < (b) ? (a) : (b))
ZL_Report trivialSegmenterFn(ZL_Segmenter* sctx)
{
    /* just cut each input into an arbitrary 100 elts */
    assert(sctx != NULL);
    size_t numInputs = ZL_Segmenter_numInputs(sctx);
    printf("trivialSegmenterFn for %zu inputs\n", numInputs);

    size_t* chunkSizes = (size_t*)malloc(numInputs * sizeof(chunkSizes[0]));
    assert(chunkSizes);

    while (1) {
        for (size_t n = 0; n < numInputs; n++) {
            size_t chunkDefaultSize = 100;
            const ZL_Input* input   = ZL_Segmenter_getInput(sctx, n);
            size_t numElts = MIN(ZL_Input_numElts(input), chunkDefaultSize);
            chunkSizes[n]  = numElts;
        }
        int someNonZeroes = 0;
        for (size_t n = 0; n < numInputs; n++) {
            someNonZeroes += (chunkSizes[n] > 0);
        }
        printf("%zu %zu %zu %zu %zu \n",
               chunkSizes[0],
               chunkSizes[1],
               chunkSizes[2],
               chunkSizes[3],
               chunkSizes[4]);
        if (!someNonZeroes) /* no more data */
            break;
        ZL_Report processR = ZL_Segmenter_processChunk(
                sctx, chunkSizes, 5, ZL_GRAPH_COMPRESS_GENERIC, NULL);
        EXPECT_FALSE(ZL_isError(processR));
    }

    free(chunkSizes);
    return ZL_returnSuccess();
}

static int genInt32Data(
        ZL_SegmenterFn segmenterf,
        const ZL_Type inputTypes[],
        size_t nbInputs,
        const char* testName,
        RunScenario run_f)
{
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    ZL_SegmenterDesc const segd = {
        .name           = "segmenter",
        .segmenterFn    = segmenterf,
        .inputTypeMasks = inputTypes,
        .numInputs      = nbInputs,
    };
    auto id = ZL_Compressor_registerSegmenter(cgraph, &segd);
    EXPECT_TRUE(ZL_GraphID_isValid(id));
    auto ret = genInt32Data(cgraph, inputTypes, nbInputs, testName, run_f);
    ZL_Compressor_free(cgraph);
    return ret;
}

TEST(MultiInput, segment5MixedInputs)
{
    // This test requires chunking functionality
    if (g_formatVersion_forTests < ZL_CHUNK_VERSION_MIN)
        return;

    std::vector<ZL_Type> types = { ZL_Type_numeric,
                                   ZL_Type_serial,
                                   ZL_Type_struct,
                                   ZL_Type_string,
                                   ZL_Type_string };

    (void)genInt32Data(
            trivialSegmenterFn,
            types.data(),
            types.size(),
            "Compression of multiple Inputs of various Types",
            roundTripSuccessTest);
}

} // namespace
