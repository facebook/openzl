// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "openzl/openzl.hpp"
#include "openzl/zl_config.h"

namespace openzl::tests {
namespace {

class SizeofTrackingFunctionGraph : public FunctionGraph {
   public:
    explicit SizeofTrackingFunctionGraph(const ZL_CCtx* cctx) : cctx_(cctx) {}

    FunctionGraphDescription functionGraphDescription() const override
    {
        FunctionGraphDescription desc = {
            .name                = "SizeofTrackingFunctionGraph",
            .inputTypeMasks      = { TypeMask::Any },
            .lastInputIsVariable = true,
        };
        return desc;
    }

    void graph(GraphState& state) const override
    {
        const size_t before = ZL_CCtx_sizeof(cctx_);
        ASSERT_NE(state.getScratchSpace(kScratchSize), nullptr);
        EXPECT_GE(ZL_CCtx_sizeof(cctx_), before + kScratchSize);
        Edge::setMultiInputDestination(state.edges(), ZL_GRAPH_STORE);
    }

   private:
    static constexpr size_t kScratchSize = 4096;
    const ZL_CCtx* cctx_;
};

class CCtxTest : public testing::Test {
   public:
    void SetUp() override
    {
        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor_.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);
    }

    Compressor compressor_;
};

std::string testRoundTrip(CCtx& cctx, const Input& input)
{
    auto compressed   = cctx.compress({ &input, 1 });
    auto decompressed = DCtx().decompress(compressed);
    EXPECT_EQ(decompressed.size(), 1);
    EXPECT_EQ(decompressed[0], input);
    return compressed;
}

} // namespace

TEST_F(CCtxTest, cctxSizeof)
{
    EXPECT_EQ(ZL_CCtx_sizeof(nullptr), 0);

    CCtx cctx;
    EXPECT_GT(ZL_CCtx_sizeof(cctx.get()), 0);
}

TEST_F(CCtxTest, dctxSizeof)
{
    EXPECT_EQ(ZL_DCtx_sizeof(nullptr), 0);

    DCtx dctx;
    EXPECT_GT(ZL_DCtx_sizeof(dctx.get()), 0);
}

TEST_F(CCtxTest, cctxSizeofIncludesGraphArena)
{
    CCtx cctx;
    const auto graph = compressor_.registerFunctionGraph(
            std::make_shared<SizeofTrackingFunctionGraph>(cctx.get()));
    cctx.selectStartingGraph(compressor_, graph);
    testRoundTrip(
            cctx,
            Input::refSerial(
                    "hello world this is some test input hello hello hello world hello test input"));
}

} // namespace openzl::tests
