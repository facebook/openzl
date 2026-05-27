// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "openzl/dict/bundle.h"
#include "openzl/dict/dict.h"
#include "openzl/dict/dict_constants.h"

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/CParam.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/LocalParams.hpp"
#include "openzl/cpp/poly/Span.hpp"

#include "openzl/zl_compressor.h"
#include "openzl/zl_compressor_serialization.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_reflection.h"
#include "openzl/zl_segmenter.h"
#include "openzl/zl_selector.h"

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

static ZL_Report passthroughNoexcept(
        ZL_Encoder* eictx,
        const ZL_Input* inputs[],
        size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
{
    return passthrough(eictx, inputs, nbInputs);
}

static ZL_RESULT_OF(ZL_VoidPtr) copyDictMaterialize(
        ZL_Materializer* matCtx,
        const void* src,
        size_t srcSize) ZL_NOEXCEPT_FUNC_PTR;

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

    static ZL_DictID makeDictID(uint8_t seed)
    {
        ZL_DictID dictID;
        memset(&dictID, 0, sizeof(dictID));
        for (size_t i = 0; i < sizeof(dictID.id.bytes); ++i) {
            dictID.id.bytes[i] = static_cast<uint8_t>(seed + i);
        }
        return dictID;
    }

    static std::vector<uint8_t> buildPackedDict(
            ZL_DictID dictID,
            ZL_IDType codecID,
            const std::vector<uint8_t>& content)
    {
        std::vector<uint8_t> packed(ZL_DICT_HEADER_SIZE + content.size(), 0);
        ZL_Report report = Dict_pack(
                packed.data(),
                packed.size(),
                dictID,
                codecID,
                true,
                content.data(),
                content.size());
        EXPECT_FALSE(ZL_isError(report));
        packed.resize(ZL_validResult(report));
        return packed;
    }

    static std::vector<uint8_t> packFatBundle(
            const std::vector<std::vector<uint8_t>>& packedDicts)
    {
        std::vector<const void*> dictPtrs;
        std::vector<size_t> dictSizes;
        size_t totalDictBytes = 0;
        for (const auto& packedDict : packedDicts) {
            dictPtrs.push_back(packedDict.data());
            dictSizes.push_back(packedDict.size());
            totalDictBytes += packedDict.size();
        }

        size_t const bundleCapacity = ZL_BUNDLE_HEADER_SIZE
                + packedDicts.size() * ZL_UNIQUE_ID_SIZE + totalDictBytes;
        std::vector<uint8_t> fatBundle(bundleCapacity, 0);
        ZL_Report report = ZL_DictBundle_packFatBundle(
                fatBundle.data(),
                fatBundle.size(),
                packedDicts.empty() ? nullptr : dictPtrs.data(),
                packedDicts.empty() ? nullptr : dictSizes.data(),
                packedDicts.size());
        EXPECT_FALSE(ZL_isError(report));
        fatBundle.resize(ZL_validResult(report));
        return fatBundle;
    }

    ZL_RESULT_OF(ZL_NodeID)
    registerDictBackedNode(const char* name, ZL_DictID dictID)
    {
        static ZL_Type typetype = ZL_Type_serial;
        ZL_MIEncoderDesc encoderDesc{
            .gd =
                    {
                            .CTid                = nextCtid_++,
                            .inputTypes          = &typetype,
                            .nbInputs            = 1,
                            .lastInputIsVariable = false,
                            .soTypes             = &typetype,
                            .nbSOs               = 1,
                            .voTypes             = nullptr,
                            .nbVOs               = 0,
                    },
            .transform_f = passthroughNoexcept,
            .localParams = {},
            .name        = name,
            .dictMat     = { .materializeFn   = copyDictMaterialize,
                             .dematerializeFn = ZL_NOOP_DEMATERIALIZE },
            .dictID      = dictID,
        };
        return ZL_Compressor_registerMIEncoder2(
                compressor_.get(), &encoderDesc);
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

// ========================================================================
// Some (Limited) Graph Materialization Tests
// ========================================================================

TEST_F(CompressorIntegrationTest,
       GIVENaFunctionGraphWithMaterializedParamWHENinvokedTHENitIsAccessible)
{
    const auto graphFunction =
            [](ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
                    ZL_NOEXCEPT_FUNC_PTR -> ZL_Report {
        ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
        ZL_ERR_IF_NE(nbInputs, 1, GENERIC);

        // Access the materialized param
        const MaterializedDictionary* materialized =
                (const MaterializedDictionary*)ZL_Graph_getLocalRefParam(
                        graph, MATERIALIZED_PARAM_ID)
                        .paramRef;
        ZL_ERR_IF_NULL(
                materialized,
                GENERIC,
                "Expected materialized object to be nonnull");

        // Verify materialized data is correct
        memcpy(materialized->ptr,
               materialized->str.data(),
               materialized->str.size());

        // Forward the input to STORE
        return ZL_Edge_setDestination(inputs[0], ZL_GRAPH_STORE);
    };

    std::string str = "Graph materialized params!";
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
            .intParams   = &ip,
            .nbIntParams = 1,
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

    static ZL_Type inputType       = ZL_Type_serial;
    ZL_FunctionGraphDesc graphDesc = {
        .name           = "test_graph_with_materializer",
        .graph_f        = graphFunction,
        .inputTypeMasks = &inputType,
        .nbInputs       = 1,
        .localParams    = lp,
        .materializer   = defaultMaterializer_,
    };

    auto graphId = compressor_.registerFunctionGraph(graphDesc);
    ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);
    compressor_.selectStartingGraph(graphId);

    compressData();
    EXPECT_EQ(str, str2);
}

TEST_F(CompressorIntegrationTest,
       GIVENaSegmenterWithMaterializedParamWHENinvokedTHENitIsAccessible)
{
    const auto segmenterFunction = [](ZL_Segmenter* segmenter)
                                           ZL_NOEXCEPT_FUNC_PTR -> ZL_Report {
        ZL_RESULT_DECLARE_SCOPE_REPORT(segmenter);

        // Access the materialized param
        const MaterializedDictionary* materialized =
                (const MaterializedDictionary*)ZL_Segmenter_getLocalRefParam(
                        segmenter, MATERIALIZED_PARAM_ID)
                        .paramRef;
        ZL_ERR_IF_NULL(
                materialized,
                GENERIC,
                "Expected materialized object to be nonnull");

        // Verify materialized data is correct
        memcpy(materialized->ptr,
               materialized->str.data(),
               materialized->str.size());

        // Process all input as a single chunk to STORE graph
        size_t numInputs = ZL_Segmenter_numInputs(segmenter);
        size_t* numElts  = (size_t*)ZL_Segmenter_getScratchSpace(
                segmenter, numInputs * sizeof(size_t));
        ZL_ERR_IF_NULL(numElts, allocation);
        ZL_ERR_IF_ERR(ZL_Segmenter_getNumElts(segmenter, numElts, numInputs));
        ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
                segmenter, numElts, numInputs, ZL_GRAPH_STORE, nullptr));

        return ZL_returnSuccess();
    };

    std::string str = "Segmenter materialized params!";
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
            .intParams   = &ip,
            .nbIntParams = 1,
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

    static ZL_Type inputType = ZL_Type_serial;
    ZL_SegmenterDesc segDesc = {
        .name                = "test_segmenter_with_materializer",
        .segmenterFn         = segmenterFunction,
        .inputTypeMasks      = &inputType,
        .numInputs           = 1,
        .lastInputIsVariable = false,
        .localParams         = lp,
        .materializer        = defaultMaterializer_,
    };

    // Use C API for segmenter registration
    auto graphId = ZL_Compressor_registerSegmenter(compressor_.get(), &segDesc);
    ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);
    compressor_.selectStartingGraph(graphId);

    compressData();
    EXPECT_EQ(str, str2);
}

