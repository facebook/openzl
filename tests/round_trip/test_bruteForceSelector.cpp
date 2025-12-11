// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/common/debug.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compress.h"

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
        ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
                cctx_, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    }

    void TearDown() override
    {
        ZL_DCtx_free(dctx_);
        ZL_Compressor_free(cgraph_);
        ZL_CCtx_free(cctx_);
    }

    void roundTripWithGid(ZL_TypedRef* data, ZL_GraphID gid)
    {
        size_t sz = ZL_Input_contentSize(data);
        if (ZL_Input_type(data) == ZL_Type_string) {
            sz += ZL_Input_numElts(data) * sizeof(uint32_t);
        }
        auto encCap = ZL_compressBound(sz);
        std::string enc(encCap, '\0');
        EXPECT_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, gid));
        EXPECT_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
        auto report =
                ZL_CCtx_compressTypedRef(cctx_, enc.data(), enc.size(), data);
        EXPECT_SUCCESS(report);

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
    const auto gid     = ZL_Compressor_registerBruteForceSelectorGraph(
            cgraph_, succs, sizeof(succs) / sizeof(succs[0]));

    roundTripWithGid(data, gid);
    ZL_TypedRef_free(data);
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
    const auto gid = ZL_Compressor_registerBruteForceSelectorGraph(
            cgraph_, succs, sizeof(succs) / sizeof(succs[0]));

    roundTripWithGid(data, gid);
    ZL_TypedRef_free(data);
}

} // namespace openzl::tests
