// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>
#include <cstring>

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/CParam.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/poly/Span.hpp"
#include "openzl/zl_compressor.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_segmenter.h"

using namespace ::testing;

namespace openzl {

// Param IDs for test
constexpr int MATERIALIZED_PARAM_ID = 200;

// Test data structures for materialization
struct MaterializedData {
    std::string message;
    void* exfiltratePtr;
};

struct RuntimeParams {
    const char* message;
    void* exfiltratePtr;
};

static ZL_Report
passthroughEncoder(ZL_Encoder* eictx, const ZL_Input* inputs[], size_t nbInputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);

    // Create the same number of outputs as inputs and copy them
    for (size_t i = 0; i < nbInputs; i++) {
        auto* input = inputs[i];
        ZL_ERR_IF_NULL(input, GENERIC);
        ZL_ERR_IF_NE(ZL_Input_type(input), ZL_Type_serial, GENERIC);
        auto* src    = ZL_Input_ptr(input);
        size_t n     = ZL_Input_numElts(input);
        auto* output = ZL_Encoder_createTypedStream(eictx, i, n, 1);
        ZL_ERR_IF_NULL(output, GENERIC);
        void* dst = ZL_Output_ptr(output);
        memcpy(dst, src, n);
        ZL_ERR_IF_ERR(ZL_Output_commit(output, n));
    }

    return ZL_returnSuccess();
}

class GraphTest : public Test {
   protected:
    void SetUp() override
    {
        nextCtid_ = 6000;
    }

    // Register a custom encoder with materialization
    ZL_NodeID registerCustomexfiltratingEncoder(
            ZL_MIEncoderFn transformFn,
            const ZL_LocalParams& localParams,
            const ZL_MaterializerDesc* materializer)
    {
        static ZL_Type typetype = ZL_Type_serial;
        ZL_MIGraphDesc graphDesc{
            .CTid                = nextCtid_++,
            .inputTypes          = &typetype,
            .nbInputs            = 1,
            .lastInputIsVariable = false,
            .soTypes             = &typetype,
            .nbSOs               = 1,
            .voTypes             = nullptr,
            .nbVOs               = 0,
        };

        ZL_MIEncoderDesc encoderDesc{
            .gd          = graphDesc,
            .transform_f = transformFn,
            .localParams = localParams,
            .name        = "graph_test_encoder",
        };
        if (materializer != nullptr) {
            encoderDesc.materializer = *materializer;
        }

        auto nodeid = compressor_.registerCustomEncoder(encoderDesc);
        EXPECT_NE(nodeid.nid, ZL_NODE_ILLEGAL.nid);
        return nodeid;
    }

    // Helper to compress data
    void compressData()
    {
        cctx_.refCompressor(compressor_);
        cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

        std::string src = "Testing graph materialization with custom encoders.";
        std::vector<char> dst(1000);
        size_t compressedSize = cctx_.compressSerial(
                poly::span<char>(dst.data(), dst.size()), src);
        (void)compressedSize; // Suppress unused variable warning
    }

    static ZL_RESULT_OF(ZL_VoidPtr) materializeGraphTestData(
            ZL_Materializer*,
            const ZL_LocalParams* params) ZL_NOEXCEPT_FUNC_PTR
    {
        ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, nullptr);
        auto* data = new MaterializedData();

        // Get the message from copy params
        ZL_ERR_IF_NE(params->copyParams.nbCopyParams, 1, GENERIC);
        auto cp = params->copyParams.copyParams[0];
        ZL_ERR_IF_NE(cp.paramId, 1, GENERIC);

        data->message = std::string((const char*)cp.paramPtr, cp.paramSize);

        // Get the exfiltrate pointer from ref params
        ZL_ERR_IF_NE(params->refParams.nbRefParams, 1, GENERIC);
        ZL_ERR_IF_NE(params->refParams.refParams[0].paramId, 2, GENERIC);
        data->exfiltratePtr =
                const_cast<void*>(params->refParams.refParams[0].paramRef);

