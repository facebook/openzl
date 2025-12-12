// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <thread>

#include "openzl/codecs/zl_clustering.h"
#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/allocation.h"
#include "openzl/cpp/Input.hpp"
#include "openzl/zl_reflection.h"
#include "src/openzl/compress/graphs/generic_clustering_graph.h"
#include "tests/datagen/DataGen.h"
#include "tools/training/clustering/train_api.h"
#include "tools/training/clustering/utils.h"
#include "tools/training/train.h"
#include "tools/training/utils/utils.h"

namespace openzl::tests {
namespace {

static ZL_Report trivialCustomParserFn(
        ZL_Graph* graph,
        ZL_Edge* inputEdges[],
        size_t numInputs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_ERR_IF_ERR(ZL_Edge_setParameterizedDestination(
            inputEdges,
            numInputs,
            ZL_Graph_getCustomGraphs(graph).graphids[0],
            NULL));
    return ZL_returnSuccess();
}

static ZL_GraphID registerTrivialParseClusterGraph(
        openzl::Compressor& compressor)
{
    ZL_Type inputTypeMask                      = ZL_Type_serial;
    ZL_FunctionGraphDesc trivialCompressorDesc = {
        .name           = "!Trivial",
        .graph_f        = trivialCustomParserFn,
        .inputTypeMasks = &inputTypeMask,
        .nbInputs       = 1,
        .customGraphs   = NULL,
        .nbCustomGraphs = 0,
        .localParams    = {},
    };
    std::vector<ZL_GraphID> successors       = { ZL_GRAPH_STORE };
    ZL_ClusteringConfig nullClusteringConfig = { .nbClusters     = 0,
                                                 .nbTypeDefaults = 0 };
    auto clusteringGraph                     = ZL_Clustering_registerGraph(
            compressor.get(),
            &nullClusteringConfig,
            successors.data(),
            successors.size());

    std::vector<ZL_GraphID> customGraphs                 = { clusteringGraph };
    openzl::GraphParameters parsingCompressorGraphParams = {
        .customGraphs = std::move(customGraphs),
    };
    auto trivialCompressorGraph =
            compressor.registerFunctionGraph(trivialCompressorDesc);
    return compressor.parameterizeGraph(
            trivialCompressorGraph, parsingCompressorGraphParams);
}

static std::unique_ptr<Compressor> createCompressorFromSerialized(
        poly::string_view serialized)
{
    auto compressor = std::make_unique<Compressor>();
    registerTrivialParseClusterGraph(*compressor);
    compressor->deserialize(serialized);
    return compressor;
}

class TestTraining : public testing::Test {
   public:
    void TearDown() override
    {
        ALLOC_Arena_freeArena(backingArena_);
        backingArena_ = nullptr;
        successors_.clear();
        clusteringCodecs_.clear();
    }

    void SetUp() override
    {
        // Register the base clustering graph as the starting graph
        compressor_.selectStartingGraph(ZL_GRAPH_CLUSTERING);

        successors_.push_back(ZL_GRAPH_STORE);
        successors_.push_back(ZL_GRAPH_FIELD_LZ);
        successors_.push_back(ZL_GRAPH_ZSTD);
        successors_.push_back(ZL_GRAPH_COMPRESS_GENERIC);
        assert(backingArena_ == nullptr);
        backingArena_ = ALLOC_HeapArena_create();
        a1cArena_     = A1C_Arena_wrap(backingArena_);

        clusteringCodecs_.push_back(ZL_NODE_CONCAT_SERIAL);
        clusteringCodecs_.push_back(ZL_NODE_CONCAT_STRUCT);
        clusteringCodecs_.push_back(ZL_NODE_CONCAT_NUMERIC);
        clusteringCodecs_.push_back(ZL_NODE_CONCAT_STRING);
        clusteringCodecs_.push_back(ZL_NODE_INTERLEAVE_STRING);
    }

    ZL_RESULT_OF(ZL_ClusteringConfig)
    deserializeToClusteringConfig(ZL_LocalParams& lparam)
    {
        const uint8_t* config =
                (const uint8_t*)lparam.copyParams.copyParams[0].paramPtr;
        size_t configSize = lparam.copyParams.copyParams[0].paramSize;
        return ZL_Clustering_deserializeClusteringConfig(
                nullptr, config, configSize, &a1cArena_);
    }

    openzl::Input createNumericData(
            const std::vector<uint64_t>& numVec,
            int tag)
    {
        auto input = openzl::Input::refNumeric<uint64_t>(numVec);
        input.setIntMetadata(0, tag);
        return input;
    }

    openzl::Input createStringData(
            const std::string& data,
            const std::vector<uint32_t>& lens,
            int tag)
    {
        auto input = openzl::Input::refString(data, lens);
        input.setIntMetadata(0, tag);
        return input;
    }