TEST_F(CompressorIntegrationTest,
       GIVENaSelectorWithMaterializedParamWHENinvokedTHENitIsAccessible)
{
    const auto selectorFunction = [](const ZL_Selector* selector,
                                     const ZL_Input*,
                                     const ZL_GraphID*,
                                     size_t)
                                          ZL_NOEXCEPT_FUNC_PTR -> ZL_GraphID {
        // Access the materialized param
        const MaterializedDictionary* materialized =
                (const MaterializedDictionary*)ZL_Selector_getLocalParam(
                        selector, MATERIALIZED_PARAM_ID)
                        .paramRef;
        if (materialized == nullptr) {
            return ZL_GRAPH_ILLEGAL;
        }

        // Verify materialized data is correct
        memcpy(materialized->ptr,
               materialized->str.data(),
               materialized->str.size());

        // Always select STORE graph
        return ZL_GRAPH_STORE;
    };

    std::string str = "Selector materialized params!";
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
            .intParams   = &ip,
            .nbIntParams = 1,
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

    ZL_SelectorDesc selDesc = {
        .selector_f   = selectorFunction,
        .inStreamType = ZL_Type_serial,
        .localParams  = lp,
        .materializer = defaultMaterializer_,
        .name         = "test_selector_with_materializer",
    };

    auto graphId = compressor_.registerSelectorGraph(selDesc);
    ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);
    compressor_.selectStartingGraph(graphId);

    compressData();
    EXPECT_EQ(str, str2);
}

