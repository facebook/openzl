// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/CParam.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/LocalParams.hpp"
#include "openzl/cpp/poly/Span.hpp"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_ctransform.h"

using namespace ::testing;

namespace openzl {

// Param IDs for test
constexpr int MATERIALIZED_PARAM_ID = 100;

// Test data structure that will be materialized
struct MaterializedDictionary {
    std::string str;
    void* ptr;
};

static ZL_Report
passthrough(ZL_Encoder* eictx, const ZL_Input* inputs[], size_t nbInputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ERR_IF_NE(nbInputs, 1, GENERIC);
    auto* input = inputs[0];
    ZL_ERR_IF_NULL(input, GENERIC);
    ZL_ERR_IF_NE(ZL_Input_type(input), ZL_Type_serial, GENERIC);
    auto* src    = ZL_Input_ptr(input);
    size_t n     = ZL_Input_numElts(input);
    auto* output = ZL_Encoder_createTypedStream(eictx, 0, n, 1);
    ZL_ERR_IF_NULL(output, GENERIC);
    void* dst = ZL_Output_ptr(output);
    memcpy(dst, src, n);
    ZL_ERR_IF_ERR(ZL_Output_commit(output, n));

    return ZL_returnSuccess();
}

class CompressorIntegrationTest : public Test {
   protected:
    void SetUp() override
    {
        nextCtid_ = 5000;
    }

    // Register a new encoder node with materialization
    ZL_NodeID registerNodeWithMaterialization(
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
            .name        = "test_encoder",
        };
        if (materializer != nullptr) {
            encoderDesc.materializer = *materializer;
        }

        auto nodeid = compressor_.registerCustomEncoder(encoderDesc);
        EXPECT_NE(nodeid.nid, ZL_NODE_ILLEGAL.nid);
        return nodeid;
    }

    // Parameterize an existing node with new local params and optional
    // materializer
    ZL_NodeID parameterizeNodeWithMaterialization(
            ZL_NodeID baseNode,
            const char* name,
            const ZL_LocalParams& localParams)
    {
        LocalParams lp(localParams);
        NodeParameters params{
            .name        = name,
            .localParams = lp,
        };

        auto paramNode = compressor_.parameterizeNode(baseNode, params);
        EXPECT_NE(paramNode.nid, ZL_NODE_ILLEGAL.nid);
        return paramNode;
    }

    // Helper to build and select a graph
    void buildAndSelectGraph(ZL_NodeID encoderNode)
    {
        auto graphId =
                compressor_.buildStaticGraph(encoderNode, { ZL_GRAPH_STORE });
        ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);
        compressor_.selectStartingGraph(graphId);
    }

    // Helper to compress data
    void compressData()
    {
        cctx_.refCompressor(compressor_);
        cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

        std::string src =
                "Let me think. That's just inherently very difficult. What are we doing?";
        std::vector<char> dst(1000);
        size_t compressedSize = cctx_.compressSerial(
                poly::span<char>(dst.data(), dst.size()), src);
        (void)compressedSize; // Suppress unused variable warning
    }

    static ZL_RESULT_OF(ZL_VoidPtr) materializeDictionary(
            ZL_Materializer*,
            const ZL_LocalParams* params) ZL_NOEXCEPT_FUNC_PTR
    {
        ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, nullptr);
        auto* dict = new MaterializedDictionary();
        auto ip    = params->intParams.intParams[0];
        ZL_ERR_IF_NE(ip.paramId, 1, GENERIC);
        ZL_ERR_IF_NE(params->copyParams.nbCopyParams, 1, GENERIC);
        auto cp = params->copyParams.copyParams[0];
        ZL_ERR_IF_NE(cp.paramId, 2, GENERIC);
        ZL_ERR_IF_NE(cp.paramSize, ip.paramValue, GENERIC);

        dict->str = std::string(cp.paramSize, 0);
        memcpy(dict->str.data(), cp.paramPtr, cp.paramSize);

        ZL_ERR_IF_NE(params->refParams.nbRefParams, 1, GENERIC);
        ZL_ERR_IF_NE(params->refParams.refParams[0].paramId, 3, GENERIC);
        // don't worry about this...
        dict->ptr = const_cast<void*>(params->refParams.refParams[0].paramRef);

        return ZL_WRAP_VALUE(dict);
    }

    static void dematerializeDictionary(ZL_Materializer*, void* materialized)
            ZL_NOEXCEPT_FUNC_PTR
    {
        auto* dict = static_cast<MaterializedDictionary*>(materialized);
        delete dict;
    }

    openzl::Compressor compressor_;
    openzl::CCtx cctx_;
    ZL_IDType nextCtid_;
    ZL_MaterializerDesc defaultMaterializer_ = {
        .materializeFn   = materializeDictionary,
        .dematerializeFn = dematerializeDictionary,
        .paramId         = MATERIALIZED_PARAM_ID,
    };
};

