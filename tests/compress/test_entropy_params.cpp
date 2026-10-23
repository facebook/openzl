// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_public_nodes.h"

namespace {

/// Bytes of a rounded Gaussian: ~200 distinct values with homogeneous
/// statistics, like quantized weights. They are drawn from std::mt19937, whose
/// output the standard specifies, unlike that of std::normal_distribution.
std::vector<uint8_t> gaussianBytes(size_t size)
{
    std::mt19937 rng(42);
    std::vector<uint8_t> data(size);
    for (auto& b : data) {
        // Box-Muller transform
        const double u1 = (rng() + 0.5) / 4294967296.0;
        const double u2 = rng() / 4294967296.0;
        const double v  = std::round(
                30 * std::sqrt(-2 * std::log(u1))
                * std::cos(6.283185307179586 * u2));
        b = (uint8_t)(int8_t)std::max(-127.0, std::min(127.0, v));
    }
    return data;
}

class EntropyParamsTest : public ::testing::Test {
   protected:
    /// Registers @p graph with @p params on compressor_.
    ZL_GraphID parameterize(ZL_GraphID graph, std::vector<ZL_IntParam> params)
    {
        params_                          = std::move(params);
        const ZL_LocalParams localParams = {
            .intParams = { params_.data(), params_.size() },
        };
        const ZL_ParameterizedGraphDesc desc = {
            .graph       = graph,
            .localParams = &localParams,
        };
        return ZL_Compressor_registerParameterizedGraph(compressor_, &desc);
    }

    /// @returns the compressed frame, checked to decompress to @p src, or an
    /// empty one on error
    std::vector<uint8_t> frame(
            const std::vector<uint8_t>& src,
            ZL_GraphID graph,
            int formatVersion = ZL_MAX_FORMAT_VERSION)
    {
        EXPECT_FALSE(ZL_isError(ZL_Compressor_setParameter(
                compressor_, ZL_CParam_formatVersion, formatVersion)));
        EXPECT_FALSE(ZL_isError(
                ZL_Compressor_selectStartingGraphID(compressor_, graph)));
        ZL_CCtx* const cctx = ZL_CCtx_create();
        EXPECT_FALSE(ZL_isError(ZL_CCtx_refCompressor(cctx, compressor_)));
        std::vector<uint8_t> dst(ZL_compressBound(src.size()));
        const ZL_Report report = ZL_CCtx_compress(
                cctx, dst.data(), dst.size(), src.data(), src.size());
        ZL_CCtx_free(cctx);
        if (ZL_isError(report)) {
            return {};
        }
        dst.resize(ZL_validResult(report));
        std::vector<uint8_t> out(src.size());
        const ZL_Report dsize =
                ZL_decompress(out.data(), out.size(), dst.data(), dst.size());
        EXPECT_FALSE(ZL_isError(dsize));
        EXPECT_EQ(out, src);
        return dst;
    }

    /// @returns the compressed size, or 0 on error
    size_t compress(
            const std::vector<uint8_t>& src,
            ZL_GraphID graph,
            int formatVersion = ZL_MAX_FORMAT_VERSION)
    {
        return frame(src, graph, formatVersion).size();
    }

    void SetUp() override
    {
        compressor_ = ZL_Compressor_create();
    }

    void TearDown() override
    {
        ZL_Compressor_free(compressor_);
    }

