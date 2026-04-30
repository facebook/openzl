// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <array>
#include <random>

#include <gtest/gtest.h>

#include "openzl/shared/bits.h"
#include "openzl/zl_opaque_types.h"
#include "tests/utils.h"
#include "tests/zstrong/test_integer_fixture.h"

namespace openzl {
namespace tests {
std::string IntegerTest::generatedData(size_t nbElts, uint64_t cardinality)
{
    std::mt19937 gen(0xdeadbeef);

    uint64_t const maxCardinality = eltWidth_ == 8
            ? (uint64_t)-1
            : ((uint64_t)1 << (eltWidth_ * 8)) - 1;
    if (cardinality > maxCardinality)
        cardinality = maxCardinality;

    ZL_REQUIRE(ZL_isLittleEndian());
    std::string out(nbElts * eltWidth_, 0);
    std::uniform_int_distribution<uint64_t> dist(
            min_, std::min(max_, cardinality - 1));
    for (size_t i = 0; i < nbElts; ++i) {
        uint64_t const elt = dist(gen);
        memcpy(out.data() + i * eltWidth_, &elt, eltWidth_);
    }
    return out;
}

void IntegerTest::test()
{
    if (min_ == 0 && max_ == (uint64_t)-1) {
        testRoundTrip("");
        testRoundTrip("a");
        testRoundTrip("foo");
        testRoundTrip("foobar foo foo bar bar foobar foo foo bar");
        testRoundTrip(kUniqueCharsTestInput);
        testRoundTrip(kFooTestInput);
        testRoundTrip(kLoremTestInput);
        testRoundTrip(kAudioPCMS32LETestInput);
        testRoundTrip(std::string(100000, 'x'));
    }
    testRoundTrip(generatedData(100, 10));
    testRoundTrip(generatedData(1000, 10));
    testRoundTrip(generatedData(1000, 100));
    testRoundTrip(generatedData(1000, 1000));
    testRoundTrip(generatedData(10000, 100));
    testRoundTrip(generatedData(10000, 10000));
    testRoundTrip(generatedData(10000, 100000));
    testRoundTrip(generatedData(10000, 1000000));
    testRoundTrip(generatedData(10000, 10000000));
    testRoundTrip(generatedData(10000, 100000000));
    testRoundTrip(generatedData(10000, 1000000000));
    testRoundTrip(generatedData(10000, (uint64_t)-1));

    std::array<size_t, 5> nbElts = { 1, 10, 100, 1000, 10000 };
    for (size_t n : nbElts) {
        for (uint64_t c = 1; c <= 63; ++c) {
            auto const cardinality = (uint64_t)1 << c;
            testRoundTrip(generatedData(n, cardinality));
        }
    }
}

void IntegerTest::testNode(ZL_NodeID node, size_t eltWidth)
{
    reset();
    ZL_GraphID graph = declareGraph(node);
    finalizeGraph(graph, eltWidth);
    test();
}

void IntegerTest::testNodeOnInput(
        ZL_NodeID node,
        size_t eltWidth,
        std::string_view data)
{
    reset();
    ZL_GraphID graph = declareGraph(node);
    finalizeGraph(graph, eltWidth);
    testRoundTrip(data);
}

void IntegerTest::testGraph(ZL_GraphID graph, size_t eltWidth)
{
    reset();
    finalizeGraph(graph, eltWidth);
    test();
}

} // namespace tests
} // namespace openzl