static ZL_RESULT_OF(ZL_VoidPtr) copyDictMaterialize(
        ZL_Materializer* matCtx,
        const void* src,
        size_t srcSize) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, matCtx);
    void* copy = ZL_Materializer_allocate(matCtx, srcSize);
    ZL_ERR_IF_NULL(copy, allocation);
    memcpy(copy, src, srcSize);
    return ZL_WRAP_VALUE(copy);
}

TEST_F(CompressorIntegrationTest,
       GIVENaDictBackedNodeWHENNoBundleIsLoadedTHENSelectingGraphFailsWithDictNoRecord)
{
    ZL_DictID dictID = makeDictID(0xD1);
    ZL_RESULT_OF(ZL_NodeID)
    nodeResult = registerDictBackedNode(
            "test_encoder_missing_bundle_failure", dictID);
    ASSERT_FALSE(ZL_RES_isError(nodeResult));
    ZL_NodeID nodeID = ZL_RES_value(nodeResult);

    auto graphId = compressor_.buildStaticGraph(nodeID, { ZL_GRAPH_STORE });
    ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);

    ZL_Report report =
            ZL_Compressor_selectStartingGraphID(compressor_.get(), graphId);
    EXPECT_TRUE(ZL_isError(report));
    EXPECT_EQ(ZL_RES_code(report), ZL_ErrorCode_dictNoRecord);

    std::string errorContext =
            ZL_Compressor_getErrorContextString(compressor_.get(), report);
    EXPECT_NE(
            errorContext.find("requires a dictionary but no bundle is loaded"),
            std::string::npos);
}

TEST_F(CompressorIntegrationTest,
       GIVENaDictBackedNodeWHENLoadingBundleWithoutRequiredDictTHENItFailsWithNoValidMaterialization)
{
    ZL_DictID requiredDictID = makeDictID(0xD1);
    ZL_DictID wrongDictID    = makeDictID(0xE1);

    ZL_RESULT_OF(ZL_NodeID)
    nodeResult = registerDictBackedNode(
            "test_encoder_wrong_bundle_failure", requiredDictID);
    ASSERT_FALSE(ZL_RES_isError(nodeResult));
    ZL_NodeID nodeID = ZL_RES_value(nodeResult);

    ZL_DictID registeredDictID =
            ZL_Compressor_Node_getDictID(compressor_.get(), nodeID);
    EXPECT_EQ(
            memcmp(&registeredDictID,
                   &requiredDictID,
                   sizeof(registeredDictID)),
            0);

    std::vector<uint8_t> wrongBundle = packFatBundle(
            { buildPackedDict(
                    wrongDictID,
                    nextCtid_ - 1,
                    std::vector<uint8_t>{ 0x11, 0x22, 0x33 }) });
    ZL_Report report = ZL_Compressor_loadDictBundle(
            compressor_.get(), wrongBundle.data(), wrongBundle.size());
    EXPECT_TRUE(ZL_isError(report));
    EXPECT_EQ(ZL_RES_code(report), ZL_ErrorCode_noValidMaterialization);

    std::string errorContext =
            ZL_Compressor_getErrorContextString(compressor_.get(), report);
    EXPECT_NE(
            errorContext.find("no materializer found for dict"),
            std::string::npos);
}

TEST_F(CompressorIntegrationTest,
       GIVENaNodeRegisteredWithDictIDWHENqueriedTHENdictIDIsReturned)
{
    ZL_DictID dictID = makeDictID(42);
    ZL_RESULT_OF(ZL_NodeID)
    nodeResult = registerDictBackedNode("test_encoder_with_dictID", dictID);
    ASSERT_FALSE(ZL_RES_isError(nodeResult));
    ZL_NodeID nodeID = ZL_RES_value(nodeResult);

    ZL_DictID retrieved =
            ZL_Compressor_Node_getDictID(compressor_.get(), nodeID);
    EXPECT_EQ(memcmp(&retrieved, &dictID, sizeof(retrieved)), 0);
}

