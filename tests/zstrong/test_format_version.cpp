// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <array>

#include <gtest/gtest.h>

#include "openzl/common/wire_format.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_reflection.h"

#include "tests/utils.h"
#include "tests/zstrong/test_zstrong_fixture.h"

namespace openzl::tests {
namespace {
class FormatVersionTest : public ZStrongTest {
   public:
    void testFormatVersion(uint32_t version)
    {
        reset();
        finalizeGraph(ZL_GRAPH_STORE, 1);
        setParameter(ZL_CParam_formatVersion, (int)version);
        std::string data(1000, 'x');
        testRoundTrip(data);
    }
};

TEST_F(FormatVersionTest, SanityChecks)
{
    ASSERT_LE(ZL_MIN_FORMAT_VERSION, ZL_MAX_FORMAT_VERSION);
}

TEST_F(FormatVersionTest, UnsetFormatVersion)
{
    auto cctx = ZL_CCtx_create();
    ZL_REQUIRE_SUCCESS(
            ZL_CCtx_selectStartingGraphID(cctx, NULL, ZL_GRAPH_STORE, NULL));
    std::string data(100, 'x');
    std::string out(ZL_compressBound(data.size()), 'x');
    auto const report = ZL_CCtx_compress(
            cctx, out.data(), out.size(), data.data(), data.size());
    ZL_CCtx_free(cctx);
    ASSERT_TRUE(ZL_isError(report));
    ASSERT_EQ(
            ZL_E_code(ZL_RES_error(report)), ZL_ErrorCode_formatVersion_notSet);
}

TEST_F(FormatVersionTest, ZeroFormatVersion)
{
    reset();
    finalizeGraph(ZL_GRAPH_STORE, 1);
    setParameter(ZL_CParam_formatVersion, 0);
    std::string data(1000, 'x');
    auto const [report, result] = compress(data);
    ASSERT_TRUE(ZL_isError(report));
    ASSERT_EQ(
            ZL_E_code(ZL_RES_error(report)), ZL_ErrorCode_formatVersion_notSet);
}

TEST_F(FormatVersionTest, MinFormatVersion)
{
    testFormatVersion(ZL_MIN_FORMAT_VERSION);
}

TEST_F(FormatVersionTest, MaxFormatVersion)
{
    testFormatVersion(ZL_MAX_FORMAT_VERSION);
}

TEST_F(FormatVersionTest, AllFormatVersions)
{
    for (uint32_t v = ZL_MIN_FORMAT_VERSION; v <= ZL_MAX_FORMAT_VERSION; ++v) {
        testFormatVersion(v);
    }
}

TEST_F(FormatVersionTest, CCtxBadFormatVersions)
{
    auto* cctx = ZL_CCtx_create();
    ASSERT_TRUE(ZL_isError(ZL_CCtx_setParameter(
            cctx, ZL_CParam_formatVersion, ZL_MIN_FORMAT_VERSION - 1)));
    ASSERT_TRUE(ZL_isError(ZL_CCtx_setParameter(
            cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION + 1)));
    ZL_CCtx_free(cctx);

    auto* cgraph = ZL_Compressor_create();
    ASSERT_TRUE(ZL_isError(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MIN_FORMAT_VERSION - 1)));
    ASSERT_TRUE(ZL_isError(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION + 1)));
    ZL_Compressor_free(cgraph);
}

TEST_F(FormatVersionTest, MinFormatVersionNotAccidentallyIncreased)
{
    int constexpr kExpectedMinFormatVersion = 8;
    ASSERT_LE(ZL_MIN_FORMAT_VERSION, kExpectedMinFormatVersion)
            << "WARNING: Be extremely careful when updating this number! "
            << "If there is still data encoded in format "
            << kExpectedMinFormatVersion << " increasing it to "
            << ZL_MIN_FORMAT_VERSION << " will make ZStrong refuse to "
            << "decompress the previous version. You must be certain that "
            << "no data encoded with the previous version still exists. "
            << "Once you've done that, you may bump kExpectedMinFormatVersion "
            << "to fix this test";
}

TEST_F(FormatVersionTest, MaxFormatVersionSucceedsOnSupportedVersion)
{
    reset();

    ZL_GraphID const graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph_, ZL_NODE_ZSTD_FIXED_DEPRECATED, ZL_GRAPH_STORE);
    finalizeGraph(graph, 1);

    ZL_REQUIRE_SUCCESS(
            ZL_Compressor_setParameter(cgraph_, ZL_CParam_formatVersion, 10));

    testRoundTrip("data");
}

TEST_F(FormatVersionTest, MaxFormatVersionWorksFailsCompression)
{
    reset();

    ZL_GraphID const graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph_, ZL_NODE_ZSTD_FIXED_DEPRECATED, ZL_GRAPH_STORE);
    finalizeGraph(graph, 1);

    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    auto [report, compressed] = compress("data large enough");

    ASSERT_TRUE(ZL_isError(report));
}

} // namespace
} // namespace openzl::tests
