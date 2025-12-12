// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <vector>

#include "openzl/compress/private_nodes.h"
#include "tests/zstrong/test_variable_fixture.h"

namespace openzl {
namespace tests {

TEST_F(VariableTest, Prefix)
{
    testNode(ZL_NODE_PREFIX);
}

TEST_F(VariableTest, Tokenize)
{
    reset();
    testGraph(ZL_Compressor_registerTokenizeGraph(
            cgraph_,
            ZL_Type_string,
            false,
            ZL_GRAPH_STRING_STORE,
            ZL_GRAPH_STORE));
}

TEST_F(VariableTest, TokenizeSorted)
{
    reset();
    testGraph(ZL_Compressor_registerTokenizeGraph(
            cgraph_,
            ZL_Type_string,
            true,
            ZL_GRAPH_STRING_STORE,
            ZL_GRAPH_STORE));
}

} // namespace tests
} // namespace openzl
