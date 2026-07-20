// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <vector>

#include "openzl/dict/bundle.h"
#include "openzl/dict/dict.h"
#include "openzl/dict/dict_constants.h"

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/CParam.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/poly/Span.hpp"

#include "openzl/zl_compressor.h"
#include "openzl/zl_compressor_serialization.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_reflection.h"
#include "openzl/zl_segmenter.h"

#include "openzl/compress/cgraph.h" // ZL_Compressor_overrideGraphParams (internal)

using namespace ::testing;

namespace openzl {

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

    openzl::Compressor compressor_;
    openzl::CCtx cctx_;
    ZL_IDType nextCtid_;
};

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
    ZL_MaterializerDesc matA{};
    matA.materializeFn   = copyDictMaterialize;
    matA.dematerializeFn = ZL_NOOP_DEMATERIALIZE;

    ZL_MaterializerDesc matB{};
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

// ============================================================================
// MParam (compression-only materialized param) tests for graphs & segmenters.
// These exercise ZL_Graph_getMParam / ZL_Segmenter_getMParam, mirroring the
// codec-side ZL_Encoder_getMParam. The materializer (copyDictMaterialize)
// produces a copy of the raw blob, so the runtime object equals the content.
// ============================================================================

// Graph function that reads its MParam and copies it into a ref-param output
// buffer, so the test can verify the materialized content.
static ZL_Report graphVerifyMParamFn(
        ZL_Graph* graph,
        ZL_Edge* inputs[],
        size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_ERR_IF_NE(nbInputs, 1, GENERIC);

    const void* mparam = ZL_Graph_getMParam(graph);
    ZL_ERR_IF_NULL(mparam, GENERIC, "Expected getMParam non-null");

    const int size = ZL_Graph_getLocalIntParam(graph, 1).paramValue;
    void* out = const_cast<void*>(ZL_Graph_getLocalRefParam(graph, 3).paramRef);
    ZL_ERR_IF_NULL(out, GENERIC, "Expected output ref param non-null");
    memcpy(out, mparam, (size_t)size);

    return ZL_Edge_setDestination(inputs[0], ZL_GRAPH_STORE);
}

// Graph function asserting that getMParam is NULL when no MParam is registered.
static ZL_Report graphExpectNoMParamFn(
        ZL_Graph* graph,
        ZL_Edge* inputs[],
        size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_ERR_IF_NE(nbInputs, 1, GENERIC);
    ZL_ERR_IF_NN(
            ZL_Graph_getMParam(graph),
            GENERIC,
            "Expected getMParam NULL for graph without an MParam");
    return ZL_Edge_setDestination(inputs[0], ZL_GRAPH_STORE);
}

TEST_F(CompressorIntegrationTest,
       GIVENaFunctionGraphWithMParamWHENinvokedTHENitIsAccessible)
{
    std::string mparamContent = "function-graph-mparam-blob";
    std::string out(mparamContent.size(), 0);

    ZL_IntParam ip = { .paramId = 1, .paramValue = (int)mparamContent.size() };
    ZL_RefParam rp = { .paramId = 3, .paramRef = out.data() };
    ZL_LocalParams lp = {
        .intParams = { .intParams = &ip, .nbIntParams = 1 },
        .refParams = { .refParams = &rp, .nbRefParams = 1 },
    };

    ZL_MaterializerDesc mparamMat{};
    mparamMat.materializeFn   = copyDictMaterialize;
    mparamMat.dematerializeFn = ZL_NOOP_DEMATERIALIZE;

    static ZL_Type inputType = ZL_Type_serial;
    ZL_FunctionGraphDesc graphDesc = {
        .name           = "test_graph_getmparam",
        .graph_f        = graphVerifyMParamFn,
        .inputTypeMasks = &inputType,
        .nbInputs       = 1,
        .localParams    = lp,
        .mparamMat      = mparamMat,
        .mparam         = {
            .content = mparamContent.data(),
            .size    = mparamContent.size(),
        },
    };

    auto graphId = compressor_.registerFunctionGraph(graphDesc);
    ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);
    compressor_.selectStartingGraph(graphId);

    compressData();
    EXPECT_EQ(mparamContent, out);
    EXPECT_EQ(ZL_Compressor_numMParams(compressor_.get()), 1u);

    // Reflection getters expose the same MParam blob + materialized object.
    const ZL_MParam* reflected =
            ZL_Compressor_Graph_getMParam(compressor_.get(), graphId);
    ASSERT_NE(reflected, nullptr);
    EXPECT_EQ(
            std::string(
                    static_cast<const char*>(reflected->content),
                    reflected->size),
            mparamContent);
    const void* reflectedObj =
            ZL_Compressor_Graph_getMParamObj(compressor_.get(), graphId);
    ASSERT_NE(reflectedObj, nullptr);
    EXPECT_EQ(
            std::string(
                    static_cast<const char*>(reflectedObj),
                    mparamContent.size()),
            mparamContent);
}

