// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "openzl/openzl.hpp"
#include "openzl/zl_compressor.h"
#include "openzl/zl_public_nodes.h"

using namespace testing;

namespace openzl::tests {

TEST(TestFrameInfo, basic)
{
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    compressor.unwrap(ZL_Compressor_selectStartingGraphID(
            compressor.get(), ZL_GRAPH_COMPRESS_GENERIC));
    std::vector<Input> inputs;
    CCtx cctx;
    cctx.refCompressor(compressor);
    std::array<int64_t, 100> data = {};
    data[50]                      = 50;
    inputs.push_back(Input::refStruct(poly::span<const int64_t>(data)));
    inputs.push_back(Input::refNumeric(poly::span<const int64_t>(data)));
    inputs.push_back(
            Input::refSerial(
                    "hello world this is some test input hello hello hello world hello test input"));
    std::array<uint32_t, 5> lengths = { 1, 3, 2, 1, 2 };
    inputs.push_back(Input::refString("133322122", lengths));
    auto compressed = cctx.compress(inputs);

    FrameInfo info(compressed);
    ASSERT_EQ(info.numOutputs(), inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        ASSERT_EQ(info.outputType(i), inputs[i].type());
        ASSERT_EQ(info.outputContentSize(i), inputs[i].contentSize());
    }
}

TEST(TestFrameInfo, HelpfulExceptionOnCorruption)
{
    try {
        FrameInfo info("not an openzl frame");
    } catch (const Exception& e) {
        ASSERT_EQ(e.code(), ZL_ErrorCode_corruption);
        ASSERT_NE(e.msg().find("Corrupt"), poly::string_view::npos);
    }
}
} // namespace openzl::tests
