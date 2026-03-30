// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/common/debug.h" // ZL_REQUIRE
#include "openzl/compress/selectors/ml/gbt.h"
#include "openzl/compress/selectors/ml/mlselector.h"
#include "openzl/shared/mem.h" // For ZS2_read64
#include "openzl/zl_compress.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_selector.h" // ZL_SelectorDesc
#include "tests/datagen/DataGen.h"
#include "tests/ml_selector_utils.h"

#define EXPECT_SUCCESS(r)                                          \
    EXPECT_FALSE(ZL_isError(r)) << "Zstrong failed with message: " \
                                << ZL_CCtx_getErrorContextString(cctx_, r)

namespace {
auto deltaFeatureGenerator(
        const ZL_Input* inputStream,
        VECTOR(LabeledFeature) * features)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    // Calculates nbElts, eltWidth and hasConstDelta features. hasConstDelta
    // represents whether or not the stream is an arithmetic sequence, which
    // will always have a constant delta since the difference between each ith
    // and (i+1)th element is the same.

    const void* data      = ZL_Input_ptr(inputStream);
    const size_t nbElts   = ZL_Input_numElts(inputStream);
    const size_t eltWidth = ZL_Input_eltWidth(inputStream);

    uint64_t hasConstDelta = 0;
    if (nbElts >= 2) {
        const uint64_t firstValue  = ZL_read64((const uint8_t*)data);
        const uint64_t secondValue = ZL_read64((const uint8_t*)data + eltWidth);

        const uint64_t initialDelta = secondValue - firstValue;
        uint64_t prevValue          = secondValue;

        for (size_t i = 2; i < nbElts; i++) {
            const uint64_t currValue =
                    ZL_read64((const uint8_t*)data + i * eltWidth);
            if (currValue - prevValue != initialDelta) {
                hasConstDelta = 1;
                break;
            }
            prevValue = currValue;
        }
    }

    LabeledFeature nbEltsFeature        = { "nbElts", (float)nbElts };
    LabeledFeature eltWidthFeature      = { "eltWidth", (float)eltWidth };
    LabeledFeature hasConstDeltaFeature = { "hasConstDelta",
                                            (float)hasConstDelta };

    bool badAlloc = false;
    badAlloc |= !VECTOR_PUSHBACK(*features, nbEltsFeature);
    badAlloc |= !VECTOR_PUSHBACK(*features, eltWidthFeature);
    badAlloc |= !VECTOR_PUSHBACK(*features, hasConstDeltaFeature);

    ZL_ERR_IF(badAlloc, allocation, "Failed to add features to vector");
    return ZL_returnSuccess();
}

// Creates a GBTModel utilizing a custom deltaFeatureGenerator alongside
// corresponding feature labels and binary class labels. This model's predictor
// contains one forest with a single tree possessing three nodes. The root
// node examines whether the stream has a const delta throughout
// (hasConstDelta < 0.5), and if so, it returns the value of the left child
// node (which is less than 0), effectively assigning the label class1.

const std::vector<GBTPredictor_Node> nodes = { { .featureIdx      = 0,
                                                 .value           = 0.5f,
                                                 .leftChildIdx    = 1,
                                                 .rightChildIdx   = 2,
                                                 .missingChildIdx = 1 },
                                               { .featureIdx      = -1,
                                                 .value           = -0.1f,
                                                 .leftChildIdx    = 0,
                                                 .rightChildIdx   = 0,
                                                 .missingChildIdx = 0 },
                                               { .featureIdx      = -1,
                                                 .value           = 0.7f,
                                                 .leftChildIdx    = 0,
                                                 .rightChildIdx   = 0,
                                                 .missingChildIdx = 0 } };

const GBTPredictor_Tree tree            = { .numNodes = nodes.size(),
                                            .nodes    = nodes.data() };
const GBTPredictor_Forest forest        = { .numTrees = 1, .trees = &tree };
const GBTPredictor binaryClassPredictor = {
    .numForests = 1,
    .forests    = &forest,
};
const std::vector<Label> featureLabels = {
    Label("hasConstDelta"),
    Label("nbElts"),
    Label("eltWidth"),
};

