// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <algorithm>
#include <random>

#include "openzl/common/allocation.h"
#include "openzl/compress/cctx.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_selector.h"
#include "openzl/zl_version.h"
#include "tests/utils.h"
#include "tests/zstrong/test_zstrong_fixture.h"

namespace openzl {
namespace tests {

template <size_t seed>
static size_t xorRandTransform(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize) noexcept
{
    EXPECT_GE(dstCapacity, srcSize);
    std::mt19937_64 generator(seed);
    std::uniform_int_distribution<unsigned char> distribution(0, 255);
    const unsigned char* const src8 = (const unsigned char*)src;
    unsigned char* const dst8       = (unsigned char*)dst;
    for (unsigned int i = 0; i < srcSize; i++) {
        dst8[i] = src8[i] ^ distribution(generator);
    }
    return srcSize;
}

const auto xorRand1 = xorRandTransform<1>;
const auto xorRand2 = xorRandTransform<2>;
const auto xorRand3 = xorRandTransform<3>;

class SelectorTest : public ZStrongTest {
   public:
    static ZL_GraphID selectorFunction(
            const ZL_Selector* selCtx,
            const ZL_Input* inputStream,
            const ZL_GraphID* cfns,
            size_t nbCfns) noexcept
    {
        EXPECT_GT(nbCfns, (size_t)0);
        size_t minSize        = std::numeric_limits<size_t>::max();
        ZL_GraphID minGraphId = cfns[0];
        std::for_each(cfns, cfns + nbCfns, [&](ZL_GraphID graph) {
            auto report = ZL_Selector_tryGraph(selCtx, inputStream, graph)
                                  .finalCompressedSize;
            if (!ZL_isError(report)) {
                size_t csize = ZL_validResult(report);
                if (csize < minSize) {
                    minSize    = csize;
                    minGraphId = graph;
                }
            }
        });
        EXPECT_NE(minSize, std::numeric_limits<size_t>::max());
        return minGraphId;
    }

    ZL_GraphID declareTryGraph(const std::vector<ZL_GraphID>& graphs)
    {
        return declareSelectorGraph(selectorFunction, graphs);
    }
    ZL_GraphID declareTryGraph(ZL_NodeID node, ZL_GraphID nextGraph)
    {
        ZL_GraphID graph                     = declareGraph(node, nextGraph);
        const std::vector<ZL_GraphID> graphs = { graph, nextGraph };
        return declareTryGraph(graphs);
    }
    // Register custom length-preserving PipeTransform by specifying
    // compress and decompress functions
    ZL_NodeID registerCustomTransform(
            ZL_PipeEncoderFn const& compress,
            ZL_PipeDecoderFn const& decompress)
    {
        customTransforms_ += 1;
        const ZL_PipeEncoderDesc compressDesc = {
            .CTid        = customTransforms_,
            .transform_f = compress,
            .dstBound_f  = [](const void* src,
                             size_t srcSize) noexcept -> size_t {
                (void)src;
                return srcSize;
            }
        };
        const ZL_PipeDecoderDesc decompressDesc = {
            .CTid       = customTransforms_,
            .dstBound_f = [](const void* src,
                             size_t srcSize) noexcept -> size_t {
                (void)src;
                return srcSize;
            },
            .transform_f = decompress,
        };
        return ZStrongTest::registerCustomTransform(
                compressDesc, decompressDesc);
    }
    void setupTryGraph()
    {
        reset();
        // Graph:
        // Optional(xorRand1) -> Optional(xorRand2) -> ROLZ
        auto xor1Vnode = registerCustomTransform(xorRand1, xorRand1);
        auto xor2Vnode = registerCustomTransform(xorRand2, xorRand2);

        auto xor2graph = declareTryGraph(xor2Vnode, ZL_GRAPH_ZSTD);
        auto graph     = declareTryGraph(xor1Vnode, xor2graph);
        finalizeGraph(graph, 1);
    }
    size_t getCompressedSize(const std::string_view data)
    {
        setupTryGraph();
        auto [csize, _] = compress(data);
        EXPECT_FALSE(ZL_isError(csize));
        return ZL_validResult(csize);
    }
    virtual void reset() override
    {
        customTransforms_ = 0;
        ZStrongTest::reset();
    }
    void testRoundtrip(const std::string_view data)
    {
        setupTryGraph();
        ZStrongTest::testRoundTrip(data);
    }
    ZL_GraphPerformance tryStream(const ZL_Input* stream)
    {
        reset();
        // We always want to run some conversion, so if we are serialized
        // convert to token and back
        ZL_GraphID graph;
        if (ZL_Input_type(stream) == ZL_Type_serial) {
            graph = declareGraph(ZL_NODE_CONVERT_SERIAL_TO_TOKEN4);
        } else {
            graph = ZL_GRAPH_STORE;
        }
        finalizeGraph(graph, 1);
        auto cctx = ZL_CCtx_create();
        ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx, cgraph_));
        std::vector<uint8_t> data(100000, 0);