TEST_F(CompressorIntegrationTest,
       GIVENaFunctionGraphWithoutMParamWHENinvokedTHENgetMParamReturnsNull)
{
    static ZL_Type inputType       = ZL_Type_serial;
    ZL_FunctionGraphDesc graphDesc = {
        .name           = "test_graph_no_mparam",
        .graph_f        = graphExpectNoMParamFn,
        .inputTypeMasks = &inputType,
        .nbInputs       = 1,
    };

    auto graphId = compressor_.registerFunctionGraph(graphDesc);
    ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);
    compressor_.selectStartingGraph(graphId);

    compressData();
    EXPECT_EQ(ZL_Compressor_numMParams(compressor_.get()), 0u);

    // Reflection getters report no MParam for a graph without one.
    EXPECT_EQ(
            ZL_Compressor_Graph_getMParam(compressor_.get(), graphId), nullptr);
    EXPECT_EQ(
            ZL_Compressor_Graph_getMParamObj(compressor_.get(), graphId),
            nullptr);
}

// Segmenter function that reads its MParam and copies it into a ref-param
// output buffer.
static ZL_Report segmenterVerifyMParamFn(ZL_Segmenter* segmenter)
        ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(segmenter);

    const void* mparam = ZL_Segmenter_getMParam(segmenter);
    ZL_ERR_IF_NULL(mparam, GENERIC, "Expected getMParam non-null");

    const int size = ZL_Segmenter_getLocalIntParam(segmenter, 1).paramValue;
    void* out      = const_cast<void*>(
            ZL_Segmenter_getLocalRefParam(segmenter, 3).paramRef);
    ZL_ERR_IF_NULL(out, GENERIC, "Expected output ref param non-null");
    memcpy(out, mparam, (size_t)size);

    // Forward the whole input as a single chunk to STORE.
    size_t numInputs = ZL_Segmenter_numInputs(segmenter);
    size_t* numElts  = (size_t*)ZL_Segmenter_getScratchSpace(
            segmenter, numInputs * sizeof(size_t));
    ZL_ERR_IF_NULL(numElts, allocation);
    ZL_ERR_IF_ERR(ZL_Segmenter_getNumElts(segmenter, numElts, numInputs));
    ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
            segmenter, numElts, numInputs, ZL_GRAPH_STORE, nullptr));
    return ZL_returnSuccess();
}

TEST_F(CompressorIntegrationTest,
       GIVENaSegmenterWithMParamWHENinvokedTHENitIsAccessible)
{
    std::string mparamContent = "segmenter-mparam-blob";
    std::string out(mparamContent.size(), 0);

    ZL_IntParam ip = { .paramId = 1, .paramValue = (int)mparamContent.size() };
    ZL_RefParam rp = { .paramId = 3, .paramRef = out.data() };
    ZL_LocalParams lp = {
        .intParams = { .intParams = &ip, .nbIntParams = 1 },
        .refParams = { .refParams = &rp, .nbRefParams = 1 },
    };

    ZL_MaterializerDesc mparamMat{};
    mparamMat.materializeFn   = copyDictMaterialize;
    mparamMat.dematerializeFn = ZL_NOOP_DEMATERIALIZE;

    static ZL_Type inputType = ZL_Type_serial;
    ZL_SegmenterDesc segDesc = {
        .name                = "test_segmenter_getmparam",
        .segmenterFn         = segmenterVerifyMParamFn,
        .inputTypeMasks      = &inputType,
        .numInputs           = 1,
        .lastInputIsVariable = false,
        .localParams         = lp,
        .mparamMat           = mparamMat,
        .mparam              = {
            .content = mparamContent.data(),
            .size    = mparamContent.size(),
        },
    };

    auto graphId = ZL_Compressor_registerSegmenter(compressor_.get(), &segDesc);
    ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);
    compressor_.selectStartingGraph(graphId);

    compressData();
    EXPECT_EQ(mparamContent, out);
    EXPECT_EQ(ZL_Compressor_numMParams(compressor_.get()), 1u);

    // Reflection getters resolve the MParam through the segmenter union member.
    const ZL_MParam* reflected =
            ZL_Compressor_Graph_getMParam(compressor_.get(), graphId);
    ASSERT_NE(reflected, nullptr);
    EXPECT_EQ(
            std::string(
                    static_cast<const char*>(reflected->content),
                    reflected->size),
            mparamContent);
    const void* reflectedObj =
            ZL_Compressor_Graph_getMParamObj(compressor_.get(), graphId);
    ASSERT_NE(reflectedObj, nullptr);
    EXPECT_EQ(
            std::string(
                    static_cast<const char*>(reflectedObj),
                    mparamContent.size()),
            mparamContent);
}