const GBTModel gbtModel = {
    .predictor        = &binaryClassPredictor,
    .featureGenerator = deltaFeatureGenerator,
    .nbSuccessors     = 2,
    .nbFeatures       = featureLabels.size(),
    .featureLabels    = featureLabels.data(),
};

ZL_GraphID selectGBTModel(
        const ZL_Selector*,
        const ZL_Input* in,
        const ZL_GraphID* graphs,
        size_t nbGraphs) noexcept
{
    // Selects subgraph based on the prediction from gbtModel. From comment
    // above, we know that the gbtModel is guaranteed to return class1 when the
    // stream is an arithmetic sequence. We assume that the first graph
    // represents delta and the second graph represents tokenize, we return the
    // first graph when label is class1. If there is any errors,
    // the first graph is returned
    ZL_RESULT_OF(size_t) result = GBTModel_predict(&gbtModel, in);
    if (ZL_RES_isError(result)) {
        return graphs[0];
    }
    return graphs[ZL_RES_value(result)];
}
} // namespace

namespace openzl::tests {
namespace {

class MLSelectorTest : public ::testing::Test {
   protected:
    ZL_CCtx* cctx_         = nullptr;
    ZL_Compressor* cgraph_ = nullptr;
    ZL_GraphID deltaGid_;
    ZL_GraphID tokenizeGid_;

    static ZL_GraphID selectDelta(
            const ZL_Selector* selCtx,
            const ZL_Input* in,
            const ZL_GraphID* graphs,
            size_t nbGraphs) noexcept
    {
        (void)selCtx;
        (void)in;
        (void)nbGraphs;
        return graphs[0];
    }

    static ZL_GraphID selectTokenize(
            const ZL_Selector* selCtx,
            const ZL_Input* in,
            const ZL_GraphID* graphs,
            size_t nbGraphs) noexcept
    {
        (void)selCtx;
        (void)in;
        (void)nbGraphs;
        return graphs[1];
    }

    std::vector<uint64_t> deltaData;
    std::vector<uint64_t> tokenizeData;

    ZS2_MLModel_Desc* zs2_model;
    ZL_MLSelectorDesc mlSelector;
    std::vector<ZL_LabeledGraphID> labeledGraphs;

    void SetUp() override
    {
        cctx_ = ZL_CCtx_create();
        // EXPECT_NE(cctx_, NULL);
        cgraph_ = ZL_Compressor_create();
        // EXPECT_NE(cgraph_, NULL);

        deltaGid_ = ZL_Compressor_registerStaticGraph_fromNode1o(
                cgraph_, ZL_NODE_DELTA_INT, ZL_GRAPH_ZSTD);
        // EXPECT_TRUE(ZL_GraphID_isValid(deltaGid_));

        tokenizeGid_ = ZL_Compressor_registerTokenizeGraph(
                cgraph_, ZL_Type_numeric, true, deltaGid_, ZL_GRAPH_ZSTD);
        // EXPECT_TRUE(ZL_GraphID_isValid(tokenizeGid_));

        deltaData = generateDeltaData();
        auto dg   = openzl::tests::datagen::DataGen();
        // Generate data with repeated values
        tokenizeData = dg.template randLongVector<uint64_t>(
                "randLongVec", 0, 500, 10000, 10000);

        labeledGraphs.push_back({ .label = "class1", .graph = deltaGid_ });
        labeledGraphs.push_back({ .label = "class2", .graph = tokenizeGid_ });
    }

    void TearDown() override
    {
        ZL_Compressor_free(cgraph_);
        cgraph_ = nullptr;
        ZL_CCtx_free(cctx_);
        cctx_ = nullptr;
        labeledGraphs.clear();
    }

    std::vector<uint8_t> compress(std::vector<uint64_t> const& data)
    {
        ZL_GraphID const gid = ZL_Compressor_registerGBTModelGraph(
                cgraph_,
                const_cast<GBTModel*>((GBTModel const*)&gbtModel),
                labeledGraphs.data(),
                labeledGraphs.size());
        return compress(data, gid);
    }

