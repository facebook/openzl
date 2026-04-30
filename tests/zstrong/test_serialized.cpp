// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <array>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/compress/private_nodes.h" // ZS2_NODE_*
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"
#include "tests/utils.h"
#include "tests/zstrong/test_serialized_fixture.h"

namespace openzl {
namespace tests {
TEST_F(SerializedTest, InterpretAsLEU64)
{
    testNode(ZL_NODE_INTERPRET_AS_LE64, 8);
}

TEST_F(SerializedTest, InterpretAsLEU32)
{
    testNode(ZL_NODE_INTERPRET_AS_LE32, 4);
}

TEST_F(SerializedTest, InterpretAsLEU16)
{
    testNode(ZL_NODE_INTERPRET_AS_LE16, 2);
}

TEST_F(SerializedTest, InterpretAsLEU8)
{
    testNode(ZL_NODE_INTERPRET_AS_LE8, 1);
}

TEST_F(SerializedTest, ConvertSerialToToken4)
{
    testNode(ZL_NODE_CONVERT_SERIAL_TO_TOKEN4, 4);
}

TEST_F(SerializedTest, ConvertSerialToToken8)
{
    testNode(ZL_NODE_CONVERT_SERIAL_TO_TOKEN8, 8);
}

TEST_F(SerializedTest, FSENode)
{
    reset();
    finalizeGraph(declareGraph({ ZL_PrivateStandardNodeID_fse_v2 }), 1);
    testRoundTrip(generatedData(1000, 2));
    testRoundTrip(generatedData(1000, 10));
    testRoundTrip(generatedData(1000, 100));
}

TEST_F(SerializedTest, FSEGraph)
{
    testGraph(ZL_GRAPH_FSE);
}

TEST_F(SerializedTest, HuffmanNode)
{
    reset();
    finalizeGraph(declareGraph({ ZL_PrivateStandardNodeID_huffman_v2 }), 1);
    testRoundTrip(generatedData(1000, 2));
    testRoundTrip(generatedData(1000, 10));
    testRoundTrip(generatedData(1000, 100));
}

TEST_F(SerializedTest, HuffmanGraph)
{
    testGraph(ZL_GRAPH_HUFFMAN);
}

TEST_F(SerializedTest, LZ4)
{
    // Test default level
    testGraph(ZL_GRAPH_LZ4);

    // Test with HC
    reset();
    auto res = ZL_Compressor_buildLZ4Graph(cgraph_, 9);
    ASSERT_TRUE(!ZL_RES_isError(res));
    finalizeGraph(ZL_RES_value(res), 1);
    test();

    // Test with default level
    reset();
    res = ZL_Compressor_buildLZ4Graph(cgraph_, 1);
    ASSERT_TRUE(!ZL_RES_isError(res));
    finalizeGraph(ZL_RES_value(res), 1);
    test();

    // Test with negative level
    reset();
    res = ZL_Compressor_buildLZ4Graph(cgraph_, -5);
    ASSERT_TRUE(!ZL_RES_isError(res));
    finalizeGraph(ZL_RES_value(res), 1);
    test();
}

TEST_F(SerializedTest, Zstd)
{
    testNode(ZL_NODE_ZSTD);
}

TEST_F(SerializedTest, Bitpack)
{
    testNode(ZL_NODE_BITPACK_SERIAL);
}

TEST_F(SerializedTest, Flatpack)
{
    testNode(ZL_NODE_FLATPACK);
}

TEST_F(SerializedTest, Bitunpack)
{
    for (size_t nbBits = 1; nbBits <= 64; nbBits++) {
        reset();
        // Build a graph that starts with bitUnpacking of nbBits elements
        // and then bitpacks them again.
        // Reason with bitpack is so we don't expand the data too much to the
        // point it wouldn't fit in the compressed bound size.
        ZL_IntParam param           = { .paramId    = ZL_Bitunpack_numBits,
                                        .paramValue = (int)nbBits };
        ZL_LocalParams const params = { .intParams = { &param, 1 } };
        const ZL_NodeID node =
                createParameterizedNode(ZS2_NODE_BITUNPACK, params);
        finalizeGraph(declareGraph(node, ZL_GRAPH_BITPACK_INT), 1);

        // Run test for different number of elements
        std::vector<size_t> nbEltsToTest = { 0, nbBits, nbBits - 1, nbBits * 64,
                                             1, 5,      100,        1000 };
        for (auto const nbElts : nbEltsToTest) {
            auto dataSize = (nbElts * nbBits + 7) / 8;
            auto data     = generatedData(dataSize, 256);
            testRoundTrip(data);
            // Test with last bits = 0
            if (data.size() >= 1) {
                data[data.size() - 1] = 0;
                testRoundTrip(data);
            }
        }
    }
}

TEST_F(SerializedTest, SetStringSizes)
{
    reset();
    auto parser = [](ZL_SetStringLensState* state,
                     const ZL_Input* in) noexcept {
        size_t constexpr kNbFields = 10;
        uint32_t* fieldSizes       = (uint32_t*)ZL_SetStringLensState_malloc(
                state, kNbFields * sizeof(uint32_t));
        auto size = (uint32_t)ZL_Input_numElts(in);
        for (size_t i = 0; i < kNbFields; ++i) {
            fieldSizes[i] = std::min<uint32_t>(10, size);
            size -= fieldSizes[i];
        }
        fieldSizes[kNbFields - 1] += size;
        return ZL_SetStringLensInstructions{ fieldSizes, kNbFields };
    };
    auto node = ZL_Compressor_registerConvertSerialToStringNode(
            cgraph_, parser, NULL);
    finalizeGraph(declareGraph(node), 1);
    test();
}

TEST_F(SerializedTest, EntropySelector)
{
    testGraph(ZL_GRAPH_ENTROPY);
}

TEST_F(SerializedTest, BitpackSelector)
{
    testGraph(ZL_GRAPH_BITPACK);
}

TEST_F(SerializedTest, ConstantSelector)
{
    const std::vector<std::string> data = {
        "111", "aaaaa", "$$$$$$$$$$$$$$$$$$$$$$", "1"
    };
    for (std::string s : data) {
        testGraphOnInput(ZL_GRAPH_CONSTANT, s);
    }
}

TEST_F(SerializedTest, Constant)
{
    const std::vector<std::string> data = {
        "111", "aaaaa", "$$$$$$$$$$$$$$$$$$$$$$", "1"
    };
    for (std::string s : data) {
        testNodeOnInput(ZL_NODE_CONSTANT_SERIAL, s);
    }
}

TEST_F(SerializedTest, ConstantZeroRanges)
{
    const std::vector<size_t> sizes = { 1, 10, 100, 1000, 10000, 50000 };
    for (size_t size : sizes) {
        std::string zeroData(size, '\0');
        testGraphOnInput(ZL_GRAPH_CONSTANT, zeroData);
        testNodeOnInput(ZL_NODE_CONSTANT_SERIAL, zeroData);
    }
}

TEST_F(SerializedTest, ConstantSingleBytePatterns)
{
    const std::vector<size_t> sizes  = { 1, 10, 100, 1000, 10000 };
    const std::vector<char> patterns = { '\x00', '\xFF', '\x55', '\xAA' };
    for (char pattern : patterns) {
        for (size_t size : sizes) {
            std::string patternData(size, pattern);
            testGraphOnInput(ZL_GRAPH_CONSTANT, patternData);
        }
    }
}

TEST_F(SerializedTest, SplitN)
{
    reset();
    {
        std::string data;
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_, ZL_Type_serial, nullptr, 0);
        finalizeGraph(declareGraph(node), 1);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data;
        std::array<size_t, 1> segmentSizes = { 0 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_serial,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 1);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data                   = "hello world";
        std::array<size_t, 1> segmentSizes = { 0 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_serial,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 1);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data                   = "hello world";
        std::array<size_t, 3> segmentSizes = { 5, 1, 5 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_serial,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 1);
        testRoundTrip(data);
    }
    reset();
    {
        std::string data                   = "hello world";
        std::array<size_t, 3> segmentSizes = { 5, 1, 0 };
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_serial,
                segmentSizes.data(),
                segmentSizes.size());
        finalizeGraph(declareGraph(node), 1);
        testRoundTrip(data);
    }
}

