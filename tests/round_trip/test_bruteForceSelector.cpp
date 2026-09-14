// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/common/debug.h"
#include "openzl/compress/cctx.h"
#include "openzl/compress/codec_output_cache.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_codec_output_cache.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_segmenter.h"

using namespace testing;
namespace openzl::tests {

#define EXPECT_SUCCESS(r)                                          \
    EXPECT_FALSE(ZL_isError(r)) << "Zstrong failed with message: " \
                                << ZL_CCtx_getErrorContextString(cctx_, r)

static std::vector<uint16_t> generateNumeric(uint32_t seed)
{
    std::vector<uint16_t> data(10000);
    std::mt19937 gen(seed);
    std::uniform_int_distribution<uint16_t> dist(0, 1 << 14);
    std::generate(data.begin(), data.end(), [&]() { return dist(gen); });
    return data;
}

static std::vector<std::string> generateString(uint32_t seed)
{
    std::vector<std::string> data;
    auto bits     = generateNumeric(seed);
    char* buf     = (char*)bits.data();
    size_t bufLen = bits.size() * sizeof(bits[0]);
    std::vector<size_t> lens;
    std::mt19937 gen(seed);
    std::uniform_int_distribution<size_t> dist(1, 100);
    for (size_t idx = 0; idx < bufLen;) {
        size_t len = dist(gen);
        if (idx + len > bufLen) {
            len = bufLen - idx;
        }
        data.push_back(std::string(buf + idx, len));
        idx += len;
    }
    return data;
}

static ZL_GraphID buildBruteForceGraph(
        ZL_Compressor* cgraph,
        const ZL_GraphID* successors,
        size_t numSuccessors)
{
    const auto res = ZL_Compressor_buildBruteForceSelectorGraph(
            cgraph, successors, numSuccessors);
    EXPECT_FALSE(ZL_RES_isError(res));
    return ZL_RES_isError(res) ? ZL_GRAPH_ILLEGAL : ZL_RES_value(res);
}

static ZL_Report routeInputsToCustomGraphs(
        ZL_Graph* graph,
        ZL_Edge* inputs[],
        size_t nbInputs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    const ZL_GraphIDList successors = ZL_Graph_getCustomGraphs(graph);
    ZL_ERR_IF_NE(nbInputs, successors.nbGraphIDs, graph_invalidNumInputs);
    for (size_t i = 0; i < nbInputs; ++i) {
        ZL_ERR_IF_ERR(
                ZL_Edge_setDestination(inputs[i], successors.graphids[i]));
    }
    return ZL_returnSuccess();
}

static ZL_Report twoEqualChunksSegmenter(ZL_Segmenter* sctx) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    ZL_ERR_IF_NE(ZL_Segmenter_numInputs(sctx), 1, graph_invalidNumInputs);
    const ZL_GraphIDList graphs = ZL_Segmenter_getCustomGraphs(sctx);
    ZL_ERR_IF_NE(graphs.nbGraphIDs, 1, graphParameter_invalid);

    const ZL_Input* input = ZL_Segmenter_getInput(sctx, 0);
    ZL_ERR_IF_NULL(input, node_invalid_input);
    const size_t totalElts = ZL_Input_numElts(input);
    ZL_ERR_IF_EQ(totalElts, 0, node_invalid_input);
    ZL_ERR_IF_NE(totalElts % 2, 0, node_invalid_input);
    const size_t chunkElts = totalElts / 2;
    ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
            sctx, &chunkElts, 1, graphs.graphids[0], nullptr));

    input = ZL_Segmenter_getInput(sctx, 0);
    ZL_ERR_IF_NULL(input, node_invalid_input);
    ZL_ERR_IF_NE(ZL_Input_numElts(input), chunkElts, node_invalid_input);
    return ZL_Segmenter_processChunk(
            sctx, &chunkElts, 1, graphs.graphids[0], nullptr);
}