        return ZL_WRAP_VALUE(data);
    }

    static void dematerializeGraphTestData(ZL_Materializer*, void* materialized)
            ZL_NOEXCEPT_FUNC_PTR
    {
        auto* data = static_cast<MaterializedData*>(materialized);
        delete data;
    }

    // Custom encoder that uses materialized params to exfiltrate data
    static ZL_Report exfiltratingEncoder(
            ZL_Encoder* eictx,
            const ZL_Input* inputs[],
            size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);

        // Access the materialized param
        MaterializedData* materialized = const_cast<MaterializedData*>(
                (const MaterializedData*)ZL_Encoder_getLocalParam(
                        eictx, MATERIALIZED_PARAM_ID)
                        .paramRef);
        ZL_ERR_IF_NULL(
                materialized,
                GENERIC,
                "Expected materialized object to be nonnull");

        // Exfiltrate data by copying message to the exfiltrate pointer
        char* exfiltrateLocation = (char*)materialized->exfiltratePtr;
        snprintf(exfiltrateLocation, 50, "%s", materialized->message.c_str());

        return passthroughEncoder(eictx, inputs, nbInputs);
    }

    static ZL_Report exfiltratingGraphFn(
            ZL_Graph* graph,
            ZL_Edge* inputs[],
            size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

        // Access the materialized param
        MaterializedData* materialized = const_cast<MaterializedData*>(
                (const MaterializedData*)ZL_Graph_getLocalRefParam(
                        graph, MATERIALIZED_PARAM_ID)
                        .paramRef);
        ZL_ERR_IF_NULL(
                materialized,
                GENERIC,
                "Expected materialized object to be nonnull");

        // Exfiltrate data by copying message to the exfiltrate pointer
        char* exfiltrateLocation = (char*)materialized->exfiltratePtr;
        snprintf(exfiltrateLocation, 50, "%s", materialized->message.c_str());

        // Send all inputs to STORE
        for (size_t i = 0; i < nbInputs; i++) {
            ZL_ERR_IF_ERR(ZL_Edge_setDestination(inputs[i], ZL_GRAPH_STORE));
        }

        return ZL_returnSuccess();
    }

    openzl::Compressor compressor_;
    openzl::CCtx cctx_;
    ZL_IDType nextCtid_;
    ZL_MaterializerDesc defaultMaterializer_ = {
        .materializeFn   = materializeGraphTestData,
        .dematerializeFn = dematerializeGraphTestData,
        .paramId         = MATERIALIZED_PARAM_ID,
    };
};

