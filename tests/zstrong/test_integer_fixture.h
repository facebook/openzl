// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <vector>

#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "tests/zstrong/test_zstrong_fixture.h"

namespace openzl {
namespace tests {

class IntegerTest : public ZStrongTest {
   public:
    void setValueBounds(uint64_t min, uint64_t max = (uint64_t)-1)
    {
        min_ = min;
        max_ = max;
    }

    /// Run the entire test suite (after finalize graph)
    void test();

    /// Helper to reset the state, set the node/eltWidth, and test()
    void testNode(ZL_NodeID node, size_t eltWidth);

    void
    testNodeOnInput(ZL_NodeID node, size_t eltWidth, std::string_view data);

    void testGraph(ZL_GraphID graph, size_t eltWidth = 1);

   private:
    std::string generatedData(size_t nbElts, uint64_t cardinality);

    uint64_t min_{ 0 };
    uint64_t max_{ (uint64_t)-1 };
};

} // namespace tests
} // namespace openzl
