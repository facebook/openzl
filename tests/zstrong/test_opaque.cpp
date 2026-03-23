// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_reflection.h"
#include "openzl/zl_selector.h"

#include "tests/utils.h"

using namespace ::testing;

class OpaqueTest : public ::testing::Test {
   public:
    void SetUp() override
    {
        compressor_ = ZL_Compressor_create();
        cctx_       = ZL_CCtx_create();
        dctx_       = ZL_DCtx_create();
    }

    void TearDown() override
    {
        ZL_Compressor_free(compressor_);
        compressor_ = nullptr;
        ZL_CCtx_free(cctx_);
        cctx_ = nullptr;
        ZL_DCtx_free(dctx_);
        dctx_ = nullptr;
    }

    void testRoundTrip(ZL_GraphID graph)
    {
        ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
                compressor_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
        ZL_REQUIRE_SUCCESS(
                ZL_Compressor_selectStartingGraphID(compressor_, graph));
        ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, compressor_));
        std::string data(10000, 'a');
        std::string compressed(ZL_compressBound(data.size()), '\0');
        auto report = ZL_CCtx_compress(
                cctx_,
                compressed.data(),
                compressed.size(),
                data.data(),
                data.size());
        ZL_REQUIRE_SUCCESS(report);
        std::string roundTripped(10000, 'b');
        report = ZL_DCtx_decompress(
                dctx_,
                roundTripped.data(),
                roundTripped.size(),
                compressed.data(),
                ZL_validResult(report));
        ZL_REQUIRE_SUCCESS(report);
        ZL_REQUIRE_EQ(ZL_validResult(report), data.size());
        ZL_REQUIRE(data == roundTripped);
    }

   protected:
    ZL_Compressor* compressor_ = nullptr;
    ZL_CCtx* cctx_             = nullptr;
    ZL_DCtx* dctx_             = nullptr;
};

static void freeWrapper(void* state, void* ptr) noexcept
{
    ZL_REQUIRE_NULL(state);
    free(ptr);
}

TEST_F(OpaqueTest, NullFreeIsNoOp)
{
    const char* ptr      = "0123456789";
    ZL_SelectorDesc desc = {
        .selector_f =
                [](const ZL_Selector* selector,
                   const ZL_Input*,
                   const ZL_GraphID*,
                   size_t) noexcept {
                    const char* opaque =
                            (const char*)ZL_Selector_getOpaquePtr(selector);
                    ZL_REQUIRE_NN(opaque);
                    ZL_REQUIRE(std::string(opaque) == "0123456789");
                    return ZL_GRAPH_STORE;
                },
        .inStreamType = ZL_Type_serial,
        .opaque       = { const_cast<void*>((const void*)ptr), NULL, NULL }
    };
    auto graph = ZL_Compressor_registerSelectorGraph(compressor_, &desc);
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);
    testRoundTrip(graph);
}

TEST_F(OpaqueTest, FreeIsLambada)
{
    char* ptr = new char[11];
    memcpy(ptr, "0123456789", 11);
    int count            = 0;
    ZL_SelectorDesc desc = {
        .selector_f =
                [](const ZL_Selector* selector,
                   const ZL_Input*,
                   const ZL_GraphID*,
                   size_t) noexcept {
                    const char* opaque =
                            (const char*)ZL_Selector_getOpaquePtr(selector);
                    ZL_REQUIRE_NN(opaque);
                    ZL_REQUIRE(std::string(opaque) == "0123456789");
                    return ZL_GRAPH_STORE;
                },
        .inStreamType = ZL_Type_serial,
        .opaque       = { (void*)ptr,
                          &count,
                          [](void* countPtr, void* owned) noexcept {
                        delete[] (char*)owned;
                        ++*(int*)countPtr;
                    } }
    };
    auto graph = ZL_Compressor_registerSelectorGraph(compressor_, &desc);
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);
    testRoundTrip(graph);
    ASSERT_EQ(count, 0);
    ZL_Compressor_free(compressor_);
    compressor_ = nullptr;
    ASSERT_EQ(count, 1);
}

TEST_F(OpaqueTest, ValidSelectorGraph)
{
    char* ptr = (char*)malloc(11);
    memcpy(ptr, "0123456789", 11);
    ZL_SelectorDesc desc = {
        .selector_f =
                [](const ZL_Selector* selector,
                   const ZL_Input*,
                   const ZL_GraphID*,
                   size_t) noexcept {
                    const char* opaque =
                            (const char*)ZL_Selector_getOpaquePtr(selector);
                    ZL_REQUIRE_NN(opaque);
                    ZL_REQUIRE(std::string(opaque) == "0123456789");
                    return ZL_GRAPH_STORE;
                },
        .inStreamType = ZL_Type_serial,
        .opaque       = { ptr, NULL, freeWrapper }
    };
    auto graph = ZL_Compressor_registerSelectorGraph(compressor_, &desc);
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);
    testRoundTrip(graph);
}

