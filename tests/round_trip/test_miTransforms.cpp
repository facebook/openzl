// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// standard C
#include <cstdio>  // printf
#include <cstring> // memcpy

// Zstrong
#include "openzl/common/debug.h"  // ZL_REQUIRE
#include "openzl/common/limits.h" // ZL_ENCODER_INPUT_LIMIT
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h" // ZS2_Compressor_*, ZL_Compressor_registerStaticGraph_fromNode1o
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_dtransform.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_selector.h"

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

#define ARRAY_SIZE(_arr) (sizeof(_arr) / sizeof(_arr[0]))

/* ------   custom transforms   -------- */

// Single input MI Transform,
// it's still a valid MI Transform,
// fully compatible with v15- wire format capability
ZL_Report
mit_copy(ZL_Encoder* eictx, const ZL_Input* inputs[], size_t nbInputs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbInputs, 1);
    ZL_ASSERT_NN(inputs);
    const ZL_Input* in = inputs[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ((int)ZL_Input_type(in), (int)ZL_Type_serial);
    size_t const size = ZL_Input_contentSize(in);

    ZL_Output* out = ZL_Encoder_createTypedStream(eictx, 0, size, 1);
    ZL_ASSERT_NN(out);

    memcpy(ZL_Output_ptr(out), ZL_Input_ptr(in), size);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, size));

    return ZL_returnSuccess();
}

#define MIT_COPY_ID 18 // any number is fine

static const ZL_MIGraphDesc mit_copy_gd = {
    .CTid       = MIT_COPY_ID,
    .inputTypes = (const ZL_Type[]){ ZL_Type_serial },
    .nbInputs   = 1,
    .voTypes    = (const ZL_Type[]){ ZL_Type_serial },
    .nbVOs      = 1,
};

static ZL_MIEncoderDesc const mit_copy_desc = {
    .gd          = mit_copy_gd,
    .transform_f = mit_copy,
    .name        = "'copy' as an MI Transform",
};

// mit_concat2: 2 inputs MI Transform
ZL_Report mit_concat2(
        ZL_Encoder* eictx,
        const ZL_Input* inputs[],
        size_t nbInputs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbInputs, 2);
    ZL_ASSERT_NN(inputs);
    ZL_ASSERT_NN(inputs[0]);
    ZL_ASSERT_NN(inputs[1]);
    ZL_ASSERT_EQ((int)ZL_Input_type(inputs[0]), (int)ZL_Type_serial);
    ZL_ASSERT_EQ((int)ZL_Input_type(inputs[1]), (int)ZL_Type_serial);

    size_t const size0     = ZL_Input_contentSize(inputs[0]);
    size_t const size1     = ZL_Input_contentSize(inputs[1]);
    size_t const totalSize = size0 + size1;

    // In this simple example, input0 can only be < 256
    assert(size0 < 256);
    unsigned char const size0_u8 = (unsigned char)size0;
    ZL_Encoder_sendCodecHeader(eictx, (const char*)&size0_u8, 1);

    ZL_Output* out = ZL_Encoder_createTypedStream(eictx, 0, totalSize, 1);
    ZL_ASSERT_NN(out);
    char* const op = (char*)ZL_Output_ptr(out);

    memcpy(op, ZL_Input_ptr(inputs[0]), size0);
    memcpy(op + size0, ZL_Input_ptr(inputs[1]), size1);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, totalSize));

    return ZL_returnSuccess();
}

#define MIT_CONCAT2_ID 2 // any unused number is fine

static const ZL_MIGraphDesc mit_concat2_gd = {
    .CTid       = MIT_CONCAT2_ID,
    .inputTypes = (const ZL_Type[]){ ZL_Type_serial, ZL_Type_serial },
    .nbInputs   = 2,
    .soTypes    = (const ZL_Type[]){ ZL_Type_serial },
    .nbSOs      = 1,
};

static ZL_MIEncoderDesc const mit_concat2_desc = {
    .gd          = mit_concat2_gd,
    .transform_f = mit_concat2,
    .name        = "concatenate 2 serial inputs",
};

// Error scenario: decoder set for 1 regen, but 2 regens declared in frame

#define INVALID_CONCAT2_BUT_1REGEN_ID 1

static const ZL_MIGraphDesc invalid_concat2_but_1regen_gd = {
    .CTid       = INVALID_CONCAT2_BUT_1REGEN_ID,
    .inputTypes = (const ZL_Type[]){ ZL_Type_serial, ZL_Type_serial },
    .nbInputs   = 2,
    .soTypes    = (const ZL_Type[]){ ZL_Type_serial },
    .nbSOs      = 1,
};

static ZL_MIEncoderDesc const invalid_concat2_but_1regen_desc = {
    .gd          = invalid_concat2_but_1regen_gd,
    .transform_f = mit_concat2,
    .name        = "invalid concat2_but_1regen transform (for testing)",
};

// Error scenario: decoder set for 3 regens, but 2 regens declared in frame

#define INVALID_CONCAT2_BUT_3REGENS_ID 3

static const ZL_MIGraphDesc invalid_concat2_but_3regens_gd = {
    .CTid       = INVALID_CONCAT2_BUT_3REGENS_ID,
    .inputTypes = (const ZL_Type[]){ ZL_Type_serial, ZL_Type_serial },
    .nbInputs   = 2,
    .soTypes    = (const ZL_Type[]){ ZL_Type_serial },
    .nbSOs      = 1,
};

static ZL_MIEncoderDesc const invalid_concat2_but_3regens_desc = {
    .gd          = invalid_concat2_but_3regens_gd,
    .transform_f = mit_concat2,
    .name        = "invalid concat2_but_3regens transform (for testing)",
};

// Error scenario: decoder attempts to create 3 regens (but only 2 declared)

#define INVALID_CONCAT2_BUT_DECL3REGENS_ID 5

static const ZL_MIGraphDesc invalid_concat2_but_decl3regens_gd = {
    .CTid       = INVALID_CONCAT2_BUT_DECL3REGENS_ID,
    .inputTypes = (const ZL_Type[]){ ZL_Type_serial, ZL_Type_serial },
    .nbInputs   = 2,
    .soTypes    = (const ZL_Type[]){ ZL_Type_serial },
    .nbSOs      = 1,
};

static ZL_MIEncoderDesc const invalid_concat2_but_decl3regens_desc = {
    .gd          = invalid_concat2_but_decl3regens_gd,
    .transform_f = mit_concat2,
    .name = "invalid: concat2, but decoders attempts to create 3 regens (for testing)",
};

// Error scenario: decoder creates only 1 regen (but 2 declared)

#define INVALID_CONCAT2_BUT_DECL1REGEN_ID 7

static const ZL_MIGraphDesc invalid_concat2_but_decl1regen_gd = {
    .CTid       = INVALID_CONCAT2_BUT_DECL1REGEN_ID,
    .inputTypes = (const ZL_Type[]){ ZL_Type_serial, ZL_Type_serial },
    .nbInputs   = 2,
    .soTypes    = (const ZL_Type[]){ ZL_Type_serial },
    .nbSOs      = 1,
};