TEST_F(CompressorIntegrationTest,
       GIVENaNodeWithAMaterializedParamWHENrequestedTHENitIsGiven)
{
    const auto encoderWithMaterializedParam =
            [](ZL_Encoder* eictx, const ZL_Input* inputs[], size_t nbInputs)
                    ZL_NOEXCEPT_FUNC_PTR -> ZL_Report {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
        // Access the materialized param
        const MaterializedDictionary* materialized =
                (const MaterializedDictionary*)ZL_Encoder_getLocalParam(
                        eictx, MATERIALIZED_PARAM_ID)
                        .paramRef;
        ZL_ERR_IF_NULL(
                materialized,
                GENERIC,
                "Expected materialized object to be nonnull");
        memcpy(materialized->ptr,
               materialized->str.data(),
               materialized->str.size());

        return passthrough(eictx, inputs, nbInputs);
    };

    std::string str = "Hello, materialized params!";
    std::string str2(str.size(), 0);
    ZL_IntParam ip = {
        .paramId    = 1,
        .paramValue = (int)str.size(),
    };
    ZL_CopyParam cp = {
        .paramId   = 2,
        .paramPtr  = str.data(),
        .paramSize = str.size(),
    };
    ZL_RefParam rp = {
        .paramId  = 3,
        .paramRef = str2.data(),
    };
    ZL_LocalParams lp = {
        .intParams = {
            .intParams = &ip,
            .nbIntParams = 1, 
        },
        .copyParams = {
            .copyParams = &cp,
            .nbCopyParams = 1,
        },
        .refParams = {
            .refParams = &rp,
            .nbRefParams = 1,
        },
    };

    ZL_NodeID encoderNode = registerNodeWithMaterialization(
            encoderWithMaterializedParam, lp, &defaultMaterializer_);
    buildAndSelectGraph(encoderNode);

    compressData();
    EXPECT_EQ(str, str2);
}

TEST_F(CompressorIntegrationTest,
       GIVENaNodeWithAMaterializedParamWHENlocalParamsRequestedTHENtheyAreGiven)
{
    const auto encoderWithMaterializedParam =
            [](ZL_Encoder* eictx, const ZL_Input* inputs[], size_t nbInputs)
                    ZL_NOEXCEPT_FUNC_PTR -> ZL_Report {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
        // Access the materialized param
        const MaterializedDictionary* materialized =
                (const MaterializedDictionary*)ZL_Encoder_getLocalParam(
                        eictx, MATERIALIZED_PARAM_ID)
                        .paramRef;
        ZL_ERR_IF_NULL(
                materialized,
                GENERIC,
                "Expected materialized object to be nonnull");
        auto cp = ZL_Encoder_getLocalCopyParam(eictx, 2);
        auto rp = ZL_Encoder_getLocalParam(eictx, 3);
        memcpy(const_cast<void*>(rp.paramRef), cp.paramPtr, cp.paramSize);

        return passthrough(eictx, inputs, nbInputs);
    };

    std::string str = "Hello, materialized params!";
    std::string str2(str.size(), 0);
    ZL_IntParam ip = {
        .paramId    = 1,
        .paramValue = (int)str.size(),
    };
    ZL_CopyParam cp = {
        .paramId   = 2,
        .paramPtr  = str.data(),
        .paramSize = str.size(),
    };
    ZL_RefParam rp = {
        .paramId  = 3,
        .paramRef = str2.data(),
    };
    ZL_LocalParams lp = {
        .intParams = {
            .intParams = &ip,
            .nbIntParams = 1, 
        },
        .copyParams = {
            .copyParams = &cp,
            .nbCopyParams = 1,
        },
        .refParams = {
            .refParams = &rp,
            .nbRefParams = 1,
        },
    };

    ZL_NodeID encoderNode = registerNodeWithMaterialization(
            encoderWithMaterializedParam, lp, &defaultMaterializer_);
    buildAndSelectGraph(encoderNode);

    compressData();
}