// Collects a compressor's MParams (id -> content) so two compressors' MParam
// sets can be compared for equality.
static ZL_Report collectMParamsCb(void* opaque, const ZL_MParam* mparam)
        ZL_NOEXCEPT_FUNC_PTR
{
    auto* out = static_cast<std::map<std::string, std::string>*>(opaque);
    std::string id(
            reinterpret_cast<const char*>(mparam->mparamID.id.bytes),
            sizeof(mparam->mparamID.id.bytes));
    std::string content(
            static_cast<const char*>(mparam->content), mparam->size);
    (*out)[id] = content;
    return ZL_returnSuccess();
}

TEST_F(CompressorIntegrationTest,
       GIVENaFunctionGraphWithMParamWHENserializedTHENitRoundTrips)
{
    std::string mparamContent = "function-graph-mparam-serialize";
    std::string out(mparamContent.size(), 0);

    ZL_IntParam ip = { .paramId = 1, .paramValue = (int)mparamContent.size() };
    ZL_RefParam rp = { .paramId = 3, .paramRef = out.data() };
    ZL_LocalParams lp = {
        .intParams = { .intParams = &ip, .nbIntParams = 1 },
        .refParams = { .refParams = &rp, .nbRefParams = 1 },
    };

    ZL_MaterializerDesc mparamMat{};
    mparamMat.materializeFn   = copyDictMaterialize;
    mparamMat.dematerializeFn = ZL_NOOP_DEMATERIALIZE;

    static ZL_Type inputType = ZL_Type_serial;
    auto makeGraphDesc       = [&]() -> ZL_FunctionGraphDesc {
        return ZL_FunctionGraphDesc{
            .name           = "test_graph_mparam_serialize",
            .graph_f        = graphVerifyMParamFn,
            .inputTypeMasks = &inputType,
            .nbInputs       = 1,
            .localParams    = lp,
            .mparamMat      = mparamMat,
            .mparam         = {
                    .content = mparamContent.data(),
                    .size    = mparamContent.size(),
            },
        };
    };

    ZL_FunctionGraphDesc graphDesc = makeGraphDesc();
    auto graphId = compressor_.registerFunctionGraph(graphDesc);
    ASSERT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);
    compressor_.selectStartingGraph(graphId);
    ASSERT_EQ(ZL_Compressor_numMParams(compressor_.get()), 1u);

    // First compress.
    cctx_.refCompressor(compressor_);
    cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    std::string src =
            "Let me think. That's just inherently very difficult. What are we doing?";
    std::vector<char> dst(1000);
    size_t compressedSize =
            cctx_.compressSerial(poly::span<char>(dst.data(), dst.size()), src);
    ASSERT_GT(compressedSize, 0u);

    // Serialize the compressor.
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

    // The MParam blob must actually be embedded in the serialized form, not
    // merely present via pre-registration on the destination compressor.
    EXPECT_NE(
            std::string(serializedCopy.begin(), serializedCopy.end())
                    .find(mparamContent),
            std::string::npos);

    // Deserialize into a new compressor. The function graph (which carries the
    // materializer) must be pre-registered under the same name, exactly as for
    // custom nodes; the MParam blob is round-tripped in the serialized form.
    openzl::Compressor compressor2;
    std::string out2(mparamContent.size(), 0);
    // Re-point the output ref param at compressor2's own buffer.
    rp.paramRef                     = out2.data();
    ZL_FunctionGraphDesc graphDesc2 = makeGraphDesc();
    compressor2.registerFunctionGraph(graphDesc2);
    ASSERT_EQ(ZL_Compressor_numMParams(compressor2.get()), 1u);

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

    // The deserialized compressor must expose an MParam set identical to the
    // original compressor's (same ids and blob contents).
    std::map<std::string, std::string> originalMParams;
    std::map<std::string, std::string> deserializedMParams;
    ASSERT_FALSE(ZL_isError(ZL_Compressor_forEachMParam(
            compressor_.get(), collectMParamsCb, &originalMParams)));
    ASSERT_FALSE(ZL_isError(ZL_Compressor_forEachMParam(
            compressor2.get(), collectMParamsCb, &deserializedMParams)));
    EXPECT_EQ(
            ZL_Compressor_numMParams(compressor2.get()),
            ZL_Compressor_numMParams(compressor_.get()));
    EXPECT_FALSE(originalMParams.empty());
    EXPECT_EQ(deserializedMParams, originalMParams);

    // Second compress with the deserialized compressor must match, and the
    // graph must observe its MParam again.
    openzl::CCtx cctx2;
    cctx2.refCompressor(compressor2);
    cctx2.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    std::vector<char> dst2(1000);
    size_t compressedSize2 = cctx2.compressSerial(
            poly::span<char>(dst2.data(), dst2.size()), src);
    ASSERT_EQ(compressedSize, compressedSize2);
    ASSERT_EQ(
            std::string(dst.data(), compressedSize),
            std::string(dst2.data(), compressedSize2));
    EXPECT_EQ(mparamContent, out2);
}