static ZL_MIEncoderDesc const invalid_concat2_but_decl1regen_desc = {
    .gd          = invalid_concat2_but_decl1regen_gd,
    .transform_f = mit_concat2,
    .name = "invalid: concat2, but decoders creates only 1 regen (for testing)",
};

// mit_concat_serial: VI Transform, can concatenate multiple Serial inputs
ZL_Report mit_concat_serial(
        ZL_Encoder* eictx,
        const ZL_Input* inputs[],
        size_t nbInputs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(inputs);
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_ASSERT_NN(inputs[n]);
        ZL_ASSERT_EQ((int)ZL_Input_type(inputs[n]), (int)ZL_Type_serial);
    }

    // Let's use 8-bit to store each input's size (requires each input < 256)
    ZL_ASSERT_GE(nbInputs, 1);
    size_t const arrSize = nbInputs;
    uint8_t* inSizes     = (uint8_t*)ZL_Encoder_getScratchSpace(eictx, arrSize);
    ZL_ASSERT_NN(inSizes);

    size_t totalSize = 0;
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_ASSERT_LE(ZL_Input_contentSize(inputs[n]), UINT8_MAX);
        inSizes[n] = (uint8_t)ZL_Input_contentSize(inputs[n]);
        totalSize += ZL_Input_contentSize(inputs[n]);
    }

    ZL_Encoder_sendCodecHeader(eictx, inSizes, arrSize);

    ZL_Output* const out = ZL_Encoder_createTypedStream(eictx, 0, totalSize, 1);
    ZL_ASSERT_NN(out);
    char* op = (char*)ZL_Output_ptr(out);

    for (size_t n = 0; n < nbInputs; n++) {
        size_t const size = ZL_Input_contentSize(inputs[n]);
        memcpy(op, ZL_Input_ptr(inputs[n]), size);
        op += size;
    }
    ZL_ERR_IF_ERR(ZL_Output_commit(out, totalSize));

    return ZL_returnSuccess();
}

#define MIT_CONCATSERIAL_ID 99 // any unused number is fine

static const ZL_MIGraphDesc mit_concatSerial_gd = {
    .CTid                = MIT_CONCATSERIAL_ID,
    .inputTypes          = (const ZL_Type[]){ ZL_Type_serial },
    .nbInputs            = 1,
    .lastInputIsVariable = 1,
    .soTypes             = (const ZL_Type[]){ ZL_Type_serial },
    .nbSOs               = 1,
};

static ZL_MIEncoderDesc const mit_concatSerial_desc = {
    .gd          = mit_concatSerial_gd,
    .transform_f = mit_concat_serial,
    .name        = "concatenate multiple serial inputs",
};

#define MIT_INVALID_0INPUT_ID 99912 // any unused number is fine

static const ZL_MIGraphDesc mit_invalid0Inputs_gd = {
    .CTid       = MIT_INVALID_0INPUT_ID,
    .inputTypes = (const ZL_Type[]){ ZL_Type_serial },
    .nbInputs   = 0,
    .soTypes    = (const ZL_Type[]){ ZL_Type_serial },
    .nbSOs      = 1,
};

static ZL_MIEncoderDesc const mit_invalid0Inputs_desc = {
    .gd          = mit_invalid0Inputs_gd,
    .transform_f = mit_concat_serial, // unimportant
    .name        = "Invalid Transform, defined with 0 inputs (for testing)",
};

/* ------   custom graphs   -------- */

// simpleGraph1 is a "classic" static graph, which only accepts 1 input
// used to test mit_copy transform
static ZL_GraphID simpleGraph1(ZL_Compressor* cgraph) noexcept
{
    // trivial graph: CustomTransform (copy) -> compress
    ZL_NodeID const copy_nid =
            ZL_Compressor_registerMIEncoder(cgraph, &mit_copy_desc);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, copy_nid, ZL_GRAPH_COMPRESS_GENERIC);
}

// dispatchToSimpleGraph1 is a dynamic graph which only accept multiple inputs
// compatible with ZS2_Type_Serial.
// It just dispatches each input to simpleGraph1.
static ZL_Report dispatchToSimpleGraph1(
        ZL_Graph* gctx,
        ZL_Edge* inputs[],
        size_t nbInputs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    ZL_GraphIDList gidl = ZL_Graph_getCustomGraphs(gctx);
    assert(gidl.nbGraphIDs == 1);
    assert(gidl.graphids != NULL);
    ZL_GraphID simpleGraph_withCopy = gidl.graphids[0];
    assert(nbInputs > 0);
    assert(inputs != NULL);
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_Edge* const input = inputs[n];
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(input, simpleGraph_withCopy));
    }
    return ZL_returnSuccess();
}

static ZL_GraphID register_dispatchToSimpleGraph1(
        ZL_Compressor* cgraph) noexcept
{
    ZL_GraphID graph_mit_copy = simpleGraph1(cgraph);

    ZL_Type const inputType                               = ZL_Type_serial;
    ZL_FunctionGraphDesc const dispatchToSimpleGraph1_dgd = {
        .name    = "dispatch inputs to simpleGraph1 (which uses mit_copy)",
        .graph_f = dispatchToSimpleGraph1,
        .inputTypeMasks      = &inputType,
        .nbInputs            = 1,
        .lastInputIsVariable = 1,
        .customGraphs        = &graph_mit_copy,
        .nbCustomGraphs      = 1,
    };

    return ZL_Compressor_registerFunctionGraph(
            cgraph, &dispatchToSimpleGraph1_dgd);
}

// MI Graph, concatenate 2 serial inputs, then compress them together
static ZL_GraphID concat2_graph(ZL_Compressor* cgraph) noexcept
{
    ZL_NodeID const concat2_nid =
            ZL_Compressor_registerMIEncoder(cgraph, &mit_concat2_desc);
    // Note: an MI Transform can be used as head of a static graph,
    // thus defining a new MI Graph.
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, concat2_nid, ZL_GRAPH_COMPRESS_GENERIC);
}

// MI Graph, concatenate multiple serial inputs, then compress them together
static ZL_GraphID concatSerial_graph(ZL_Compressor* cgraph) noexcept
{
    ZL_NodeID const concatSerial_nid =
            ZL_Compressor_registerMIEncoder(cgraph, &mit_concatSerial_desc);
    // Note: an MI Transform can be used as head of a static graph,
    // thus defining a new MI Graph. In this case, it's a VI (Variable Inputs)
    // Transform, so it defines a new VI Graph.
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, concatSerial_nid, ZL_GRAPH_COMPRESS_GENERIC);
}

static ZL_GraphID standardConcatSerial_graph(ZL_Compressor* cgraph) noexcept
{
    const ZL_GraphID successors[2] = { ZL_GRAPH_COMPRESS_GENERIC,
                                       ZL_GRAPH_COMPRESS_GENERIC };
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, ZL_NODE_CONCAT_SERIAL, successors, ARRAY_SIZE(successors));
}

