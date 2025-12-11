// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <gtest/gtest.h>

#include "openzl/zl_compressor.h"
#include "tests/zstrong/test_zstrong_fixture.h"

namespace openzl {
namespace tests {

class VariableTest : public ZStrongTest {
   public:
    std::vector<uint32_t> genFieldSizes(std::string_view input);
    void testVsfRoundTrip(
            ZL_GraphID graph,
            std::string_view input,
            std::vector<uint32_t> fieldSizes,
            bool useLargeBounds);
    void testNode(ZL_NodeID node);
    void testGraph(ZL_GraphID graph);
    void testNodeOnInput(
            ZL_NodeID node,
            std::string_view input,
            std::vector<uint32_t> fieldSizes,
            bool useLargeBounds);
    void testGraphOnInput(
            ZL_GraphID graph,
            std::string_view input,
            std::vector<uint32_t> fieldSizes,
            bool useLargeBounds);
};

} // namespace tests
} // namespace openzl