static ZL_Report twoEqualChunksDifferentGraphsSegmenter(
        ZL_Segmenter* sctx) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
    ZL_ERR_IF_NE(ZL_Segmenter_numInputs(sctx), 1, graph_invalidNumInputs);
    const ZL_GraphIDList graphs = ZL_Segmenter_getCustomGraphs(sctx);
    ZL_ERR_IF_NE(graphs.nbGraphIDs, 2, graphParameter_invalid);

    const ZL_Input* input = ZL_Segmenter_getInput(sctx, 0);
    ZL_ERR_IF_NULL(input, node_invalid_input);
    const size_t totalElts = ZL_Input_numElts(input);
    ZL_ERR_IF_EQ(totalElts, 0, node_invalid_input);
    ZL_ERR_IF_NE(totalElts % 2, 0, node_invalid_input);
    const size_t chunkElts = totalElts / 2;
    ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
            sctx, &chunkElts, 1, graphs.graphids[0], nullptr));

    input = ZL_Segmenter_getInput(sctx, 0);
    ZL_ERR_IF_NULL(input, node_invalid_input);
    ZL_ERR_IF_NE(ZL_Input_numElts(input), chunkElts, node_invalid_input);
    return ZL_Segmenter_processChunk(
            sctx, &chunkElts, 1, graphs.graphids[1], nullptr);
}

enum class CacheHitCheck {
    Required,
    Skipped,
};

class BruteForceSelectorTest : public ::testing::Test {
   protected:
    ZL_Compressor* cgraph_;
    ZL_CCtx* cctx_;
    ZL_DCtx* dctx_;

    void SetUp() override
    {
        cctx_   = ZL_CCtx_create();
        cgraph_ = ZL_Compressor_create();
        dctx_   = ZL_DCtx_create();
        CCTX_setTryGraphCacheStatsEnabled(cctx_, true);
        ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
                cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    }

    void TearDown() override
    {
        ZL_DCtx_free(dctx_);
        ZL_Compressor_free(cgraph_);
        ZL_CCtx_free(cctx_);
    }

    CodecCache_Stats lastChunkTryGraphCacheStats() const
    {
        CodecCache_Stats stats{};
        CCTX_getLastChunkTryGraphCacheStats(&stats, cctx_);
        return stats;
    }

    void enableTryGraphCache()
    {
        ZL_REQUIRE_SUCCESS(ZL_CCtx_setTryGraphCacheBudget(
                cctx_, CodecCache_getDefaultMaxBytes()));
    }