TEST_F(CompressorIntegrationTest,
       GIVENmultipleNodesWithTheSameMaterializerAndParamsWHENrequestedTHENtheSameObjectIsReturned)
{
    const auto encoderWithMaterializedParam =
            [](ZL_Encoder* eictx, const ZL_Input* inputs[], size_t nbInputs)
                    ZL_NOEXCEPT_FUNC_PTR -> ZL_Report {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
        // Access the materialized param
        MaterializedDictionary* materialized =
                const_cast<MaterializedDictionary*>(
                        (const MaterializedDictionary*) // don't do this in prod
                        ZL_Encoder_getLocalParam(eictx, MATERIALIZED_PARAM_ID)
                                .paramRef);
        ZL_ERR_IF_NULL(
                materialized,
                GENERIC,
                "Expected materialized object to be nonnull");
        void** address = (void**)materialized->ptr;
        if (*address == nullptr) {
            *address = materialized;
        } else {
            ZL_ERR_IF_NE(
                    materialized,
                    (MaterializedDictionary*)(*address),
                    GENERIC,
                    "Expected the address of returned materialized object to match");
        }
        return passthrough(eictx, inputs, nbInputs);
    };

    std::string str = "Hello, materialized params!";
    std::string str2(str.size(), 0);
    ZL_IntParam ip = {
        .paramId    = 1,
        .paramValue = (int)str.size(),
    };
    ZL_CopyParam cp = {
        .paramId   = 2,
        .paramPtr  = str.data(),
        .paramSize = str.size(),
    };
    ZL_RefParam rp = {
        .paramId  = 3,
        .paramRef = str2.data(),
    };
    ZL_LocalParams lp = {
        .intParams = {
            .intParams = &ip,
            .nbIntParams = 1, 
        },
        .copyParams = {
            .copyParams = &cp,
            .nbCopyParams = 1,
        },
        .refParams = {
            .refParams = &rp,
            .nbRefParams = 1,
        },
    };

    ZL_NodeID encoderNode1 = registerNodeWithMaterialization(
            encoderWithMaterializedParam, lp, &defaultMaterializer_);
    ZL_NodeID encoderNode2 = registerNodeWithMaterialization(
            encoderWithMaterializedParam, lp, &defaultMaterializer_);

    // build and select graph
    std::array<ZL_NodeID, 2> nodes = { encoderNode1, encoderNode2 };
    auto graphId = ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
            compressor_.get(), nodes.data(), nodes.size(), ZL_GRAPH_STORE);

    ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);

    compressor_.selectStartingGraph(graphId);
    compressData();
}