   protected:
    Compressor compressor_{};
    std::vector<ZL_GraphID> successors_;
    std::vector<ZL_NodeID> clusteringCodecs_;
    A1C_Arena a1cArena_{};
    Arena* backingArena_{};
    openzl::tests::datagen::DataGen dataGen_;
};

TEST_F(TestTraining, TestTrainingBasic)
{
    std::vector<training::MultiInput> samples;

    auto sample1                  = training::MultiInput();
    std::vector<uint64_t> numVec1 = { 0, 1, 2, 1, 1 };
    sample1.add(createNumericData(numVec1, 0));

    std::vector<uint64_t> numVec2 = { 1, 2, 3, 2, 2 };
    sample1.add(createNumericData(numVec2, 1));

    std::string strs              = "aaabaababaaaaaaaaaaaaaa";
    std::vector<uint32_t> strLens = { 2, 5, 6, 4, 6 };
    sample1.add(createStringData(strs, strLens, 2));

    samples.emplace_back(std::move(sample1));

    std::map<std::pair<ZL_Type, size_t>, size_t> typeToDefaultSuccessorIdxMap;
    typeToDefaultSuccessorIdxMap[{ ZL_Type_serial, 1 }]  = 1;
    typeToDefaultSuccessorIdxMap[{ ZL_Type_numeric, 8 }] = 2;
    typeToDefaultSuccessorIdxMap[{ ZL_Type_string, 0 }]  = 3;

    training::TrainParams trainParams = {
        .clusteringTrainer = training::ClusteringTrainer::FullSplit
    };

    auto trainedGraphId = openzl::training::train_cluster(
            compressor_.get(),
            *backingArena_,
            samples,
            successors_,
            clusteringCodecs_,
            typeToDefaultSuccessorIdxMap,
            trainParams);
    auto lparam = ZL_Compressor_Graph_getLocalParams(
            compressor_.get(), trainedGraphId);
    auto r = deserializeToClusteringConfig(lparam);
    EXPECT_TRUE(!ZL_RES_isError(r));
    auto config = ZL_RES_value(r);
    EXPECT_EQ(config.nbClusters, 3);
}

TEST_F(TestTraining, TestTrainingGreedy)
{
    std::vector<training::MultiInput> samples;
    std::vector<std::vector<std::vector<uint64_t>>> data;

    std::map<std::pair<ZL_Type, size_t>, size_t> typeToDefaultSuccessorIdxMap;
    for (size_t i = 0; i < 10; i++) {
        auto sample = training::MultiInput();
        std::vector<std::vector<uint64_t>> numVecs;
        for (size_t j = 0; j < 10; j++) {
            auto numVec = dataGen_.randLongVector<uint64_t>(
                    "vec", 0, 1000, 100, 1000);
            sample.add(createNumericData(numVec, (int)(j + i)));
            numVecs.emplace_back(std::move(numVec));
        }
        data.emplace_back(std::move(numVecs));
        samples.emplace_back(std::move(sample));
    }
    training::TrainParams trainParams = {
        .clusteringTrainer = training::ClusteringTrainer::Greedy
    };

    auto trainedGraphId = openzl::training::train_cluster(
            compressor_.get(),
            *backingArena_,
            samples,
            successors_,
            clusteringCodecs_,
            typeToDefaultSuccessorIdxMap,
            trainParams);
    auto lparam = ZL_Compressor_Graph_getLocalParams(
            compressor_.get(), trainedGraphId);
    auto r = deserializeToClusteringConfig(lparam);
    EXPECT_TRUE(!ZL_RES_isError(r));
}

TEST_F(TestTraining, TestTrainingClusteringCodecs)
{
    std::vector<training::MultiInput> samples;

    std::map<std::pair<ZL_Type, size_t>, size_t> typeToDefaultSuccessorIdxMap;

    std::vector<std::vector<std::vector<uint64_t>>> data;
    for (size_t i = 0; i < 10; i++) {
        auto sample = training::MultiInput();
        std::vector<std::vector<uint64_t>> numVecs;
        for (size_t j = 0; j < 10; j++) {
            auto numVec = dataGen_.randLongVector<uint64_t>(
                    "vec", 0, 1000, 100, 1000);
            sample.add(createNumericData(numVec, (int)(j + i)));
            numVecs.emplace_back(std::move(numVec));
        }
        data.emplace_back(std::move(numVecs));
        samples.emplace_back(std::move(sample));
    }

    // Note: ZL_NODE_FLOAT16_DECONSTRUCT and ZL_NODE_DELTA_INT are not valid
    // because they are single input but the input is non-variable.

    // Train with insufficient clustering codecs: expect to fail.
    training::TrainParams trainParams = {
        .clusteringTrainer = training::ClusteringTrainer::Greedy
    };
    EXPECT_THROW(
            try {
                openzl::training::train_cluster(
                        compressor_.get(),
                        *backingArena_,
                        samples,
                        successors_,
                        { ZL_NODE_FLOAT16_DECONSTRUCT,
                          ZL_NODE_CONCAT_SERIAL,
                          ZL_NODE_CONCAT_NUMERIC },
                        typeToDefaultSuccessorIdxMap,
                        trainParams);
            } catch (const Exception& e) {
                EXPECT_EQ(
                        e.msg(),
                        "A clustering codec must be provided for each possible input type.");
                throw;
            },
            std::runtime_error);

    // Train with some invalid clustering codecs: expect to succeed and
    // just ignore bad codecs
    clusteringCodecs_.emplace_back(ZL_NODE_FLOAT16_DECONSTRUCT);
    clusteringCodecs_.emplace_back(ZL_NODE_DELTA_INT);

    auto trainedGraphId = openzl::training::train_cluster(
            compressor_.get(),
            *backingArena_,
            samples,
            successors_,
            clusteringCodecs_,
            typeToDefaultSuccessorIdxMap,
            trainParams);
    auto lparam = ZL_Compressor_Graph_getLocalParams(
            compressor_.get(), trainedGraphId);
    auto r = deserializeToClusteringConfig(lparam);
    EXPECT_TRUE(!ZL_RES_isError(r));
}

TEST_F(TestTraining, TestTrainingSameTagDifferentTypes)
{
    std::vector<training::MultiInput> samples;

    auto sample1                  = training::MultiInput();
    std::vector<uint64_t> numVec1 = { 0, 1, 2, 1, 1 };
    sample1.add(createNumericData(numVec1, 0));

    std::string strs              = "aaabaababaaaaaaaaaaaaaa";
    std::vector<uint32_t> strLens = { 2, 5, 6, 4, 6 };
    sample1.add(createStringData(strs, strLens, 0));

    training::TrainParams trainParams = {
        .clusteringTrainer = training::ClusteringTrainer::FullSplit
    };
    std::map<std::pair<ZL_Type, size_t>, size_t> typeToDefaultSuccessorIdxMap;
    auto trainedGraphId = openzl::training::train_cluster(
            compressor_.get(),
            *backingArena_,
            samples,
            successors_,
            clusteringCodecs_,
            typeToDefaultSuccessorIdxMap,
            trainParams);
    auto lparam = ZL_Compressor_Graph_getLocalParams(
            compressor_.get(), trainedGraphId);
    auto r = deserializeToClusteringConfig(lparam);
    EXPECT_TRUE(!ZL_RES_isError(r));
}

TEST_F(TestTraining, TestTrainingThrowsWithoutGenFunc)
{
    std::vector<training::MultiInput> inputs;
    training::TrainParams nullParams = {};
    EXPECT_THROW(
            try {
                training::train(inputs, compressor_, nullParams);
            } catch (const Exception& e) {
                EXPECT_EQ(e.msg(), "Compressor generator function is not set.");
                throw;
            },
            Exception);
}

TEST_F(TestTraining, TestTrainingCustomParserRegistrationWorks)
{
    std::vector<training::MultiInput> inputs;
    auto trivialCompressorGraph = registerTrivialParseClusterGraph(compressor_);
    compressor_.selectStartingGraph(trivialCompressorGraph);
    training::TrainParams trainParams = {
        .compressorGenFunc = createCompressorFromSerialized,
        .clusteringTrainer = training::ClusteringTrainer::Greedy,
    };
    auto serialized = training::train(inputs, compressor_, trainParams);
    EXPECT_GT(serialized.size(), 0);
}

TEST_F(TestTraining, TestTrainingDifferentTypeSameTag)
{
    std::vector<training::MultiInput> samples;

    auto sample1                  = training::MultiInput();
    std::vector<uint64_t> numVec1 = { 0, 1, 2, 1, 1 };
    sample1.add(createNumericData(numVec1, 0));

    std::vector<uint64_t> numVec2 = { 1, 2, 3, 2, 2 };
    sample1.add(createNumericData(numVec2, 1));

    std::string strs1              = "aaabaababaaaaaaaaaaaaaa";
    std::vector<uint32_t> strLens1 = { 2, 5, 6, 4, 6 };
    sample1.add(createStringData(strs1, strLens1, 0));

    std::string strs2              = "bbabaababaaaaaaaaaaaaaa";
    std::vector<uint32_t> strLens2 = { 2, 4, 7, 4, 6 };
    sample1.add(createStringData(strs1, strLens1, 1));

    samples.emplace_back(std::move(sample1));

    std::map<std::pair<ZL_Type, size_t>, size_t> typeToDefaultSuccessorIdxMap;
    typeToDefaultSuccessorIdxMap[{ ZL_Type_serial, 1 }]  = 1;
    typeToDefaultSuccessorIdxMap[{ ZL_Type_numeric, 8 }] = 2;
    typeToDefaultSuccessorIdxMap[{ ZL_Type_string, 0 }]  = 3;

    training::TrainParams trainParams = {
        .clusteringTrainer = training::ClusteringTrainer::Greedy
    };

    auto trainedGraphId = openzl::training::train_cluster(
            compressor_.get(),
            *backingArena_,
            samples,
            successors_,
            clusteringCodecs_,
            typeToDefaultSuccessorIdxMap,
            trainParams);
    auto lparam = ZL_Compressor_Graph_getLocalParams(
            compressor_.get(), trainedGraphId);
    auto r = deserializeToClusteringConfig(lparam);
    EXPECT_TRUE(!ZL_RES_isError(r));
}

} // namespace
} // namespace openzl::tests