    void roundTripWithGid(
            ZL_TypedRef* data,
            ZL_GraphID gid,
            CacheHitCheck cacheHitCheck)
    {
        size_t sz = ZL_Input_contentSize(data);
        if (ZL_Input_type(data) == ZL_Type_string) {
            sz += ZL_Input_numElts(data) * sizeof(uint32_t);
        }
        auto encCap = ZL_compressBound(sz);
        std::string enc(encCap, '\0');
        if (cacheHitCheck == CacheHitCheck::Required) {
            enableTryGraphCache();
        }
        EXPECT_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, gid));
        EXPECT_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
        auto report =
                ZL_CCtx_compressTypedRef(cctx_, enc.data(), enc.size(), data);
        EXPECT_SUCCESS(report);
        if (cacheHitCheck == CacheHitCheck::Required) {
            const CodecCache_Stats stats = lastChunkTryGraphCacheStats();
            EXPECT_GT(stats.misses, 0);
            EXPECT_GT(stats.hits, 0);
        }

        // check to make sure the stuff is actually working
        std::string encBaseline(encCap, '\0');
        ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
                cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
        EXPECT_SUCCESS(ZL_Compressor_selectStartingGraphID(
                cgraph_, ZL_GRAPH_COMPRESS_GENERIC));
        EXPECT_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
        auto reportBaseline = ZL_CCtx_compressTypedRef(
                cctx_, encBaseline.data(), encBaseline.size(), data);
        EXPECT_SUCCESS(reportBaseline);
        EXPECT_EQ(CCTX_getCodecOutputCache(cctx_), nullptr);
        EXPECT_LE(ZL_validResult(report), ZL_validResult(reportBaseline));

        // roundtrip
        ZL_TypedBuffer* regen = ZL_TypedBuffer_create();
        EXPECT_SUCCESS(ZL_DCtx_decompressTBuffer(
                dctx_, regen, enc.data(), ZL_validResult(report)));
        EXPECT_EQ(ZL_Input_contentSize(data), ZL_TypedBuffer_byteSize(regen));
        EXPECT_EQ(
                0,
                memcmp(ZL_Input_ptr(data),
                       ZL_TypedBuffer_rPtr(regen),
                       ZL_Input_contentSize(data)));
        ZL_TypedBuffer_free(regen);
    }
};
TEST_F(BruteForceSelectorTest, testNumeric)
{
    auto dataVec = generateNumeric(0);
    auto* data   = ZL_TypedRef_createNumeric(
            dataVec.data(), sizeof(dataVec[0]), dataVec.size());
    ZL_GraphID succs[] = { ZL_GRAPH_HUFFMAN,
                           ZL_GRAPH_FIELD_LZ,
                           ZL_GRAPH_BITPACK,
                           ZL_GRAPH_RANGE_PACK_ZSTD };
    const auto gid     = buildBruteForceGraph(
            cgraph_, succs, sizeof(succs) / sizeof(succs[0]));

    roundTripWithGid(data, gid, CacheHitCheck::Required);
    ZL_TypedRef_free(data);
}

TEST_F(BruteForceSelectorTest, testSelectedGraphReusesTrialResult)
{
    auto dataVec = generateNumeric(0);
    auto* data   = ZL_TypedRef_createNumeric(
            dataVec.data(), sizeof(dataVec[0]), dataVec.size());
    const ZL_GraphID successor = ZL_GRAPH_COMPRESS_GENERIC;
    const auto gid             = buildBruteForceGraph(cgraph_, &successor, 1);

    roundTripWithGid(data, gid, CacheHitCheck::Required);
    ZL_TypedRef_free(data);
}

TEST_F(BruteForceSelectorTest, testCompressionWithoutTryGraphClearsCacheStats)
{
    enableTryGraphCache();
    const std::vector<uint16_t> dataVec = generateNumeric(0);
    std::unique_ptr<ZL_TypedRef, decltype(&ZL_TypedRef_free)> data(
            ZL_TypedRef_createNumeric(
                    dataVec.data(), sizeof(dataVec[0]), dataVec.size()),
            &ZL_TypedRef_free);
    ASSERT_NE(data, nullptr);

    const ZL_GraphID successor = ZL_GRAPH_COMPRESS_GENERIC;
    const ZL_GraphID selector  = buildBruteForceGraph(cgraph_, &successor, 1);
    ASSERT_TRUE(ZL_GraphID_isValid(selector));
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, selector));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));

    const size_t capacity = ZL_compressBound(ZL_Input_contentSize(data.get()));
    std::string selectorOutput(capacity, '\0');
    ZL_REQUIRE_SUCCESS(ZL_CCtx_compressTypedRef(
            cctx_, selectorOutput.data(), selectorOutput.size(), data.get()));
    const CodecCache_Stats selectorStats = lastChunkTryGraphCacheStats();
    ASSERT_GT(selectorStats.hits, 0);

    ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
            cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    ZL_REQUIRE_SUCCESS(
            ZL_Compressor_selectStartingGraphID(cgraph_, ZL_GRAPH_STORE));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
    std::string storeOutput(capacity, '\0');
    ZL_REQUIRE_SUCCESS(ZL_CCtx_compressTypedRef(
            cctx_, storeOutput.data(), storeOutput.size(), data.get()));

    EXPECT_EQ(CCTX_getCodecOutputCache(cctx_), nullptr);
    const CodecCache_Stats storeStats = lastChunkTryGraphCacheStats();
    EXPECT_EQ(storeStats.hits, 0);
    EXPECT_EQ(storeStats.misses, 0);
    EXPECT_EQ(storeStats.inserts, 0);
}