    ZL_Compressor* compressor_ = nullptr;
    std::vector<ZL_IntParam> params_;
};

TEST_F(EntropyParamsTest, LargerChunksAndTablesShrinkHomogeneousData)
{
    const auto data           = gaussianBytes(4 << 20);
    const size_t defaults     = compress(data, ZL_GRAPH_FSE);
    const size_t largerChunks = compress(
            data,
            parameterize(
                    ZL_GRAPH_FSE, { { ZL_ENTROPY_CHUNK_SIZE_PID, 1 << 19 } }));
    const size_t largerBoth = compress(
            data,
            parameterize(
                    ZL_GRAPH_FSE,
                    { { ZL_ENTROPY_CHUNK_SIZE_PID, 1 << 19 },
                      { ZL_ENTROPY_FSE_MAX_TABLE_LOG_PID, 12 } }));

    ASSERT_GT(defaults, 0u);
    ASSERT_GT(largerChunks, 0u);
    ASSERT_GT(largerBoth, 0u);
    EXPECT_LT(largerChunks, defaults);
    EXPECT_LT(largerBoth, largerChunks);
}

TEST_F(EntropyParamsTest, ChunkSizeAppliesToTheEntropyGraph)
{
    const auto data           = gaussianBytes(4 << 20);
    const size_t defaults     = compress(data, ZL_GRAPH_ENTROPY);
    const size_t largerChunks = compress(
            data,
            parameterize(
                    ZL_GRAPH_ENTROPY,
                    { { ZL_ENTROPY_CHUNK_SIZE_PID, 1 << 19 } }));

    ASSERT_GT(defaults, 0u);
    ASSERT_GT(largerChunks, 0u);
    EXPECT_LT(largerChunks, defaults);
}

TEST_F(EntropyParamsTest, LongerHuffmanCodesShrinkWideAlphabets)
{
    const auto data       = gaussianBytes(4 << 20);
    const size_t defaults = compress(data, ZL_GRAPH_HUFFMAN);
    const size_t longer   = compress(
            data,
            parameterize(
                    ZL_GRAPH_HUFFMAN,
                    { { ZL_ENTROPY_HUFFMAN_MAX_TABLE_LOG_PID, 12 } }));

    ASSERT_GT(defaults, 0u);
    ASSERT_GT(longer, 0u);
    EXPECT_LT(longer, defaults);
}

TEST_F(EntropyParamsTest, LargeChunksKeepHuffmanWithinItsStreamSizes)
{
    // 4-stream Huffman stores stream sizes on 16 bits: 512 KiB chunks of
    // this data would overflow them.
    const auto data = gaussianBytes(4 << 20);
    for (int formatVersion : { 26, ZL_MAX_FORMAT_VERSION }) {
        for (ZL_GraphID graph : { ZL_GRAPH_HUFFMAN, ZL_GRAPH_ENTROPY }) {
            EXPECT_GT(
                    compress(
                            data,
                            parameterize(
                                    graph,
                                    { { ZL_ENTROPY_CHUNK_SIZE_PID, 1 << 19 } }),
                            formatVersion),
                    0u)
                    << "format " << formatVersion;
        }
    }
}

TEST_F(EntropyParamsTest, LongerHuffmanCodesReachPivCo)
{
    // PivCo codes with the table built by the entropy graph.
    const auto data       = gaussianBytes(4 << 20);
    const size_t defaults = compress(data, ZL_GRAPH_HUFFMAN_PIVCO);
    const size_t longer   = compress(
            data,
            parameterize(
                    ZL_GRAPH_HUFFMAN_PIVCO,
                    { { ZL_ENTROPY_HUFFMAN_MAX_TABLE_LOG_PID, 12 } }));

    ASSERT_GT(defaults, 0u);
    ASSERT_GT(longer, 0u);
    EXPECT_LT(longer, defaults);
}

TEST_F(EntropyParamsTest, ChunkSizeDoesNotCutInputsOfOneChunk)
{
    const auto small = gaussianBytes(64 << 10); // not cut by default either
    const auto exact = gaussianBytes(128 << 10);

    const auto smallDefault = frame(small, ZL_GRAPH_ENTROPY);
    const auto smallLarger =
            frame(small,
                  parameterize(
                          ZL_GRAPH_ENTROPY,
                          { { ZL_ENTROPY_CHUNK_SIZE_PID, 1 << 19 } }));
    const auto exactLarger =
            frame(exact,
                  parameterize(
                          ZL_GRAPH_ENTROPY,
                          { { ZL_ENTROPY_CHUNK_SIZE_PID, 1 << 20 } }));
    const auto exactEqual =
            frame(exact,
                  parameterize(
                          ZL_GRAPH_ENTROPY,
                          { { ZL_ENTROPY_CHUNK_SIZE_PID, 128 << 10 } }));

    ASSERT_FALSE(smallDefault.empty());
    ASSERT_FALSE(exactLarger.empty());
    EXPECT_EQ(smallLarger, smallDefault);
    EXPECT_EQ(exactEqual, exactLarger);
}

TEST_F(EntropyParamsTest, ChunkSizeLeavesWideElementsUnchanged)
{
    // 300,000 elements: 10 chunks if the chunk size applied.
    const auto data = gaussianBytes(600000);
    const auto wide = [this](ZL_GraphID entropy) {
        return ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_, ZL_NODE_CONVERT_SERIAL_TO_STRUCT2, entropy);
    };