namespace {
std::vector<std::string> genSplitSegments()
{
    std::string chunk;
    chunk.reserve(300000);
    std::mt19937 gen(0xdeadbeef);
    std::uniform_int_distribution<int> dist(0, 50);
    for (size_t i = 0; i < 300000; ++i) {
        chunk.push_back((char)dist(gen));
    }
    std::vector<std::string> segments = {
        "",
        "a",
        "aa",
        chunk,
        std::string(128, 'a'),
        std::string(1024, 'b'),
        std::string(256088, 'c'),
        kFooTestInput,
        kLoremTestInput,
        kAudioPCMS32LETestInput,
        kUniqueCharsTestInput,
        kMoviesCsvFormatInput,
        kStudentGradesCsvFormatInput,
    };
    return segments;
}

ZL_Report splitOptimizationBackendGraph(
        ZL_Graph* gctx,
        ZL_Edge* inputs[],
        size_t nbInputs,
        std::mt19937& gen)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    std::array<std::vector<ZL_Edge*>, 2> concats;
    std::uniform_int_distribution<size_t> destDist(0, 2);
    std::uniform_int_distribution<size_t> flushDist(0, 4);

    auto graphs = ZL_Graph_getCustomGraphs(gctx);
    std::uniform_int_distribution<size_t> graphDist(0, graphs.nbGraphIDs - 1);