TEST_F(BruteForceSelectorTest, testAutomaticCacheBudgetControlsCaching)
{
    auto dataVec = generateNumeric(0);
    std::unique_ptr<ZL_TypedRef, decltype(&ZL_TypedRef_free)> data(
            ZL_TypedRef_createNumeric(
                    dataVec.data(), sizeof(dataVec[0]), dataVec.size()),
            &ZL_TypedRef_free);
    ASSERT_NE(data, nullptr);
    const ZL_GraphID successor = ZL_GRAPH_COMPRESS_GENERIC;
    const ZL_GraphID selector  = buildBruteForceGraph(cgraph_, &successor, 1);
    ASSERT_TRUE(ZL_GraphID_isValid(selector));
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, selector));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));

    const size_t capacity = ZL_compressBound(ZL_Input_contentSize(data.get()));
    std::string disabledByDefaultOutput(capacity, '\0');
    const ZL_Report disabledByDefaultReport = ZL_CCtx_compressTypedRef(
            cctx_,
            disabledByDefaultOutput.data(),
            disabledByDefaultOutput.size(),
            data.get());
    ASSERT_FALSE(ZL_isError(disabledByDefaultReport));
    const CodecCache_Stats disabledByDefaultStats =
            lastChunkTryGraphCacheStats();
    EXPECT_EQ(disabledByDefaultStats.hits, 0);
    EXPECT_EQ(disabledByDefaultStats.misses, 0);
    EXPECT_EQ(disabledByDefaultStats.inserts, 0);

    enableTryGraphCache();
    ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
            cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, selector));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
    std::string enabledOutput(capacity, '\0');
    const ZL_Report enabledReport = ZL_CCtx_compressTypedRef(
            cctx_, enabledOutput.data(), enabledOutput.size(), data.get());
    ASSERT_FALSE(ZL_isError(enabledReport));
    ASSERT_EQ(
            ZL_validResult(disabledByDefaultReport),
            ZL_validResult(enabledReport));
    EXPECT_TRUE(
            std::equal(
                    disabledByDefaultOutput.begin(),
                    disabledByDefaultOutput.begin()
                            + ZL_validResult(disabledByDefaultReport),
                    enabledOutput.begin()));
    const CodecCache_Stats enabledStats = lastChunkTryGraphCacheStats();
    EXPECT_GT(enabledStats.hits, 0);
    EXPECT_GT(enabledStats.misses, 0);

    ZL_REQUIRE_SUCCESS(ZL_CCtx_setTryGraphCacheBudget(cctx_, 1));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
            cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, selector));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
    std::string constrainedOutput(capacity, '\0');
    const ZL_Report constrainedReport = ZL_CCtx_compressTypedRef(
            cctx_,
            constrainedOutput.data(),
            constrainedOutput.size(),
            data.get());
    ASSERT_FALSE(ZL_isError(constrainedReport));
    ASSERT_EQ(ZL_validResult(enabledReport), ZL_validResult(constrainedReport));
    EXPECT_TRUE(
            std::equal(
                    enabledOutput.begin(),
                    enabledOutput.begin() + ZL_validResult(enabledReport),
                    constrainedOutput.begin()));
    const CodecCache_Stats constrainedStats = lastChunkTryGraphCacheStats();
    EXPECT_EQ(constrainedStats.hits, 0);
    EXPECT_GT(constrainedStats.misses, 0);
    EXPECT_EQ(constrainedStats.inserts, 0);
    EXPECT_GT(constrainedStats.budgetSkips, 0);

    ZL_REQUIRE_SUCCESS(ZL_CCtx_setTryGraphCacheBudget(cctx_, 0));
    for (size_t attempt = 0; attempt < 2; ++attempt) {
        ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
                cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
        ZL_REQUIRE_SUCCESS(
                ZL_Compressor_selectStartingGraphID(cgraph_, selector));
        ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
        std::string disabledOutput(capacity, '\0');
        const ZL_Report disabledReport = ZL_CCtx_compressTypedRef(
                cctx_,
                disabledOutput.data(),
                disabledOutput.size(),
                data.get());
        ASSERT_FALSE(ZL_isError(disabledReport));
        ASSERT_EQ(
                ZL_validResult(enabledReport), ZL_validResult(disabledReport));
        EXPECT_TRUE(
                std::equal(
                        enabledOutput.begin(),
                        enabledOutput.begin() + ZL_validResult(enabledReport),
                        disabledOutput.begin()));

        const CodecCache_Stats disabledStats = lastChunkTryGraphCacheStats();
        EXPECT_EQ(disabledStats.hits, 0);
        EXPECT_EQ(disabledStats.misses, 0);
        EXPECT_EQ(disabledStats.inserts, 0);
    }
}