TEST_F(GraphTest,
       GIVENaFunctionGraphCallingCustomEncoderTwiceWHENmaterializingTHENdataIsExfiltrated)
{
    // Setup for first encoder call
    std::string message1 = "FirstCall";
    std::vector<char> exfiltrateBuffer(200, 0);

    ZL_CopyParam cp1 = {
        .paramId   = 1,
        .paramPtr  = message1.data(),
        .paramSize = message1.size(),
    };
    ZL_RefParam rp1 = {
        .paramId  = 2,
        .paramRef = exfiltrateBuffer.data(),
    };
    ZL_LocalParams lp1 = {
        .intParams = {
            .intParams   = nullptr,
            .nbIntParams = 0,
        },
        .copyParams = {
            .copyParams   = &cp1,
            .nbCopyParams = 1,
        },
        .refParams = {
            .refParams   = &rp1,
            .nbRefParams = 1,
        },
    };

    // Register encoder node once with initial local params
    ZL_NodeID encoderNode = registerCustomexfiltratingEncoder(
            exfiltratingEncoder, lp1, &defaultMaterializer_);

    // Setup parameters for second call via opaque pointer
    std::string message2        = "SecondCall";
    RuntimeParams runtimeParams = {
        .message       = message2.c_str(),
        .exfiltratePtr = exfiltrateBuffer.data() + 50,
    };

    // Function graph that calls the encoder twice
    auto graphFunction = [](ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
                                 ZL_NOEXCEPT_FUNC_PTR -> ZL_Report {
        ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
        ZL_ERR_IF_NE(nbInputs, 1, GENERIC);

        // Get the custom encoder node from the graph's custom nodes list
        ZL_NodeIDList customNodes = ZL_Graph_getCustomNodes(graph);
        ZL_ERR_IF_NE(customNodes.nbNodeIDs, 1, GENERIC);
        ZL_NodeID encNode = customNodes.nodeids[0];

        // Run first encoder node without custom params (uses registered local
        // params)
        ZL_TRY_LET(ZL_EdgeList, edges1, ZL_Edge_runNode(inputs[0], encNode));
        ZL_ERR_IF_NE(edges1.nbEdges, 1, GENERIC);

        // Get the opaque pointer which contains the params for the second call
        const RuntimeParams* rtp =
                (const RuntimeParams*)ZL_Graph_getOpaquePtr(graph);
        ZL_ERR_IF_NULL(rtp, GENERIC);

        // Setup custom local params for second call
        ZL_CopyParam cp2 = {
            .paramId   = 1,
            .paramPtr  = rtp->message,
            .paramSize = strlen(rtp->message),
        };
        ZL_RefParam rp2 = {
            .paramId  = 2,
            .paramRef = rtp->exfiltratePtr,
        };
        ZL_LocalParams lp2 = {
            .copyParams = {
                .copyParams   = &cp2,
                .nbCopyParams = 1,
            },
            .refParams = {
                .refParams   = &rp2,
                .nbRefParams = 1,
            },
        };

        // Run second encoder node with custom params (override)
        ZL_TRY_LET(
                ZL_EdgeList,
                edges2,
                ZL_Edge_runNode_withParams(edges1.edges[0], encNode, &lp2));
        ZL_ERR_IF_NE(edges2.nbEdges, 1, GENERIC);

        // Forward final output to STORE
        return ZL_Edge_setDestination(edges2.edges[0], ZL_GRAPH_STORE);
    };

    // Register the custom encoder node with the function graph
    ZL_NodeID customNodes[] = { encoderNode };

    static ZL_Type inputType = ZL_Type_serial;
    ZL_FunctionGraphDesc graphDesc = {
        .name           = "test_graph_with_two_encoder_calls",
        .graph_f        = graphFunction,
        .validate_f     = nullptr,
        .inputTypeMasks = &inputType,
        .nbInputs       = 1,
        .lastInputIsVariable = 0,
        .customGraphs   = nullptr,
        .nbCustomGraphs = 0,
        .customNodes    = customNodes,
        .nbCustomNodes  = 1,
        .localParams    = lp1,
        .materializer   = defaultMaterializer_,
        .opaque         = {
            .ptr = (void*)&runtimeParams,
            .freeOpaquePtr = nullptr,
            .freeFn = nullptr,
        },
    };

    auto graphId = compressor_.registerFunctionGraph(graphDesc);
    ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);
    compressor_.selectStartingGraph(graphId);

    compressData();

    // Verify that both encoder calls exfiltrated their data correctly
    std::string result1(exfiltrateBuffer.data(), 50);
    std::string result2(exfiltrateBuffer.data() + 50, 50);

    // First call uses the registered local params
    EXPECT_TRUE(result1.find("FirstCall") != std::string::npos)
            << "FirstCall message not found in: " << result1;

    // Second call uses the overridden local params from
    // ZL_Edge_runNode_withParams
    EXPECT_TRUE(result2.find("SecondCall") != std::string::npos)
            << "SecondCall message not found in: " << result2;
}