    const auto defaults = frame(data, wide(ZL_GRAPH_ENTROPY));
    const auto smallest =
            frame(data,
                  wide(parameterize(
                          ZL_GRAPH_ENTROPY,
                          { { ZL_ENTROPY_CHUNK_SIZE_PID, 1 << 15 } })));
    const auto tooSmall =
            frame(data,
                  wide(parameterize(
                          ZL_GRAPH_ENTROPY,
                          { { ZL_ENTROPY_CHUNK_SIZE_PID, (1 << 15) - 1 } })));

    ASSERT_FALSE(defaults.empty());
    EXPECT_EQ(smallest, defaults);
    EXPECT_TRUE(tooSmall.empty());
}

TEST_F(EntropyParamsTest, SmallestChunkSizeCodesLargeInputs)
{
    // No more chunks than by default, which fit in the streams of a frame.
    const auto data = gaussianBytes(4 << 20);
    for (int formatVersion : { 15, ZL_MAX_FORMAT_VERSION }) {
        EXPECT_GT(
                compress(
                        data,
                        parameterize(
                                ZL_GRAPH_FSE,
                                { { ZL_ENTROPY_CHUNK_SIZE_PID, 1 << 15 } }),
                        formatVersion),
                0u)
                << "format " << formatVersion;
    }
}

TEST_F(EntropyParamsTest, ExplicitDefaultsGiveTheDefaultFrames)
{
    // A fixed corpus: bytes cut into default chunks or left whole, even when
    // larger than a chunk, taking few values, or incompressible, and as many
    // 16-bit elements.
    std::mt19937 rng(7);
    std::vector<uint8_t> fewValues(300000), incompressible(300000);
    for (auto& b : fewValues) {
        b = (uint8_t)(rng() % 3);
    }
    for (auto& b : incompressible) {
        b = (uint8_t)rng();
    }
    const std::vector<std::pair<std::string, std::vector<uint8_t>>> corpus = {
        { "many chunks", gaussianBytes(1 << 20) },
        { "one chunk", gaussianBytes(20000) },
        { "two chunks left whole", gaussianBytes(50000) },
        { "few values", fewValues },
        { "incompressible", incompressible },
    };
    // Byte inputs of at least 100000 bytes are cut into chunks of 2^15 by
    // default, and table logs default to 11.
    const std::vector<ZL_IntParam> defaults = {
        { ZL_ENTROPY_CHUNK_SIZE_PID, 1 << 15 },
        { ZL_ENTROPY_FSE_MAX_TABLE_LOG_PID, 11 },
        { ZL_ENTROPY_HUFFMAN_MAX_TABLE_LOG_PID, 11 },
    };
    const auto wide = [this](ZL_GraphID entropy) {
        return ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor_, ZL_NODE_CONVERT_SERIAL_TO_STRUCT2, entropy);
    };

    for (const ZL_GraphID graph : { ZL_GRAPH_FSE,
                                    ZL_GRAPH_HUFFMAN,
                                    ZL_GRAPH_HUFFMAN_PIVCO,
                                    ZL_GRAPH_ENTROPY }) {
        for (const auto& [what, data] : corpus) {
            const auto bytes  = frame(data, graph);
            const auto shorts = frame(data, wide(graph));
            const auto bytesP = frame(data, parameterize(graph, defaults));
            const auto shortsP =
                    frame(data, wide(parameterize(graph, defaults)));

            ASSERT_FALSE(bytes.empty()) << graph.gid << " " << what;
            ASSERT_FALSE(shorts.empty()) << graph.gid << " " << what;
            EXPECT_EQ(bytesP, bytes) << graph.gid << " " << what;
            EXPECT_EQ(shortsP, shorts) << graph.gid << " 16-bit " << what;
        }
    }
}