TEST_F(BruteForceSelectorTest, testAutomaticCacheDisablePreservesAttachedCache)
{
    auto dataVec = generateNumeric(0);
    std::unique_ptr<ZL_TypedRef, decltype(&ZL_TypedRef_free)> data(
            ZL_TypedRef_createNumeric(
                    dataVec.data(), sizeof(dataVec[0]), dataVec.size()),
            &ZL_TypedRef_free);
    ASSERT_NE(data, nullptr);
    const ZL_GraphID successor = ZL_GRAPH_COMPRESS_GENERIC;
    const ZL_GraphID selector  = buildBruteForceGraph(cgraph_, &successor, 1);
    ASSERT_TRUE(ZL_GraphID_isValid(selector));
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, selector));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));

    std::unique_ptr<ZL_CodecOutputCache, decltype(&ZL_CodecOutputCache_free)>
            cache(ZL_CodecOutputCache_create(), &ZL_CodecOutputCache_free);
    ASSERT_NE(cache, nullptr);
    CodecCache_setStatsEnabled(cache.get(), true);
    ZL_REQUIRE_SUCCESS(ZL_CCtx_setTryGraphCacheBudget(cctx_, 0));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_setCodecOutputCache(cctx_, cache.get()));

    std::string output(
            ZL_compressBound(ZL_Input_contentSize(data.get())), '\0');
    const ZL_Report report = ZL_CCtx_compressTypedRef(
            cctx_, output.data(), output.size(), data.get());
    ASSERT_FALSE(ZL_isError(report));
    const CodecCache_Stats stats = CodecCache_getStats(cache.get());
    EXPECT_GT(stats.hits, 0);
    EXPECT_GT(stats.misses, 0);

    ZL_REQUIRE_SUCCESS(ZL_CCtx_setCodecOutputCache(cctx_, nullptr));
}