        Arena* const arena = ALLOC_HeapArena_create();
        ZL_REQUIRE_NN(arena);
        auto res = CCTX_tryGraph(cctx, &stream, 1, arena, graph, NULL);
        ZL_REQUIRE(!ZL_RES_isError(res));

        ALLOC_Arena_freeArena(arena);
        ZL_CCtx_free(cctx);
        return ZL_RES_value(res);
    }

   protected:
    ZL_IDType customTransforms_ = 0;
};

TEST_F(SelectorTest, TestGetInput0MaskForGraph)
{
    reset();
    const auto stringSplitGraph = declareGraph(
            ZL_NODE_SEPARATE_STRING_COMPONENTS,
            { ZL_GRAPH_STORE, ZL_GRAPH_STORE });
    std::vector<ZL_GraphID> graphs = {
        ZL_GRAPH_ZSTD,       // serial only
        ZL_GRAPH_RANGE_PACK, // numeric only
        stringSplitGraph,    // string only
    };

    const std::string serialData =
            "hello hello hello world this is some serialized data for you";
    const std::vector<uint32_t> numericData = { 55, 44, 33, 22, 11, 55, 44, 33,
                                                22, 11, 55, 44, 33, 22, 11 };
    const std::vector<uint32_t> stringLens  = {
        6, 6, 6, 6, 5, 3, 5, 11, 5, 4, 3
    };
    auto* serialStream =
            ZL_TypedRef_createSerial(serialData.data(), serialData.size());
    auto* numericStream = ZL_TypedRef_createNumeric(
            numericData.data(), sizeof(numericData[0]), numericData.size());
    auto* stringStream = ZL_TypedRef_createString(
            serialData.data(),
            serialData.size(),
            stringLens.data(),
            stringLens.size());
    // auto* structStream =
    //         ZL_TypedRef_createStruct(serialData.data(), 10, serialData.size()
    //         / 10);

    const auto validateInput0MaskFn = [](const ZL_Selector* selCtx,
                                         const ZL_Input* input,
                                         const ZL_GraphID* cfns,
                                         size_t nbCfns) noexcept {
        for (size_t i = 0; i < nbCfns; i++) {
            int mask  = ZL_Selector_getInput0MaskForGraph(selCtx, cfns[i]);
            int mask2 = ZL_Input_type(input);
            if ((mask & mask2) != 0) {
                return cfns[i];
            }
        }
        return ZL_GRAPH_ILLEGAL;
    };

    const auto selectorGid = declareSelectorGraph(validateInput0MaskFn, graphs);
    finalizeGraph(selectorGid, 1);

    auto res = compressTyped(serialStream);
    ZL_REQUIRE_SUCCESS(res.first);

    res = compressTyped(numericStream);
    ZL_REQUIRE_SUCCESS(res.first);

    res = compressTyped(stringStream);
    ZL_REQUIRE_SUCCESS(res.first);

    // res = compressTyped(structStream);
    // ASSERT_TRUE(ZL_isError(res.first));

    ZL_TypedRef_free(serialStream);
    ZL_TypedRef_free(numericStream);
    ZL_TypedRef_free(stringStream);
    // ZL_TypedRef_free(structStream);
}