TEST_F(OpaqueTest, InvalidSelectorGraph)
{
    char* ptr = (char*)malloc(11);
    memcpy(ptr, "0123456789", 11);
    ZL_GraphID successor = ZL_GRAPH_FIELD_LZ;
    ZL_SelectorDesc desc = {
        .selector_f =
                [](const ZL_Selector* selector,
                   const ZL_Input*,
                   const ZL_GraphID*,
                   size_t) noexcept {
                    const char* opaque =
                            (const char*)ZL_Selector_getOpaquePtr(selector);
                    ZL_REQUIRE_NN(opaque);
                    ZL_REQUIRE(std::string(opaque) == "0123456789");
                    return ZL_GRAPH_STORE;
                },
        .inStreamType   = ZL_Type_serial,
        .customGraphs   = &successor,
        .nbCustomGraphs = 1,
        .opaque         = { ptr, NULL, freeWrapper },
    };
    auto graph = ZL_Compressor_registerSelectorGraph(compressor_, &desc);
    ASSERT_EQ(graph, ZL_GRAPH_ILLEGAL);
}

TEST_F(OpaqueTest, ValidFunctionGraph)
{
    char* ptr = (char*)malloc(11);
    memcpy(ptr, "0123456789", 11);
    ZL_Type type              = ZL_Type_serial;
    ZL_FunctionGraphDesc desc = {
        .graph_f =
                [](ZL_Graph* graph,
                   ZL_Edge* edges[],
                   size_t numEdges) noexcept {
                    const char* opaque =
                            (const char*)ZL_Graph_getOpaquePtr(graph);
                    ZL_REQUIRE_NN(opaque);
                    ZL_REQUIRE(std::string(opaque) == "0123456789");

                    for (size_t i = 0; i < numEdges; ++i) {
                        ZL_REQUIRE_SUCCESS(ZL_Edge_setDestination(
                                edges[i], ZL_GRAPH_STORE));
                    }

                    return ZL_returnSuccess();
                },
        .inputTypeMasks = &type,
        .nbInputs       = type,
        .opaque         = { ptr, NULL, freeWrapper },
    };
    auto graph = ZL_Compressor_registerFunctionGraph(compressor_, &desc);
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);
    testRoundTrip(graph);
}

TEST_F(OpaqueTest, InvalidFunctionGraph)
{
    char* ptr = (char*)malloc(11);
    memcpy(ptr, "0123456789", 11);
    ZL_Type type              = ZL_Type_serial;
    ZL_FunctionGraphDesc desc = {
        .graph_f =
                [](ZL_Graph* graph,
                   ZL_Edge* edges[],
                   size_t numEdges) noexcept {
                    const char* opaque =
                            (const char*)ZL_Graph_getOpaquePtr(graph);
                    ZL_REQUIRE_NN(opaque);
                    ZL_REQUIRE(std::string(opaque) == "0123456789");

                    for (size_t i = 0; i < numEdges; ++i) {
                        ZL_REQUIRE_SUCCESS(ZL_Edge_setDestination(
                                edges[i], ZL_GRAPH_STORE));
                    }

                    return ZL_returnSuccess();
                },
        .validate_f = [](auto, auto) noexcept { return 0; },
        .inputTypeMasks = &type,
        .nbInputs       = type,
        .opaque         = { ptr, NULL, freeWrapper },
    };
    auto graph = ZL_Compressor_registerFunctionGraph(compressor_, &desc);
    ASSERT_EQ(graph, ZL_GRAPH_ILLEGAL);
}

