// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string>

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <vector>
#include "openzl/common/errors_internal.h"
#include "openzl/compress/gcparams.h"
#include "openzl/zl_common_types.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"

using namespace ::testing;

namespace {

template <typename T>
std::vector<T> getData(size_t length)
{
    static_assert(std::is_integral<T>::value, "T must be an integral type");
    // Generate some data
    std::vector<T> vec(length);
    std::mt19937 mersenne_engine(10);
    std::uniform_int_distribution<T> dist(0, std::numeric_limits<T>::max());
    auto gen = [&dist, &mersenne_engine]() { return dist(mersenne_engine); };

    std::generate(vec.begin(), vec.end(), gen);
    return vec;
}

class CompressParamsTest : public Test {
   protected:
};

TEST_F(CompressParamsTest, permissiveCompression)
{
    auto input = getData<uint16_t>(1001);
    auto size  = input.size() * sizeof(uint16_t);
    std::string compressed(ZL_compressBound(size), '\0');

    auto cctx   = ZL_CCtx_create();
    auto cgraph = ZL_Compressor_create();

    // Set up a graph that will fail if we don't have permissive compression
    auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_INTERPRET_AS_LE32, ZL_GRAPH_COMPRESS_GENERIC);
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph, graph));

    // Attempt compression with permissive compression disabled
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx, cgraph));
    auto const resFail = ZL_CCtx_compress(
            cctx, compressed.data(), compressed.size(), input.data(), size);

    EXPECT_TRUE(ZL_isError(resFail));

    // Attempt compression with permissive compression enabled
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx, cgraph));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
            cctx, ZL_CParam_permissiveCompression, ZL_TernaryParam_enable));
    auto const res = ZL_CCtx_compress(
            cctx, compressed.data(), compressed.size(), input.data(), size);

    EXPECT_FALSE(ZL_isError(res));

    auto const warnings = ZL_CCtx_getWarnings(cctx);
    EXPECT_EQ(warnings.size, (size_t)1);

    // Expect decompression to succeed
    auto dctx = ZL_DCtx_create();
    std::string decompressed(size, '\0');
    ZL_REQUIRE_SUCCESS(ZL_DCtx_decompress(
            dctx,
            decompressed.data(),
            decompressed.size(),
            compressed.data(),
            ZL_RES_value(res)));

    ZL_CCtx_free(cctx);
    ZL_Compressor_free(cgraph);
    ZL_DCtx_free(dctx);
}

TEST_F(CompressParamsTest, strToParam)
{
    ASSERT_EQ(
            ZL_validResult(GCParams_strToParam("stickyParameters")),
            ZL_CParam_stickyParameters);
    ASSERT_EQ(
            ZL_validResult(GCParams_strToParam("compressionLevel")),
            ZL_CParam_compressionLevel);
    ASSERT_EQ(
            ZL_validResult(GCParams_strToParam("formatVersion")),
            ZL_CParam_formatVersion);
    ASSERT_EQ(
            ZL_validResult(GCParams_strToParam("permissiveCompression")),
            ZL_CParam_permissiveCompression);
    ASSERT_EQ(
            ZL_validResult(GCParams_strToParam("compressedChecksum")),
            ZL_CParam_compressedChecksum);
    ASSERT_EQ(
            ZL_validResult(GCParams_strToParam("minStreamSize")),
            ZL_CParam_minStreamSize);
    ASSERT_TRUE(ZL_isError(GCParams_strToParam("invalid")));
    ASSERT_TRUE(ZL_isError(GCParams_strToParam("")));
}

TEST_F(CompressParamsTest, setTernaryParam_validValues)
{
    const ZL_CParam ternaryParams[] = {
        ZL_CParam_permissiveCompression,
        ZL_CParam_compressedChecksum,
        ZL_CParam_contentChecksum,
        ZL_CParam_storeOnExpansion,
    };
    const int validValues[] = {
        ZL_TernaryParam_auto,
        ZL_TernaryParam_enable,
        ZL_TernaryParam_disable,
    };

    for (auto param : ternaryParams) {
        GCParams gcparams = GCParams_default;
        for (auto value : validValues) {
            EXPECT_FALSE(
                    ZL_isError(GCParams_setParameter(&gcparams, param, value)))
                    << "param=" << GCParams_paramToStr(param)
                    << " value=" << value;
            EXPECT_EQ(GCParams_getParameter(&gcparams, param), value);
        }
    }
}

TEST_F(CompressParamsTest, setTernaryParam_invalidValues)
{
    const ZL_CParam ternaryParams[] = {
        ZL_CParam_permissiveCompression,
        ZL_CParam_compressedChecksum,
        ZL_CParam_contentChecksum,
        ZL_CParam_storeOnExpansion,
    };
    const int invalidValues[] = { 3, -1, 42 };

    for (auto param : ternaryParams) {
        for (auto value : invalidValues) {
            GCParams gcparams = GCParams_default;
            EXPECT_TRUE(
                    ZL_isError(GCParams_setParameter(&gcparams, param, value)))
                    << "param=" << GCParams_paramToStr(param)
                    << " value=" << value;
        }
    }
}

TEST_F(CompressParamsTest, paramToStr)
{
    ASSERT_EQ(
            std::string("stickyParameters"),
            GCParams_paramToStr(ZL_CParam_stickyParameters));
    ASSERT_EQ(
            std::string("compressionLevel"),
            GCParams_paramToStr(ZL_CParam_compressionLevel));
    ASSERT_EQ(
            std::string("decompressionLevel"),
            GCParams_paramToStr(ZL_CParam_decompressionLevel));
    ASSERT_EQ(
            std::string("formatVersion"),
            GCParams_paramToStr(ZL_CParam_formatVersion));
    ASSERT_EQ(
            std::string("permissiveCompression"),
            GCParams_paramToStr(ZL_CParam_permissiveCompression));
    ASSERT_EQ(
            std::string("compressedChecksum"),
            GCParams_paramToStr(ZL_CParam_compressedChecksum));
    ASSERT_EQ(
            std::string("minStreamSize"),
            GCParams_paramToStr(ZL_CParam_minStreamSize));
    ASSERT_EQ(NULL, GCParams_paramToStr((ZL_CParam)0x424242));
}
} // namespace
