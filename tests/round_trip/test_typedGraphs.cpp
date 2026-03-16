// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// standard C
#include <stdint.h> // uint_X,
#include <stdio.h>  // printf
#include <string.h> // memcpy

#include <zstd.h>

// Zstrong
#include "openzl/codecs/tokenize/decode_tokenize4to2_kernel.h"
#include "openzl/codecs/tokenize/encode_tokenize4to2_kernel.h" // ZS_tokenize4to2_encode
#include "openzl/common/debug.h"                               // ZL_REQUIRE
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h" // ZL_Compressor_registerTypedEncoder, ZL_Compressor_registerStaticGraph_fromPipelineNodes1o
#include "openzl/zl_ctransform.h" // ZL_TypedEncoderDesc
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_dtransform.h"
#include "openzl/zl_localParams.h"
#include "openzl/zl_selector.h" // ZL_SelectorDesc

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

#define CT_ADD1_ID 2
#define CT_TOKEN4toSERIAL_ID 3
#define CT_TOKENIZEU32_ID 4
#define CT_SPLIT2_ID 5
#define CT_FORGETCOMMIT_ID 6

// Note : kernels are as lean as possible
using u32 = uint32_t;
static void add1_u32(u32* dst32, const u32* src32, size_t nbU32)
{
    for (size_t n = 0; n < nbU32; n++)
        dst32[n] = src32[n] + 1;
}

// Note : integer transforms should be compatible with any integer size
static ZL_Report add1_int(
        ZL_Encoder* eictx, // To create output stream
        const ZL_Input* in) noexcept
{
    assert(in != nullptr);
    size_t const eltWidth = ZL_Input_eltWidth(in);
    printf("add1 transform (integer width : %zu)\n", eltWidth);
    assert(ZL_Input_type(in) == ZL_Type_numeric);
    assert(eltWidth == 4); // This silly example integer transform only works on
                           // 32-bit integers However, a more complete example
                           // should be ready to work with any integer width
    size_t const nbElts = ZL_Input_numElts(in);
    assert(eictx != nullptr);
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eictx, 0, nbElts, eltWidth);
    ZL_RET_R_IF_NULL(allocation, out); // control allocation success

    const u32* const iarr = (const u32*)ZL_Input_ptr(in);
    u32* const oarr       = (u32*)ZL_Output_ptr(out);
    switch (eltWidth) {
        case 4:
            add1_u32(oarr, iarr, nbElts);
            break;
        case 1: // not provided, but should for the general case
        case 2: // not provided, but should for the general case
        case 8: // not provided, but should for the general case
        default:
            printf("incorrect eltWidth (%zu) \n", eltWidth);
            assert(0); // should be impossible (eltWidth asserted just before)
    }
    ZL_RET_R_IF_ERR(ZL_Output_commit(out, nbElts));

    return ZL_returnValue(1); // nb Out Streams
}
// We use a #define, to be employed as initializer in static const declarations
// below.
#define ADD1_GDESC                                                     \
    (ZL_TypedGraphDesc){ .CTid         = CT_ADD1_ID,                   \
                         .inStreamType = ZL_Type_numeric,              \
                         .outStreamTypes =                             \
                                 (const ZL_Type[]){ ZL_Type_numeric }, \
                         .nbOutStreams = 1 }
static ZL_TypedEncoderDesc const add1_CDesc = {
    .gd          = ADD1_GDESC,
    .transform_f = add1_int,
};

// Variant specialized in 32-bit -> 16-bit tokenization
// Note : in future, tokenize should be compatible with any integer width
// Note 2 : Not (yet) used in below graph
//
using u16 = uint16_t;