TEST_F(CompressorIntegrationTest,
       GIVENaParameterizedNodeWHENqueriedTHENdictIDIsPreservedFromBaseNode)
{
    ZL_DictID dictID = makeDictID(99);
    ZL_RESULT_OF(ZL_NodeID)
    baseResult =
            registerDictBackedNode("test_encoder_parameterized_dictID", dictID);
    ASSERT_FALSE(ZL_RES_isError(baseResult));
    ZL_NodeID baseNode = ZL_RES_value(baseResult);

    ZL_IntParam intParam = {
        .paramId    = 1,
        .paramValue = 42,
    };
    ZL_LocalParams localParams = {
        .intParams = {
            .intParams   = &intParam,
            .nbIntParams = 1,
        },
    };
    ZL_ParameterizedNodeDesc desc = {
        .node        = baseNode,
        .localParams = &localParams,
    };
    ZL_NodeID paramNode =
            ZL_Compressor_registerParameterizedNode(compressor_.get(), &desc);
    ASSERT_NE(paramNode.nid, ZL_NODE_ILLEGAL.nid);

    ZL_DictID retrieved =
            ZL_Compressor_Node_getDictID(compressor_.get(), paramNode);
    EXPECT_EQ(memcmp(&retrieved, &dictID, sizeof(retrieved)), 0);
}

TEST_F(CompressorIntegrationTest,
       GIVENaParameterizedNodeWithNewDictIDWHENqueriedTHENnewDictIDIsUsed)
{
    ZL_DictID originalDictID = makeDictID(10);
    ZL_RESULT_OF(ZL_NodeID)
    baseResult = registerDictBackedNode(
            "test_encoder_override_dictID", originalDictID);
    ASSERT_FALSE(ZL_RES_isError(baseResult));
    ZL_NodeID baseNode = ZL_RES_value(baseResult);

    ZL_DictID newDictID      = makeDictID(77);
    ZL_NodeParameters params = {
        .dictID = newDictID,
    };
    ZL_RESULT_OF(ZL_NodeID)
    result = ZL_Compressor_parameterizeNode(
            compressor_.get(), baseNode, &params);
    ASSERT_FALSE(ZL_RES_isError(result));
    ZL_NodeID paramNode = ZL_RES_value(result);
    ASSERT_NE(paramNode.nid, ZL_NODE_ILLEGAL.nid);

    ZL_DictID retrieved =
            ZL_Compressor_Node_getDictID(compressor_.get(), paramNode);
    EXPECT_EQ(memcmp(&retrieved, &newDictID, sizeof(retrieved)), 0);

    ZL_DictID baseRetrieved =
            ZL_Compressor_Node_getDictID(compressor_.get(), baseNode);
    EXPECT_EQ(
            memcmp(&baseRetrieved, &originalDictID, sizeof(baseRetrieved)), 0);
}