TEST_F(BruteForceSelectorTest, testCacheIsInactiveAfterSelectedGraphSubtree)
{
    enableTryGraphCache();
    auto dataVec = generateNumeric(0);
    auto* data   = ZL_TypedRef_createNumeric(
            dataVec.data(), sizeof(dataVec[0]), dataVec.size());
    const ZL_GraphID selectedGraph = ZL_GRAPH_HUFFMAN;
    const ZL_GraphID selector =
            buildBruteForceGraph(cgraph_, &selectedGraph, 1);

    const size_t inputSize = ZL_Input_contentSize(data);
    std::string selectorOutput(ZL_compressBound(inputSize), '\0');
    EXPECT_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, selector));
    EXPECT_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
    EXPECT_SUCCESS(ZL_CCtx_compressTypedRef(
            cctx_, selectorOutput.data(), selectorOutput.size(), data));
    const CodecCache_Stats selectorStats = lastChunkTryGraphCacheStats();
    EXPECT_GT(selectorStats.hits, 0);
    EXPECT_GT(selectorStats.misses, 0);

    const ZL_GraphID successors[]       = { selector, ZL_GRAPH_ZSTD };
    const ZL_Type inputTypes[]          = { ZL_Type_numeric, ZL_Type_numeric };
    const ZL_FunctionGraphDesc rootDesc = {
        .name           = "selector followed by unrelated codec",
        .graph_f        = routeInputsToCustomGraphs,
        .inputTypeMasks = inputTypes,
        .nbInputs       = 2,
        .customGraphs   = successors,
        .nbCustomGraphs = 2,
    };
    const ZL_GraphID root =
            ZL_Compressor_registerFunctionGraph(cgraph_, &rootDesc);
    ASSERT_TRUE(ZL_GraphID_isValid(root));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
            cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    EXPECT_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, root));
    EXPECT_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));

    const ZL_TypedRef* inputs[] = { data, data };
    std::string rootOutput(2 * ZL_compressBound(inputSize), '\0');
    EXPECT_SUCCESS(ZL_CCtx_compressMultiTypedRef(
            cctx_, rootOutput.data(), rootOutput.size(), inputs, 2));
    const CodecCache_Stats rootStats = lastChunkTryGraphCacheStats();
    EXPECT_EQ(rootStats.hits, selectorStats.hits);
    EXPECT_EQ(rootStats.misses, selectorStats.misses);
    EXPECT_EQ(rootStats.inserts, selectorStats.inserts);
    EXPECT_EQ(CCTX_getCodecOutputCache(cctx_), nullptr);

    ZL_TypedRef_free(data);
}

TEST_F(BruteForceSelectorTest, testCacheAccumulatesAcrossTryGraphSubtrees)
{
    enableTryGraphCache();
    const std::vector<uint16_t> dataVec = generateNumeric(0);
    std::unique_ptr<ZL_TypedRef, decltype(&ZL_TypedRef_free)> data(
            ZL_TypedRef_createNumeric(
                    dataVec.data(), sizeof(dataVec[0]), dataVec.size()),
            &ZL_TypedRef_free);
    ASSERT_NE(data, nullptr);

    const ZL_GraphID selectedGraph = ZL_GRAPH_HUFFMAN;
    const ZL_GraphID selector =
            buildBruteForceGraph(cgraph_, &selectedGraph, 1);
    ASSERT_TRUE(ZL_GraphID_isValid(selector));

    const size_t inputSize = ZL_Input_contentSize(data.get());
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, selector));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
    std::string singleOutput(ZL_compressBound(inputSize), '\0');
    ZL_REQUIRE_SUCCESS(ZL_CCtx_compressTypedRef(
            cctx_, singleOutput.data(), singleOutput.size(), data.get()));
    const CodecCache_Stats singleStats = lastChunkTryGraphCacheStats();
    ASSERT_GT(singleStats.hits, 0);
    ASSERT_GT(singleStats.misses, 0);
    ASSERT_GT(singleStats.inserts, 0);

    const ZL_GraphID successors[]       = { selector, selector };
    const ZL_Type inputTypes[]          = { ZL_Type_numeric, ZL_Type_numeric };
    const ZL_FunctionGraphDesc rootDesc = {
        .name           = "two selectors over the same input",
        .graph_f        = routeInputsToCustomGraphs,
        .inputTypeMasks = inputTypes,
        .nbInputs       = 2,
        .customGraphs   = successors,
        .nbCustomGraphs = 2,
    };
    const ZL_GraphID root =
            ZL_Compressor_registerFunctionGraph(cgraph_, &rootDesc);
    ASSERT_TRUE(ZL_GraphID_isValid(root));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
            cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, root));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));

    const ZL_TypedRef* inputs[] = { data.get(), data.get() };
    std::string rootOutput(2 * ZL_compressBound(inputSize), '\0');
    ZL_REQUIRE_SUCCESS(ZL_CCtx_compressMultiTypedRef(
            cctx_, rootOutput.data(), rootOutput.size(), inputs, 2));
    const CodecCache_Stats rootStats = lastChunkTryGraphCacheStats();
    EXPECT_GT(rootStats.hits, singleStats.hits);
    EXPECT_EQ(rootStats.misses, singleStats.misses);
    EXPECT_EQ(rootStats.inserts, singleStats.inserts);
    EXPECT_EQ(rootStats.bytesStored, singleStats.bytesStored);
}