TEST_F(SelectorTest, TestSetSuccessorParams)
{
    reset();

    const auto successorDgFn = [](ZL_Graph* gctx,
                                  ZL_Edge* inputs[],
                                  size_t nbIns) noexcept -> ZL_Report {
        // verify passed params
        const auto p1 = ZL_Graph_getLocalIntParam(gctx, 1);
        ZL_RET_R_IF_NE(GENERIC, p1.paramValue, 2);
        const auto p2 = ZL_Graph_getLocalIntParam(gctx, 2);
        ZL_RET_R_IF_NE(GENERIC, p2.paramValue, 4);
        const auto p3 = ZL_Graph_getLocalIntParam(gctx, 3);
        ZL_RET_R_IF_NE(GENERIC, p3.paramValue, 6);
        const auto p4 = ZL_Graph_getLocalRefParam(gctx, 4);
        if (std::string((const char*)p4.paramRef, 4) != std::string("I am")) {
            return ZL_returnError(ZL_ErrorCode_GENERIC);
        }
        const auto p5 = ZL_Graph_getLocalRefParam(gctx, 5);
        if (std::string((const char*)p5.paramRef, 4) != std::string(" the")) {
            return ZL_returnError(ZL_ErrorCode_GENERIC);
        }
        ZL_RET_R_IF(graph_invalidNumInputs, nbIns != 1);
        ZL_Edge* input = inputs[0];
        ZL_REQUIRE_SUCCESS(ZL_Edge_setDestination(input, ZL_GRAPH_STORE));
        return ZL_returnSuccess();
    };
    ZL_Type inputTypeMask                = ZL_Type_serial;
    ZL_FunctionGraphDesc successorDgDesc = {
        .name                = "successorDg",
        .graph_f             = successorDgFn,
        .inputTypeMasks      = &inputTypeMask,
        .nbInputs            = 1,
        .lastInputIsVariable = false,
        .nbCustomGraphs      = 0,
        .nbCustomNodes       = 0,
    };
    const auto gid =
            ZL_Compressor_registerFunctionGraph(cgraph_, &successorDgDesc);
    const auto setSuccessorFn = [](const ZL_Selector* selCtx,
                                   const ZL_Input* input,
                                   const ZL_GraphID* cfns,
                                   size_t) noexcept {
        // boilerplate for generating params
        ZL_IntParam intParams[] = {
            { 1, 2 },
            { 2, 4 },
            { 3, 6 },
        };
        const char* data        = (const char*)ZL_Input_ptr(input);
        ZL_RefParam refParams[] = {
            { .paramId = 4, .paramRef = data },
            { .paramId = 5, .paramRef = data + 4 },
        };
        ZL_LocalParams lps = {
            .intParams = { intParams,
                           sizeof(intParams) / sizeof(intParams[0]) },
            .refParams = { refParams,
                           sizeof(refParams) / sizeof(refParams[0]) },
        };
        ZL_REQUIRE_SUCCESS(ZL_Selector_setSuccessorParams(selCtx, &lps));
        return cfns[0];
    };
    const auto selectorGid = declareSelectorGraph(setSuccessorFn, { gid });
    finalizeGraph(selectorGid, 1);

    constexpr const std::string_view data = "I am the Glob-glo-gab-galab";
    const auto [report, _]                = compress(data);
    ASSERT_FALSE(ZL_isError(report));
}

/********************************
 ******** TEST TRY_GRAPH ********
 ********************************/
