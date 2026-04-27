// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <cassert>
#include <cstring>

#include "openzl/zl_compressor.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_reflection.h"

#include "openzl/common/errors_internal.h"
#include "openzl/common/wire_format.h"
#include "openzl/dict/bundle.h"
#include "openzl/dict/dict.h"
#include "openzl/dict/dict_constants.h"

#include "tests/datagen/DataGen.h"

using namespace ::testing;

namespace openzl {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static ZL_RESULT_OF(ZL_VoidPtr) mockDictMaterialize(
        ZL_Materializer* matCtx,
        const void* src,
        size_t srcSize) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, nullptr);
    void* copy = ZL_Materializer_allocate(matCtx, srcSize);
    if (copy != nullptr) {
        std::memcpy(copy, src, srcSize);
    }
    return ZL_WRAP_VALUE(copy);
}

static ZL_Report passthroughFn(
        ZL_Encoder* eictx,
        const ZL_Input* inputs[],
        size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ERR_IF_NE(nbInputs, 1, GENERIC);
    auto* input  = inputs[0];
    size_t n     = ZL_Input_numElts(input);
    auto* output = ZL_Encoder_createTypedStream(eictx, 0, n, 1);
    ZL_ERR_IF_NULL(output, GENERIC);
    memcpy(ZL_Output_ptr(output), ZL_Input_ptr(input), n);
    ZL_ERR_IF_ERR(ZL_Output_commit(output, n));
    return ZL_returnSuccess();
}

static ZL_DictID makeDictID(uint8_t seed)
{
    ZL_DictID id;
    std::memset(&id, 0, sizeof(id));
    for (size_t i = 0; i < sizeof(id.id.bytes); i++) {
        id.id.bytes[i] = static_cast<uint8_t>(seed + i);
    }
    return id;
}

static ZL_DictID makeNullDictID()
{
    ZL_DictID id;
    std::memset(&id, 0, sizeof(id));
    return id;
}

/// Build a packed dict wire buffer using datagen-produced content.
static std::vector<uint8_t> buildPackedDict(
        const std::vector<uint8_t>& content,
        ZL_DictID dictID,
        ZL_IDType codec = 0)
{
    std::vector<uint8_t> buf(ZL_DICT_HEADER_SIZE + content.size(), 0);

    ZL_REQUIRE_SUCCESS(Dict_pack(
            buf.data(),
            buf.size(),
            dictID,
            codec,
            true,
            content.data(),
            content.size()));
    return buf;
}

/// Pack multiple dicts into a fat bundle.
static std::vector<uint8_t> packFatBundle(
        const std::vector<std::vector<uint8_t>>& dicts)
{
    std::vector<const void*> dictPtrs;
    std::vector<size_t> dictSizes;
    size_t totalDictBytes = 0;
    for (auto& d : dicts) {
        dictPtrs.push_back(d.data());
        dictSizes.push_back(d.size());
        totalDictBytes += d.size();
    }

    size_t bufSize = ZL_BUNDLE_HEADER_SIZE + dicts.size() * ZL_UNIQUE_ID_SIZE
            + totalDictBytes;
    std::vector<uint8_t> buf(bufSize);

    ZL_Report r = ZL_DictBundle_packFatBundle(
            buf.data(),
            buf.size(),
            dicts.empty() ? nullptr : dictPtrs.data(),
            dicts.empty() ? nullptr : dictSizes.data(),
            dicts.size());
    EXPECT_FALSE(ZL_isError(r));
    buf.resize(ZL_validResult(r));
    return buf;
}