TEST_F(GraphTest,
       GIVENaSegmenterWithMaterializationWHENruntimeParamsSetTHENmaterializationHappens)
{
    // Setup test data
    std::vector<char> exfiltrateBuffer(200, 0);

    std::string message = "SegmenterTest";
    ZL_CopyParam cp     = {
            .paramId   = 1,
            .paramPtr  = message.data(),
            .paramSize = message.size(),
    };
    ZL_RefParam rp = {
        .paramId  = 2,
        .paramRef = exfiltrateBuffer.data(),
    };
    ZL_LocalParams localParams = {
        .intParams = {
            .intParams   = nullptr,
            .nbIntParams = 0,
        },
        .copyParams = {
            .copyParams   = &cp,
            .nbCopyParams = 1,
        },
        .refParams = {
            .refParams   = &rp,
            .nbRefParams = 1,
        },
    };

    // Define the segmenter function
    auto segmenterFunction = [](ZL_Segmenter* sictx) ZL_NOEXCEPT_FUNC_PTR {
        ZL_RESULT_DECLARE_SCOPE_REPORT(sictx);

        // Access the materialized param
        MaterializedData* materialized = const_cast<MaterializedData*>(
                (const MaterializedData*)ZL_Segmenter_getLocalRefParam(
                        sictx, MATERIALIZED_PARAM_ID)
                        .paramRef);
        ZL_ERR_IF_NULL(
                materialized,
                GENERIC,
                "Expected materialized object to be nonnull");

        // Exfiltrate data by copying message to the exfiltrate pointer
        char* exfiltrateLocation = (char*)materialized->exfiltratePtr;
        snprintf(exfiltrateLocation, 50, "%s", materialized->message.c_str());

        // Process all input as a single chunk through the successor graph
        size_t nbInputs = ZL_Segmenter_numInputs(sictx);
        std::vector<size_t> numElts(nbInputs);
        ZL_ERR_IF_ERR(ZL_Segmenter_getNumElts(sictx, numElts.data(), nbInputs));
        ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
                sictx, numElts.data(), nbInputs, ZL_GRAPH_STORE, nullptr));

        return ZL_returnSuccess();
    };

    auto inputType = ZL_Type_serial;
    // Register the segmenter
    ZL_SegmenterDesc segmenterDesc = {
        .name                = "test_segmenter",
        .segmenterFn         = segmenterFunction,
        .inputTypeMasks      = &inputType,
        .numInputs           = 1,
        .lastInputIsVariable = false,
        .numCustomGraphs     = 0,
        .localParams         = localParams,
        .materializer        = defaultMaterializer_,
        .opaque              = {},
    };

    auto segmenterId =
            ZL_Compressor_registerSegmenter(compressor_.get(), &segmenterDesc);
    ASSERT_NE(segmenterId.gid, ZL_GRAPH_ILLEGAL.gid);

    std::string message2 = "test segmenter";
    // Setup custom local params for second call
    ZL_CopyParam cp2 = {
        .paramId   = 1,
        .paramPtr  = message2.data(),
        .paramSize = message2.size(),
    };
    ZL_RefParam rp2 = {
        .paramId  = 2,
        .paramRef = exfiltrateBuffer.data() + 50,
    };
    ZL_LocalParams lp2 = {
            .copyParams = {
                .copyParams   = &cp2,
                .nbCopyParams = 1,
            },
            .refParams = {
                .refParams   = &rp2,
                .nbRefParams = 1,
            },
        };
    ZL_RuntimeGraphParameters rgp = {
        .localParams = &lp2,
    };

    cctx_.refCompressor(compressor_);
    cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    ASSERT_FALSE(ZL_isError(ZL_CCtx_selectStartingGraphID(
            cctx_.get(), NULL, segmenterId, &rgp)));

    std::string src = "Testing graph materialization with custom encoders.";
    std::vector<char> dst(1000);
    size_t compressedSize =
            cctx_.compressSerial(poly::span<char>(dst.data(), dst.size()), src);
    (void)compressedSize; // Suppress unused variable warning

    // Verify that the segmenter exfiltrated data correctly
    std::string result1(exfiltrateBuffer.data(), 50);
    EXPECT_EQ(result1, std::string(50, 0));
    std::string result2(exfiltrateBuffer.data() + 50, 50);
    EXPECT_TRUE(result2.find("test segmenter") != std::string::npos)
            << "Runtime message not found in: " << result2;
}