static ZL_Report tokenize_u32(ZL_Encoder* eictx, const ZL_Input* in) noexcept
{
    printf("tokenize_u32 \n");
    assert(ZL_Input_type(in) == ZL_Type_numeric);
    assert(ZL_Input_eltWidth(in) == 4); // 32-bit
    size_t const nbElts = ZL_Input_numElts(in);

    enum { alphabetStreamID = 0, indexStreamID = 1 };
    // Alphabet : where all unique symbols will be listed
    size_t const alphabetCapacity = 65536; // 16-bit
    ZL_Output* const alphabet     = ZL_Encoder_createTypedStream(
            eictx, alphabetStreamID, alphabetCapacity, 4);
    assert(alphabet != nullptr); // control allocation success
    // Indexes are presumed to fit into 16-bit values.
    // I'm cheating here : in reality, the transform should first check the
    // cardinality and switch to some backup strategies when reality doesn't
    // match expectation.
    ZL_Output* const indexes =
            ZL_Encoder_createTypedStream(eictx, indexStreamID, nbElts, 2);
    assert(indexes != nullptr); // control allocation success

    const u32* const iarr32 = (const u32*)ZL_Input_ptr(in);
    assert(iarr32 != nullptr);
    u32* const oarr32 = (u32*)ZL_Output_ptr(alphabet);
    assert(oarr32 != nullptr);
    u16* const index16 = (u16*)ZL_Output_ptr(indexes);
    assert(index16 != nullptr);

    size_t const cardinality = ZS_tokenize4to2_encode(
            index16,
            nbElts,
            oarr32,
            alphabetCapacity,
            iarr32,
            nbElts,
            ZS_tam_unsorted);
    assert(cardinality <= alphabetCapacity);

    ZL_RET_R_IF_ERR(ZL_Output_commit(alphabet, cardinality));
    ZL_RET_R_IF_ERR(ZL_Output_commit(indexes, nbElts));
    return ZL_returnValue(2); // nb Out Streams
}
#define TOKENIZE32_GDESC                                                     \
    {                                                                        \
        .CTid           = CT_TOKENIZEU32_ID,                                 \
        .inStreamType   = ZL_Type_numeric,                                   \
        .outStreamTypes = (const ZL_Type[]){ ZL_Type_numeric /* alphabet */, \
                                             ZL_Type_numeric /* index */ },  \
        .nbOutStreams   = 2,                                                 \
    }
static ZL_TypedEncoderDesc const tokenize32_CDesc = {
    .gd          = TOKENIZE32_GDESC,
    .transform_f = tokenize_u32,
};

static ZL_Report forgetCommit(
        ZL_Encoder* eictx, // To create output stream
        const ZL_Input* in) noexcept
{
    assert(in != nullptr);
    assert(ZL_Input_type(in) == ZL_Type_serial);
    size_t const size = ZL_Input_contentSize(in);
    assert(eictx != nullptr);
    ZL_Output* const out = ZL_Encoder_createTypedStream(eictx, 0, size, 1);
    ZL_RET_R_IF_NULL(allocation, out); // control allocation success

    memcpy(ZL_Output_ptr(out), ZL_Input_ptr(in), size);

    // No commit => forget about it => should result in an error

    return ZL_returnValue(1); // nb Out Streams
}
#define FORGETCOMMIT_GDESC                                            \
    (ZL_TypedGraphDesc){ .CTid         = CT_FORGETCOMMIT_ID,          \
                         .inStreamType = ZL_Type_serial,              \
                         .outStreamTypes =                            \
                                 (const ZL_Type[]){ ZL_Type_serial }, \
                         .nbOutStreams = 1 }
static ZL_TypedEncoderDesc const forgetCommit_CDesc = {
    .gd          = FORGETCOMMIT_GDESC,
    .transform_f = forgetCommit,
};

/* ------   create custom typed selector   -------- */

static ZL_GraphID select_first_custom(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* cfns,
        size_t nbCfns) noexcept
{
    (void)selCtx; // only useful to ask parameters
    ZL_Type const st = ZL_Input_type(inputStream);
    assert(st == ZL_Type_struct);
    (void)st;
    assert(cfns != nullptr);
    assert(nbCfns >= 1);
    (void)nbCfns;
    printf("select_first_custom : selecting first custom transform (graphid=%u) \n",
           cfns[0].gid);
    return cfns[0];
}