// This test exercises MParams thoroughly
// 1. Create materializers and encoders that take materialized params
// 2. Register nodes onto compressor (MParams materialize at registration time)
// 3. Compress
// 4. Serialize + Deserialize
// 5. Compress again (must match artifact in step 3)
TEST_F(CompressorIntegrationTest,
       GIVENmparamsLoadedWHENserializedAndDeserializedTHENcompressedOutputMatches)
{
    // --- MParam content blobs ---
    const std::string mparam1_content = "mparam-blob-for-node-A-original";
    const std::string mparam2_content = "mparam-blob-for-node-B-different-mat";
    const std::string mparam3_content =
            "mparam-blob-for-node-A-parameterized-1";
    const std::string mparam4_content =
            "mparam-blob-for-node-A-parameterized-2";

    // --- MParam IDs ---
    ZL_MParamID id1 = ZL_MPARAM_ID_NULL;
    id1.id.bytes[0] = 0x01;
    ZL_MParamID id2 = ZL_MPARAM_ID_NULL;
    id2.id.bytes[0] = 0x02;
    ZL_MParamID id3 = ZL_MPARAM_ID_NULL;
    id3.id.bytes[0] = 0x03;
    ZL_MParamID id4 = ZL_MPARAM_ID_NULL;
    id4.id.bytes[0] = 0x04;

    // --- Two different materializers ---
    ZL_MaterializerDesc2 matA{};
    matA.materializeFn   = copyDictMaterialize;
    matA.dematerializeFn = ZL_NOOP_DEMATERIALIZE;

    ZL_MaterializerDesc2 matB{};
    matB.materializeFn   = copyDictMaterialize;
    matB.dematerializeFn = ZL_NOOP_DEMATERIALIZE;
    matB.opaque          = { .ptr = (void*)0xBEEF };

    // --- Encoder that verifies MParam content against CopyParam ---
    const auto encoderVerifyingMParam =
            [](ZL_Encoder* eictx, const ZL_Input* inputs[], size_t nbInputs)
                    ZL_NOEXCEPT_FUNC_PTR -> ZL_Report {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);

        const void* mparam = ZL_Encoder_getMParam(eictx);
        ZL_ERR_IF_NULL(mparam, GENERIC, "Expected getMParam non-null");

        auto cp = ZL_Encoder_getLocalCopyParam(eictx, 1);
        ZL_ERR_IF_NULL(cp.paramPtr, GENERIC, "Expected CopyParam(1) non-null");
        ZL_ERR_IF_NE(
                memcmp(mparam, cp.paramPtr, cp.paramSize),
                0,
                GENERIC,
                "MParam content mismatch with expected CopyParam");

        return passthrough(eictx, inputs, nbInputs);
    };

    // --- Encoder that verifies MParam is NULL ---
    const auto encoderNoMParam =
            [](ZL_Encoder* eictx, const ZL_Input* inputs[], size_t nbInputs)
                    ZL_NOEXCEPT_FUNC_PTR -> ZL_Report {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);

        const void* mparam = ZL_Encoder_getMParam(eictx);
        ZL_ERR_IF_NN(
                mparam,
                GENERIC,
                "Expected getMParam NULL for node without mparam");

        return passthrough(eictx, inputs, nbInputs);
    };

    // --- Register nodes ---
    static ZL_Type typetype = ZL_Type_serial;

    auto makeGraphDesc = [&]() -> ZL_MIGraphDesc {
        return ZL_MIGraphDesc{
            .CTid                = nextCtid_++,
            .inputTypes          = &typetype,
            .nbInputs            = 1,
            .lastInputIsVariable = false,
            .soTypes             = &typetype,
            .nbSOs               = 1,
            .voTypes             = nullptr,
            .nbVOs               = 0,
        };
    };

    // Node A: matA + id1, CopyParam holds expected mparam1_content
    ZL_CopyParam cpA = {
        .paramId   = 1,
        .paramPtr  = mparam1_content.data(),
        .paramSize = mparam1_content.size(),
    };
    ZL_LocalParams lpA = {
        .copyParams = {
            .copyParams   = &cpA,
            .nbCopyParams = 1,
        },
    };
    ZL_MIEncoderDesc descA{
        .gd          = makeGraphDesc(),
        .transform_f = encoderVerifyingMParam,
        .localParams = lpA,
        .name        = "!encoder_mparam_A",
        .mparamMat   = matA,
        .mparam      = {
            .content  = mparam1_content.data(),
            .size     = mparam1_content.size(),
            .mparamID = id1,
        },
    };
    auto nodeA = compressor_.registerCustomEncoder(descA);
    ASSERT_NE(nodeA.nid, ZL_NODE_ILLEGAL.nid);

    // Node B: matB + id2 (different materializer)
    ZL_CopyParam cpB = {
        .paramId   = 1,
        .paramPtr  = mparam2_content.data(),
        .paramSize = mparam2_content.size(),
    };
    ZL_LocalParams lpB = {
        .copyParams = {
            .copyParams   = &cpB,
            .nbCopyParams = 1,
        },
    };
    ZL_MIEncoderDesc descB{
        .gd          = makeGraphDesc(),
        .transform_f = encoderVerifyingMParam,
        .localParams = lpB,
        .name        = "!encoder_mparam_B",
        .mparamMat   = matB,
        .mparam      = {
            .content  = mparam2_content.data(),
            .size     = mparam2_content.size(),
            .mparamID = id2,
        },
    };
    auto nodeB = compressor_.registerCustomEncoder(descB);
    ASSERT_NE(nodeB.nid, ZL_NODE_ILLEGAL.nid);

    // Node C: no mparam
    ZL_MIEncoderDesc descC{
        .gd          = makeGraphDesc(),
        .transform_f = encoderNoMParam,
        .name        = "!encoder_no_mparam",
    };
    auto nodeC = compressor_.registerCustomEncoder(descC);
    ASSERT_NE(nodeC.nid, ZL_NODE_ILLEGAL.nid);

    // Parameterize nodeA with id3 (same materializer, different MParam)
    ZL_CopyParam cp3 = {
        .paramId   = 1,
        .paramPtr  = mparam3_content.data(),
        .paramSize = mparam3_content.size(),
    };
    ZL_LocalParams lp3 = {
        .copyParams = {
            .copyParams   = &cp3,
            .nbCopyParams = 1,
        },
    };
    ZL_NodeParameters params3{
        .localParams = &lp3,
        .mparam      = {
            .content  = mparam3_content.data(),
            .size     = mparam3_content.size(),
            .mparamID = id3,
        },
    };
    auto result3 =
            ZL_Compressor_parameterizeNode(compressor_.get(), nodeA, &params3);
    ASSERT_FALSE(ZL_RES_isError(result3));
    auto nodeA_param1 = ZL_RES_value(result3);

    // Parameterize nodeA again with id4
    ZL_CopyParam cp4 = {
        .paramId   = 1,
        .paramPtr  = mparam4_content.data(),
        .paramSize = mparam4_content.size(),
    };
    ZL_LocalParams lp4 = {
        .copyParams = {
            .copyParams   = &cp4,
            .nbCopyParams = 1,
        },
    };
    ZL_NodeParameters params4{
        .localParams = &lp4,
        .mparam      = {
            .content  = mparam4_content.data(),
            .size     = mparam4_content.size(),
            .mparamID = id4,
        },
    };
    auto result4 =
            ZL_Compressor_parameterizeNode(compressor_.get(), nodeA, &params4);
    ASSERT_FALSE(ZL_RES_isError(result4));
    auto nodeA_param2 = ZL_RES_value(result4);

    // --- Build graph: nodeA → nodeB → nodeC → nodeA_param1 → nodeA_param2 →
    // STORE ---
    std::array<ZL_NodeID, 5> nodes = {
        nodeA, nodeB, nodeC, nodeA_param1, nodeA_param2
    };
    auto graphId = ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
            compressor_.get(), nodes.data(), nodes.size(), ZL_GRAPH_STORE);
    ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);

    compressor_.selectStartingGraph(graphId);

    // --- First compress: verify MParam content ---
    cctx_.refCompressor(compressor_);
    cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

    std::string src =
            "Let me think. That's just inherently very difficult. What are we doing?";
    std::vector<char> dst(1000);
    size_t compressedSize =
            cctx_.compressSerial(poly::span<char>(dst.data(), dst.size()), src);
    ASSERT_GT(compressedSize, 0u);

    // --- Serialize the compressor ---
    ZL_CompressorSerializer* serializer = ZL_CompressorSerializer_create();
    ASSERT_NE(serializer, nullptr);

    void* serialized      = nullptr;
    size_t serializedSize = 0;
    {
        ZL_Report r = ZL_CompressorSerializer_serialize(
                serializer, compressor_.get(), &serialized, &serializedSize);
        ASSERT_FALSE(ZL_isError(r));
    }

    std::vector<uint8_t> serializedCopy(
            (const uint8_t*)serialized,
            (const uint8_t*)serialized + serializedSize);
    ZL_CompressorSerializer_free(serializer);

    // --- Deserialize into a new compressor ---
    openzl::Compressor compressor2;

    // Pre-register the same custom nodes
    compressor2.registerCustomEncoder(descA);
    compressor2.registerCustomEncoder(descB);
    compressor2.registerCustomEncoder(descC);

    // Deserialize — MParams should be auto-materialized from the CBOR
    ZL_CompressorDeserializer* deserializer =
            ZL_CompressorDeserializer_create();
    ASSERT_NE(deserializer, nullptr);
    {
        ZL_Report r = ZL_CompressorDeserializer_deserialize(
                deserializer,
                compressor2.get(),
                serializedCopy.data(),
                serializedCopy.size(),
                nullptr,
                0);
        ASSERT_FALSE(ZL_isError(r));
    }
    ZL_CompressorDeserializer_free(deserializer);

    // --- Second compress with deserialized compressor ---
    openzl::CCtx cctx2;
    cctx2.refCompressor(compressor2);
    cctx2.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

    std::vector<char> dst2(1000);
    size_t compressedSize2 = cctx2.compressSerial(
            poly::span<char>(dst2.data(), dst2.size()), src);
    ASSERT_EQ(compressedSize, compressedSize2);
    ASSERT_EQ(
            std::string(dst.data(), compressedSize),
            std::string(dst2.data(), compressedSize));
}

} // namespace openzl