TEST_F(SelectorTest, RoundTripZeroes)
{
    std::string data(1000, 0);
    testRoundtrip(data);
}

TEST_F(SelectorTest, RoundTripXor1)
{
    std::string data(1000, 0);
    xorRand1(data.data(), data.size(), data.data(), data.size());
    testRoundtrip(data);
}

TEST_F(SelectorTest, RoundTripXor2)
{
    std::string data(1000, 0);
    xorRand2(data.data(), data.size(), data.data(), data.size());
    testRoundtrip(data);
}

TEST_F(SelectorTest, RoundTripXor1And2)
{
    std::string data(1000, 0);
    xorRand1(data.data(), data.size(), data.data(), data.size());
    xorRand2(data.data(), data.size(), data.data(), data.size());
    testRoundtrip(data);
}

TEST_F(SelectorTest, CompressedSizeSanity)
{
    std::string data(100000, 0);
    const size_t compressedSize = getCompressedSize(data);
    ASSERT_LE(compressedSize, (size_t)300); // sanity - make sure we compress
    xorRand3(data.data(), data.size(), data.data(), data.size());
    const size_t compressedSizeXor3 = getCompressedSize(data);
    ASSERT_GE(
            compressedSizeXor3,
            (size_t)100000); // sanity - make sure we can't compress
}

TEST_F(SelectorTest, CompressedSizeXor1)
{
    std::string data(100000, 0);
    const size_t expectedCompressedSize = getCompressedSize(data);
    xorRand1(data.data(), data.size(), data.data(), data.size());
    const size_t compressedSize = getCompressedSize(data);
    ASSERT_LE(compressedSize, expectedCompressedSize + 50);
}

TEST_F(SelectorTest, CompressedSizeXor2)
{
    std::string data(100000, 0);
    const size_t expectedCompressedSize = getCompressedSize(data);
    xorRand2(data.data(), data.size(), data.data(), data.size());
    const size_t compressedSize = getCompressedSize(data);
    ASSERT_LE(compressedSize, expectedCompressedSize + 50);
}

TEST_F(SelectorTest, CompressedSizeXor1And2)
{
    std::string data(100000, 0);
    const size_t expectedCompressedSize = getCompressedSize(data);
    xorRand1(data.data(), data.size(), data.data(), data.size());
    xorRand2(data.data(), data.size(), data.data(), data.size());
    const size_t compressedSize = getCompressedSize(data);
    ASSERT_LE(compressedSize, expectedCompressedSize + 50);
}

TEST_F(SelectorTest, StreamTypeToken)
{
    const std::vector<uint8_t> data(100000, 0);
    auto stream = WrappedStream(data, ZL_Type_struct);
    tryStream(stream.getStream());
}

TEST_F(SelectorTest, StreamTypeIntegerFails)
{
    const std::vector<uint8_t> data(100000, 0);
    auto stream = WrappedStream(data, ZL_Type_numeric);
    tryStream(stream.getStream());
}

TEST_F(SelectorTest, StreamTypeSerializedSuccess)
{
    const std::vector<uint8_t> data(100000, 0);
    auto stream     = WrappedStream(data, ZL_Type_serial);
    auto res        = tryStream(stream.getStream());
    auto [csize, _] = compress(std::string(data.begin(), data.end()));
    ASSERT_FALSE(ZL_isError(csize));

    // Adding 8 bytes for version < ZL_CHUNK_VERSION_MIN
    // Adding 9 bytes for version >= ZL_CHUNK_VERSION_MIN, assuming single
    // chunk. Note: we assume that the version used for tests is
    // ZL_MAX_FORMAT_VERSION
    size_t checksumBytes = 8 + (ZL_MAX_FORMAT_VERSION >= ZL_CHUNK_VERSION_MIN);
    ASSERT_EQ(ZL_validResult(csize), res.compressedSize + checksumBytes);
}

} // namespace tests
} // namespace openzl