/* ------   create custom graph   -------- */

// This graph function follows the ZL_GraphFn definition
// It's in charge of registering custom transforms,
// and convert them in a succession of nodes, creating a graph
static ZL_GraphID typedGraph(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    (void)tokenize32_CDesc; // not used, just a transform example
    ZL_NodeID const node_add1 =
            ZL_Compressor_registerTypedEncoder(cgraph, &add1_CDesc);

    // test exercising ZL_Compressor_registerParameterizedNode() from a standard
    // Node
    ZL_LocalParams const lp               = { { nullptr, 0 },
                                              { nullptr, 0 },
                                              { nullptr, 0 } };
    const ZL_ParameterizedNodeDesc pndesc = {
        .node        = ZL_NODE_TRANSPOSE_SPLIT,
        .localParams = &lp,
    };
    ZL_NodeID const node_myTranspose =
            ZL_Compressor_registerParameterizedNode(cgraph, &pndesc);

    // Graph : src => serial->int32 => add1 => delta => convertToken (implicit)
    // => tselect
    // /=> transpose => convertSerial (implicit) => lz_compress
    // tselect is a type-selector
    // that will always select first custom node as successor (just for test)

    const ZL_NodeID transpose_pipeline[] = { node_myTranspose };
    ZL_GraphID const graph_transpose =
            ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
                    cgraph, transpose_pipeline, 1, ZL_GRAPH_ZSTD);

    ZL_SelectorDesc const tselect = {
        .selector_f     = select_first_custom,
        .inStreamType   = ZL_Type_struct,
        .customGraphs   = &graph_transpose,
        .nbCustomGraphs = 1,
    };

    const ZL_NodeID pipeline[] = { ZL_NODE_INTERPRET_AS_LE32,
                                   node_add1,
                                   ZL_NODE_DELTA_INT };
    return ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
            cgraph,
            pipeline,
            3,
            ZL_Compressor_registerSelectorGraph(cgraph, &tselect));
}

static ZL_GraphID graph_zstd_level(ZL_Compressor* cgraph, int level)
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    const ZL_GraphID dst         = ZL_GRAPH_STORE;
    const ZL_IntParam clevel     = { ZSTD_c_compressionLevel, level };
    const ZL_LocalParams lparams = { .intParams = { &clevel, 1 } };
    const ZL_StaticGraphDesc sgd = {
        .name           = "Zstd Graph with custom compression level",
        .headNodeid     = ZL_NODE_ZSTD,
        .successor_gids = &dst,
        .nbGids         = 1,
        .localParams    = &lparams,
    };
    return ZL_Compressor_registerStaticGraph(cgraph, &sgd);
}

static ZL_GraphID graph_zstd_lvl1(ZL_Compressor* cgraph) noexcept
{
    return graph_zstd_level(cgraph, 1);
}

static ZL_GraphID graph_zstd_lvl19(ZL_Compressor* cgraph) noexcept
{
    return graph_zstd_level(cgraph, 19);
}

static ZL_GraphID graph_zstd_wNewParams_level(ZL_Compressor* cgraph, int level)
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    const ZL_IntParam clevel       = { ZSTD_c_compressionLevel, level };
    const ZL_LocalParams lparams   = { .intParams = { &clevel, 1 } };
    ZL_ParameterizedGraphDesc desc = {
        .graph       = ZL_GRAPH_ZSTD,
        .localParams = &lparams,
    };
    return ZL_Compressor_registerParameterizedGraph(cgraph, &desc);
}

static ZL_GraphID graph_zstd_wNewParam_lvl1(ZL_Compressor* cgraph) noexcept
{
    return graph_zstd_wNewParams_level(cgraph, 1);
}