static ZL_GraphID standardConcatNum_graph(ZL_Compressor* cgraph) noexcept
{
    const ZL_GraphID successors[2] = { ZL_GRAPH_COMPRESS_GENERIC,
                                       ZL_GRAPH_COMPRESS_GENERIC };
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, ZL_NODE_CONCAT_NUMERIC, successors, ARRAY_SIZE(successors));
}

static ZL_GraphID standardConcatStruct_graph(ZL_Compressor* cgraph) noexcept
{
    const ZL_GraphID successors[2] = { ZL_GRAPH_COMPRESS_GENERIC,
                                       ZL_GRAPH_COMPRESS_GENERIC };
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, ZL_NODE_CONCAT_STRUCT, successors, ARRAY_SIZE(successors));
}

static ZL_GraphID standardConcatString_graph(ZL_Compressor* cgraph) noexcept
{
    const ZL_GraphID successors[2] = { ZL_GRAPH_COMPRESS_GENERIC,
                                       ZL_GRAPH_COMPRESS_GENERIC };
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, ZL_NODE_CONCAT_STRING, successors, ARRAY_SIZE(successors));
}

// Variable Input Graph, deduplicate multiple identical numeric inputs, then
// compress the remaining one
static ZL_GraphID dedupNum_graph(ZL_Compressor* cgraph) noexcept
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_DEDUP_NUMERIC, ZL_GRAPH_COMPRESS_GENERIC);
}

// Example dispatch graph, accepts 5 inputs, redirects them to 3 outputs
// grouping 0-1 and 2-3 using concat2
static ZL_Report
dispatch5Inputs(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    ZL_NodeIDList nidl = ZL_Graph_getCustomNodes(gctx);
    assert(nidl.nbNodeIDs == 1);
    assert(nidl.nodeids != NULL);
    ZL_NodeID concat2 = nidl.nodeids[0];
    assert(nbInputs == 5);
    assert(inputs != NULL);

    ZL_TRY_LET(ZL_EdgeList, c1, ZL_Edge_runMultiInputNode(inputs, 2, concat2));
    ZL_TRY_LET(
            ZL_EdgeList, c2, ZL_Edge_runMultiInputNode(inputs + 2, 2, concat2));
    assert(c1.nbEdges == 1);
    assert(c2.nbEdges == 1);

    ZL_ERR_IF_ERR(
            ZL_Edge_setDestination(c1.edges[0], ZL_GRAPH_COMPRESS_GENERIC));
    ZL_ERR_IF_ERR(
            ZL_Edge_setDestination(c2.edges[0], ZL_GRAPH_COMPRESS_GENERIC));
    ZL_ERR_IF_ERR(ZL_Edge_setDestination(inputs[4], ZL_GRAPH_COMPRESS_GENERIC));

    return ZL_returnSuccess();
}

static ZL_GraphID register_dispatch5Inputs(ZL_Compressor* cgraph) noexcept
{
    ZL_NodeID const concat2_nid =
            ZL_Compressor_registerMIEncoder(cgraph, &mit_concat2_desc);

    ZL_Type const inputTypes[] = {
        ZL_Type_serial, ZL_Type_serial, ZL_Type_serial,
        ZL_Type_serial, ZL_Type_serial,
    };
    ZL_FunctionGraphDesc const dispatch5Inputs_dgd = {
        .name           = "dispatch 5 inputs into 3 outputs, via 2 concat2",
        .graph_f        = dispatch5Inputs,
        .inputTypeMasks = inputTypes,
        .nbInputs       = 5,
        .lastInputIsVariable = 0,
        .customNodes         = &concat2_nid,
        .nbCustomNodes       = 1,
    };

    return ZL_Compressor_registerFunctionGraph(cgraph, &dispatch5Inputs_dgd);
}

// Concat4, organized as 2 levels of concat2,
// tests multi-levels MI Transforms.
static ZL_Report
concat4_as2x2(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    ZL_NodeIDList nidl = ZL_Graph_getCustomNodes(gctx);
    assert(nidl.nbNodeIDs == 1);
    assert(nidl.nodeids != NULL);
    ZL_NodeID concat2 = nidl.nodeids[0];
    assert(nbInputs == 4);
    assert(inputs != NULL);

    ZL_TRY_LET(
            ZL_EdgeList, l1_0, ZL_Edge_runMultiInputNode(inputs, 2, concat2));
    ZL_TRY_LET(
            ZL_EdgeList,
            l1_1,
            ZL_Edge_runMultiInputNode(inputs + 2, 2, concat2));
    assert(l1_0.nbEdges == 1);
    assert(l1_1.nbEdges == 1);

    ZL_Edge* l1s[2] = { l1_0.edges[0], l1_1.edges[0] };

    ZL_TRY_LET(ZL_EdgeList, l2_0, ZL_Edge_runMultiInputNode(l1s, 2, concat2));
    assert(l2_0.nbEdges == 1);

    ZL_ERR_IF_ERR(
            ZL_Edge_setDestination(l2_0.edges[0], ZL_GRAPH_COMPRESS_GENERIC));

    return ZL_returnSuccess();
}

static ZL_GraphID register_concat4(ZL_Compressor* cgraph) noexcept
{
    ZL_NodeID const concat2_nid =
            ZL_Compressor_registerMIEncoder(cgraph, &mit_concat2_desc);

    ZL_Type const inputTypes[] = {
        ZL_Type_serial,
        ZL_Type_serial,
        ZL_Type_serial,
        ZL_Type_serial,
    };
    ZL_FunctionGraphDesc const concat4_dgd = {
        .name                = "concat4, delivered as 2 layers of concat2",
        .graph_f             = concat4_as2x2,
        .inputTypeMasks      = inputTypes,
        .nbInputs            = 4,
        .lastInputIsVariable = 0,
        .customNodes         = &concat2_nid,
        .nbCustomNodes       = 1,
    };

    return ZL_Compressor_registerFunctionGraph(cgraph, &concat4_dgd);
}

/* fake selector, just used for registration test (not really used) */
ZL_GraphID fakeSelector(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs) noexcept
{
    (void)selCtx;
    (void)inputStream;
    (void)customGraphs;
    (void)nbCustomGraphs;
    return ZL_GRAPH_COMPRESS_GENERIC;
}

static ZL_GraphID register_invalidSelectorSuccessor(
        ZL_Compressor* cgraph) noexcept
{
    const ZL_GraphID concat2 = concat2_graph(cgraph);

    const ZL_SelectorDesc kWrongSelectorSuccessor_desc = {
        .selector_f     = fakeSelector,
        .customGraphs   = &concat2,
        .nbCustomGraphs = 1,
        .name = "Selector incorrectly registered with an MI Successor",
    };

    return ZL_Compressor_registerSelectorGraph(
            cgraph, &kWrongSelectorSuccessor_desc);
}