/// Register a minimal passthrough node, optionally with a dictID.
static ZL_NodeID
registerPassthroughNode(ZL_Compressor* comp, const char* name, ZL_DictID dictID)
{
    const ZL_Type inputType  = ZL_Type_serial;
    const ZL_Type outputType = ZL_Type_serial;

    ZL_MIEncoderDesc desc;
    std::memset(&desc, 0, sizeof(desc));
    desc.gd.nbInputs             = 1;
    desc.gd.inputTypes           = &inputType;
    desc.gd.nbSOs                = 1;
    desc.gd.soTypes              = &outputType;
    desc.transform_f             = passthroughFn;
    desc.name                    = name;
    desc.dictID                  = dictID;
    desc.dictMat.materializeFn   = mockDictMaterialize;
    desc.dictMat.dematerializeFn = ZL_NOOP_DEMATERIALIZE;

    ZL_RESULT_OF(ZL_NodeID) res = ZL_Compressor_registerMIEncoder2(comp, &desc);
    assert(!ZL_RES_isError(res));
    return ZL_RES_value(res);
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class DictIndexValidationTest : public ::testing::Test {
   protected:
    void SetUp() override
    {
        comp_ = ZL_Compressor_create();
        ASSERT_NE(comp_, nullptr);
    }

    void TearDown() override
    {
        ZL_Compressor_free(comp_);
    }

    /// Generate random dict content bytes using datagen.
    std::vector<uint8_t> randomContent(const char* name, size_t size = 32)
    {
        return dg_.randVector<uint8_t>(name, 0, 255, size);
    }

    /// Register a simple graph (does NOT select or validate).
    ZL_GraphID registerSimpleGraph(ZL_NodeID headNode)
    {
        ZL_GraphID gid = ZL_Compressor_registerStaticGraph_fromNode1o(
                comp_, headNode, ZL_GRAPH_STORE);
        EXPECT_TRUE(ZL_GraphID_isValid(gid));
        return gid;
    }

    ZL_Compressor* comp_ = nullptr;
    tests::datagen::DataGen dg_;
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(DictIndexValidationTest, NoBundleLoaded_ValidationFails)
{
    ZL_NodeID node =
            registerPassthroughNode(comp_, "nodeWithDict", makeDictID(1));
    ASSERT_TRUE(ZL_NodeID_isValid(node));

    ZL_NodeID nodeNone =
            registerPassthroughNode(comp_, "nodeNoDict", makeNullDictID());
    ASSERT_TRUE(ZL_NodeID_isValid(nodeNone));

    ZL_GraphID gid = registerSimpleGraph(node);

    // If a node has a dictID and no bundle is loaded, validate returns an
    // error.
    ZL_Report r = ZL_Compressor_validate(comp_, gid);
    EXPECT_TRUE(ZL_isError(r));

    // The node without a dict should return an error from getDictIndex
    EXPECT_TRUE(ZL_isError(ZL_Compressor_Node_getDictIndex(comp_, nodeNone)));
}

TEST_F(DictIndexValidationTest, MultipleDictNodes_IndicesMatchBundleOrder)
{
    ZL_DictID id0 = makeDictID(10);
    ZL_DictID id1 = makeDictID(20);
    ZL_DictID id2 = makeDictID(30);

    ZL_NodeID node0 = registerPassthroughNode(comp_, "dict0", id0);
    ZL_NodeID node1 = registerPassthroughNode(comp_, "dict1", id1);
    ZL_NodeID node2 = registerPassthroughNode(comp_, "dict2", id2);
    ASSERT_TRUE(ZL_NodeID_isValid(node0));
    ASSERT_TRUE(ZL_NodeID_isValid(node1));
    ASSERT_TRUE(ZL_NodeID_isValid(node2));

    ZL_NodeID noDictNode =
            registerPassthroughNode(comp_, "noDict", makeNullDictID());

    ZL_GraphID gid = registerSimpleGraph(node0);

    auto c0     = randomContent("content0");
    auto c1     = randomContent("content1");
    auto c2     = randomContent("content2");
    auto p0     = buildPackedDict(c0, id0);
    auto p1     = buildPackedDict(c1, id1);
    auto p2     = buildPackedDict(c2, id2);
    auto fatBuf = packFatBundle({ p0, p1, p2 });

    ZL_REQUIRE_SUCCESS(
            ZL_Compressor_loadDictBundle(comp_, fatBuf.data(), fatBuf.size()));

    ZL_Report r = ZL_Compressor_validate(comp_, gid);
    ASSERT_FALSE(ZL_isError(r));

    ZL_Report r0 = ZL_Compressor_Node_getDictIndex(comp_, node0);
    ZL_Report r1 = ZL_Compressor_Node_getDictIndex(comp_, node1);
    ZL_Report r2 = ZL_Compressor_Node_getDictIndex(comp_, node2);
    ASSERT_FALSE(ZL_isError(r0));
    ASSERT_FALSE(ZL_isError(r1));
    ASSERT_FALSE(ZL_isError(r2));
    EXPECT_EQ(ZL_validResult(r0), 0u);
    EXPECT_EQ(ZL_validResult(r1), 1u);
    EXPECT_EQ(ZL_validResult(r2), 2u);
    EXPECT_TRUE(ZL_isError(ZL_Compressor_Node_getDictIndex(comp_, noDictNode)));
}

TEST_F(DictIndexValidationTest, MissingDictInBundle_ValidationFails)
{
    ZL_DictID dictID      = makeDictID(10);
    ZL_DictID otherDictID = makeDictID(20);

    ZL_NodeID node = registerPassthroughNode(comp_, "missingDict", dictID);
    ASSERT_TRUE(ZL_NodeID_isValid(node));
    ZL_NodeID otherNode =
            registerPassthroughNode(comp_, "otherDict", otherDictID);
    ASSERT_TRUE(ZL_NodeID_isValid(otherNode));

    ZL_NodeID nodes[2] = { node, otherNode };
    ZL_GraphID gid     = ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
            comp_, nodes, 2, ZL_GRAPH_STORE);
    EXPECT_TRUE(ZL_GraphID_isValid(gid));

    // Load a bundle that does NOT contain all the dictsb the node needs
    auto content = randomContent("otherContent");
    auto packed  = buildPackedDict(content, otherDictID);
    auto fatBuf  = packFatBundle({ packed });

    ZL_REQUIRE_SUCCESS(
            ZL_Compressor_loadDictBundle(comp_, fatBuf.data(), fatBuf.size()));

    ZL_Report r = ZL_Compressor_validate(comp_, gid);
    EXPECT_TRUE(ZL_isError(r));
}

} // namespace openzl
