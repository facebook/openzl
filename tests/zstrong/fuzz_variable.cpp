// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include "security/lionhead/utils/lib_ftest/ftest.h"

#include "openzl/codecs/zl_parse_int.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_data.h"
#include "tests/datagen/random_producer/LionheadFDPWrapper.h"
#include "tests/datagen/random_producer/RandWrapper.h"
#include "tests/datagen/structures/IntegerStringProducer.h"
#include "tests/fuzz_utils.h"
#include "tests/zstrong/test_variable_fixture.h"

namespace zstrong {
namespace tests {
namespace {

template <typename FDP>
std::vector<size_t> getSegments(
        FDP& f,
        size_t const srcSize,
        bool lastZero      = true,
        size_t maxSegments = 512)
{
    size_t const numSegments = f.usize_range(
            "num_segments",
            0,
            std::min(maxSegments, std::max<size_t>(srcSize, 10)));
    std::vector<size_t> segmentSizes;
    size_t totalSize = 0;
    segmentSizes.reserve(numSegments);
    for (size_t i = 0; i < numSegments; ++i) {
        size_t const segmentSize =
                f.usize_range("segment_size", 0, srcSize - totalSize);
        segmentSizes.push_back(segmentSize);
        totalSize += segmentSize;
    }
    if (totalSize < srcSize) {
        if (lastZero) {
            segmentSizes.push_back(0);
        } else {
            segmentSizes.push_back(srcSize - totalSize);
        }
    }
    return segmentSizes;
}

FUZZ_F(VariableTest, FuzzPrefixRoundTrip)
{
    std::string input = gen_str(f, "input_str", InputLengthInBytes(1));

    StructuredFDP<HarnessMode>* fPtr = &f;

    reset();
    auto parser = [](ZL_SetStringLensState* state, ZL_Input const* in) {
        auto& fdp = **static_cast<StructuredFDP<HarnessMode>* const*>(
                ZL_SetStringLensState_getOpaquePtr(state));
        auto const segments = getSegments(fdp, ZL_Input_numElts(in), false);
        auto segmentSizes   = (uint32_t*)ZL_SetStringLensState_malloc(
                state, segments.size() * sizeof(uint32_t));
        ZL_SetStringLensInstructions instructions = { segmentSizes, 0 };
        if (segmentSizes == NULL) {
            return instructions;
        }
        for (size_t i = 0; i < segments.size(); ++i) {
            segmentSizes[i] = uint32_t(segments[i]);
        }
        instructions.nbStrings = segments.size();
        return instructions;
    };

    ZL_GraphID prefixGraph = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph_,
            ZL_NODE_PREFIX,
            ZL_GRAPHLIST(ZL_GRAPH_STRING_STORE, ZL_GRAPH_STORE));
    prefixGraph = declareGraph(
            ZL_Compressor_registerConvertSerialToStringNode(
                    cgraph_, parser, &fPtr),
            prefixGraph);
    finalizeGraph(prefixGraph, 1);
    setLargeCompressBound(8);
    testRoundTrip(input);
}

FUZZ_F(VariableTest, FuzzTokenizeRoundTrip)
{
    std::string input = gen_str(f, "input_str", InputLengthInBytes(1));

    StructuredFDP<HarnessMode>* fPtr = &f;

    reset();
    auto parser = [](ZL_SetStringLensState* state, ZL_Input const* in) {
        auto& fdp = **static_cast<StructuredFDP<HarnessMode>* const*>(
                ZL_SetStringLensState_getOpaquePtr(state));
        auto const segments = getSegments(fdp, ZL_Input_numElts(in), false);
        auto segmentSizes   = (uint32_t*)ZL_SetStringLensState_malloc(
                state, segments.size() * sizeof(uint32_t));
        ZL_SetStringLensInstructions instructions = { segmentSizes, 0 };
        if (segmentSizes == NULL) {
            return instructions;
        }
        for (size_t i = 0; i < segments.size(); ++i) {
            segmentSizes[i] = uint32_t(segments[i]);
        }
        instructions.nbStrings = segments.size();
        return instructions;
    };
    ZL_GraphID tokenizeGraph = ZL_Compressor_registerTokenizeGraph(
            cgraph_,
            ZL_Type_string,
            false,
            ZL_GRAPH_STRING_STORE,
            ZL_GRAPH_STORE);
    tokenizeGraph = declareGraph(
            ZL_Compressor_registerConvertSerialToStringNode(
                    cgraph_, parser, &fPtr),
            tokenizeGraph);
    finalizeGraph(tokenizeGraph, 1);
    setLargeCompressBound(8);
    testRoundTrip(input);
}

FUZZ_F(VariableTest, FuzzTokenizeSortedRoundTrip)
{
    std::string input = gen_str(f, "input_str", InputLengthInBytes(1));

    StructuredFDP<HarnessMode>* fPtr = &f;

    reset();
    auto parser = [](ZL_SetStringLensState* state, ZL_Input const* in) {
        auto& fdp = **static_cast<StructuredFDP<HarnessMode>* const*>(
                ZL_SetStringLensState_getOpaquePtr(state));
        auto const segments = getSegments(fdp, ZL_Input_numElts(in), false);
        auto segmentSizes   = (uint32_t*)ZL_SetStringLensState_malloc(
                state, segments.size() * sizeof(uint32_t));
        ZL_SetStringLensInstructions instructions = { segmentSizes, 0 };
        if (segmentSizes == NULL) {
            return instructions;
        }
        for (size_t i = 0; i < segments.size(); ++i) {
            segmentSizes[i] = uint32_t(segments[i]);
        }
        instructions.nbStrings = segments.size();
        return instructions;
    };
    ZL_GraphID tokenizeSortedGraph = ZL_Compressor_registerTokenizeGraph(
            cgraph_,
            ZL_Type_string,
            true,
            ZL_GRAPH_STRING_STORE,
            ZL_GRAPH_STORE);
    tokenizeSortedGraph = declareGraph(
            ZL_Compressor_registerConvertSerialToStringNode(
                    cgraph_, parser, &fPtr),
            tokenizeSortedGraph);
    finalizeGraph(tokenizeSortedGraph, 1);
    setLargeCompressBound(8);
    testRoundTrip(input);
}

FUZZ_F(VariableTest, FuzzDispatchStringRoundTrip)
{
    std::string input = gen_str(f, "inputStr", InputLengthInBytes(1));

    reset();

    const auto tmpSegments = getSegments(f, input.size(), false);
    const std::vector<uint32_t> segments(
            tmpSegments.begin(), tmpSegments.end());
    uint16_t nbOutputs = f.u16_range("nbOutputs", 0, 2048);
    const auto indices = VecDistribution<Range<uint16_t>, Const<size_t>>(
                                 Range<uint16_t>(0, nbOutputs),
                                 Const<size_t>(segments.size()))
                                 .gen("indices", f);

    if (0) {
        std::cout << "input_str of length " << input.size() << " \"" << std::hex
                  << input << "\"" << std::endl;
        std::cout << std::dec;
        std::cout << "nbOutputs " << (unsigned)nbOutputs << std::endl;
        std::cout << "segments [";
        for (auto s : segments) {
            std::cout << " " << s;
        }
        std::cout << std::endl;
        std::cout << "indices [";
        for (auto i : indices) {
            std::cout << " " << (unsigned)i;
        }
        std::cout << std::endl;
    }

    setVsfFieldSizes(segments);
    const auto dispatchStringNode = ZL_Compressor_registerDispatchStringNode(
            cgraph_, nbOutputs, indices.data());
    ZL_GraphID dispatchGraph = declareGraph(dispatchStringNode);
    finalizeGraph(dispatchGraph, 1);
    setLargeCompressBound(1024);

    bool compressionShouldSucceed =
            std::all_of(indices.begin(), indices.end(), [nbOutputs](auto i) {
                return i < nbOutputs;
            });

    if (compressionShouldSucceed) {
        testRoundTrip(input);
    } else {
        testRoundTripCompressionMayFail(input);
    }
}

FUZZ_F(VariableTest, FuzzParseIntRoundTrip)
{
    auto rw = std::make_shared<
            datagen::LionheadFDPWrapper<StructuredFDP<HarnessMode>>>(f);
    auto gen  = datagen::IntegerStringProducer(rw);
    auto data = gen("data");

    reset();
    auto const [input, fieldSizes] =
            zstrong::tests::datagen::IntegerStringProducer::flatten(data);

    std::vector<std::unique_ptr<ZL_TypedRef, ZL_TypedRef_Deleter>> inputs;
    std::vector<TypedInputDesc> inputDescs;
    inputDescs.emplace_back(
            std::move(input), ZL_Type_string, 1, std::move(fieldSizes));
    inputs.emplace_back(ZL_TypedRef_createString(
            inputDescs.front().data.data(),
            inputDescs.front().data.size(),
            inputDescs.front().strLens.data(),
            inputDescs.front().strLens.size()));

    ZL_GraphID parseIntGraph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph_, ZL_NODE_PARSE_INT, ZL_GRAPH_STORE);
    ZL_REQUIRE_SUCCESS(
            ZL_Compressor_selectStartingGraphID(cgraph_, parseIntGraph));
    testRoundTripMI(inputs, inputDescs);
}

FUZZ_F(VariableTest, FuzzParseIntSafeRoundTrip)
{
    std::string input = gen_str(f, "inputStr", InputLengthInBytes(1));

    reset();
    const auto tmpSegments = getSegments(f, input.size(), false);
    std::vector<uint32_t> fieldSizes(tmpSegments.begin(), tmpSegments.end());

    std::vector<std::unique_ptr<ZL_TypedRef, ZL_TypedRef_Deleter>> inputs;
    std::vector<TypedInputDesc> inputDescs;
    inputDescs.emplace_back(
            std::move(input), ZL_Type_string, 1, std::move(fieldSizes));
    const auto& inputDesc = inputDescs[0];
    inputs.emplace_back(ZL_TypedRef_createString(
            inputDesc.data.data(),
            inputDesc.data.size(),
            inputDesc.strLens.data(),
            inputDesc.strLens.size()));

    ZL_GraphID parseIntSafeGraph =
            ZL_RES_value(ZL_Compressor_parameterizeTryParseIntGraph(
                    cgraph_, ZL_GRAPH_FIELD_LZ, ZL_GRAPH_COMPRESS_GENERIC));
    ZL_REQUIRE_SUCCESS(
            ZL_Compressor_selectStartingGraphID(cgraph_, parseIntSafeGraph));
    testRoundTripMI(inputs, inputDescs);
}

} // namespace
} // namespace tests
} // namespace zstrong
