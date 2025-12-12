// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <array>
#include <numeric>
#include <random>

#include <gtest/gtest.h>

#include "tests/utils.h"
#include "tests/zstrong/test_serialized_fixture.h"

namespace openzl {
namespace tests {

std::string SerializedTest::generatedData(size_t nbElts, size_t cardinality)
{
    std::mt19937 gen(0xdeadbeef);

    cardinality = std::min(cardinality, size_t(256));

    std::string alphabet(256, 0);
    std::iota(alphabet.begin(), alphabet.end(), 0);
    std::shuffle(alphabet.begin(), alphabet.end(), gen);
    alphabet.resize(cardinality);

    std::string out;
    out.reserve(nbElts);
    std::uniform_int_distribution<size_t> dist(0, cardinality - 1);
    for (size_t i = 0; i < nbElts; ++i) {
        out.push_back(alphabet[dist(gen)]);
    }
    return out;
}

void SerializedTest::test()
{
    testRoundTrip("");
    testRoundTrip("a");
    testRoundTrip("foo");
    testRoundTrip("foobar foo foo bar bar foobar foo foo bar");
    testRoundTrip(kUniqueCharsTestInput);
    testRoundTrip(kFooTestInput);
    testRoundTrip(kLoremTestInput);
    testRoundTrip(kAudioPCMS32LETestInput);
    testRoundTrip(std::string(100000, 'x'));
    testRoundTrip(generatedData(1, 1));
    testRoundTrip(generatedData(10, 1));
    testRoundTrip(generatedData(10, 5));
    testRoundTrip(generatedData(10, 10));
    testRoundTrip(generatedData(100, 10));
    testRoundTrip(generatedData(100, 100));
    testRoundTrip(generatedData(1000, 2));
    testRoundTrip(generatedData(1000, 10));
    testRoundTrip(generatedData(1000, 100));
    testRoundTrip(generatedData(1000, 256));

    std::array<size_t, 5> nbElts       = { 1, 10, 100, 1000, 10000 };
    std::array<size_t, 20> cardinality = { 1,  2,   3,   4,   7,   8,  15,
                                           16, 28,  31,  32,  48,  63, 64,
                                           94, 127, 128, 150, 255, 256 };
    for (size_t n : nbElts) {
        for (size_t c : cardinality) {
            testRoundTrip(generatedData(n, c));
            if (c > n)
                break;
        }
    }
}

void SerializedTest::testNode(ZL_NodeID node, size_t eltWidth)
{
    reset();
    finalizeGraph(declareGraph(node), eltWidth);
    test();
}

void SerializedTest::testGraph(ZL_GraphID graph, size_t eltWidth)
{
    reset();
    finalizeGraph(graph, eltWidth);
    test();
}

void SerializedTest::testNodeOnInput(
        ZL_NodeID node,
        std::string_view input,
        size_t eltWidth)
{
    reset();
    finalizeGraph(declareGraph(node), eltWidth);
    testRoundTrip(input);
}

void SerializedTest::testGraphOnInput(
        ZL_GraphID graph,
        std::string_view input,
        size_t eltWidth)
{
    reset();
    finalizeGraph(graph, eltWidth);
    testRoundTrip(input);
}

void SerializedTest::testParameterizedNodeOnInput(
        ZL_NodeID node,
        const ZL_LocalParams& localParams,
        std::string_view input,
        size_t eltWidth)
{
    reset();
    ZL_NodeID paramNode = createParameterizedNode(node, localParams);
    finalizeGraph(declareGraph(paramNode), eltWidth);
    testRoundTrip(input);
}

} // namespace tests
} // namespace openzl