static ZL_GraphID graph_zstd_wNewParam_lvl19(ZL_Compressor* cgraph) noexcept
{
    return graph_zstd_wNewParams_level(cgraph, 19);
}

/* -----   Test : create output streams in "wrong" order   ----- */

static ZL_Report split2_reverseDeclarationOrder(
        ZL_Encoder* eictx, // To create output stream
        const ZL_Input* in) noexcept
{
    assert(in != nullptr);
    assert(ZL_Input_type(in) == ZL_Type_serial);
    printf("split2_reverseOrder \n");
    size_t const nbBytes  = ZL_Input_numElts(in);
    size_t const seg1Size = nbBytes / 2;
    size_t const seg2Size = nbBytes - seg1Size;

    /* intentionally create output streams in reverse order : out2, then out1 */
    assert(eictx != nullptr);
    ZL_Output* const out2 = ZL_Encoder_createTypedStream(eictx, 1, seg2Size, 1);
    ZL_RET_R_IF_NULL(allocation, out2); // control allocation success
    ZL_Output* const out1 = ZL_Encoder_createTypedStream(eictx, 0, seg1Size, 1);
    ZL_RET_R_IF_NULL(allocation, out1); // control allocation success

    const char* const ip = (const char*)ZL_Input_ptr(in);
    char* const op1      = (char*)ZL_Output_ptr(out1);
    char* const op2      = (char*)ZL_Output_ptr(out2);

    memcpy(op1, ip, seg1Size);
    ZL_RET_R_IF_ERR(ZL_Output_commit(out1, seg1Size));

    memcpy(op2, ip + seg1Size, seg2Size);
    ZL_RET_R_IF_ERR(ZL_Output_commit(out2, seg2Size));

    return ZL_returnValue(2); // nb Out Streams
}
// Use a #define, to be employed as initializer in const declarations
#define SPLIT2RDO_DESC                                                         \
    (ZL_TypedGraphDesc){                                                       \
        .CTid           = CT_SPLIT2_ID,                                        \
        .inStreamType   = ZL_Type_serial,                                      \
        .outStreamTypes = (const ZL_Type[]){ ZL_Type_serial, ZL_Type_serial }, \
        .nbOutStreams   = 2                                                    \
    }
static ZL_TypedEncoderDesc const split2rdo_CDesc = {
    .gd          = SPLIT2RDO_DESC,
    .transform_f = split2_reverseDeclarationOrder,
};

// This graph function follows the ZL_GraphFn definition
static ZL_GraphID split2rdo_graph(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    (void)tokenize32_CDesc; // not used, just a transform example
    ZL_NodeID const node_split2rdo =
            ZL_Compressor_registerTypedEncoder(cgraph, &split2rdo_CDesc);

    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph,
            node_split2rdo,
            ZL_GRAPHLIST(ZL_GRAPH_STORE, ZL_GRAPH_STORE));
}

/* Test 3 : forget to commit an output Stream */

static ZL_GraphID forgetCommit_graphF(ZL_Compressor* cgraph) noexcept
{
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    ZL_NodeID const node_forget =
            ZL_Compressor_registerTypedEncoder(cgraph, &forgetCommit_CDesc);

    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node_forget, ZL_GRAPH_ZSTD);
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
    ZL_REQUIRE_NN(cgraph);
    ZL_Report const gssr = ZL_Compressor_initUsingGraphFn(cgraph, graphf);
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

static void sub1_u32(u32* dst32, const u32* src32, size_t nbU32)
{
    for (size_t n = 0; n < nbU32; n++)
        dst32[n] = src32[n] - 1;
}

