// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <numeric>
#include <random>

#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "tests/utils.h" // @manual

using namespace ::testing;

// push @nbInputs random serial strings of size @inputSizeEach through a concat
// + zstd graph
static std::string randomCompress(size_t nbInputs, size_t inputSizeEach)
{
    // TODO(T210132483): use generalized rand input generator
    const auto genRand = [](size_t size, long seed) -> std::string {
        std::mt19937 gen(seed);
        std::uniform_int_distribution<> dis(0, 255);
        std::string s(size, 0);
        for (size_t i = 0; i < size; ++i) {
            s[i] = (char)dis(gen);
        }
        return s;
    };

    std::vector<std::string> inputs;
    long seed = 142;
    for (size_t i = 0; i < nbInputs; ++i) {
        inputs.push_back(genRand(inputSizeEach, seed++));
    }
    std::vector<ZL_TypedRef*> inputRefs;
    for (auto& s : inputs) {
        inputRefs.push_back(ZL_TypedRef_createSerial(s.data(), s.size()));
    }
    std::vector<const ZL_TypedRef*> constInputRefs;
    for (auto& ref : inputRefs) {
        constInputRefs.push_back(ref);
    }

    size_t const totalInputSize = std::accumulate(
            inputs.begin(), inputs.end(), 0ul, [](auto acc, const auto& s) {
                return acc + s.size();
            });
    size_t dstCap = ZL_COMPRESSBOUND_UNGUARDED(totalInputSize);
    std::string dst(dstCap, 0);

    ZL_CCtx* cctx = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx);
    ZL_Compressor* cgraph = ZL_Compressor_create();
    ZL_REQUIRE_NN(cgraph);
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    const ZL_GraphID successors[2] = { ZL_GRAPH_COMPRESS_GENERIC,
                                       ZL_GRAPH_COMPRESS_GENERIC };
    const auto gid                 = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, ZL_NODE_CONCAT_SERIAL, successors, 2);

    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph, gid));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx, cgraph));

    // TODO(T210132483): add to ZStrongTest fixture
    ZL_Report const r = ZL_CCtx_compressMultiTypedRef(
            cctx,
            dst.data(),
            dst.size(),
            constInputRefs.data(),
            constInputRefs.size());

    printf("%s\n", ZL_ErrorCode_toString(ZL_errorCode(r)));
    fflush(stdout);
    ZL_REQUIRE(!ZL_isError(r));

    for (auto ref : inputRefs) {
        ZL_TypedRef_free(ref);
    }
    ZL_Compressor_free(cgraph);
    ZL_CCtx_free(cctx);
    return dst;
}

TEST(DecodeFrameheaderTest, NbOutputsTest)
{
    std::string c1 = randomCompress(1, 1000);
    auto rep       = ZL_getNumOutputs(c1.data(), c1.size());
    EXPECT_EQ(ZL_isError(rep), false);
    ASSERT_EQ(ZL_validResult(rep), 1ul);

    std::string c3 = randomCompress(3, 4000);
    rep            = ZL_getNumOutputs(c3.data(), c3.size());
    EXPECT_EQ(ZL_isError(rep), false);
    ASSERT_EQ(ZL_validResult(rep), 3ul);

    std::string c5 = randomCompress(5, 2000);
    rep            = ZL_getNumOutputs(c5.data(), c5.size());
    EXPECT_EQ(ZL_isError(rep), false);
    ASSERT_EQ(ZL_validResult(rep), 5ul);

    std::string c10 = randomCompress(10, 1000);
    rep             = ZL_getNumOutputs(c10.data(), c10.size());
    EXPECT_EQ(ZL_isError(rep), false);
    ASSERT_EQ(ZL_validResult(rep), 10ul);
}