static ZL_NodeID registerTypedEncoder(ZL_Compressor* compressor_)
{
    char* ptr = (char*)malloc(11);
    memcpy(ptr, "0123456789", 11);
    ZL_Type type                = ZL_Type_serial;
    ZL_TypedGraphDesc graphDesc = {
        .CTid           = 0,
        .inStreamType   = ZL_Type_serial,
        .outStreamTypes = &type,
        .nbOutStreams   = 1,
    };
    ZL_TypedEncoderDesc encodeDesc = {
        .gd = graphDesc,
        .transform_f =
                [](ZL_Encoder* encoder, const ZL_Input* input) noexcept {
                    const char* opaque =
                            (const char*)ZL_Encoder_getOpaquePtr(encoder);
                    ZL_REQUIRE_NN(opaque);
                    ZL_REQUIRE(std::string(opaque) == "0123456789");

                    auto* out = ZL_Encoder_createTypedStream(
                            encoder, 0, ZL_Input_numElts(input), 1);
                    ZL_REQUIRE_NN(out);
                    memcpy(ZL_Output_ptr(out),
                           ZL_Input_ptr(input),
                           ZL_Input_numElts(input));

                    ZL_REQUIRE_SUCCESS(
                            ZL_Output_commit(out, ZL_Input_numElts(input)));

                    return ZL_returnSuccess();
                },
        .opaque = { ptr, NULL, freeWrapper },
    };
    auto node = ZL_Compressor_registerTypedEncoder(compressor_, &encodeDesc);
    EXPECT_NE(node, ZL_NODE_ILLEGAL);
    return node;
}

static void registerTypedDecoder(ZL_DCtx* dctx_)
{
    char* ptr = (char*)malloc(11);
    memcpy(ptr, "0123456789", 11);
    ZL_Type type                = ZL_Type_serial;
    ZL_TypedGraphDesc graphDesc = {
        .CTid           = 0,
        .inStreamType   = ZL_Type_serial,
        .outStreamTypes = &type,
        .nbOutStreams   = 1,
    };
    ZL_TypedDecoderDesc decodeDesc = {
        .gd = graphDesc,
        .transform_f =
                [](ZL_Decoder* decoder, const ZL_Input* inputs[]) noexcept {
                    const ZL_Input* input = inputs[0];
                    const char* opaque =
                            (const char*)ZL_Decoder_getOpaquePtr(decoder);
                    ZL_REQUIRE_NN(opaque);
                    ZL_REQUIRE(std::string(opaque) == "0123456789");

                    auto* out = ZL_Decoder_create1OutStream(
                            decoder, ZL_Input_numElts(input), 1);
                    ZL_REQUIRE_NN(out);
                    memcpy(ZL_Output_ptr(out),
                           ZL_Input_ptr(input),
                           ZL_Input_numElts(input));

                    ZL_REQUIRE_SUCCESS(
                            ZL_Output_commit(out, ZL_Input_numElts(input)));

                    return ZL_returnSuccess();
                },
        .opaque = { ptr, NULL, freeWrapper },
    };
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(dctx_, &decodeDesc));
}

TEST_F(OpaqueTest, TypedCodec)
{
    auto node  = registerTypedEncoder(compressor_);
    auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_, node, ZL_GRAPH_STORE);
    graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_, node, graph);

    registerTypedDecoder(dctx_);
    registerTypedDecoder(dctx_); // register twice to ensure no leak on double
    testRoundTrip(graph);
}

static void registerVODecoder(ZL_DCtx* dctx_)
{
    char* ptr = (char*)malloc(11);
    memcpy(ptr, "0123456789", 11);
    ZL_Type type             = ZL_Type_serial;
    ZL_VOGraphDesc graphDesc = {
        .CTid         = 0,
        .inStreamType = ZL_Type_serial,
        .voTypes      = &type,
        .nbVOs        = 1,
    };
    ZL_VODecoderDesc decodeDesc = {
        .gd = graphDesc,
        .transform_f =
                [](ZL_Decoder* decoder,
                   const ZL_Input*[],
                   size_t,
                   const ZL_Input* inputs[],
                   size_t numInputs) noexcept {
                    const ZL_Input* input = inputs[0];
                    const char* opaque =
                            (const char*)ZL_Decoder_getOpaquePtr(decoder);
                    ZL_REQUIRE_NN(opaque);
                    ZL_REQUIRE(std::string(opaque) == "0123456789");

                    auto* out = ZL_Decoder_create1OutStream(
                            decoder, ZL_Input_numElts(input), 1);
                    ZL_REQUIRE_NN(out);
                    memcpy(ZL_Output_ptr(out),
                           ZL_Input_ptr(input),
                           ZL_Input_numElts(input));

                    ZL_REQUIRE_SUCCESS(
                            ZL_Output_commit(out, ZL_Input_numElts(input)));

                    return ZL_returnSuccess();
                },
        .opaque = { ptr, NULL, freeWrapper },
    };
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerVODecoder(dctx_, &decodeDesc));
}

