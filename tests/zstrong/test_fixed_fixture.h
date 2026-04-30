// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <vector>

#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "tests/zstrong/test_zstrong_fixture.h"

namespace openzl {
namespace tests {

class FixedTest : public ZStrongTest {
   public:
    std::string generatedData(size_t nbElts, size_t cardinality);

    void test();

    void testPipeNodes(ZL_NodeID node0, ZL_NodeID node1, size_t eltWidth);

    void testNode(ZL_NodeID node, size_t eltWidth);

    void testGraph(ZL_GraphID graph, size_t eltWidth);

    void testPipeNodesOnInput(
            ZL_NodeID node0,
            ZL_NodeID node1,
            size_t eltWidth,
            std::string_view input);

    void
    testNodeOnInput(ZL_NodeID node, size_t eltWidth, std::string_view input);

    void
    testGraphOnInput(ZL_GraphID graph, size_t eltWidth, std::string_view input);

    void setAlphabetMask(std::string const& mask);

   private:
    std::string alphabetMask_;
};

} // namespace tests
} // namespace openzl
