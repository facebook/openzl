// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/zl_config.h"

#if ZL_ALLOW_INTROSPECTION
#    include <gtest/gtest.h>

#    include "openzl/cpp/CCtx.hpp"
#    include "openzl/cpp/Compressor.hpp"
#    include "openzl/zl_compressor.h"
#    include "openzl/zl_input.h"
#    include "openzl/zl_segmenter.h"
#    include "tests/datagen/DataGen.h"
using namespace ::testing;

namespace openzl {
constexpr size_t SMALL_CHUNK_SIZE = 200;

// Segmenter function for numeric input
static ZL_Report numericChunkSegmenterFn(ZL_Segmenter* sctx)
{
    assert(ZL_Segmenter_numInputs(sctx) == 1);
    const ZL_Input* input = ZL_Segmenter_getInput(sctx, 0);
    assert(ZL_Input_type(input) == ZL_Type_numeric);

    size_t eltWidth = ZL_Input_eltWidth(input);

    while (ZL_Input_numElts(input) > 0) {
        size_t inElts       = ZL_Input_numElts(input);
        size_t maxChunkElts = SMALL_CHUNK_SIZE / eltWidth;
        size_t chunkNumElts = (inElts < maxChunkElts) ? inElts : maxChunkElts;

        // Using flatpack since it is static and has two outputs.
        ZL_Report processR = ZL_Segmenter_processChunk(
                sctx, &chunkNumElts, 1, ZL_GRAPH_FLATPACK, NULL);
        if (ZL_isError(processR)) {
            return processR;
        }
        // Update input pointer for next iteration
        input = ZL_Segmenter_getInput(sctx, 0);
    }

    return ZL_returnSuccess();
}

// Segmenter descriptor for numeric input
static ZL_SegmenterDesc const numericChunkSegmenter = {
    .name           = "NumericChunkSegmenter",
    .segmenterFn    = numericChunkSegmenterFn,
    .inputTypeMasks = (const ZL_Type[]){ ZL_Type_numeric },
    .numInputs      = 1,
};

// Helper to register the numeric chunk segmenter
static ZL_GraphID registerNumericChunkSegmenter(
        ZL_Compressor* compressor) noexcept
{
    ZL_Report const r = ZL_Compressor_setParameter(
            compressor, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION);
    if (ZL_isError(r)) {
        abort();
    }
    return ZL_Compressor_registerSegmenter(compressor, &numericChunkSegmenter);
}

class StreamdumpTest : public ::testing::Test {
   protected:
    void SetUp() override
    {
        compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);
    }

    void generateAndCompressData(size_t sz = 1000, bool force_size = false)
    {
        auto dg = openzl::tests::datagen::DataGen();
        data    = dg.template randVector<int64_t>("randVec", 0, 100, sz);
        if (force_size) {
            data.resize(sz);
        }
        auto input =
                Input::refNumeric(data.data(), sizeof(int64_t), data.size());

        compressData(input);
    }

    void compressData(Input& input)
    {
        cctx.refCompressor(compressor);
        cctx.writeTraces(true, true);

        auto compressed  = cctx.compressOne(input);
        auto traceResult = cctx.getLatestTrace();

        trace      = traceResult.first;
        streamdump = traceResult.second;

        EXPECT_FALSE(trace.empty());
        EXPECT_FALSE(streamdump.empty());
    }

    std::string trace;
    std::map<std::string, std::pair<poly::string_view, poly::string_view>>
            streamdump;
    std::vector<int64_t> data;

    Compressor compressor;
    CCtx cctx;
};

TEST_F(StreamdumpTest, VerifyStreamdump)
{
    generateAndCompressData();
    EXPECT_FALSE(trace.empty());
    EXPECT_FALSE(streamdump.empty());

    // Verify all streams are from chunk 0 (single chunk compression)
    // All streams are unique since streamdump is a map
    for (const auto& [key, value] : streamdump) {
        EXPECT_TRUE(key.find("chunk_0_") == 0);
    }
}

TEST_F(StreamdumpTest, VerifyMultiChunkStreamdump)
{
    ZL_GraphID segmenterId = registerNumericChunkSegmenter(compressor.get());
    compressor.selectStartingGraph(segmenterId);
    generateAndCompressData(1000, true);
    int chunkSize = SMALL_CHUNK_SIZE / sizeof(int64_t);
    int numDataChunks =
            (data.size() + chunkSize - 1) / chunkSize; // ceiling division
    int numChunks =
            numDataChunks + 1; // +1 for the main/wrapper chunk at index 0
    std::map<int, int> streamsPerChunk;

    // Verify streams are from chunks between 0 and numChunks
    for (const auto& [key, value] : streamdump) {
        // Extract chunk ID from key format
        // "chunk_{chunkId}_stream{streamId}"
        size_t chunkStart = key.find("chunk_") + 6;
        size_t chunkEnd   = key.find("_stream");
        ASSERT_NE(chunkEnd, std::string::npos) << "Invalid key format: " << key;
        int chunkId = std::stoi(key.substr(chunkStart, chunkEnd - chunkStart));
        streamsPerChunk[chunkId]++;

        EXPECT_GE(chunkId, 0) << "Chunk ID should be >= 0, got: " << chunkId;
        EXPECT_LT(chunkId, numChunks)
                << "Chunk ID should be < " << numChunks << ", got: " << chunkId;
    }

    // Check that all data chunks have the same number of streams
    ASSERT_FALSE(streamsPerChunk.empty()) << "No streams found in streamdump";
    int expectedNumStreams = streamsPerChunk.begin()->second;
    for (const auto& [chunkId, numStreams] : streamsPerChunk) {
        std::cout << "Chunk " << chunkId << ": " << numStreams << " streams"
                  << std::endl;
        EXPECT_EQ(numStreams, expectedNumStreams)
                << "Chunk " << chunkId << " has " << numStreams
                << " streams, expected: " << expectedNumStreams;
    }
}

TEST_F(StreamdumpTest, TruncateStreamPreview)
{
    std::vector<int64_t> basicData(100000, 10);
    auto input = Input::refNumeric(
            basicData.data(), sizeof(int64_t), basicData.size());

    compressData(input);

    EXPECT_FALSE(trace.empty());
    EXPECT_LE(trace.size(), 5000);
}

} // namespace openzl
#endif // ZL_ALLOW_INTROSPECTION