static ZL_GraphID invalidMISuccessor_graph(ZL_Compressor* cgraph) noexcept
{
    const size_t segmentSizes[] = { 50, 0 };
    const ZL_NodeID split2      = ZL_Compressor_registerSplitNode_withParams(
            cgraph, ZL_Type_serial, segmentSizes, 2);
    const ZL_GraphID concat2      = concat2_graph(cgraph);
    const ZL_GraphID successors[] = { concat2 };
    // Note: it's invalid to select an MIGraph as Successor.
    // the following declaration should fail.
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, split2, successors, ARRAY_SIZE(successors));
}

static ZL_GraphID invalid_0Inputs_MITransform_graph(
        ZL_Compressor* cgraph) noexcept
{
    ZL_NodeID const invalid0Inputs_nid =
            ZL_Compressor_registerMIEncoder(cgraph, &mit_invalid0Inputs_desc);
    EXPECT_FALSE(ZL_NodeID_isValid(invalid0Inputs_nid));
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, invalid0Inputs_nid, ZL_GRAPH_COMPRESS_GENERIC);
}

static ZL_GraphID invalid_concat2_but_1regen_graph(
        ZL_Compressor* cgraph) noexcept
{
    ZL_NodeID const concat2_nid = ZL_Compressor_registerMIEncoder(
            cgraph, &invalid_concat2_but_1regen_desc);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, concat2_nid, ZL_GRAPH_COMPRESS_GENERIC);
}

static ZL_GraphID invalid_concat2_but_3regens_graph(
        ZL_Compressor* cgraph) noexcept
{
    ZL_NodeID const concat2_nid = ZL_Compressor_registerMIEncoder(
            cgraph, &invalid_concat2_but_3regens_desc);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, concat2_nid, ZL_GRAPH_COMPRESS_GENERIC);
}

static ZL_GraphID invalid_concat2_but_decl3regens_graph(
        ZL_Compressor* cgraph) noexcept
{
    ZL_NodeID const concat2_nid = ZL_Compressor_registerMIEncoder(
            cgraph, &invalid_concat2_but_decl3regens_desc);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, concat2_nid, ZL_GRAPH_COMPRESS_GENERIC);
}

static ZL_GraphID invalid_concat2_but_decl1regen_graph(
        ZL_Compressor* cgraph) noexcept
{
    ZL_NodeID const concat2_nid = ZL_Compressor_registerMIEncoder(
            cgraph, &invalid_concat2_but_decl1regen_desc);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, concat2_nid, ZL_GRAPH_COMPRESS_GENERIC);
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

static ZL_Report compress(
        void* dst,
        size_t dstCapacity,
        const ZL_TypedRef* inputs[],
        size_t nbInputs,
        ZL_GraphFn graphf)
{
    ZL_Report r         = ZL_returnError(ZL_ErrorCode_GENERIC);
    ZL_CCtx* const cctx = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx);

    // CGraph setup
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    {
        ZL_Report const gssr = ZL_Compressor_initUsingGraphFn(cgraph, graphf);
        if (ZL_isError(gssr)) {
            r = gssr;
            goto _compress_clean;
        }
    }
    {
        ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx, cgraph);
        if (ZL_isError(rcgr)) {
            r = rcgr;
            goto _compress_clean;
        }
    }
    // Parameter setup
    ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
            cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    r = ZL_CCtx_compressMultiTypedRef(cctx, dst, dstCapacity, inputs, nbInputs);

_compress_clean:
    ZL_Compressor_free(cgraph);
    ZL_CCtx_free(cctx);
    return r;
}

/* ------ define custom decoder transforms ------- */

// expects to receive one input as a VOsrc
static ZL_Report mit_copy_decoder(
        ZL_Decoder* dictx,
        const ZL_Input* O1srcs[],
        size_t nbO1Srcs,
        const ZL_Input* VOsrcs[],
        size_t nbVOSrcs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    assert(nbO1Srcs == 0);
    assert(nbVOSrcs == 1);
    assert(VOsrcs != nullptr);
    for (size_t n = 0; n < nbVOSrcs; n++)
        assert(VOsrcs[n] != nullptr);
    for (size_t n = 0; n < nbVOSrcs; n++)
        assert(ZL_Input_type(O1srcs[n]) == ZL_Type_serial);

    const ZL_Input* in   = VOsrcs[0];
    size_t const dstSize = ZL_Input_contentSize(in);

    ZL_Output* const out = ZL_Decoder_createTypedStream(dictx, 0, dstSize, 1);
    ZL_ERR_IF_NULL(out, allocation);

    memcpy(ZL_Output_ptr(out), ZL_Input_ptr(in), dstSize);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, dstSize));

    return ZL_returnSuccess();
}

// custom decoder transform description
static ZL_MIDecoderDesc const mit_copy_DDesc = { .gd = mit_copy_gd,
                                                 .transform_f =
                                                         mit_copy_decoder,
                                                 .name = "mit_copy_decoder" };

// Decoder direction: 1 serial input => 2 serial outputs
static ZL_Report mit_concat2_decoder(
        ZL_Decoder* dictx,
        const ZL_Input* O1srcs[],
        size_t nbO1Srcs,
        const ZL_Input* VOsrcs[],
        size_t nbVOSrcs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    assert(nbO1Srcs == 1);
    assert(nbVOSrcs == 0);
    (void)VOsrcs;
    assert(O1srcs != nullptr);
    for (size_t n = 0; n < nbO1Srcs; n++)
        assert(O1srcs[n] != nullptr);
    for (size_t n = 0; n < nbO1Srcs; n++)
        assert(ZL_Input_type(O1srcs[n]) == ZL_Type_serial);

    const ZL_Input* in = O1srcs[0];
    size_t const size  = ZL_Input_contentSize(in);

    ZL_RBuffer header = ZL_Decoder_getCodecHeader(dictx);
    assert(header.size == 1);
    size_t const dstSize0 = (size_t)((const unsigned char*)header.start)[0];
    assert(dstSize0 <= size);
    size_t const dstSize1 = size - dstSize0;

    ZL_Output* const out0 = ZL_Decoder_createTypedStream(dictx, 0, dstSize0, 1);
    ZL_ERR_IF_NULL(out0, allocation);
    ZL_Output* const out1 = ZL_Decoder_createTypedStream(dictx, 1, dstSize1, 1);
    ZL_ERR_IF_NULL(out1, allocation);

    memcpy(ZL_Output_ptr(out0), ZL_Input_ptr(in), dstSize0);
    ZL_ERR_IF_ERR(ZL_Output_commit(out0, dstSize0));
    memcpy(ZL_Output_ptr(out1), ZL_Input_ptr(in), dstSize1);
    ZL_ERR_IF_ERR(ZL_Output_commit(out1, dstSize1));

    return ZL_returnSuccess();
}

static ZL_MIDecoderDesc const mit_concat2_DDesc = {
    .gd          = mit_concat2_gd,
    .transform_f = mit_concat2_decoder,
    .name        = "mit_concat2_decoder",
};

