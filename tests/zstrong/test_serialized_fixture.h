// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <vector>

#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "tests/zstrong/test_zstrong_fixture.h"

namespace openzl {
namespace tests {

class SerializedTest : public ZStrongTest {
   public:
    void test();

    void testNode(ZL_NodeID node, size_t eltWidth = 1);

    void testGraph(ZL_GraphID node, size_t eltWidth = 1);

    std::string generatedData(size_t nbElts, size_t cardinality);

    void testNodeOnInput(
            ZL_NodeID node,
            std::string_view input,
            size_t eltWidth = 1);

    void testGraphOnInput(
            ZL_GraphID graph,
            std::string_view input,
            size_t eltWidth = 1);

    void testParameterizedNodeOnInput(
            ZL_NodeID node,
            const ZL_LocalParams& localParams,
            std::string_view input,
            size_t eltWidth = 1);
};

} // namespace tests
} // namespace openzl