    auto finish = [&](ZL_Edge* edge) -> ZL_Report {
        auto data = ZL_Edge_getData(edge);
        if (ZL_Input_numElts(data) % 8 == 0) {
            std::uniform_int_distribution<size_t> convertDist(0, 2);
            if (convertDist(gen) == 0) {
                ZL_TRY_LET(
                        ZL_EdgeList,
                        successors,
                        ZL_Edge_runNode(edge, ZL_NODE_INTERPRET_AS_LE64));
                edge = successors.edges[0];
                if (std::uniform_int_distribution<size_t>(0, 1)(gen) == 0) {
                    ZL_ERR_IF_ERR(ZL_Edge_setDestination(edge, ZL_GRAPH_STORE));
                } else {
                    ZL_ERR_IF_ERR(ZL_Edge_setDestination(edge, ZL_GRAPH_ZSTD));
                }
                return ZL_returnSuccess();
            }
        }
        auto graph = graphs.graphids[graphDist(gen)];
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(edge, graph));
        return ZL_returnSuccess();
    };

    auto flush = [&](std::vector<ZL_Edge*>& concat) -> ZL_Report {
        if (!concat.empty()) {
            ZL_TRY_LET(
                    ZL_EdgeList,
                    successors,
                    ZL_Edge_runMultiInputNode(
                            concat.data(),
                            concat.size(),
                            ZL_NODE_CONCAT_SERIAL));
            ZL_ASSERT_EQ(successors.nbEdges, 2);
            ZL_ERR_IF_ERR(ZL_Edge_setDestination(
                    successors.edges[0], ZL_GRAPH_FIELD_LZ));
            ZL_ERR_IF_ERR(finish(successors.edges[1]));
        }
        concat.clear();
        return ZL_returnSuccess();
    };

    std::vector<ZL_Edge*> shuffledInputs(inputs, inputs + nbInputs);
    std::shuffle(shuffledInputs.begin(), shuffledInputs.end(), gen);

    for (auto input : shuffledInputs) {
        auto dest = destDist(gen);
        if (dest < concats.size()) {
            concats[dest].push_back(input);

            if (flushDist(gen) == 0) {
                ZL_ERR_IF_ERR(flush(concats[dest]));
            }
        } else {
            ZL_ERR_IF_ERR(finish(input));
        }
    }

    for (auto& concat : concats) {
        ZL_ERR_IF_ERR(flush(concat));
    }

    return ZL_returnSuccess();
}

ZL_Report splitOptimizationGraph(
        ZL_Graph* gctx,
        ZL_Edge* inputs[],
        size_t nbInputs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    const ZL_IntParam seed = ZL_Graph_getLocalIntParam(gctx, 0);
    std::mt19937 gen(seed.paramValue);

    std::vector<ZL_Edge*> successors;

    std::vector<ZL_Edge*> shuffledInputs(inputs, inputs + nbInputs);
    std::shuffle(shuffledInputs.begin(), shuffledInputs.end(), gen);

    std::uniform_int_distribution<int> splitDist(0, 4);
    for (auto input : shuffledInputs) {
        if (splitDist(gen) != 0) {
            std::vector<size_t> segmentSizes;
            auto data        = ZL_Edge_getData(input);
            size_t remaining = ZL_Input_numElts(data);
            while (remaining > 0) {
                std::uniform_int_distribution<size_t> segmentDist(0, remaining);
                segmentSizes.push_back(segmentDist(gen));
                remaining -= segmentSizes.back();
            }
            ZL_TRY_LET(
                    ZL_EdgeList,
                    splitSuccessors,
                    ZL_Edge_runSplitNode(
                            input, segmentSizes.data(), segmentSizes.size()));
            successors.insert(
                    successors.end(),
                    splitSuccessors.edges,
                    splitSuccessors.edges + splitSuccessors.nbEdges);
        } else {
            successors.push_back(input);
        }
    }
    return splitOptimizationBackendGraph(
            gctx, successors.data(), successors.size(), gen);
}