TEST_F(CompressorIntegrationTest,
       GIVENmultipleNodesWithDifferentMaterializersButSameParamsWHENrequestedTHENdifferentObjectsAreReturned)
{
    const auto newMaterialize =
            [](ZL_Materializer*, const ZL_LocalParams* params)
                    ZL_NOEXCEPT_FUNC_PTR -> ZL_RESULT_OF(ZL_VoidPtr) {
        ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, nullptr);
        auto* dict = new MaterializedDictionary();
        ZL_ERR_IF_NE(params->refParams.nbRefParams, 1, GENERIC);
        ZL_ERR_IF_NE(params->refParams.refParams[0].paramId, 3, GENERIC);
        // don't worry about this...
        dict->ptr = const_cast<void*>(params->refParams.refParams[0].paramRef);

        return ZL_WRAP_VALUE(dict);
    };

    const auto encoderWithMaterializedParam =
            [](ZL_Encoder* eictx, const ZL_Input* inputs[], size_t nbInputs)
                    ZL_NOEXCEPT_FUNC_PTR -> ZL_Report {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
        // Access the materialized param
        MaterializedDictionary* materialized =
                const_cast<MaterializedDictionary*>(
                        (const MaterializedDictionary*)ZL_Encoder_getLocalParam(
                                eictx, MATERIALIZED_PARAM_ID)
                                .paramRef);
        ZL_ERR_IF_NULL(
                materialized,
                GENERIC,
                "Expected materialized object to be nonnull");
        void** address = (void**)materialized->ptr;
        if (*address == nullptr) {
            *address = materialized;
        } else {
            ZL_ERR_IF_EQ(
                    materialized,
                    (MaterializedDictionary*)(*address),
                    GENERIC,
                    "Expected the address of returned materialized object to be different");
        }
        return passthrough(eictx, inputs, nbInputs);
    };

    std::string str = "Hello, materialized params!";
    std::string str2(str.size(), 0);
    ZL_IntParam ip = {
        .paramId    = 1,
        .paramValue = (int)str.size(),
    };
    ZL_CopyParam cp = {
        .paramId   = 2,
        .paramPtr  = str.data(),
        .paramSize = str.size(),
    };
    ZL_RefParam rp = {
        .paramId  = 3,
        .paramRef = str2.data(),
    };
    ZL_LocalParams lp = {
        .intParams = {
            .intParams = &ip,
            .nbIntParams = 1,
        },
        .copyParams = {
            .copyParams = &cp,
            .nbCopyParams = 1,
        },
        .refParams = {
            .refParams = &rp,
            .nbRefParams = 1,
        },
    };

    ZL_NodeID encoderNode1 = registerNodeWithMaterialization(
            encoderWithMaterializedParam, lp, &defaultMaterializer_);

    ZL_MaterializerDesc mat = {
        .materializeFn   = newMaterialize,
        .dematerializeFn = dematerializeDictionary,
        .paramId         = MATERIALIZED_PARAM_ID,
    };
    ZL_NodeID encoderNode2 = registerNodeWithMaterialization(
            encoderWithMaterializedParam, lp, &mat);

    // build and select graph
    std::array<ZL_NodeID, 2> nodes = { encoderNode1, encoderNode2 };
    auto graphId = ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
            compressor_.get(), nodes.data(), nodes.size(), ZL_GRAPH_STORE);

    ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);

    compressor_.selectStartingGraph(graphId);
    compressData();
}

TEST_F(CompressorIntegrationTest,
       GIVENnoNodesWithMaterializedParamsWHENrequestedTHENanErrorIsReturned)
{
    const auto encoderWithoutMaterializedParam =
            [](ZL_Encoder* eictx, const ZL_Input* inputs[], size_t nbInputs)
                    ZL_NOEXCEPT_FUNC_PTR -> ZL_Report {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
        auto materialized =
                ZL_Encoder_getLocalParam(eictx, MATERIALIZED_PARAM_ID).paramRef;
        ZL_ERR_IF_NN(
                materialized,
                GENERIC,
                "No materializer means no materialized param");

        return passthrough(eictx, inputs, nbInputs);
    };
    ZL_NodeID encoderNode = registerNodeWithMaterialization(
            encoderWithoutMaterializedParam, {}, nullptr);
    buildAndSelectGraph(encoderNode);
    compressData();
}

} // namespace openzl