TEST_F(BruteForceSelectorTest, testPrivateCacheIsResetBetweenChunks)
{
    enableTryGraphCache();
    const std::vector<uint16_t> chunk = generateNumeric(0);
    std::unique_ptr<ZL_TypedRef, decltype(&ZL_TypedRef_free)> chunkInput(
            ZL_TypedRef_createNumeric(
                    chunk.data(), sizeof(chunk[0]), chunk.size()),
            &ZL_TypedRef_free);
    ASSERT_NE(chunkInput, nullptr);

    const ZL_GraphID successor = ZL_GRAPH_COMPRESS_GENERIC;
    const ZL_GraphID selector  = buildBruteForceGraph(cgraph_, &successor, 1);
    ASSERT_TRUE(ZL_GraphID_isValid(selector));
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, selector));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
    std::string singleOutput(
            ZL_compressBound(ZL_Input_contentSize(chunkInput.get())), '\0');
    const ZL_Report singleReport = ZL_CCtx_compressTypedRef(
            cctx_, singleOutput.data(), singleOutput.size(), chunkInput.get());
    ASSERT_FALSE(ZL_isError(singleReport));
    const CodecCache_Stats singleStats = lastChunkTryGraphCacheStats();
    ASSERT_GT(singleStats.hits, 0);
    ASSERT_GT(singleStats.inserts, 0);

    std::vector<uint16_t> twoChunks = chunk;
    twoChunks.insert(twoChunks.end(), chunk.begin(), chunk.end());
    std::unique_ptr<ZL_TypedRef, decltype(&ZL_TypedRef_free)> twoChunkInput(
            ZL_TypedRef_createNumeric(
                    twoChunks.data(), sizeof(twoChunks[0]), twoChunks.size()),
            &ZL_TypedRef_free);
    ASSERT_NE(twoChunkInput, nullptr);
    const ZL_Type inputType              = ZL_Type_numeric;
    const ZL_SegmenterDesc segmenterDesc = {
        .name            = "two equal chunks",
        .segmenterFn     = twoEqualChunksSegmenter,
        .inputTypeMasks  = &inputType,
        .numInputs       = 1,
        .customGraphs    = &selector,
        .numCustomGraphs = 1,
    };
    const ZL_GraphID segmenter =
            ZL_Compressor_registerSegmenter(cgraph_, &segmenterDesc);
    ASSERT_TRUE(ZL_GraphID_isValid(segmenter));

    ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
            cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, segmenter));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
    std::string chunkedOutput(
            ZL_compressBound(ZL_Input_contentSize(twoChunkInput.get())), '\0');
    const ZL_Report chunkedReport = ZL_CCtx_compressTypedRef(
            cctx_,
            chunkedOutput.data(),
            chunkedOutput.size(),
            twoChunkInput.get());
    ASSERT_FALSE(ZL_isError(chunkedReport));
    const CodecCache_Stats chunkedStats = lastChunkTryGraphCacheStats();
    EXPECT_EQ(chunkedStats.hits, singleStats.hits);
    EXPECT_EQ(chunkedStats.misses, singleStats.misses);
    EXPECT_EQ(chunkedStats.inserts, singleStats.inserts);
    EXPECT_EQ(chunkedStats.bytesStored, singleStats.bytesStored);

    ZL_TypedBuffer* const regenerated = ZL_TypedBuffer_create();
    ASSERT_NE(regenerated, nullptr);
    ASSERT_FALSE(ZL_isError(ZL_DCtx_decompressTBuffer(
            dctx_,
            regenerated,
            chunkedOutput.data(),
            ZL_validResult(chunkedReport))));
    EXPECT_EQ(
            ZL_TypedBuffer_byteSize(regenerated),
            ZL_Input_contentSize(twoChunkInput.get()));
    EXPECT_EQ(
            std::memcmp(
                    ZL_TypedBuffer_rPtr(regenerated),
                    ZL_Input_ptr(twoChunkInput.get()),
                    ZL_Input_contentSize(twoChunkInput.get())),
            0);
    ZL_TypedBuffer_free(regenerated);

    const ZL_GraphID mixedGraphs[]            = { selector, ZL_GRAPH_STORE };
    const ZL_SegmenterDesc mixedSegmenterDesc = {
        .name            = "selector then store chunks",
        .segmenterFn     = twoEqualChunksDifferentGraphsSegmenter,
        .inputTypeMasks  = &inputType,
        .numInputs       = 1,
        .customGraphs    = mixedGraphs,
        .numCustomGraphs = 2,
    };
    const ZL_GraphID mixedSegmenter =
            ZL_Compressor_registerSegmenter(cgraph_, &mixedSegmenterDesc);
    ASSERT_TRUE(ZL_GraphID_isValid(mixedSegmenter));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
            cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    ZL_REQUIRE_SUCCESS(
            ZL_Compressor_selectStartingGraphID(cgraph_, mixedSegmenter));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
    std::string mixedOutput(
            ZL_compressBound(ZL_Input_contentSize(twoChunkInput.get())), '\0');
    const ZL_Report mixedReport = ZL_CCtx_compressTypedRef(
            cctx_, mixedOutput.data(), mixedOutput.size(), twoChunkInput.get());
    ASSERT_FALSE(ZL_isError(mixedReport));
    const CodecCache_Stats mixedStats = lastChunkTryGraphCacheStats();
    EXPECT_EQ(mixedStats.hits, 0);
    EXPECT_EQ(mixedStats.misses, 0);
    EXPECT_EQ(mixedStats.inserts, 0);
}