// custom decoder transform description
static ZL_Report add1_decode(ZL_Decoder* eictx, const ZL_Input* ins[]) noexcept
{
    assert(ins != nullptr);
    const ZL_Input* const in = ins[0];
    assert(in != nullptr);
    size_t const nbElts   = ZL_Input_numElts(in);
    size_t const eltWidth = ZL_Input_eltWidth(in);
    printf("delta_decode (width:%zu bytes)\n", eltWidth);
    assert(ZL_Input_type(in) == ZL_Type_numeric);
    assert(eltWidth == 4); // This silly example integer transform only works on
                           // 32-bit integers However, a more complete example
                           // should be ready to work with any integer width
    ZL_Output* const out = ZL_Decoder_create1OutStream(eictx, nbElts, eltWidth);
    ZL_RET_R_IF_NULL(allocation, out); // control allocation success

    const u32* const iarr = (const u32*)ZL_Input_ptr(in);
    u32* const oarr       = (u32*)ZL_Output_ptr(out);
    switch (eltWidth) {
        case 4:
            sub1_u32(oarr, iarr, nbElts);
            break;
        case 1: // not provided, but should for the general case
        case 2: // not provided, but should for the general case
        case 8: // not provided, but should for the general case
        default:
            printf("incorrect eltWidth (%zu) \n", eltWidth);
            assert(0); // should be impossible (eltWidth asserted just before)
    }
    ZL_RET_R_IF_ERR(ZL_Output_commit(out, nbElts));

    return ZL_returnValue(1); // nb Out Streams
}
static ZL_TypedDecoderDesc const add1_DDesc = {
    .gd          = ADD1_GDESC,
    .transform_f = add1_decode,
};

// Note : not used (yet) in the example graph
static ZL_Report tokenize_u32_decode(
        ZL_Decoder* dictx,
        const ZL_Input* ins[]) noexcept
{
    printf("tokenize_u32_decode \n");
    enum { alphabetStreamID = 0, indexStreamID = 1 };
    assert(ins != nullptr);
    const ZL_Input* const alphabet = ins[alphabetStreamID];
    assert(alphabet != nullptr);
    assert(ZL_Input_type(alphabet) == ZL_Type_numeric);
    assert(ZL_Input_eltWidth(alphabet) == 4); // 32-bit
    size_t const alphabetSize     = ZL_Input_numElts(alphabet);
    const ZL_Input* const indexes = ins[indexStreamID];
    assert(indexes != nullptr);
    assert(ZL_Input_type(indexes) == ZL_Type_numeric);
    assert(ZL_Input_eltWidth(indexes) == 2); // 16-bit
    size_t const nbElts = ZL_Input_numElts(indexes);

    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, nbElts, 4);
    assert(out != nullptr); // control allocation success

    const u32* const alphabet32 = (const u32*)ZL_Input_ptr(alphabet);
    assert(alphabet32 != nullptr);
    const u16* const index16 = (const u16*)ZL_Input_ptr(indexes);
    assert(index16 != nullptr);
    u32* const oarr32 = (u32*)ZL_Output_ptr(out);
    assert(oarr32 != nullptr);

    size_t const nbEltsRegenerated = ZS_tokenize4to2_decode(
            oarr32, nbElts, index16, alphabetSize, alphabet32, nbElts);
    assert(nbEltsRegenerated == nbElts);
    (void)nbEltsRegenerated;

    ZL_RET_R_IF_ERR(ZL_Output_commit(out, nbElts));
    return ZL_returnValue(1); // nb Out Streams
}
static ZL_TypedDecoderDesc const tokenize32DDesc = {
    .gd          = TOKENIZE32_GDESC,
    .transform_f = tokenize_u32_decode,
};

