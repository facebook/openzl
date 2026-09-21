// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "openzl/common/errors_internal.h"
#include "openzl/compress/cctx.h"
#include "openzl/openzl.hpp"
#include "openzl/zl_config.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_input.h"
#include "openzl/zl_output.h"

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
        EXPECT_GT(ZL_CCtx_sizeof(cctx_), before);
        Edge::setMultiInputDestination(state.edges(), ZL_GRAPH_STORE);
    }

   private:
    static constexpr size_t kScratchSize = 4096;
    const ZL_CCtx* cctx_;
};

struct SizedCodecState {
    char data[1024];
};

void* sizedCodecStateAlloc() noexcept
{
    return std::malloc(sizeof(SizedCodecState));
}

void sizedCodecStateFree(void* state) noexcept
{
    std::free(state);
}

size_t sizedCodecStateSizeof(const void* state) noexcept
{
    return state == nullptr ? 0 : sizeof(SizedCodecState);
}

ZL_Report statefulCopyEncoder(
        ZL_Encoder* ectx,
        const ZL_Input* inputs[],
        size_t nbInputs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(ectx);
    ZL_ASSERT_EQ(nbInputs, 1);
    const ZL_Input* const input = inputs[0];
    ZL_ERR_IF_NULL(ZL_Encoder_getState(ectx), allocation);

    ZL_Output* const output = ZL_Encoder_createTypedStream(
            ectx, 0, ZL_Input_numElts(input), ZL_Input_eltWidth(input));
    ZL_ERR_IF_NULL(output, allocation);
    std::memcpy(
            ZL_Output_ptr(output),
            ZL_Input_ptr(input),
            ZL_Input_contentSize(input));
    ZL_ERR_IF_ERR(ZL_Output_commit(output, ZL_Input_numElts(input)));
    return ZL_returnValue(1);
}

ZL_Report unusedCopyDecoder(ZL_Decoder*, const ZL_Input*[]) noexcept
{
    return ZL_returnSuccess();
}

void freeOpaque(void*, void* ptr) noexcept
{
    std::free(ptr);
}

ZL_GraphID genericGraph(ZL_Compressor*, const void*) noexcept
{
    return ZL_GRAPH_COMPRESS_GENERIC;
}

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

TEST_F(CCtxTest, compressorSizeof)
{
    EXPECT_EQ(ZL_Compressor_sizeof(nullptr), 0);

    const std::unique_ptr<ZL_Compressor, decltype(&ZL_Compressor_free)>
            compressor(ZL_Compressor_create(), ZL_Compressor_free);
    ASSERT_NE(compressor.get(), nullptr);
    EXPECT_GT(ZL_Compressor_sizeof(compressor.get()), 0);
}

TEST_F(CCtxTest, cctxSizeofIncludesInternalCompressor)
{
    const std::unique_ptr<ZL_Compressor, decltype(&ZL_Compressor_free)>
            compressor(ZL_Compressor_create(), ZL_Compressor_free);
    ASSERT_NE(compressor.get(), nullptr);
    const size_t compressorSize = ZL_Compressor_sizeof(compressor.get());

    CCtx cctx;
    const size_t before           = ZL_CCtx_sizeof(cctx.get());
    const ZL_Graph2Desc graphDesc = { genericGraph, nullptr };
    ASSERT_FALSE(ZL_isError(
            CCTX_setLocalCGraph_usingGraph2Desc(cctx.get(), graphDesc)));
    EXPECT_GE(ZL_CCtx_sizeof(cctx.get()), before + compressorSize);
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

TEST_F(CCtxTest, cctxSizeofIncludesCachedCodecStates)
{
    const ZL_Type inputTypes[]  = { ZL_Type_serial };
    const ZL_Type outputTypes[] = { ZL_Type_serial };
    const ZL_MIEncoderDesc encoderDesc    = {
            .gd = { .CTid           = 0x515545,
                    .inputTypes     = inputTypes,
                    .nbInputs       = 1,
                    .soTypes        = outputTypes,
                    .nbSOs          = 1 },
            .transform_f = statefulCopyEncoder,
            .name        = "sizeof_stateful_copy",
            .trStateMgr  = {
                     .stateAlloc      = sizedCodecStateAlloc,
                     .stateFree       = sizedCodecStateFree,
                     .optionalStateID = 0x515545,
                     .stateSizeof     = sizedCodecStateSizeof,
            },
    };
    const ZL_NodeID node =
            ZL_Compressor_registerMIEncoder(compressor_.get(), &encoderDesc);
    ASSERT_NE(node.nid, ZL_NODE_ILLEGAL.nid);
    const ZL_GraphID graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor_.get(), node, ZL_GRAPH_STORE);
    ASSERT_NE(graph.gid, ZL_GRAPH_ILLEGAL.gid);

    CCtx cctx;
    cctx.selectStartingGraph(compressor_, graph);
    const size_t before = ZL_CCtx_sizeof(cctx.get());
    const std::string compressed =
            cctx.compressOne(Input::refSerial("stateful copy input"));
    EXPECT_FALSE(compressed.empty());
    EXPECT_GE(ZL_CCtx_sizeof(cctx.get()), before + sizeof(SizedCodecState));
}

TEST_F(CCtxTest, dctxSizeofIncludesTransformManagerStorage)
{
    DCtx dctx;
    const size_t before = ZL_DCtx_sizeof(dctx.get());

    void* const opaque = std::malloc(1);
    ASSERT_NE(opaque, nullptr);
    const ZL_Type outputTypes[]           = { ZL_Type_serial };
    const ZL_TypedDecoderDesc decoderDesc = {
        .gd          = { .CTid           = 0x515546,
                         .inStreamType   = ZL_Type_serial,
                         .outStreamTypes = outputTypes,
                         .nbOutStreams   = 1 },
        .transform_f = unusedCopyDecoder,
        .name        = "sizeof_decoder",
        .opaque      = { opaque, nullptr, freeOpaque },
    };
    ASSERT_FALSE(
            ZL_isError(ZL_DCtx_registerTypedDecoder(dctx.get(), &decoderDesc)));

    EXPECT_GT(ZL_DCtx_sizeof(dctx.get()), before);
}

} // namespace openzl::tests