TEST_F(GraphTest,
       GIVENfunctionGraphWithMaterializationWHENstartingGraphSelectedWithParamsTHENmaterializationHappens)
{
    // Setup test data
    std::vector<char> exfiltrateBuffer(200, 0);

    std::string message = "RegisteredParams";
    ZL_CopyParam cp     = {
            .paramId   = 1,
            .paramPtr  = message.data(),
            .paramSize = message.size(),
    };
    ZL_RefParam rp = {
        .paramId  = 2,
        .paramRef = exfiltrateBuffer.data(),
    };
    ZL_LocalParams localParams = {
        .intParams = {
            .intParams   = nullptr,
            .nbIntParams = 0,
        },
        .copyParams = {
            .copyParams   = &cp,
            .nbCopyParams = 1,
        },
        .refParams = {
            .refParams   = &rp,
            .nbRefParams = 1,
        },
    };

    // Register the exfiltrating graph with initial local params
    static ZL_Type inputType       = ZL_Type_serial;
    ZL_FunctionGraphDesc graphDesc = {
        .name                = "exfiltrating_graph",
        .graph_f             = exfiltratingGraphFn,
        .validate_f          = nullptr,
        .inputTypeMasks      = &inputType,
        .nbInputs            = 1,
        .lastInputIsVariable = 0,
        .customGraphs        = nullptr,
        .nbCustomGraphs      = 0,
        .customNodes         = nullptr,
        .nbCustomNodes       = 0,
        .localParams         = localParams,
        .materializer        = defaultMaterializer_,
        .opaque              = {},
    };

    auto graphId = compressor_.registerFunctionGraph(graphDesc);
    ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);

    std::string message2 = "RuntimeParams";
    // Setup custom local params for runtime override
    ZL_CopyParam cp2 = {
        .paramId   = 1,
        .paramPtr  = message2.data(),
        .paramSize = message2.size(),
    };
    ZL_RefParam rp2 = {
        .paramId  = 2,
        .paramRef = exfiltrateBuffer.data() + 50,
    };
    ZL_LocalParams lp2 = {
            .copyParams = {
                .copyParams   = &cp2,
                .nbCopyParams = 1,
            },
            .refParams = {
                .refParams   = &rp2,
                .nbRefParams = 1,
            },
        };
    ZL_RuntimeGraphParameters rgp = {
        .localParams = &lp2,
    };

    cctx_.refCompressor(compressor_);
    cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    ASSERT_FALSE(ZL_isError(
            ZL_CCtx_selectStartingGraphID(cctx_.get(), NULL, graphId, &rgp)));

    std::string src = "Testing graph materialization with custom encoders.";
    std::vector<char> dst(1000);
    size_t compressedSize =
            cctx_.compressSerial(poly::span<char>(dst.data(), dst.size()), src);
    (void)compressedSize; // Suppress unused variable warning

    // Verify that the graph exfiltrated data correctly
    std::string result1(exfiltrateBuffer.data(), 50);
    EXPECT_EQ(result1, std::string(50, 0))
            << "Registered params should not be used: " << result1;
    std::string result2(exfiltrateBuffer.data() + 50, 50);
    EXPECT_TRUE(result2.find("RuntimeParams") != std::string::npos)
            << "Runtime message not found in: " << result2;
}

