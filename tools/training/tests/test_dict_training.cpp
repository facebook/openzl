// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "openzl/codecs/zl_zstd.h"
#include "openzl/codecs/zstd/common_zstd.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/Input.hpp"
#include "openzl/dict/bundle.h"
#include "openzl/dict/dict.h"
#include "openzl/dict/dict_constants.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_localParams.h"
#include "openzl/zl_reflection.h"
#include "openzl/zl_unique_id.h"

#include <zstd.h>

#include "tools/training/dict/base_dict_trainer.h"
#include "tools/training/dict/zstd_dict_trainer.h"
#include "tools/training/trained_candidate.h"
#include "tools/training/utils/utils.h"

namespace openzl::training {
namespace {

static std::vector<std::string> generateSampleData(
        size_t numSamples,
        size_t sampleSize)
{
    std::vector<std::string> samples;
    samples.reserve(numSamples);
    const std::string prefix = "HEADER_v1: common_prefix_data=";
    const std::string suffix = " END_OF_RECORD\n";
    for (size_t i = 0; i < numSamples; ++i) {
        std::string sample;
        sample.reserve(sampleSize);
        while (sample.size() < sampleSize) {
            sample += prefix;
            for (size_t j = 0; j < 32; ++j) {
                sample += static_cast<char>('A' + ((i + j) % 26));
            }
            sample += suffix;
        }
        sample.resize(sampleSize);
        samples.push_back(std::move(sample));
    }
    return samples;
}

static std::vector<MultiInput> toMultiInputs(
        const std::vector<std::string>& data)
{
    std::vector<MultiInput> inputs;
    inputs.reserve(data.size());
    for (const auto& s : data) {
        MultiInput mi;
        mi.add(Input::refSerial(s.data(), s.size()));
        inputs.push_back(std::move(mi));
    }
    return inputs;
}

// -------------------------------------------------------
// Unit tests for ZstdDictTrainer class
// -------------------------------------------------------

TEST(ZstdDictTrainer, TrainDictReturnsPackedContent)
{
    auto sampleData = generateSampleData(100, 4096);
    auto inputs     = toMultiInputs(sampleData);

    ZstdDictTrainer trainer;
    Compressor compressor;
    compressor.setParameter(CParam::CompressionLevel, 3);
    ZL_LocalParams localParams{};
    auto dictOpt = trainer.trainDict(inputs, compressor, localParams);
    ASSERT_TRUE(dictOpt.has_value());
    std::string dictContent = dictOpt.value();

    ASSERT_FALSE(dictContent.empty());
    ASSERT_GE(dictContent.size(), ZL_TRAINED_ZSTD_CONTENT_HEADER_SIZE);
    ZL_TrainedZstdContentParsed parsed{};
    ASSERT_TRUE(ZL_TrainedZstdContent_parse(
            dictContent.data(), dictContent.size(), &parsed));
    EXPECT_EQ(parsed.clevel, 3);
    EXPECT_GT(parsed.rawDictSize, 0u);
}

TEST(ZstdDictTrainer, TrainDictUsesCompressionLevelFromLocalParams)
{
    auto sampleData = generateSampleData(100, 4096);
    auto inputs     = toMultiInputs(sampleData);

    ZstdDictTrainer trainer;
    Compressor compressor;
    ZL_IntParam clevelParam = { ZSTD_c_compressionLevel, 7 };
    ZL_LocalParams localParams{
        .intParams = { .intParams = &clevelParam, .nbIntParams = 1 },
    };
    auto dictOpt = trainer.trainDict(inputs, compressor, localParams);
    ASSERT_TRUE(dictOpt.has_value());
    std::string dictContent = dictOpt.value();

    ASSERT_FALSE(dictContent.empty());
    ZL_TrainedZstdContentParsed parsed{};
    ASSERT_TRUE(ZL_TrainedZstdContent_parse(
            dictContent.data(), dictContent.size(), &parsed));
    EXPECT_EQ(parsed.clevel, 7);
}

TEST(ZstdDictTrainer, FindDictNodesReturnsEmptyForDefaultCompressor)
{
    // A default compressor has no starting graph, so the recursive
    // walk finds nothing.
    Compressor compressor;
    auto sampleData = generateSampleData(10, 256);
    auto inputs     = toMultiInputs(sampleData);
    TrainParams params{};

    auto candidate = trainDictsForCandidate(inputs, compressor, params);
    EXPECT_TRUE(candidate.dicts.empty());
}

TEST(ZstdDictTrainer, FindDictNodesFindsMultipleZstdNodes)
{
    Compressor compressor;

    // Register two distinct trainable zstd graphs, and a custom level graph
    ZL_GraphID g1 = openzl::unwrap(
            ZL_Compressor_buildTrainableZstdGraph(compressor.get()));
    ZL_GraphID g2 = openzl::unwrap(
            ZL_Compressor_buildTrainableZstdGraph(compressor.get()));
    ZL_GraphID g3 =
            ZL_Compressor_registerZstdGraph_withLevel(compressor.get(), 12);
    ASSERT_TRUE(ZL_GraphID_isValid(g1));
    ASSERT_TRUE(ZL_GraphID_isValid(g2));
    ASSERT_TRUE(ZL_GraphID_isValid(g3));

    // Create a split graph that sends data to these different graphs
    const size_t segmentSizes[4]   = { 1024, 1024, 1024, 0 };
    const ZL_GraphID successors[4] = { g1, g2, g3, ZL_GRAPH_ZSTD };
    ZL_GraphID gHead               = ZL_Compressor_registerSplitGraph(
            compressor.get(), ZL_Type_serial, segmentSizes, successors, 4);
    ZL_Report r = ZL_Compressor_selectStartingGraphID(compressor.get(), gHead);
    ASSERT_FALSE(ZL_isError(r));

    ZstdDictTrainer zstdTrainer;
    const auto nodesToTrain = zstdTrainer.findDictNodes(compressor);
    ASSERT_EQ(nodesToTrain.size(), 2);
    EXPECT_EQ(nodesToTrain[0].nodeName, "zl.trainable.zstd#0");
    EXPECT_EQ(nodesToTrain[1].nodeName, "zl.trainable.zstd#1");
}

// -------------------------------------------------------
// Unit tests for TrainedCandidate integration helpers
// -------------------------------------------------------

TEST(TrainedCandidateHelpers, ReplaceBundleID)
{
    TrainedCandidate candidate;
    EXPECT_FALSE(ZL_UniqueID_isValid(&candidate.bundleID.id));

    ZL_BundleID newID;
    newID.id = ZL_UniqueID_computeSHA256("test_bundle", 11);
    candidate.replaceBundleID(newID);

    EXPECT_TRUE(ZL_UniqueID_eq(&candidate.bundleID.id, &newID.id));
}

TEST(TrainedCandidateHelpers, ReplaceDictIDUpdatesEntryAndPackedBlob)
{
    // Build a packed dict blob using Dict_pack.
    const std::string content = "fake_dict_content_for_testing";
    std::string packed(ZL_DICT_HEADER_SIZE + content.size(), '\0');
    ZL_Report report = Dict_pack(
            packed.data(),
            packed.size(),
            ZL_DICT_ID_NULL,
            42,
            false,
            content.data(),
            content.size());
    ASSERT_FALSE(ZL_isError(report));
    packed.resize(ZL_validResult(report));

    ZL_DictID originalID = Dict_extractID(packed.data(), packed.size());
    ASSERT_TRUE(ZL_UniqueID_isValid(&originalID.id));

    TrainedCandidate candidate;
    candidate.dicts.push_back(
            TrainedCandidate::DictEntry{
                    .dictID     = originalID,
                    .packedDict = packed,
            });

    // Replace the dictID (batch of 1).
    ZL_DictID newID;
    newID.id = ZL_UniqueID_computeSHA256("replacement", 11);
    ASSERT_FALSE(ZL_UniqueID_eq(&originalID.id, &newID.id));

    candidate.replaceDictID({ originalID }, { newID });

    // The struct field should be updated.
    EXPECT_TRUE(ZL_UniqueID_eq(&candidate.dicts[0].dictID.id, &newID.id));

    // The packed blob bytes 4-35 should be rewritten.
    ZL_DictID extractedID = Dict_extractID(
            candidate.dicts[0].packedDict.data(),
            candidate.dicts[0].packedDict.size());
    EXPECT_TRUE(ZL_UniqueID_eq(&extractedID.id, &newID.id));
}

TEST(TrainedCandidateHelpers, ReplaceDictIDThrowsOnMiss)
{
    TrainedCandidate candidate;
    ZL_DictID bogusID;
    bogusID.id = ZL_UniqueID_computeSHA256("bogus", 5);
    ZL_DictID anotherID;
    anotherID.id = ZL_UniqueID_computeSHA256("another", 7);

    EXPECT_THROW(
            candidate.replaceDictID({ bogusID }, { anotherID }), Exception);
}

TEST(TrainedCandidateHelpers, ReplaceDictIDBatch)
{
    auto makePacked = [](const std::string& content, ZL_IDType codec) {
        std::string packed(ZL_DICT_HEADER_SIZE + content.size(), '\0');
        ZL_Report report = Dict_pack(
                packed.data(),
                packed.size(),
                ZL_DICT_ID_NULL,
                codec,
                false,
                content.data(),
                content.size());
        EXPECT_FALSE(ZL_isError(report));
        packed.resize(ZL_validResult(report));
        return packed;
    };

    std::string packed1 = makePacked("dict_alpha_AAAA", 42);
    std::string packed2 = makePacked("dict_bravo_BBBB", 43);
    ZL_DictID id1       = Dict_extractID(packed1.data(), packed1.size());
    ZL_DictID id2       = Dict_extractID(packed2.data(), packed2.size());

    TrainedCandidate candidate;
    candidate.dicts.push_back(
            TrainedCandidate::DictEntry{
                    .dictID     = id1,
                    .packedDict = std::move(packed1),
            });
    candidate.dicts.push_back(
            TrainedCandidate::DictEntry{
                    .dictID     = id2,
                    .packedDict = std::move(packed2),
            });

    ZL_DictID new1, new2;
    new1.id = ZL_UniqueID_computeSHA256("new1", 4);
    new2.id = ZL_UniqueID_computeSHA256("new2", 4);

    candidate.replaceDictID({ id1, id2 }, { new1, new2 });

    EXPECT_TRUE(ZL_UniqueID_eq(&candidate.dicts[0].dictID.id, &new1.id));
    EXPECT_TRUE(ZL_UniqueID_eq(&candidate.dicts[1].dictID.id, &new2.id));

    ZL_DictID ext1 = Dict_extractID(
            candidate.dicts[0].packedDict.data(),
            candidate.dicts[0].packedDict.size());
    ZL_DictID ext2 = Dict_extractID(
            candidate.dicts[1].packedDict.data(),
            candidate.dicts[1].packedDict.size());
    EXPECT_TRUE(ZL_UniqueID_eq(&ext1.id, &new1.id));
    EXPECT_TRUE(ZL_UniqueID_eq(&ext2.id, &new2.id));
}

TEST(TrainedCandidateHelpers, PackFatBundleRoundTrip)
{
    // Create two packed dicts.
    const std::string content1 = "dict_content_one_AAAAAAAAA";
    const std::string content2 = "dict_content_two_BBBBBBBBB";

    auto makePacked = [](const std::string& content, ZL_IDType codec) {
        std::string packed(ZL_DICT_HEADER_SIZE + content.size(), '\0');
        ZL_Report report = Dict_pack(
                packed.data(),
                packed.size(),
                ZL_DICT_ID_NULL,
                codec,
                false,
                content.data(),
                content.size());
        EXPECT_FALSE(ZL_isError(report));
        packed.resize(ZL_validResult(report));
        return packed;
    };

    std::string packed1 = makePacked(content1, 42);
    std::string packed2 = makePacked(content2, 43);

    ZL_DictID id1 = Dict_extractID(packed1.data(), packed1.size());
    ZL_DictID id2 = Dict_extractID(packed2.data(), packed2.size());

    TrainedCandidate candidate;
    candidate.dicts.push_back(
            TrainedCandidate::DictEntry{
                    .dictID     = id1,
                    .packedDict = std::move(packed1),
            });
    candidate.dicts.push_back(
            TrainedCandidate::DictEntry{
                    .dictID     = id2,
                    .packedDict = std::move(packed2),
            });
    candidate.bundleID = ZL_DictBundle_genBundleID(
            std::vector<ZL_DictID>{ id1, id2 }.data(), 2);

    // Pack the fat bundle.
    std::string fatBundle = candidate.packFatBundle();
    ASSERT_FALSE(fatBundle.empty());

    // Parse the BundleInfo header from the fat bundle.
    auto parseResult = ZL_BundleInfo_parse(fatBundle.data(), fatBundle.size());
    ASSERT_FALSE(ZL_RES_isError(parseResult));

    ZL_BundleInfo info = ZL_RES_value(parseResult);
    EXPECT_TRUE(info.isFatBundle);
    EXPECT_EQ(info.numDicts, 2u);
    EXPECT_TRUE(ZL_UniqueID_eq(&info.bundleID.id, &candidate.bundleID.id));

    // The dict IDs in the header should match.
    EXPECT_TRUE(ZL_UniqueID_eq(&info.dictIDs[0].id, &id1.id));
    EXPECT_TRUE(ZL_UniqueID_eq(&info.dictIDs[1].id, &id2.id));
}

TEST(TrainedCandidateHelpers, PackFatBundleThrowsOnEmpty)
{
    TrainedCandidate candidate;
    EXPECT_THROW(candidate.packFatBundle(), Exception);
}

} // namespace
} // namespace openzl::training