// Decoder direction: 1 serial input => XXX (runtime discovered) regenerated
// serial outputs
static ZL_Report mit_concatSerial_decoder(
        ZL_Decoder* dictx,
        const ZL_Input* O1srcs[],
        size_t nbO1Srcs,
        const ZL_Input* VOsrcs[],
        size_t nbVOSrcs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    assert(nbO1Srcs == 1);
    assert(nbVOSrcs == 0);
    (void)VOsrcs;
    assert(O1srcs != nullptr);
    for (size_t n = 0; n < nbO1Srcs; n++)
        assert(O1srcs[n] != nullptr);
    for (size_t n = 0; n < nbO1Srcs; n++)
        assert(ZL_Input_type(O1srcs[n]) == ZL_Type_serial);

    const ZL_Input* const in = O1srcs[0];
    size_t const srcSize     = ZL_Input_contentSize(in);

    ZL_RBuffer header  = ZL_Decoder_getCodecHeader(dictx);
    size_t const hSize = header.size;
    assert(hSize >= 1);
    const uint8_t* const regenSizes = (const uint8_t*)header.start;
    size_t const nbRegens           = hSize;

    size_t totalRegenSize = 0;
    for (size_t n = 0; n < nbRegens; n++) {
        totalRegenSize += regenSizes[n];
    }
    assert(totalRegenSize == srcSize);
    (void)totalRegenSize;
    (void)srcSize;

    const char* ip = (const char*)ZL_Input_ptr(in);
    for (size_t n = 0; n < nbRegens; n++) {
        size_t const dstSize = regenSizes[n];
        ZL_Output* const out =
                ZL_Decoder_createTypedStream(dictx, (int)n, dstSize, 1);
        ZL_ERR_IF_NULL(out, allocation);
        memcpy(ZL_Output_ptr(out), ip, dstSize);
        ip += dstSize;
        ZL_ERR_IF_ERR(ZL_Output_commit(out, dstSize));
    }

    return ZL_returnSuccess();
}

static ZL_MIDecoderDesc const mit_concatSerial_DDesc = {
    .gd          = mit_concatSerial_gd,
    .transform_f = mit_concatSerial_decoder,
    .name        = "mit_concatSerial_decoder",
};

// Error scenario: decoder set for 1 regen, but 2 declared in frame
static const ZL_MIGraphDesc invalid_concat2_but_1regen_decSide_gd = {
    .CTid       = INVALID_CONCAT2_BUT_1REGEN_ID,
    .inputTypes = (const ZL_Type[]){ ZL_Type_serial },
    .nbInputs   = 1,
    .soTypes    = (const ZL_Type[]){ ZL_Type_serial },
    .nbSOs      = 1,
};

static ZL_MIDecoderDesc const invalid_concat2_but_1regen_DDesc = {
    .gd          = invalid_concat2_but_1regen_decSide_gd,
    .transform_f = mit_concat2_decoder,
    .name        = "invalid concat2_but_1regen decoder",
};

// Error scenario: decoder set for 3 regens, but 2 declared in frame
static const ZL_MIGraphDesc invalid_concat2_but_3regens_decSide_gd = {
    .CTid = INVALID_CONCAT2_BUT_3REGENS_ID,
    .inputTypes =
            (const ZL_Type[]){ ZL_Type_serial, ZL_Type_serial, ZL_Type_serial },
    .nbInputs = 3,
    .soTypes  = (const ZL_Type[]){ ZL_Type_serial },
    .nbSOs    = 1,
};

static ZL_MIDecoderDesc const invalid_concat2_but_3regens_DDesc = {
    .gd          = invalid_concat2_but_3regens_decSide_gd,
    .transform_f = mit_concat2_decoder,
    .name        = "invalid concat2_but_3regens decoder",
};

// Invalid decoder: tries to regenerate 3 streams, but only 2 declared
static ZL_Report mit_concat2_but_decl3regens_decoder(
        ZL_Decoder* dictx,
        const ZL_Input* O1srcs[],
        size_t nbO1Srcs,
        const ZL_Input* VOsrcs[],
        size_t nbVOSrcs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    assert(nbO1Srcs == 1);
    assert(nbVOSrcs == 0);
    (void)VOsrcs;
    assert(O1srcs != nullptr);
    for (size_t n = 0; n < nbO1Srcs; n++)
        assert(O1srcs[n] != nullptr);
    for (size_t n = 0; n < nbO1Srcs; n++)
        assert(ZL_Input_type(O1srcs[n]) == ZL_Type_serial);

    const ZL_Input* in = O1srcs[0];
    size_t const size  = ZL_Input_contentSize(in);

    ZL_RBuffer header = ZL_Decoder_getCodecHeader(dictx);
    assert(header.size == 1);
    size_t const dstSize0 = (size_t)((const unsigned char*)header.start)[0];
    assert(dstSize0 <= size);
    size_t const dstSize1 = size - dstSize0;

    ZL_Output* const out0 = ZL_Decoder_createTypedStream(dictx, 0, dstSize0, 1);
    ZL_ERR_IF_NULL(out0, allocation);
    ZL_Output* const out1 = ZL_Decoder_createTypedStream(dictx, 1, dstSize1, 1);
    ZL_ERR_IF_NULL(out1, allocation);
    ZL_Output* const out2 = ZL_Decoder_createTypedStream(dictx, 2, dstSize0, 1);
    /* this should fail */
    ZL_ERR_IF_NULL(out2, allocation);

    memcpy(ZL_Output_ptr(out0), ZL_Input_ptr(in), dstSize0);
    ZL_ERR_IF_ERR(ZL_Output_commit(out0, dstSize0));
    memcpy(ZL_Output_ptr(out1), ZL_Input_ptr(in), dstSize1);
    ZL_ERR_IF_ERR(ZL_Output_commit(out1, dstSize1));

    return ZL_returnSuccess();
}

static ZL_MIDecoderDesc const mit_concat2_but_decl3regens_DDesc = {
    .gd          = invalid_concat2_but_decl3regens_gd,
    .transform_f = mit_concat2_but_decl3regens_decoder,
    .name        = "erroneous decoder: concat2, but tries to create 3 regens",
};

// Invalid decoder: regenerates only 1 stream, but 2 declared
static ZL_Report mit_concat2_but_decl1regen_decoder(
        ZL_Decoder* dictx,
        const ZL_Input* O1srcs[],
        size_t nbO1Srcs,
        const ZL_Input* VOsrcs[],
        size_t nbVOSrcs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    assert(nbO1Srcs == 1);
    assert(nbVOSrcs == 0);
    (void)VOsrcs;
    assert(O1srcs != nullptr);
    for (size_t n = 0; n < nbO1Srcs; n++)
        assert(O1srcs[n] != nullptr);
    for (size_t n = 0; n < nbO1Srcs; n++)
        assert(ZL_Input_type(O1srcs[n]) == ZL_Type_serial);

    const ZL_Input* in = O1srcs[0];
    size_t const size  = ZL_Input_contentSize(in);

    ZL_RBuffer header = ZL_Decoder_getCodecHeader(dictx);
    assert(header.size == 1);
    size_t const dstSize0 = (size_t)((const unsigned char*)header.start)[0];
    assert(dstSize0 <= size);
    (void)size;

    ZL_Output* const out0 = ZL_Decoder_createTypedStream(dictx, 0, dstSize0, 1);
    ZL_ERR_IF_NULL(out0, allocation);

    memcpy(ZL_Output_ptr(out0), ZL_Input_ptr(in), dstSize0);
    ZL_ERR_IF_ERR(ZL_Output_commit(out0, dstSize0));

    // This is erroneous, and should be detected by the decompression engine
    return ZL_returnSuccess();
}