// ============================================================================
// Parameterized-graph MParam tests. A parameterized graph inherits its base
// graph's materializer (mparamMat) and carries its own per-instance MParam blob
// via ZL_ParameterizedGraphDesc.mparam / ZL_GraphParameters.mparam. These
// exercise the register, override (re-materialize), and serialize round-trip
// paths that complete the graph MParam API to parity with nodes.
// ============================================================================

// Graph function that only asserts its MParam is present and routes to STORE.
// It uses no ref params, so a graph parameterized from it stays serializable.
static ZL_Report graphRequireMParamFn(
        ZL_Graph* graph,
        ZL_Edge* inputs[],
        size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_ERR_IF_NE(nbInputs, 1, GENERIC);
    ZL_ERR_IF_NULL(
            ZL_Graph_getMParam(graph),
            GENERIC,
            "Expected getMParam non-null on parameterized graph");
    return ZL_Edge_setDestination(inputs[0], ZL_GRAPH_STORE);
}

TEST_F(CompressorIntegrationTest,
       GIVENaParameterizedGraphWithMParamWHENinvokedTHENitIsAccessible)
{
    const std::string mparamContent = "parameterized-graph-mparam-blob";
    std::string out(mparamContent.size(), 0);

    ZL_IntParam ip = { .paramId = 1, .paramValue = (int)mparamContent.size() };
    ZL_RefParam rp = { .paramId = 3, .paramRef = out.data() };
    ZL_LocalParams lp = {
        .intParams = { .intParams = &ip, .nbIntParams = 1 },
        .refParams = { .refParams = &rp, .nbRefParams = 1 },
    };

    ZL_MaterializerDesc mparamMat{};
    mparamMat.materializeFn   = copyDictMaterialize;
    mparamMat.dematerializeFn = ZL_NOOP_DEMATERIALIZE;

    static ZL_Type inputType = ZL_Type_serial;
    // Base function graph carries the materializer but no MParam of its own.
    ZL_FunctionGraphDesc baseDesc = {
        .name           = "test_param_base",
        .graph_f        = graphVerifyMParamFn,
        .inputTypeMasks = &inputType,
        .nbInputs       = 1,
        .localParams    = lp,
        .mparamMat      = mparamMat,
    };
    auto baseId = compressor_.registerFunctionGraph(baseDesc);
    ASSERT_NE(baseId.gid, ZL_GRAPH_ILLEGAL.gid);
    EXPECT_EQ(ZL_Compressor_numMParams(compressor_.get()), 0u);

    // Parameterize the base, attaching the per-instance MParam blob.
    const ZL_ParameterizedGraphDesc paramDesc = {
        .graph  = baseId,
        .mparam = { .content = mparamContent.data(),
                    .size    = mparamContent.size() },
    };
    const ZL_GraphID paramId = ZL_Compressor_registerParameterizedGraph(
            compressor_.get(), &paramDesc);
    ASSERT_NE(paramId.gid, ZL_GRAPH_ILLEGAL.gid);
    EXPECT_EQ(ZL_Compressor_numMParams(compressor_.get()), 1u);

    ASSERT_FALSE(ZL_isError(
            ZL_Compressor_selectStartingGraphID(compressor_.get(), paramId)));
    compressData();
    EXPECT_EQ(mparamContent, out);

    // Reflection getters expose the per-instance blob + materialized object.
    const ZL_MParam* reflected =
            ZL_Compressor_Graph_getMParam(compressor_.get(), paramId);
    ASSERT_NE(reflected, nullptr);
    EXPECT_EQ(
            std::string(
                    static_cast<const char*>(reflected->content),
                    reflected->size),
            mparamContent);
    const void* reflectedObj =
            ZL_Compressor_Graph_getMParamObj(compressor_.get(), paramId);
    ASSERT_NE(reflectedObj, nullptr);
    EXPECT_EQ(
            std::string(
                    static_cast<const char*>(reflectedObj),
                    mparamContent.size()),
            mparamContent);
}