TEST_F(OpaqueTest, VariableOutputCodec)
{
    char* ptr = (char*)malloc(11);
    memcpy(ptr, "0123456789", 11);
    ZL_Type type             = ZL_Type_serial;
    ZL_VOGraphDesc graphDesc = {
        .CTid         = 0,
        .inStreamType = ZL_Type_serial,
        .voTypes      = &type,
        .nbVOs        = 1,
    };
    ZL_VOEncoderDesc encodeDesc = {
        .gd = graphDesc,
        .transform_f =
                [](ZL_Encoder* encoder, const ZL_Input* input) noexcept {
                    const char* opaque =
                            (const char*)ZL_Encoder_getOpaquePtr(encoder);
                    ZL_REQUIRE_NN(opaque);
                    ZL_REQUIRE(std::string(opaque) == "0123456789");

                    auto* out = ZL_Encoder_createTypedStream(
                            encoder, 0, ZL_Input_numElts(input), 1);
                    ZL_REQUIRE_NN(out);
                    memcpy(ZL_Output_ptr(out),
                           ZL_Input_ptr(input),
                           ZL_Input_numElts(input));

                    ZL_REQUIRE_SUCCESS(
                            ZL_Output_commit(out, ZL_Input_numElts(input)));

                    return ZL_returnSuccess();
                },
        .opaque = { ptr, NULL, freeWrapper },
    };

    auto node = ZL_Compressor_registerVOEncoder(compressor_, &encodeDesc);
    ASSERT_NE(node, ZL_NODE_ILLEGAL);
    auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_, node, ZL_GRAPH_STORE);
    graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_, node, graph);

    registerVODecoder(dctx_);
    registerVODecoder(dctx_); // register twice to ensure no leak on double
    testRoundTrip(graph);
}

static void registerMIDecoder(ZL_DCtx* dctx_)
{
    char* ptr = (char*)malloc(11);
    memcpy(ptr, "0123456789", 11);
    ZL_Type type             = ZL_Type_serial;
    ZL_MIGraphDesc graphDesc = {
        .CTid       = 0,
        .inputTypes = &type,
        .nbInputs   = 1,
        .soTypes    = &type,
        .nbSOs      = 1,
    };
    ZL_MIDecoderDesc decodeDesc = {
        .gd = graphDesc,
        .transform_f =
                [](ZL_Decoder* decoder,
                   const ZL_Input* inputs[],
                   size_t numInputs,
                   const ZL_Input*[],
                   size_t) noexcept {
                    const ZL_Input* input = inputs[0];
                    const char* opaque =
                            (const char*)ZL_Decoder_getOpaquePtr(decoder);
                    ZL_REQUIRE_NN(opaque);
                    ZL_REQUIRE(std::string(opaque) == "0123456789");

                    auto* out = ZL_Decoder_create1OutStream(
                            decoder, ZL_Input_numElts(input), 1);
                    ZL_REQUIRE_NN(out);
                    memcpy(ZL_Output_ptr(out),
                           ZL_Input_ptr(input),
                           ZL_Input_numElts(input));

                    ZL_REQUIRE_SUCCESS(
                            ZL_Output_commit(out, ZL_Input_numElts(input)));

                    return ZL_returnSuccess();
                },
        .opaque = { ptr, NULL, freeWrapper },
    };
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerMIDecoder(dctx_, &decodeDesc));
}

TEST_F(OpaqueTest, MultiInputCodec)
{
    char* ptr = (char*)malloc(11);
    memcpy(ptr, "0123456789", 11);
    ZL_Type type             = ZL_Type_serial;
    ZL_MIGraphDesc graphDesc = {
        .CTid       = 0,
        .inputTypes = &type,
        .nbInputs   = 1,
        .soTypes    = &type,
        .nbSOs      = 1,
    };
    ZL_MIEncoderDesc encodeDesc = {
        .gd = graphDesc,
        .transform_f =
                [](ZL_Encoder* encoder,
                   const ZL_Input* inputs[],
                   size_t numInputs) noexcept {
                    const ZL_Input* input = inputs[0];
                    const char* opaque =
                            (const char*)ZL_Encoder_getOpaquePtr(encoder);
                    ZL_REQUIRE_NN(opaque);
                    ZL_REQUIRE(std::string(opaque) == "0123456789");

                    auto* out = ZL_Encoder_createTypedStream(
                            encoder, 0, ZL_Input_numElts(input), 1);
                    ZL_REQUIRE_NN(out);
                    memcpy(ZL_Output_ptr(out),
                           ZL_Input_ptr(input),
                           ZL_Input_numElts(input));

                    ZL_REQUIRE_SUCCESS(
                            ZL_Output_commit(out, ZL_Input_numElts(input)));

                    return ZL_returnSuccess();
                },
        .opaque = { ptr, NULL, freeWrapper },
    };

    auto node = ZL_Compressor_registerMIEncoder(compressor_, &encodeDesc);
    ASSERT_NE(node, ZL_NODE_ILLEGAL);
    auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_, node, ZL_GRAPH_STORE);
    graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_, node, graph);

    registerMIDecoder(dctx_);
    registerMIDecoder(dctx_); // register twice to ensure no leak on double
    testRoundTrip(graph);
}