static ZL_MIDecoderDesc const mit_concat2_but_decl1regen_DDesc = {
    .gd          = invalid_concat2_but_decl1regen_gd,
    .transform_f = mit_concat2_but_decl1regen_decoder,
    .name        = "erroneous decoder: concat2, but only 1 regen created",
};

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
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerMIDecoder(dctx, &mit_copy_DDesc));
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerMIDecoder(dctx, &mit_concat2_DDesc));
    ZL_REQUIRE_SUCCESS(
            ZL_DCtx_registerMIDecoder(dctx, &mit_concatSerial_DDesc));
    ZL_REQUIRE_SUCCESS(
            ZL_DCtx_registerMIDecoder(dctx, &invalid_concat2_but_1regen_DDesc));
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerMIDecoder(
            dctx, &invalid_concat2_but_3regens_DDesc));
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerMIDecoder(
            dctx, &mit_concat2_but_decl3regens_DDesc));
    ZL_REQUIRE_SUCCESS(
            ZL_DCtx_registerMIDecoder(dctx, &mit_concat2_but_decl1regen_DDesc));

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

static int roundTripSuccessTest(
        ZL_GraphFn graphf,
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
            compress(compressed, compressedBound, readOnly, nbInputs, graphf);
    EXPECT_EQ(ZL_isError(compressionReport), 0)
            << "Compression failed with code: "
            << ZL_ErrorCode_toString(ZL_errorCode(compressionReport)) << "\n";
    size_t const compressedSize = ZL_validResult(compressionReport);

    printf("compressed %zu input bytes from %zu inputs into %zu compressed bytes \n",
           totalSrcSize,
           nbInputs,
           compressedSize);

    size_t const nbOutputs = nbInputs;
    ZL_TypedBuffer** outputs =
            (ZL_TypedBuffer**)malloc(nbOutputs * sizeof(ZL_TypedBuffer*));
    for (size_t n = 0; n < nbOutputs; n++) {
        outputs[n] = ZL_TypedBuffer_create();
        assert(outputs[n]);
    }
    ZL_Report const decompressionReport =
            decompress(outputs, nbOutputs, compressed, compressedSize);
    EXPECT_EQ(ZL_isError(decompressionReport), 0)
            << "Decompression failed with code: "
            << ZL_ErrorCode_toString(ZL_errorCode(decompressionReport)) << "\n";
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

typedef int (*RunScenario)(
        ZL_GraphFn graphf,
        const InputDesc* inputs,
        size_t nbInputs,
        const char* testName);

static int genInt32Data(
        ZL_GraphFn graphf,
        const ZL_Type inputTypes[],
        size_t nbInputs,
        const char* testName,
        RunScenario run_f)
{
    // Generate test input
#define NB_INTS 31
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    std::vector<InputDesc> inDesc;
    inDesc.resize(nbInputs);
    for (size_t n = 0; n < nbInputs; n++) {
        inDesc[n] = (InputDesc){ input, sizeof(input), inputTypes[n] };
    }

    return run_f(graphf, inDesc.data(), nbInputs, testName);
}

/* ------   error tests   ------ */

static int cFailTest(
        ZL_GraphFn graphf,
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
            compress(compressed, compressedBound, readOnly, nbInputs, graphf);
    EXPECT_EQ(ZL_isError(compressionReport), 1)
            << "compression should have failed \n";

    ZL_ErrorCode errorCode = ZL_errorCode(compressionReport);
    printf("compression failed as expected (%u:%s) \n",
           errorCode,
           ZL_ErrorCode_toString(errorCode));

    // clean
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_TypedRef_free(typedInputs[n]);
    }
    free(readOnly);
    free(typedInputs);
    free(compressed);
    return 0;
}