TEST_F(GraphTest,
       GIVENfunctionGraphWithMaterializationWHENruntimeParamsSetTHENmaterializationHappens)
{
    // Setup for first graph call
    std::string message1 = "FirstGraphCall";
    std::vector<char> exfiltrateBuffer(200, 0);

    ZL_CopyParam cp1 = {
        .paramId   = 1,
        .paramPtr  = message1.data(),
        .paramSize = message1.size(),
    };
    ZL_RefParam rp1 = {
        .paramId  = 2,
        .paramRef = exfiltrateBuffer.data(),
    };
    ZL_LocalParams lp1 = {
        .intParams = {
            .intParams   = nullptr,
            .nbIntParams = 0,
        },
        .copyParams = {
            .copyParams   = &cp1,
            .nbCopyParams = 1,
        },
        .refParams = {
            .refParams   = &rp1,
            .nbRefParams = 1,
        },
    };

    // Register the exfiltrating graph with initial local params
    static ZL_Type inputType                   = ZL_Type_serial;
    ZL_FunctionGraphDesc exfiltratingGraphDesc = {
        .name                = "exfiltrating_graph",
        .graph_f             = exfiltratingGraphFn,
        .validate_f          = nullptr,
        .inputTypeMasks      = &inputType,
        .nbInputs            = 1,
        .lastInputIsVariable = 0,
        .customGraphs        = nullptr,
        .nbCustomGraphs      = 0,
        .customNodes         = nullptr,
        .nbCustomNodes       = 0,
        .localParams         = lp1,
        .materializer        = defaultMaterializer_,
        .opaque              = {},
    };

    auto exfiltratingGraphId =
            compressor_.registerFunctionGraph(exfiltratingGraphDesc);
    ASSERT_NE(exfiltratingGraphId.gid, ZL_GRAPH_ILLEGAL.gid);

    // Setup parameters for second call via opaque pointer
    std::string message2        = "SecondGraphCall";
    RuntimeParams runtimeParams = {
        .message       = message2.c_str(),
        .exfiltratePtr = exfiltrateBuffer.data() + 50,
    };

    // Function graph that calls the exfiltrating graph twice
    auto graphFn = [](ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
                           ZL_NOEXCEPT_FUNC_PTR -> ZL_Report {
        ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
        ZL_ERR_IF_NE(nbInputs, 1, GENERIC);

        // Get the exfiltrating graph from custom graphs list
        ZL_GraphIDList customGraphs = ZL_Graph_getCustomGraphs(graph);
        ZL_ERR_IF_NE(customGraphs.nbGraphIDs, 1, GENERIC);
        ZL_GraphID exfiltratingGraph = customGraphs.graphids[0];

        // Get the opaque pointer which contains the params
        const RuntimeParams* rtp =
                (const RuntimeParams*)ZL_Graph_getOpaquePtr(graph);
        ZL_ERR_IF_NULL(rtp, GENERIC);

        // Setup custom local params
        ZL_CopyParam cp2 = {
            .paramId   = 1,
            .paramPtr  = rtp->message,
            .paramSize = strlen(rtp->message),
        };
        ZL_RefParam rp2 = {
            .paramId  = 2,
            .paramRef = rtp->exfiltratePtr,
        };
        ZL_LocalParams lp2 = {
            .copyParams = {
                .copyParams   = &cp2,
                .nbCopyParams = 1,
            },
            .refParams = {
                .refParams   = &rp2,
                .nbRefParams = 1,
            },
        };
        ZL_RuntimeGraphParameters rgp = {
            .localParams = &lp2,
        };

        // Second call: use overridden local params via runtime parameters
        ZL_ERR_IF_ERR(ZL_Edge_setParameterizedDestination(
                inputs, nbInputs, exfiltratingGraph, &rgp));

        return ZL_returnSuccess();
    };

    // Register the wrapper function graph
    ZL_GraphID customGraphs[] = { exfiltratingGraphId };
    ZL_FunctionGraphDesc wrapperGraphDesc = {
        .name                = "wrapper_graph",
        .graph_f             = graphFn,
        .validate_f          = nullptr,
        .inputTypeMasks      = &inputType,
        .nbInputs            = 1,
        .lastInputIsVariable = 0,
        .customGraphs        = customGraphs,
        .nbCustomGraphs      = 1,
        .customNodes         = nullptr,
        .nbCustomNodes       = 0,
        .localParams         = lp1,
        .materializer        = defaultMaterializer_,
        .opaque              = {
            .ptr           = (void*)&runtimeParams,
            .freeOpaquePtr = nullptr,
            .freeFn        = nullptr,
        },
    };

    auto wrapperGraphId = compressor_.registerFunctionGraph(wrapperGraphDesc);
    ASSERT_NE(wrapperGraphId.gid, ZL_GRAPH_ILLEGAL.gid);
    compressor_.selectStartingGraph(wrapperGraphId);

    compressData();

    // Verify that both graph calls exfiltrated their data correctly
    std::string result1(exfiltrateBuffer.data(), 50);
    std::string result2(exfiltrateBuffer.data() + 50, 50);

    // First call uses the registered local params
    EXPECT_EQ(result1, std::string(50, 0))
            << "FirstGraphCall message found in: " << result1;

    // Second call uses the overridden local params from runtime parameters
    EXPECT_TRUE(result2.find("SecondGraphCall") != std::string::npos)
            << "SecondGraphCall message not found in: " << result2;
}

TEST_F(GraphTest,
       GIVENfunctionGraphWithMaterializationWHENtryGraphWithParamsTHENmaterializationHappens)
{
    // Setup for graph call
    std::string message1 = "RegisteredParams";
    std::vector<char> exfiltrateBuffer(200, 0);

    ZL_CopyParam cp1 = {
        .paramId   = 1,
        .paramPtr  = message1.data(),
        .paramSize = message1.size(),
    };
    ZL_RefParam rp1 = {
        .paramId  = 2,
        .paramRef = exfiltrateBuffer.data(),
    };
    ZL_LocalParams lp1 = {
        .intParams = {
            .intParams   = nullptr,
            .nbIntParams = 0,
        },
        .copyParams = {
            .copyParams   = &cp1,
            .nbCopyParams = 1,
        },
        .refParams = {
            .refParams   = &rp1,
            .nbRefParams = 1,
        },
    };

    // Register the exfiltrating graph with initial local params
    static ZL_Type inputType                   = ZL_Type_serial;
    ZL_FunctionGraphDesc exfiltratingGraphDesc = {
        .name                = "exfiltrating_graph",
        .graph_f             = exfiltratingGraphFn,
        .validate_f          = nullptr,
        .inputTypeMasks      = &inputType,
        .nbInputs            = 1,
        .lastInputIsVariable = 0,
        .customGraphs        = nullptr,
        .nbCustomGraphs      = 0,
        .customNodes         = nullptr,
        .nbCustomNodes       = 0,
        .localParams         = lp1,
        .materializer        = defaultMaterializer_,
        .opaque              = {},
    };

    auto exfiltratingGraphId =
            compressor_.registerFunctionGraph(exfiltratingGraphDesc);
    ASSERT_NE(exfiltratingGraphId.gid, ZL_GRAPH_ILLEGAL.gid);

    // Setup parameters for tryGraph call via opaque pointer
    std::string message2        = "TryGraphParams";
    RuntimeParams runtimeParams = {
        .message       = message2.c_str(),
        .exfiltratePtr = exfiltrateBuffer.data() + 50,
    };

    // Function graph that calls tryGraph with runtime parameters
    auto graphFn = [](ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
                           ZL_NOEXCEPT_FUNC_PTR -> ZL_Report {
        ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
        ZL_ERR_IF_NE(nbInputs, 1, GENERIC);

        // Get the exfiltrating graph from custom graphs list
        ZL_GraphIDList customGraphs = ZL_Graph_getCustomGraphs(graph);
        ZL_ERR_IF_NE(customGraphs.nbGraphIDs, 1, GENERIC);
        ZL_GraphID exfiltratingGraph = customGraphs.graphids[0];

        // Get the opaque pointer which contains the params
        const RuntimeParams* rtp =
                (const RuntimeParams*)ZL_Graph_getOpaquePtr(graph);
        ZL_ERR_IF_NULL(rtp, GENERIC);

        // Setup custom local params for tryGraph
        ZL_CopyParam cp2 = {
            .paramId   = 1,
            .paramPtr  = rtp->message,
            .paramSize = strlen(rtp->message),
        };
        ZL_RefParam rp2 = {
            .paramId  = 2,
            .paramRef = rtp->exfiltratePtr,
        };
        ZL_LocalParams lp2 = {
            .copyParams = {
                .copyParams   = &cp2,
                .nbCopyParams = 1,
            },
            .refParams = {
                .refParams   = &rp2,
                .nbRefParams = 1,
            },
        };
        ZL_RuntimeGraphParameters rgp = {
            .localParams = &lp2,
        };

        // Get the input from the edge
        const ZL_Input* input = ZL_Edge_getData(inputs[0]);
        ZL_ERR_IF_NULL(input, GENERIC);

        // Try the graph with runtime parameters
        ZL_TRY_LET(
                ZL_GraphPerformance,
                perf,
                ZL_Graph_tryGraph(graph, input, exfiltratingGraph, &rgp));
        (void)perf; // Suppress unused variable warning

        // Send all inputs to STORE
        for (size_t i = 0; i < nbInputs; i++) {
            ZL_ERR_IF_ERR(ZL_Edge_setDestination(inputs[i], ZL_GRAPH_STORE));
        }

        return ZL_returnSuccess();
    };

    // Register the wrapper function graph
    ZL_GraphID customGraphs[] = { exfiltratingGraphId };
    ZL_FunctionGraphDesc wrapperGraphDesc = {
        .name                = "wrapper_graph",
        .graph_f             = graphFn,
        .validate_f          = nullptr,
        .inputTypeMasks      = &inputType,
        .nbInputs            = 1,
        .lastInputIsVariable = 0,
        .customGraphs        = customGraphs,
        .nbCustomGraphs      = 1,
        .customNodes         = nullptr,
        .nbCustomNodes       = 0,
        .localParams         = lp1,
        .materializer        = defaultMaterializer_,
        .opaque              = {
            .ptr           = (void*)&runtimeParams,
            .freeOpaquePtr = nullptr,
            .freeFn        = nullptr,
        },
    };

    auto wrapperGraphId = compressor_.registerFunctionGraph(wrapperGraphDesc);
    ASSERT_NE(wrapperGraphId.gid, ZL_GRAPH_ILLEGAL.gid);
    compressor_.selectStartingGraph(wrapperGraphId);

    compressData();

    // Verify that the graph call exfiltrated data correctly
    std::string result1(exfiltrateBuffer.data(), 50);
    std::string result2(exfiltrateBuffer.data() + 50, 50);

    // First buffer should be empty (registered params not used)
    EXPECT_EQ(result1, std::string(50, 0))
            << "Registered params should not be used: " << result1;

    // Second buffer should contain the runtime params message
    EXPECT_TRUE(result2.find("TryGraphParams") != std::string::npos)
            << "TryGraphParams message not found in: " << result2;
}

} // namespace openzl