TEST_F(CompressorIntegrationTest,
       GIVENaParameterizedGraphMParamWHENoverriddenTHENitIsReMaterialized)
{
    // Two equal-length blobs so the graph's size int-param stays valid.
    const std::string mparamContent1 = "parameterized-graph-mparam-AAAAA";
    const std::string mparamContent2 = "parameterized-graph-mparam-BBBBB";
    ASSERT_EQ(mparamContent1.size(), mparamContent2.size());
    std::string out(mparamContent1.size(), 0);

    ZL_IntParam ip = { .paramId = 1, .paramValue = (int)mparamContent1.size() };
    ZL_RefParam rp = { .paramId = 3, .paramRef = out.data() };
    ZL_LocalParams lp = {
        .intParams = { .intParams = &ip, .nbIntParams = 1 },
        .refParams = { .refParams = &rp, .nbRefParams = 1 },
    };

    ZL_MaterializerDesc mparamMat{};
    mparamMat.materializeFn   = copyDictMaterialize;
    mparamMat.dematerializeFn = ZL_NOOP_DEMATERIALIZE;

    static ZL_Type inputType      = ZL_Type_serial;
    ZL_FunctionGraphDesc baseDesc = {
        .name           = "test_param_override_base",
        .graph_f        = graphVerifyMParamFn,
        .inputTypeMasks = &inputType,
        .nbInputs       = 1,
        .localParams    = lp,
        .mparamMat      = mparamMat,
    };
    auto baseId = compressor_.registerFunctionGraph(baseDesc);
    ASSERT_NE(baseId.gid, ZL_GRAPH_ILLEGAL.gid);

    const ZL_ParameterizedGraphDesc paramDesc = {
        .graph  = baseId,
        .mparam = { .content = mparamContent1.data(),
                    .size    = mparamContent1.size() },
    };
    const ZL_GraphID paramId = ZL_Compressor_registerParameterizedGraph(
            compressor_.get(), &paramDesc);
    ASSERT_NE(paramId.gid, ZL_GRAPH_ILLEGAL.gid);
    ASSERT_FALSE(ZL_isError(
            ZL_Compressor_selectStartingGraphID(compressor_.get(), paramId)));

    compressData();
    EXPECT_EQ(mparamContent1, out);

    // Override with a new MParam blob; it must be re-materialized in place.
    const ZL_GraphParameters overrideParams = {
        .mparam = { .content = mparamContent2.data(),
                    .size    = mparamContent2.size() },
    };
    ASSERT_FALSE(ZL_isError(ZL_Compressor_overrideGraphParams(
            compressor_.get(), paramId, &overrideParams)));

    out.assign(out.size(), 0);
    compressData();
    EXPECT_EQ(mparamContent2, out);

    const ZL_MParam* reflected =
            ZL_Compressor_Graph_getMParam(compressor_.get(), paramId);
    ASSERT_NE(reflected, nullptr);
    EXPECT_EQ(
            std::string(
                    static_cast<const char*>(reflected->content),
                    reflected->size),
            mparamContent2);
}