// Join2 - reverse of Split2
static size_t join2Size(const ZL_RBuffer src[]) noexcept
{
    (void)src;
    return src[0].size + src[1].size;
}
static size_t join2(ZL_WBuffer dst, const ZL_RBuffer src[]) noexcept
{
    printf("joining %zu + %zu bytes \n", src[0].size, src[1].size);

    assert(dst.capacity >= join2Size(src));
    size_t pos = 0;
    for (size_t n = 0; n < 2; n++) {
        memcpy((char*)dst.start + pos, src[n].start, src[n].size);
        pos += src[n].size;
    }
    return join2Size(src);
}
static ZL_SplitDecoderDesc const join2_DDesc = {
    .CTid           = CT_SPLIT2_ID, // Use same ID as compression side
    .nbInputStreams = 2,
    .dstBound_f     = join2Size,
    .transform_f    = join2,
};

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
    (void)tokenize32DDesc; // not used, just a transform example
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(dctx, &add1_DDesc));
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerSplitDecoder(dctx, &join2_DDesc));

    // Decompress, using custom decoder(s)
    ZL_Report const r =
            ZL_DCtx_decompress(dctx, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 0) << "decompression failed \n";

    return ZL_validResult(r);
}

/* ------   round trip test   ------ */

static size_t roundTripTest(
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

    ZL_Report decSize_r = ZL_getDecompressedSize(compressed, compressedSize);
    EXPECT_EQ(ZL_isError(decSize_r), 0);
    size_t decSize_header = ZL_validResult(decSize_r);
    EXPECT_EQ((int)decSize_header, (int)inputSize);

    void* const decompressed = malloc(inputSize);
    ZL_REQUIRE_NN(decompressed);

    size_t const decompressedSize =
            decompress(decompressed, inputSize, compressed, compressedSize);
    printf("decompressed %zu input bytes into %zu original bytes \n",
           compressedSize,
           decompressedSize);

    // round-trip check
    EXPECT_EQ((int)decompressedSize, (int)inputSize)
            << "Error : decompressed size != original size \n";
    if (inputSize) {
        EXPECT_EQ(memcmp(input, decompressed, inputSize), 0)
                << "Error : decompressed content differs from original (corruption issue) !!!  \n";
    }

    printf("round-trip success \n");
    free(decompressed);
    free(compressed);
    return compressedSize;
}

static size_t roundTripIntegers(ZL_GraphFn graphf, const char* name)
{
    // Generate test input
#define NB_INTS 84
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    return roundTripTest(graphf, input, sizeof(input), name);
}

/* this test is expected to fail predictably */
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

// List of tests

TEST(TypedGraphs, typedGraph)
{
    (void)roundTripIntegers(
            typedGraph,
            "Example graph with typed transforms and typed selectors");
}

TEST(TypedGraphs, unordered_outputs)
{
    (void)roundTripIntegers(
            split2rdo_graph, "Node allocating output streams in unordered way");
}

TEST(TypedGraphs, stream_not_committed)
{
    (void)cFailTest(
            forgetCommit_graphF, "Forgetting to commit a stream is an error");
}

TEST(TypedGraphs, staticZstdGraph_wParams)
{
    size_t const cSize_lvl1 = roundTripIntegers(
            graph_zstd_lvl1,
            "Static Graph setting zstd compression level 1 at registration time");
    size_t const cSize_lvl19 = roundTripIntegers(
            graph_zstd_lvl19,
            "Static Graph setting zstd compression level 19 at registration time");
    EXPECT_LT(cSize_lvl19, cSize_lvl1);
    printf("As expected, level 19 compresses more (%zu < %zu) than level 1 \n",
           cSize_lvl19,
           cSize_lvl1);
}

TEST(TypedGraphs, zstdGraph_wNewParams)
{
    size_t const cSize_lvl1 = roundTripIntegers(
            graph_zstd_wNewParam_lvl1,
            "Specialize Standard Graph zstd, setting compression level 1 at registration time");
    size_t const cSize_lvl19 = roundTripIntegers(
            graph_zstd_wNewParam_lvl19,
            "Specialize Standard Graph zstd, setting compression level 19 at registration time");
    EXPECT_LT(cSize_lvl19, cSize_lvl1);
    printf("As expected, level 19 compresses more (%zu < %zu) than level 1 \n",
           cSize_lvl19,
           cSize_lvl1);
}

} // namespace