TEST_F(BruteForceSelectorTest, testString)
{
    auto dataVec  = generateString(0);
    size_t totLen = 0;
    std::vector<uint32_t> lens;
    lens.reserve(dataVec.size());
    for (const auto& s : dataVec) {
        lens.push_back(s.size());
        totLen += s.size();
    }
    std::string catStrs(totLen, '\0');
    size_t catPtr = 0;
    for (const auto& s : dataVec) {
        std::memcpy(catStrs.data() + catPtr, s.data(), s.size());
        catPtr += s.size();
    }
    auto* data = ZL_TypedRef_createString(
            catStrs.data(), totLen, lens.data(), lens.size());

    ZL_GraphID customSucc[]      = { ZL_GRAPH_ZSTD, ZL_GRAPH_RANGE_PACK_ZSTD };
    ZL_GraphID customStringGraph = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph_, ZL_NODE_SEPARATE_STRING_COMPONENTS, customSucc, 2);
    ZL_GraphID succs[] = {
        ZL_GRAPH_COMPRESS_GENERIC,
        customStringGraph,
        (ZL_GraphID){ ZL_PrivateStandardGraphID_string_compress }
    };
    const auto gid = buildBruteForceGraph(
            cgraph_, succs, sizeof(succs) / sizeof(succs[0]));

    roundTripWithGid(data, gid, CacheHitCheck::Skipped);
    ZL_TypedRef_free(data);
}

} // namespace openzl::tests