TEST_F(CompressorIntegrationTest,
       GIVENaParameterizedGraphWithMParamWHENserializedTHENitRoundTrips)
{
    const std::string mparamContent = "parameterized-graph-mparam-serialize";

    ZL_MaterializerDesc mparamMat{};
    mparamMat.materializeFn   = copyDictMaterialize;
    mparamMat.dematerializeFn = ZL_NOOP_DEMATERIALIZE;

    static ZL_Type inputType = ZL_Type_serial;
    // Base function graph provides the materializer. Like a custom node backing
    // a serialized parameterized node, it is non-serializable and must be
    // pre-registered on the deserialization target under the same name.
    auto makeBaseDesc = [&]() -> ZL_FunctionGraphDesc {
        return ZL_FunctionGraphDesc{
            .name           = "test_param_serialize_base",
            .graph_f        = graphRequireMParamFn,
            .inputTypeMasks = &inputType,
            .nbInputs       = 1,
            .mparamMat      = mparamMat,
        };
    };

    ZL_FunctionGraphDesc baseDesc = makeBaseDesc();
    auto baseId                   = compressor_.registerFunctionGraph(baseDesc);
    ASSERT_NE(baseId.gid, ZL_GRAPH_ILLEGAL.gid);

    const ZL_ParameterizedGraphDesc paramDesc = {
        .name   = "test_param_serialize_instance",
        .graph  = baseId,
        .mparam = { .content = mparamContent.data(),
                    .size    = mparamContent.size() },
    };
    const ZL_GraphID paramId = ZL_Compressor_registerParameterizedGraph(
            compressor_.get(), &paramDesc);
    ASSERT_NE(paramId.gid, ZL_GRAPH_ILLEGAL.gid);
    ASSERT_FALSE(ZL_isError(
            ZL_Compressor_selectStartingGraphID(compressor_.get(), paramId)));
    ASSERT_EQ(ZL_Compressor_numMParams(compressor_.get()), 1u);

    // First compress.
    cctx_.refCompressor(compressor_);
    cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    const std::string src =
            "Let me think. That's just inherently very difficult. What are we doing?";
    std::vector<char> dst(1000);
    const size_t compressedSize =
            cctx_.compressSerial(poly::span<char>(dst.data(), dst.size()), src);
    ASSERT_GT(compressedSize, 0u);

    // Serialize; the blob must be embedded, not merely referenced.
    const std::string serialized = compressor_.serialize();
    EXPECT_NE(serialized.find(mparamContent), std::string::npos);

    // Deserialize into a fresh compressor with only the base pre-registered.
    openzl::Compressor compressor2;
    ZL_FunctionGraphDesc baseDesc2 = makeBaseDesc();
    compressor2.registerFunctionGraph(baseDesc2);
    compressor2.deserialize(serialized);

    // MParam sets must match (same ids and blob contents).
    std::map<std::string, std::string> originalMParams;
    std::map<std::string, std::string> deserializedMParams;
    ASSERT_FALSE(ZL_isError(ZL_Compressor_forEachMParam(
            compressor_.get(), collectMParamsCb, &originalMParams)));
    ASSERT_FALSE(ZL_isError(ZL_Compressor_forEachMParam(
            compressor2.get(), collectMParamsCb, &deserializedMParams)));
    EXPECT_FALSE(originalMParams.empty());
    EXPECT_EQ(deserializedMParams, originalMParams);

    // The deserialized parameterized graph must expose the same blob.
    const ZL_GraphID startGid = compressor2.getStartingGraph();
    const ZL_MParam* reflected =
            ZL_Compressor_Graph_getMParam(compressor2.get(), startGid);
    ASSERT_NE(reflected, nullptr);
    EXPECT_EQ(
            std::string(
                    static_cast<const char*>(reflected->content),
                    reflected->size),
            mparamContent);

    // Second compress with the deserialized compressor must match.
    openzl::CCtx cctx2;
    cctx2.refCompressor(compressor2);
    cctx2.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    std::vector<char> dst2(1000);
    const size_t compressedSize2 = cctx2.compressSerial(
            poly::span<char>(dst2.data(), dst2.size()), src);
    ASSERT_EQ(compressedSize, compressedSize2);
    ASSERT_EQ(
            std::string(dst.data(), compressedSize),
            std::string(dst2.data(), compressedSize2));
}

} // namespace openzl