static int dFailTest(
        ZL_GraphFn graphf,
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
            compress(compressed, compressedBound, readOnly, nbInputs, graphf);
    EXPECT_EQ(ZL_isError(compressionReport), 0) << "compression failed \n";
    size_t const compressedSize = ZL_validResult(compressionReport);

    const size_t nbOutputs = nbInputs;
    ZL_TypedBuffer** outputs =
            (ZL_TypedBuffer**)malloc(nbOutputs * sizeof(ZL_TypedBuffer*));
    for (size_t n = 0; n < nbOutputs; n++) {
        outputs[n] = ZL_TypedBuffer_create();
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

/* ------   exposed tests   ------ */

TEST(MITransforms, mit_copy_serial_1_input)
{
    const ZL_Type types[] = { ZL_Type_serial };

    (void)genInt32Data(
            register_dispatchToSimpleGraph1,
            types,
            ARRAY_SIZE(types),
            "MI Transform copy, just 1 serial input",
            roundTripSuccessTest);
}

TEST(MITransforms, mit_copy_serial_2_inputs)
{
    const ZL_Type types[] = { ZL_Type_serial, ZL_Type_serial };

    (void)genInt32Data(
            register_dispatchToSimpleGraph1,
            types,
            ARRAY_SIZE(types),
            "MI Transform copy, applied on 2 serial inputs",
            roundTripSuccessTest);
}

TEST(MITransforms, concat2)
{
    const ZL_Type types[] = { ZL_Type_serial, ZL_Type_serial };

    (void)genInt32Data(
            concat2_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenate 2 inputs, then compress",
            roundTripSuccessTest);
}

TEST(MITransforms, concatSerial_2_inputs)
{
    const ZL_Type types[] = { ZL_Type_serial, ZL_Type_serial };

    (void)genInt32Data(
            concatSerial_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenate 2 inputs using concatSerial VI Transform",
            roundTripSuccessTest);
}

TEST(MITransforms, concatSerial_3_inputs)
{
    const ZL_Type types[] = { ZL_Type_serial, ZL_Type_serial, ZL_Type_serial };

    (void)genInt32Data(
            concatSerial_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenate 3 inputs using concatSerial VI Transform",
            roundTripSuccessTest);
}

TEST(MITransforms, concatSerial_16_inputs)
{
    const ZL_Type types[] = {
        ZL_Type_serial, ZL_Type_serial, ZL_Type_serial, ZL_Type_serial,
        ZL_Type_serial, ZL_Type_serial, ZL_Type_serial, ZL_Type_serial,
        ZL_Type_serial, ZL_Type_serial, ZL_Type_serial, ZL_Type_serial,
        ZL_Type_serial, ZL_Type_serial, ZL_Type_serial, ZL_Type_serial,
    };

    (void)genInt32Data(
            concatSerial_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenate 16 inputs using concatSerial VI Transform",
            roundTripSuccessTest);
}

TEST(MITransforms, dispatch5)
{
    const ZL_Type types[] = { ZL_Type_serial,
                              ZL_Type_serial,
                              ZL_Type_serial,
                              ZL_Type_serial,
                              ZL_Type_serial };

    (void)genInt32Data(
            register_dispatch5Inputs,
            types,
            ARRAY_SIZE(types),
            "Dispatch 5 inputs into 3 outputs, via 2 concat2 MI transforms",
            roundTripSuccessTest);
}

TEST(MITransforms, standardDedupNum_2_inputs)
{
    const ZL_Type types[] = { ZL_Type_numeric, ZL_Type_numeric };

    (void)genInt32Data(
            dedupNum_graph,
            types,
            ARRAY_SIZE(types),
            "Deduplicate 2 identical numeric Inputs",
            roundTripSuccessTest);
}

TEST(MITransforms, standardDedupNum_7_inputs)
{
    const ZL_Type types[] = {
        ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric,
        ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric,
    };

    (void)genInt32Data(
            dedupNum_graph,
            types,
            ARRAY_SIZE(types),
            "Deduplicate 7 identical numeric Inputs",
            roundTripSuccessTest);
}

TEST(MITransforms, standardDedupNum_19_inputs)
{
    const ZL_Type types[] = {
        ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric,
        ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric,
        ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric,
        ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric,
        ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric,
    };

    (void)genInt32Data(
            dedupNum_graph,
            types,
            ARRAY_SIZE(types),
            "Deduplicate 19 identical numeric Inputs",
            roundTripSuccessTest);
}

TEST(MITransforms, concat4)
{
    const ZL_Type types[] = {
        ZL_Type_serial, ZL_Type_serial, ZL_Type_serial, ZL_Type_serial
    };

    (void)genInt32Data(
            register_concat4,
            types,
            ARRAY_SIZE(types),
            "concat4, delivered as 2 layers of concat2",
            roundTripSuccessTest);
}

std::vector<ZL_Type> createArrayOfTypes(size_t size)
{
    std::vector<ZL_Type> types;
    types.reserve(size);
    const ZL_Type typeValues[] = {
        ZL_Type_serial,
        ZL_Type_struct,
        ZL_Type_numeric,
    };
    const size_t cycleLength = sizeof(typeValues) / sizeof(typeValues[0]);
    for (size_t i = 0; i < size; ++i) {
        types.push_back(typeValues[i % cycleLength]); // Cycle through the array
    }
    return types;
}

void roundTripTest(const char* name, ZL_GraphFn graph_f, size_t nbInputs)
{
    std::vector<ZL_Type> types    = createArrayOfTypes(nbInputs);
    const ZL_Type* const typesPtr = types.data();

    (void)genInt32Data(
            graph_f, typesPtr, types.size(), name, roundTripSuccessTest);
}

TEST(MITransforms, _2_types)
{
    roundTripTest(
            "MIT Transform copy, on 2 Inputs of various Types",
            register_dispatchToSimpleGraph1,
            2);
}

TEST(MITransforms, _4_types)
{
    roundTripTest(
            "MIT Transform copy, on 4 Inputs of various Types",
            register_dispatchToSimpleGraph1,
            4);
}

TEST(MITransforms, concatSerial_2_inputs_multiTypes)
{
    roundTripTest(
            "Concatenation of 2 Inputs of various Types",
            concatSerial_graph,
            2);
}

TEST(MITransforms, concatSerial_256_inputs)
{
    roundTripTest(
            "Concatenation of 256 Inputs of various Types",
            concatSerial_graph,
            256);
}

TEST(MITransforms, standardConcatSerial_256_inputs)
{
    roundTripTest(
            "Concatenation of 256 Inputs, using Standard concat_serial Transform",
            standardConcatSerial_graph,
            256);
}

TEST(MITransforms, standardConcatNum_1_inputs)
{
    ZL_Type types[1] = { ZL_Type_numeric };
    (void)genInt32Data(
            standardConcatStruct_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenation of 1 Inputs, using Standard concat_num Transform",
            roundTripSuccessTest);
}

TEST(MITransforms, standardConcatNum_4_inputs)
{
    ZL_Type types[4] = {
        ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric, ZL_Type_numeric
    };

    (void)genInt32Data(
            standardConcatStruct_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenation of 4 Inputs, using Standard concat_num Transform",
            roundTripSuccessTest);
}

TEST(MITransforms, standardConcatNum_256_inputs)
{
    ZL_Type types[256];
    for (size_t i = 0; i < ARRAY_SIZE(types); ++i) {
        types[i] = ZL_Type_numeric;
    }

    (void)genInt32Data(
            standardConcatStruct_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenation of 256 Inputs, using Standard concat_num Transform",
            roundTripSuccessTest);
}

TEST(MITransforms, standardConcatStruct_1_inputs)
{
    ZL_Type types[1] = { ZL_Type_struct };

    (void)genInt32Data(
            standardConcatStruct_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenation of 1 Inputs, using Standard concat_struct Transform",
            roundTripSuccessTest);
}

TEST(MITransforms, standardConcatStruct_4_inputs)
{
    ZL_Type types[4] = {
        ZL_Type_struct, ZL_Type_struct, ZL_Type_struct, ZL_Type_struct
    };

    (void)genInt32Data(
            standardConcatStruct_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenation of 4 Inputs, using Standard concat_struct Transform",
            roundTripSuccessTest);
}

TEST(MITransforms, standardConcatStruct_256_inputs)
{
    ZL_Type types[256];
    for (size_t i = 0; i < ARRAY_SIZE(types); ++i) {
        types[i] = ZL_Type_struct;
    }

    (void)genInt32Data(
            standardConcatStruct_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenation of 256 Inputs, using Standard concat_struct Transform",
            roundTripSuccessTest);
}

TEST(MITransforms, standardConcatString_1_inputs)
{
    ZL_Type types[1] = { ZL_Type_string };

    (void)genInt32Data(
            standardConcatString_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenation of 1 inputs, using Standard concat_string Transform",
            roundTripSuccessTest);
}

TEST(MITransforms, standardConcatString_4_inputs)
{
    ZL_Type types[4] = {
        ZL_Type_string, ZL_Type_string, ZL_Type_string, ZL_Type_string
    };

    (void)genInt32Data(
            standardConcatString_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenation of 4 Inputs, using Standard concat_string Transform",
            roundTripSuccessTest);
}

TEST(MITransforms, standardConcatString_256_inputs)
{
    ZL_Type types[256];
    for (size_t i = 0; i < ARRAY_SIZE(types); ++i) {
        types[i] = ZL_Type_string;
    }

    (void)genInt32Data(
            standardConcatString_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenation of 256 Inputs, using Standard concat_string Transform",
            roundTripSuccessTest);
}

TEST(MITransforms, standardConcatNum__mixedInputs)
{
    ZL_Type types[2] = { ZL_Type_numeric, ZL_Type_serial };

    (void)genInt32Data(
            standardConcatNum_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenation of 2 Inputs, using Standard concat_num Transform",
            cFailTest);
}

TEST(MITransforms, standardConcatSerial__mixedInputs)
{
    ZL_Type types[2] = { ZL_Type_numeric, ZL_Type_serial };

    (void)genInt32Data(
            standardConcatSerial_graph,
            types,
            ARRAY_SIZE(types),
            "Concatenation of 2 Inputs, using Standard concat_serial Transform",
            roundTripSuccessTest);
}

TEST(MITransforms, concatSerial_max_inputs)
{
    roundTripTest(
            "Concatenation of Maximum Nb of Inputs of various Types",
            concatSerial_graph,
            ZL_ENCODER_INPUT_LIMIT);
}

/* failure scenarios */

TEST(MITransforms, _invalid_transform_0Inputs)
{
    const ZL_Type types[] = { ZL_Type_serial };

    (void)genInt32Data(
            invalid_0Inputs_MITransform_graph,
            types,
            ARRAY_SIZE(types),
            "Attempting to register an Invalid MI Transform with 0 inputs => should fail",
            cFailTest);
}

TEST(MITransforms, _too_few_Inputs)
{
    const ZL_Type types[] = { ZL_Type_serial };

    (void)genInt32Data(
            concat2_graph,
            types,
            ARRAY_SIZE(types),
            "Only 1 input provided for concat2 => should fail",
            cFailTest);
}

TEST(MITransforms, _too_many_Inputs)
{
    const ZL_Type types[] = { ZL_Type_serial, ZL_Type_serial, ZL_Type_serial };

    (void)genInt32Data(
            concat2_graph,
            types,
            ARRAY_SIZE(types),
            "3 inputs provided for concat2 => should fail",
            cFailTest);
}

TEST(MITransforms, concatSerial_too_many_inputs)
{
    std::vector<ZL_Type> types = createArrayOfTypes(ZL_ENCODER_INPUT_LIMIT + 1);
    const ZL_Type* const typesPtr = types.data();

    char buffer[100] = { 0 };
    int written      = std::snprintf(
            buffer,
            sizeof(buffer),
            "Request concatenation of too many Inputs => should fail");
    assert(written > 0);
    (void)written;
    std::string formattedString(buffer); // Initialize directly from buffer

    (void)genInt32Data(
            concatSerial_graph,
            typesPtr,
            types.size(),
            formattedString.c_str(),
            cFailTest);
}

TEST(MITransforms, _MIGraph_invalidSuccessor)
{
    const ZL_Type types[] = { ZL_Type_serial };

    (void)genInt32Data(
            invalidMISuccessor_graph,
            types,
            ARRAY_SIZE(types),
            "declaring concat2 as a successor for a Static Graph => should fail",
            cFailTest);
}

TEST(MITransforms, _Selector_invalidSuccessor)
{
    const ZL_Type types[] = { ZL_Type_serial };

    (void)genInt32Data(
            register_invalidSelectorSuccessor,
            types,
            ARRAY_SIZE(types),
            "declaring concat2 as a successor for a Static Graph => should fail",
            cFailTest);
}

TEST(MITransforms, _DedupNum_invalidInputType)
{
    const ZL_Type types[] = { ZL_Type_numeric,
                              ZL_Type_serial,
                              ZL_Type_numeric };

    (void)genInt32Data(
            dedupNum_graph,
            types,
            ARRAY_SIZE(types),
            "dedup_num but some inputs are not numeric => should fail",
            cFailTest);
}

TEST(MITransforms, _too_many_regens)
{
    const ZL_Type types[] = { ZL_Type_serial, ZL_Type_serial };

    (void)genInt32Data(
            invalid_concat2_but_1regen_graph,
            types,
            ARRAY_SIZE(types),
            "decoder set for 1 regen, but 2 regens declared in frame => should fail",
            dFailTest);
}

TEST(MITransforms, _not_enough_regens)
{
    const ZL_Type types[] = { ZL_Type_serial, ZL_Type_serial };

    (void)genInt32Data(
            invalid_concat2_but_3regens_graph,
            types,
            ARRAY_SIZE(types),
            "decoder set for 3 regens, but 2 regens declared in frame => should fail",
            dFailTest);
}

TEST(MITransforms, _declare_too_many_regens)
{
    const ZL_Type types[] = { ZL_Type_serial, ZL_Type_serial };

    (void)genInt32Data(
            invalid_concat2_but_decl3regens_graph,
            types,
            ARRAY_SIZE(types),
            "decoder attempts to create 3 regens, but only 2 defined => should fail",
            dFailTest);
}

TEST(MITransforms, _declare_not_enough_regens)
{
    const ZL_Type types[] = { ZL_Type_serial, ZL_Type_serial };

    (void)genInt32Data(
            invalid_concat2_but_decl1regen_graph,
            types,
            ARRAY_SIZE(types),
            "decoder creates only 1 regen, but 2 defined => should fail",
            dFailTest);
}

TEST(MITransforms, _dedup_not_identical)
{
    static const size_t nbInputs = 2;
    static const size_t nbInts   = 31;

    // Generate test inputs
    int input[nbInts * nbInputs];
    for (size_t i = 0; i < nbInts * nbInputs; i++)
        input[i] = (int)i;

    std::vector<InputDesc> inDesc;
    inDesc.resize(nbInputs);
    for (size_t n = 0; n < nbInputs; n++) {
        inDesc[n] = (InputDesc){ input + nbInts * n,
                                 nbInts * sizeof(int),
                                 ZL_Type_numeric };
    }

    cFailTest(
            dedupNum_graph,
            inDesc.data(),
            nbInputs,
            "attempt dedup on non-identical inputs");
}

#if 0
// Note(@Cyan) : this test cannot be run in CI, because it triggers an `abort()`
// signal. This may change in the future, if the behavior is updated. It's still
// useful to keep this test around, to be run manually: it checks the tested
// property, aka, this registration is erroneous, so it *must* break.
TEST(MITransforms, _invalid_dTransform_0Inputs)
{
    static ZL_DCtx* dctx = NULL;
    if (dctx == NULL) {
        dctx = ZL_DCtx_create();
        ZL_REQUIRE_NN(dctx);
    }

    ZL_MIDecoderDesc const dTransform_invalid_0Inputs_desc = {
        .gd          = mit_invalid0Inputs_gd,
        .transform_f = mit_concatSerial_decoder /* unimportant */,
        .name        = "erroneous decoder: defined with 0 inputs",
    };

    // Expected to trigger a REQUIRE (abort())
    ZL_DCtx_registerMIDecoder(dctx, &dTransform_invalid_0Inputs_desc);
}
#endif

} // namespace