TEST_F(EntropyParamsTest, FormatsBefore15IgnoreTheParameters)
{
    // Even out of range values.
    const auto data                       = gaussianBytes(1 << 20);
    const std::vector<ZL_IntParam> params = {
        { ZL_ENTROPY_CHUNK_SIZE_PID, 1 },
        { ZL_ENTROPY_FSE_MAX_TABLE_LOG_PID, 99 },
        { ZL_ENTROPY_HUFFMAN_MAX_TABLE_LOG_PID, 99 },
    };
    for (const ZL_GraphID graph :
         { ZL_GRAPH_FSE, ZL_GRAPH_HUFFMAN, ZL_GRAPH_ENTROPY }) {
        const auto defaults = frame(data, graph, 14);
        const auto ignored  = frame(data, parameterize(graph, params), 14);

        ASSERT_FALSE(defaults.empty()) << graph.gid;
        EXPECT_EQ(ignored, defaults) << graph.gid;
    }
}

TEST_F(EntropyParamsTest, AcceptsParametersAtTheirBounds)
{
    const auto data = gaussianBytes(64 << 10);
    const std::vector<std::pair<ZL_GraphID, ZL_IntParam>> params = {
        { ZL_GRAPH_FSE, { ZL_ENTROPY_CHUNK_SIZE_PID, 1 << 15 } },
        { ZL_GRAPH_FSE, { ZL_ENTROPY_CHUNK_SIZE_PID, 1 << 26 } },
        { ZL_GRAPH_FSE, { ZL_ENTROPY_FSE_MAX_TABLE_LOG_PID, 9 } },
        { ZL_GRAPH_FSE, { ZL_ENTROPY_FSE_MAX_TABLE_LOG_PID, 12 } },
        { ZL_GRAPH_HUFFMAN, { ZL_ENTROPY_HUFFMAN_MAX_TABLE_LOG_PID, 11 } },
        { ZL_GRAPH_HUFFMAN, { ZL_ENTROPY_HUFFMAN_MAX_TABLE_LOG_PID, 12 } },
    };
    for (const auto& [graph, param] : params) {
        EXPECT_GT(compress(data, parameterize(graph, { param })), 0u)
                << param.paramId << " = " << param.paramValue;
    }
}

TEST_F(EntropyParamsTest, RejectsOutOfRangeParameters)
{
    const auto data = gaussianBytes(1 << 20);
    EXPECT_EQ(
            compress(
                    data,
                    parameterize(
                            ZL_GRAPH_FSE,
                            { { ZL_ENTROPY_CHUNK_SIZE_PID, (1 << 15) - 1 } })),
            0u);
    EXPECT_EQ(
            compress(
                    data,
                    parameterize(
                            ZL_GRAPH_FSE,
                            { { ZL_ENTROPY_CHUNK_SIZE_PID, (1 << 26) + 1 } })),
            0u);
    EXPECT_EQ(
            compress(
                    data,
                    parameterize(
                            ZL_GRAPH_FSE,
                            { { ZL_ENTROPY_FSE_MAX_TABLE_LOG_PID, 13 } })),
            0u);
    EXPECT_EQ(
            compress(
                    data,
                    parameterize(
                            ZL_GRAPH_FSE,
                            { { ZL_ENTROPY_FSE_MAX_TABLE_LOG_PID, 8 } })),
            0u);
    EXPECT_EQ(
            compress(
                    data,
                    parameterize(
                            ZL_GRAPH_HUFFMAN,
                            { { ZL_ENTROPY_HUFFMAN_MAX_TABLE_LOG_PID, 13 } })),
            0u);
    EXPECT_EQ(
            compress(
                    data,
                    parameterize(
                            ZL_GRAPH_HUFFMAN,
                            { { ZL_ENTROPY_HUFFMAN_MAX_TABLE_LOG_PID, 10 } })),
            0u);
}

} // namespace