    std::vector<uint8_t> compress(
            std::vector<uint64_t> const& data,
            ZL_GraphID const gid)
    {
        std::vector<uint8_t> compressed(
                ZL_compressBound(data.size() * sizeof(data[0])));

        EXPECT_TRUE(ZL_GraphID_isValid(gid));

        EXPECT_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, gid));

        EXPECT_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));

        ZL_TypedRef* const tref = ZL_TypedRef_createNumeric(
                data.data(), sizeof(data[0]), data.size());
        EXPECT_NE(tref, nullptr);

        // We need a version with typed input support
        EXPECT_SUCCESS(ZL_CCtx_setParameter(
                cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

        ZL_Report const r = ZL_CCtx_compressTypedRef(
                cctx_, compressed.data(), compressed.size(), tref);
        EXPECT_SUCCESS(r);

        compressed.resize(ZL_validResult(r));

        ZL_TypedRef_free(tref);

        return compressed;
    }

    std::vector<uint8_t> compress(
            std::vector<uint64_t> const& data,
            const ZL_SelectorDesc& selector)
    {
        ZL_GraphID const gid =
                ZL_Compressor_registerSelectorGraph(cgraph_, &selector);
        return compress(data, gid);
    }
    std::vector<uint8_t> compress(
            std::vector<uint64_t> const& data,
            ZL_SelectorFn selector_f)
    {
        std::vector<ZL_GraphID> successors = { deltaGid_, tokenizeGid_ };
        ZL_SelectorDesc selector           = {
                      .selector_f     = selector_f,
                      .inStreamType   = ZL_Type_numeric,
                      .customGraphs   = successors.data(),
                      .nbCustomGraphs = successors.size(),
        };
        return compress(data, selector);
    }
};
} // namespace

TEST_F(MLSelectorTest, Sanity)
{
    // This is just a sanity test to make sure our assumptions in the following
    // test cases hold. We are going to test that delta compresses better with
    // delta->zstd and that tokenization compresses better with
    // tokenize->[delta->zstd,zstd].
    ASSERT_LT(
            compress(deltaData, selectDelta).size(),
            compress(deltaData, selectTokenize).size());
    ASSERT_LT(
            compress(tokenizeData, selectTokenize).size(),
            compress(tokenizeData, selectDelta).size());
}

TEST_F(MLSelectorTest, HardcodedGBTSelector)
{
    // Hardcode TypedSelector that uses GBTModel to select between delta and
    // tokenize compression methods based on whether or not the stream is a
    // arithmetic sequence or not (delta is zero throughout all of the
    // sequence). This means that the resulting compression for delta data
    // should have the same resulting size as if you were to use the selectDelta
    // selector. Similarly, the size for tokenize data should be the same size
    // as if you were to use selectTokenize.
    ZL_SelectorFn selectML = selectGBTModel;

    ASSERT_EQ(
            compress(tokenizeData, selectTokenize).size(),
            compress(tokenizeData, selectML).size());
    ASSERT_EQ(
            compress(deltaData, selectDelta).size(),
            compress(deltaData, selectML).size());
}

TEST_F(MLSelectorTest, SimpleMLSelectorDelta)
{
    // Uses the mlSelector to select between delta and tokenize compression. The
    // mlSelector uses the same underlying gbtModel as the HardcodedGBTSelector
    // test case. This means that the resulting compression for delta
    // data should be the same size as if you were to use the
    // selectDelta selector.

    ASSERT_EQ(compress(deltaData), compress(deltaData, selectDelta));
}

TEST_F(MLSelectorTest, SimpleMLSelectorTokenize)
{
    // Uses the mlSelector to select between delta and tokenize compression. The
    // mlSelector uses the same underlying gbtModel as the HardcodedGBTSelector
    // test case. This means that the resulting compression for tokenize data
    // should be the same size as if you were to use the selectTokenize
    // selector.

    ASSERT_EQ(
            compress(tokenizeData).size(),
            compress(tokenizeData, selectTokenize).size());
}
} // namespace openzl::tests