ZL_GraphID buildSplitOptimizationGraph(ZL_Compressor* compressor, int seed)
{
    const ZL_IntParam seedParam = {
        .paramId    = 0,
        .paramValue = seed,
    };
    const ZL_LocalParams params = { .intParams = {
                                            .intParams   = &seedParam,
                                            .nbIntParams = 1,
                                    } };

    auto interpretI8 = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor, ZL_NODE_INTERPRET_AS_LE8, ZL_GRAPH_STORE);
    std::vector<ZL_GraphID> graphs = {
        ZL_GRAPH_STORE,
        ZL_GRAPH_ENTROPY,
        interpretI8,
        ZL_GRAPH_ZSTD,
    };

    const ZL_Type inputType   = ZL_Type_serial;
    ZL_FunctionGraphDesc desc = {
        .name                = "split_optimization_graph",
        .graph_f             = splitOptimizationGraph,
        .inputTypeMasks      = &inputType,
        .nbInputs            = 1,
        .lastInputIsVariable = true,
        .customGraphs        = graphs.data(),
        .nbCustomGraphs      = graphs.size(),
        .localParams         = params,
    };
    return ZL_Compressor_registerFunctionGraph(compressor, &desc);
}
} // namespace

TEST_F(SerializedTest, SplitOptimizationSimple)
{
    reset();
    auto segments = genSplitSegments();
    for (size_t prefix = 0; prefix <= segments.size(); ++prefix) {
        reset();
        std::vector<size_t> segmentSizes;
        segmentSizes.reserve(prefix);
        std::string data;
        for (size_t i = 0; i < prefix; ++i) {
            data += segments[i];
            segmentSizes.push_back(segments[i].size());
        }
        auto node = ZL_Compressor_registerSplitNode_withParams(
                cgraph_,
                ZL_Type_serial,
                segmentSizes.data(),
                segmentSizes.size());
        auto graph = ZL_Compressor_registerStaticGraph_fromNode1o(
                cgraph_, node, ZL_GRAPH_ENTROPY);
        finalizeGraph(graph, 1);
        testRoundTrip(data);
    }
}

TEST_F(SerializedTest, SplitOptimizationInMultiInputGraph)
{
    using TypedRef = std::unique_ptr<ZL_TypedRef, ZS2_TypedRef_Deleter>;

    auto segments = genSplitSegments();
    std::string concatenated;
    for (const auto& segment : segments) {
        concatenated += segment;
    }

    std::mt19937 gen(0xdeadbeef);
    for (size_t iter = 0; iter < 20; ++iter) {
        reset();
        auto graph = buildSplitOptimizationGraph(cgraph_, gen());
        finalizeGraph(graph, 1);

        auto nbInputs = std::uniform_int_distribution<size_t>(1, 20)(gen);
        std::vector<TypedRef> inputs;
        std::vector<TypedInputDesc> descs;
        for (size_t i = 0; i < nbInputs; ++i) {
            std::uniform_int_distribution<size_t> dist(
                    0, concatenated.size() - 1);
            std::string_view data{ concatenated.data(), dist(gen) };
            inputs.emplace_back(
                    ZL_TypedRef_createSerial(data.data(), data.size()));
            if (inputs.back().get() == nullptr) {
                throw std::runtime_error("ZL_TypedRef_createSerial() failed");
            }
            TypedInputDesc desc = {
                .data     = std::string(data),
                .type     = ZL_Type_serial,
                .eltWidth = 1,
            };
            descs.push_back(std::move(desc));
        }

        testRoundTripMI(inputs, descs);

        auto [cSize, compressed] = compressMI(inputs);
        ZL_REQUIRE_SUCCESS(cSize);

        std::vector<std::unique_ptr<char[]>> rtBuffers;
        std::vector<ZL_TypedBuffer*> decompressed;
        for (size_t i = 0; i < nbInputs; ++i) {
            auto size = ZL_Input_numElts(inputs[i].get());
            rtBuffers.push_back(std::make_unique<char[]>(size));
            decompressed.push_back(ZL_TypedBuffer_createWrapSerial(
                    rtBuffers.back().get(), size));
            ZL_REQUIRE_NN(decompressed.back());
        }
        auto report = ZL_DCtx_decompressMultiTBuffer(
                dctx_,
                decompressed.data(),
                decompressed.size(),
                compressed->data(),
                compressed->size());
        ZL_REQUIRE_SUCCESS(report);
        for (size_t i = 0; i < nbInputs; ++i) {
            assertEqual(decompressed[i], descs[i]);
            ZL_TypedBuffer_free(decompressed[i]);
        }
    }
}

} // namespace tests
} // namespace openzl
