// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "openzl/common/a1cbor_helpers.h"
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "tests/utils.h"                         // @manual
#include "tools/ml_selector/ml_selector_graph.h" // @manual

namespace openzl::tests {
namespace {

class TestMLSelectorGraph : public testing::Test {
   public:
    void SetUp() override
    {
        cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    }

    ZL_GraphID getSelectorGraphWithSelectedSuccessor(
            size_t selectedSuccessor,
            openzl::Compressor& compressor)
    {
        ZL_MLSelectorConfig mlSelectorConfig = { .selectedSuccessor =
                                                         selectedSuccessor };

        auto graph = ZL_MLSelector_registerGraph(
                compressor.get(),
                &mlSelectorConfig,
                successors_.data(),
                successors_.size());
        EXPECT_FALSE(ZL_RES_isError(graph));

        return ZL_RES_value(graph);
    }

    void testSelection(
            const std::string& input,
            ZL_GraphID gid,
            ZL_GraphID sgid,
            Compressor& compressor)
    {
        auto compressBound = ZL_compressBound(input.size());

        // Compress using selected successor
        std::string cBuffer = std::string(compressBound, '\0');
        compress(compressor, cBuffer, input, sgid);

        // Compress using ml selector graph
        std::string scBuffer = std::string(compressBound, '\0');
        compress(compressor, scBuffer, input, gid);

        // Check that the ml selector graph selects the correct successor
        EXPECT_EQ(cBuffer, scBuffer);
    }

    void testRoundTrip(
            const std::string& input,
            ZL_GraphID sgid,
            Compressor& compressor)
    {
        // Compress using ml selector graph
        std::string cBuffer = std::string(ZL_compressBound(input.size()), '\0');
        auto compressedSize = compress(compressor, cBuffer, input, sgid);
        cBuffer.resize(compressedSize);

        // Decompress and verify that the result is the same as the input
        std::string decompressedOutput = dctx_.decompressSerial(cBuffer);
        EXPECT_EQ(decompressedOutput, input);
    }

    size_t compress(
            Compressor& compressor,
            std::string& dst,
            const std::string& input,
            ZL_GraphID sgid)
    {
        compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor.selectStartingGraph(sgid);
        cctx_.refCompressor(compressor);
        return cctx_.compressSerial(dst, input);
    }

   protected:
    Compressor compressor_;
    DCtx dctx_;
    CCtx cctx_;

   public:
    std::string movies                  = zstrong::tests::kMoviesCsvFormatInput;
    std::vector<ZL_GraphID> successors_ = { ZL_GRAPH_HUFFMAN,
                                            ZL_GRAPH_ZSTD,
                                            ZL_GRAPH_STORE };
};

TEST_F(TestMLSelectorGraph, TestMLSelectorGraphRoundtrip)
{
    auto compressMLSelectorGraph =
            getSelectorGraphWithSelectedSuccessor(1, compressor_);

    testRoundTrip(movies, compressMLSelectorGraph, compressor_);
}

TEST_F(TestMLSelectorGraph, TestMLSelectorGraphSelection)
{
    for (size_t successor = 0; successor < successors_.size(); ++successor) {
        auto compressMLSelectorGraph =
                getSelectorGraphWithSelectedSuccessor(successor, compressor_);

        testSelection(
                movies,
                compressMLSelectorGraph,
                successors_[successor],
                compressor_);
    }
}

TEST_F(TestMLSelectorGraph, TestMLSelectorConfigSerializable)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor_.get());

    // Serialize config
    ZL_MLSelectorConfig config = { .selectedSuccessor = 0 };
    Arena* arena               = ALLOC_HeapArena_create();
    A1C_Arena a1cArena         = A1C_Arena_wrap(arena);

    ZL_RESULT_OF(ZL_SerializedMLConfig)
    serializedResult = MLSelector_serializeMLSelectorConfig(
            ZL_ERR_CTX_PTR, &config, &a1cArena);
    EXPECT_FALSE(ZL_RES_isError(serializedResult));

    auto serializedConfig = ZL_RES_value(serializedResult);
    // Deserialize config
    auto result = MLSelector_deserializeMLSelectorConfig(
            ZL_ERR_CTX_PTR,
            serializedConfig.data,
            serializedConfig.size,
            &a1cArena);
    EXPECT_FALSE(ZL_RES_isError(result));

    // Check that the deserialized config is the same as the original config
    EXPECT_EQ(ZL_RES_value(result).selectedSuccessor, config.selectedSuccessor);
    ALLOC_Arena_freeArena(arena);
}

TEST_F(TestMLSelectorGraph, TestMLSelectorGraphSerializable)
{
    Compressor compressor;
    size_t selectedSuccessor     = 2;
    auto compressMLSelectorGraph = getSelectorGraphWithSelectedSuccessor(
            selectedSuccessor, compressor);

    // Make sure selection works before serialization
    testSelection(
            movies,
            compressMLSelectorGraph,
            successors_[selectedSuccessor],
            compressor);

    std::string serialCompress = compressor.serialize();

    Compressor deserializedCompressor;
    // need to register base graph for decompression

    ZL_RES_value(ZL_MLSelector_registerBaseGraph(deserializedCompressor.get()));
    deserializedCompressor.deserialize(serialCompress);

    // Make sure selection works after deserialization
    testSelection(
            movies,
            compressMLSelectorGraph,
            successors_[selectedSuccessor],
            deserializedCompressor);
    // Make sure round trip works after deserialization
    testRoundTrip(movies, compressMLSelectorGraph, deserializedCompressor);
}
} // namespace
} // namespace openzl::tests