TEST_F(OpaqueTest, InvalidMultiInputEncoder)
{
    char* ptr = (char*)malloc(11);
    memcpy(ptr, "0123456789", 11);
    ZL_MIGraphDesc graphDesc = {
        .CTid       = 0,
        .inputTypes = NULL,
        .nbInputs   = 0,
        .soTypes    = NULL,
        .nbSOs      = 1,
    };
    ZL_MIEncoderDesc encodeDesc = {
        .gd = graphDesc,
        .transform_f =
                [](ZL_Encoder* encoder,
                   const ZL_Input* inputs[],
                   size_t numInputs) noexcept { return ZL_returnSuccess(); },
        .opaque                 = { ptr, NULL, freeWrapper },
    };

    auto node = ZL_Compressor_registerMIEncoder(compressor_, &encodeDesc);
    ASSERT_EQ(node, ZL_NODE_ILLEGAL);
}

TEST_F(OpaqueTest, InvalidMultiInputDecoder)
{
    char* ptr = (char*)malloc(11);
    memcpy(ptr, "0123456789", 11);
    ZL_MIGraphDesc graphDesc = {
        .CTid       = 0,
        .inputTypes = NULL,
        .nbInputs   = 0,
        .soTypes    = NULL,
        .nbSOs      = 0,
    };
    ZL_MIDecoderDesc decodeDesc = {
        .gd          = graphDesc,
        .transform_f = [](ZL_Decoder* decoder,
                          const ZL_Input* inputs[],
                          size_t numInputs,
                          const ZL_Input*[],
                          size_t) noexcept { return ZL_returnSuccess(); },
        .opaque                 = { ptr, NULL, freeWrapper },
    };
    auto report = ZL_DCtx_registerMIDecoder(dctx_, &decodeDesc);
    ZL_REQUIRE(ZL_isError(report));
    registerMIDecoder(dctx_); // Re-registering succeeds
}

TEST_F(OpaqueTest, ParameterizeGraphWithOpaque)
{
    char* ptr = (char*)malloc(11);
    memcpy(ptr, "0123456789", 11);
    ZL_SelectorDesc desc = {
        .selector_f =
                [](const ZL_Selector* selector,
                   const ZL_Input*,
                   const ZL_GraphID* graphs,
                   size_t numGraphs) noexcept {
                    const char* opaque =
                            (const char*)ZL_Selector_getOpaquePtr(selector);
                    ZL_REQUIRE_NN(opaque);
                    ZL_REQUIRE(std::string(opaque) == "0123456789");
                    ZL_REQUIRE_EQ(numGraphs, 1);
                    return graphs[0];
                },
        .inStreamType = ZL_Type_serial,
        .opaque       = { ptr, NULL, freeWrapper }
    };
    auto graph = ZL_Compressor_registerSelectorGraph(compressor_, &desc);
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);

    ZL_GraphID graphID                  = ZL_GRAPH_ZSTD;
    ZL_ParameterizedGraphDesc paramDesc = {
        .graph          = graph,
        .customGraphs   = &graphID,
        .nbCustomGraphs = 1,
    };

    graph = ZL_Compressor_registerParameterizedGraph(compressor_, &paramDesc);
    ASSERT_NE(graph, ZL_GRAPH_ILLEGAL);
    testRoundTrip(graph);
}
TEST_F(OpaqueTest, ParameterizeNodeWithOpaque)
{
    auto node  = registerTypedEncoder(compressor_);
    auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_, node, ZL_GRAPH_STORE);

    ZL_IntParam intParam = { 0, 0 };
    ZL_LocalParams params = {
        .intParams = {
            .intParams = &intParam,
            .nbIntParams = 1,
        },
    };
    const ZL_ParameterizedNodeDesc pndesc = {
        .node        = node,
        .localParams = &params,
    };
    node = ZL_Compressor_registerParameterizedNode(compressor_, &pndesc);

    graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_, node, graph);

    registerTypedDecoder(dctx_);
    testRoundTrip(graph);
}
