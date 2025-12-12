// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <random>

#include "tests/utils.h"
#include "tests/zstrong/test_variable_fixture.h"

namespace openzl {
namespace tests {

// TODO: replace with the datagen StringInputProducer
std::vector<uint32_t> VariableTest::genFieldSizes(std::string_view input)
{
    if (input.size() == 0) {
        return { 0 };
    }

    std::vector<uint32_t> fieldSizes;
    fieldSizes.reserve(input.size());
    uint32_t fieldSize = 0;
    for (char c : input) {
        if (c != ' ') {
            ++fieldSize;
        } else {
            if (fieldSize > 0) {
                fieldSizes.push_back(fieldSize);
            }
            fieldSizes.push_back(1);
            fieldSize = 0;
        }
    }

    if (fieldSize > 0) {
        fieldSizes.push_back(fieldSize);
    }

    return fieldSizes;
}

void VariableTest::testVsfRoundTrip(
        ZL_GraphID graph,
        std::string_view input,
        std::vector<uint32_t> fieldSizes,
        bool useLargeBounds)
{
    if (fieldSizes.size() == 0) {
        fieldSizes = genFieldSizes(input);
    }
    setLargeCompressBound(useLargeBounds ? 8 : 1);
    setVsfFieldSizes(fieldSizes);
    finalizeGraph(graph, 1);
    testRoundTrip(input);
}

void VariableTest::testNode(ZL_NodeID node)
{
    reset();
    testGraph(declareGraph(node));
}

void VariableTest::testGraph(ZL_GraphID graph)
{
    testVsfRoundTrip(graph, "", {}, false);
    testVsfRoundTrip(graph, "a", {}, false);
    testVsfRoundTrip(graph, "aaaaaa", { 1, 2, 3 }, false);
    testVsfRoundTrip(graph, "aaaaaa", { 3, 2, 1 }, false);
    testVsfRoundTrip(
            graph, "appappleapple pieapple pies", { 3, 5, 9, 10 }, false);
    testVsfRoundTrip(
            graph, "foobar foo foo bar bar foobar foo foo bar", {}, false);
    testVsfRoundTrip(
            graph,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
            { 40, 40, 40 },
            false);
    testVsfRoundTrip(graph, kUniqueCharsTestInput, {}, false);
    testVsfRoundTrip(graph, kFooTestInput, {}, false);
    testVsfRoundTrip(graph, kLoremTestInput, {}, true);
    testVsfRoundTrip(graph, kAudioPCMS32LETestInput, {}, false);
    testVsfRoundTrip(graph, std::string(100000, 'x'), {}, false);
}

void VariableTest::testNodeOnInput(
        ZL_NodeID node,
        std::string_view input,
        std::vector<uint32_t> fieldSizes,
        bool useLargeBounds)
{
    reset();
    testGraphOnInput(declareGraph(node), input, fieldSizes, useLargeBounds);
}

void VariableTest::testGraphOnInput(
        ZL_GraphID graph,
        std::string_view input,
        std::vector<uint32_t> fieldSizes,
        bool useLargeBounds)
{
    testVsfRoundTrip(graph, input, fieldSizes, useLargeBounds);
}

} // namespace tests
} // namespace openzl
