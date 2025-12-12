// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <random>

#include <gtest/gtest.h>

#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_errors.h"
#include "tests/zstrong/test_zstrong_fixture.h"

namespace openzl::tests {

class GraphValidationTest : public ZStrongTest {
   public:
    ZL_GraphID declareTrivialSelector(
            ZL_Type inStreamType,
            std::vector<ZL_GraphID> graphs)
    {
        ZL_SelectorFn selectorFunction = [](const ZL_Selector* selCtx,
                                            const ZL_Input* inputStream,
                                            const ZL_GraphID* cfns,
                                            size_t nbCfns) noexcept {
            (void)selCtx;
            (void)inputStream;
            (void)nbCfns;
            return cfns[0];
        };
        ZL_SelectorDesc selector_desc = {
            .selector_f     = selectorFunction,
            .inStreamType   = inStreamType,
            .customGraphs   = graphs.data(),
            .nbCustomGraphs = graphs.size(),
        };

        return ZL_Compressor_registerSelectorGraph(cgraph_, &selector_desc);
    }
};

/* Note(@Cyan): since zstrong supports Typed Inputs, there is no longer a
 * requirement for the first default Graph to support Serial Inputs. */

TEST_F(GraphValidationTest, StartSerialized)
{
    reset();
    ASSERT_FALSE(ZL_isError(
            ZL_Compressor_validate(cgraph_, ZL_GRAPH_BITPACK_SERIAL)));
}

TEST_F(GraphValidationTest, Token2ToSerialExplicit)
{
    reset();
    ZL_GraphID graph = declareGraph(
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN2,
            declareGraph(ZL_NODE_CONVERT_TOKEN_TO_SERIAL, ZL_GRAPH_STORE));
    ASSERT_FALSE(ZL_isError(ZL_Compressor_validate(cgraph_, graph)));
}

TEST_F(GraphValidationTest, Token2ToSerialImplicit)
{
    reset();
    ZL_GraphID graph = declareGraph(
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN2, ZL_GRAPH_BITPACK_SERIAL);
    ASSERT_FALSE(ZL_isError(ZL_Compressor_validate(cgraph_, graph)));
}

TEST_F(GraphValidationTest, MismatchedStreamTypeLayeredGraph)
{
    reset();
    ZL_GraphID graph = declareGraph(
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN2,
            declareGraph(
                    ZL_NODE_CONVERT_TOKEN_TO_SERIAL, ZL_GRAPH_BITPACK_INT));
    ASSERT_TRUE(ZL_isError(ZL_Compressor_validate(cgraph_, graph)));
}

TEST_F(GraphValidationTest, SelectorMatchedSuccessors)
{
    reset();
    ZL_GraphID graph = declareTrivialSelector(
            ZL_Type_serial, { ZL_GRAPH_STORE, ZL_GRAPH_BITPACK_SERIAL });
    ASSERT_FALSE(ZL_isError(ZL_Compressor_validate(cgraph_, graph)));
}

TEST_F(GraphValidationTest, SelectorMismatchedSuccessors)
{
    reset();
    ZL_GraphID graph = declareTrivialSelector(
            ZL_Type_serial,
            { ZL_GRAPH_STORE, ZL_GRAPH_BITPACK_SERIAL, ZL_GRAPH_BITPACK_INT });
    ASSERT_TRUE(ZL_isError(ZL_Compressor_validate(cgraph_, graph)));
}

TEST_F(GraphValidationTest, MismatchedStreamTypeLayeredGraphSelector)
{
    reset();
    ZL_GraphID graph = declareGraph(
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN2,
            declareGraph(
                    ZL_NODE_CONVERT_TOKEN_TO_SERIAL,
                    declareTrivialSelector(
                            ZL_Type_serial,
                            { ZL_GRAPH_STORE,
                              ZL_GRAPH_BITPACK_SERIAL,
                              ZL_GRAPH_BITPACK_INT })));
    ASSERT_TRUE(ZL_isError(ZL_Compressor_validate(cgraph_, graph)));
}

TEST_F(GraphValidationTest, StandardGraph)
{
    reset();
    ASSERT_FALSE(ZL_isError(
            ZL_Compressor_validate(cgraph_, ZL_GRAPH_BITPACK_SERIAL)));
}

TEST_F(GraphValidationTest, WrongNumberOfSuccessors)
{
    reset();
    ZL_GraphID graph = declareGraph(
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN2,
            { ZL_GRAPH_STORE, ZL_GRAPH_STORE });
    ASSERT_TRUE(ZL_isError(ZL_Compressor_validate(cgraph_, graph)));
}

static size_t numNewlines(std::string_view data)
{
    return (size_t)std::count(data.begin(), data.end(), '\n');
}

TEST_F(GraphValidationTest, ErrorContextIsProvided)
{
    reset();
    ZL_GraphID graph = declareGraph(
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN2,
            { ZL_GRAPH_STORE, ZL_GRAPH_STORE });

    auto const report = ZL_Compressor_validate(cgraph_, graph);
    ASSERT_TRUE(ZL_isError(report));

    auto const errorContext =
            ZL_Compressor_getErrorContextString(cgraph_, report);
    // Check that we have at least 6 lines, because error context strings print
    // lines for the code, message, graph id, node id, transform id, and stack
    // trace.
    ASSERT_GT(numNewlines(errorContext), size_t(5))
            << std::string(errorContext);
}

} // namespace openzl::tests
