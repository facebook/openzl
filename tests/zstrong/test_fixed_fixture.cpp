// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <array>
#include <random>

#include <gtest/gtest.h>

#include "openzl/zl_opaque_types.h"
#include "tests/datagen/random_producer/compat_uniform_distribution.h"
#include "tests/utils.h"
#include "tests/zstrong/test_fixed_fixture.h"

namespace openzl {
namespace tests {
void FixedTest::setAlphabetMask(const std::string& mask)
{
    alphabetMask_ = mask;
}

std::string FixedTest::generatedData(size_t nbElts, size_t cardinality)
{
    ZL_DLOG(V,
            "generatedData(nbElts=%zu, cardinality=%zu)",
            nbElts,
            cardinality);
    std::mt19937 gen(42);
    std::string alphabet(cardinality * eltWidth_, 0);
    {
        datagen::compat_uniform_int_distribution<int8_t> dist;
        for (size_t i = 0; i < alphabet.size(); ++i) {
            alphabet[i] = dist(gen);
            if (i < alphabetMask_.size()) {
                alphabet[i] = (char)(alphabet[i] & alphabetMask_[i]);
            }
        }
    }

    std::string out(nbElts * eltWidth_, 0);
    std::uniform_int_distribution<size_t> dist(0, cardinality - 1);
    for (size_t i = 0; i < nbElts; ++i) {
        size_t const s = dist(gen);
        memcpy(out.data() + i * eltWidth_,
               alphabet.data() + s * eltWidth_,
               eltWidth_);
    }
    return out;
}

void FixedTest::test()
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
    testRoundTrip(generatedData(100, 10));
    testRoundTrip(generatedData(1000, 10));
    testRoundTrip(generatedData(1000, 100));
    testRoundTrip(generatedData(1000, 1000));
    testRoundTrip(generatedData(10000, 100));
    testRoundTrip(generatedData(10000, 1000));
    testRoundTrip(generatedData(10000, 10000));
    testRoundTrip(generatedData(100000, 100));

    std::array<size_t, 8> nbElts       = { 1, 10, 100, 1000, 10000, 50000 };
    std::array<size_t, 17> cardinality = { 1,   2,   4,   8,   16,  28,
                                           32,  48,  64,  90,  128, 180,
                                           256, 300, 512, 750, 1024 };
    for (size_t n : nbElts) {
        for (size_t c : cardinality) {
            testRoundTrip(generatedData(n, c));
            if (c > n)
                break;
        }
    }
}

void FixedTest::testPipeNodes(ZL_NodeID node0, ZL_NodeID node1, size_t eltWidth)
{
    reset();
    ZL_GraphID graph = declareGraph(node1);
    graph            = declareGraph(node0, graph);
    testGraph(graph, eltWidth);
}

void FixedTest::testNode(ZL_NodeID node, size_t eltWidth)
{
    reset();
    ZL_GraphID const graph = declareGraph(node);
    testGraph(graph, eltWidth);
}

void FixedTest::testGraph(ZL_GraphID graph, size_t eltWidth)
{
    finalizeGraph(graph, eltWidth);
    test();
}

void FixedTest::testPipeNodesOnInput(
        ZL_NodeID node0,
        ZL_NodeID node1,
        size_t eltWidth,
        std::string_view input)
{
    reset();
    ZL_GraphID graph = declareGraph(node1);
    graph            = declareGraph(node0, graph);
    testGraphOnInput(graph, eltWidth, input);
}

void FixedTest::testNodeOnInput(
        ZL_NodeID node,
        size_t eltWidth,
        std::string_view input)
{
    reset();
    ZL_GraphID const graph = declareGraph(node);
    testGraphOnInput(graph, eltWidth, input);
}

void FixedTest::testGraphOnInput(
        ZL_GraphID graph,
        size_t eltWidth,
        std::string_view input)
{
    finalizeGraph(graph, eltWidth);
    testRoundTrip(input);
}
} // namespace tests
} // namespace openzl
