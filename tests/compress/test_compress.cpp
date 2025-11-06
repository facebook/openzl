// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string>

#include <gtest/gtest.h>

#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/limits.h"
#include "openzl/common/wire_format.h"
#include "openzl/compress/cctx.h"
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_public_nodes.h"

#include "tests/utils.h"

using namespace ::testing;
using namespace ::openzl;

namespace {

class CompressTest : public Test {
   protected:
    void SetUp() override
    {
        cctx_.setParameter(CParam::StickyParameters, 1);
        cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        compressor_.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);
        cctx_.refCompressor(compressor_);
    }

    size_t compress(
            void* dst,
            size_t dstCapacity,
            void const* src,
            size_t srcSize,
            ZL_GraphID graph)
    {
        auto cctx = ZL_CCtx_create();

        ZL_REQUIRE_SUCCESS(ZL_CCtx_setParameter(
                cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
        ZL_REQUIRE_SUCCESS(
                ZL_CCtx_selectStartingGraphID(cctx, NULL, graph, NULL));

        auto const report =
                ZL_CCtx_compress(cctx, dst, dstCapacity, src, srcSize);
        ZL_REQUIRE_SUCCESS(report);

        ZL_CCtx_free(cctx);

        return ZL_validResult(report);
    }

    CCtx cctx_;
    Compressor compressor_;
};

TEST_F(CompressTest, CompressionSucceedsWithSmallDstBuffer)
{
    std::string data(1000, 'a');
    std::string dst(100, '\0');

    auto const cSize0 = compress(
            dst.data(),
            dst.size(),
            data.data(),
            data.size(),
            ZL_GRAPH_CONSTANT);
    auto const cSize1 = compress(
            dst.data(), cSize0, data.data(), data.size(), ZL_GRAPH_CONSTANT);
    ASSERT_EQ(cSize0, cSize1);
}

TEST_F(CompressTest, CompressionZeroLengthCommentIsNotSent)
{
    std::string data(1000, 'a');
    std::string comment = "";
    auto d1             = cctx_.compressSerial(data);
    EXPECT_ZS_VALID(ZL_CCtx_addHeaderComment(
            cctx_.get(), (const uint8_t*)comment.data(), comment.size()));
    auto d2 = cctx_.compressSerial(data);
    EXPECT_EQ(d1, d2);
}
TEST_F(CompressTest, CompressionCommentReadCorrectly)
{
    std::string data(1000, 'a');
    std::string comment = "comment";
    EXPECT_ZS_VALID(ZL_CCtx_addHeaderComment(
            cctx_.get(), (const uint8_t*)comment.data(), comment.size()));
    EXPECT_ZS_VALID(ZL_CCtx_addHeaderComment(cctx_.get(), NULL, 0));
    auto dst = cctx_.compressSerial(data);
    auto zfi = ZL_FrameInfo_create(dst.data(), dst.size());
    auto r   = ZL_RES_value(ZL_FrameInfo_getComment(zfi));
    std::string reconstructed((const char*)r.data, r.size);
    EXPECT_EQ(r.size, 0);
    ZL_FrameInfo_free(zfi);
}

TEST_F(CompressTest, CompressionZeroLengthCommentClearsField)
{
    std::string data(1000, 'a');
    std::string comment = "comment";
    EXPECT_ZS_VALID(ZL_CCtx_addHeaderComment(
            cctx_.get(), (const uint8_t*)comment.data(), comment.size()));
    auto dst = cctx_.compressSerial(data);
    auto zfi = ZL_FrameInfo_create(dst.data(), dst.size());
    auto r   = ZL_RES_value(ZL_FrameInfo_getComment(zfi));
    std::string reconstructed((const char*)r.data, r.size);
    EXPECT_EQ(comment, reconstructed);
    ZL_FrameInfo_free(zfi);
}

TEST_F(CompressTest, CompressionCommentSentIsOverriden)
{
    std::string data(1000, 'a');
    std::string comment1 = "comment1";
    std::string comment2 = "comment2";

    EXPECT_ZS_VALID(ZL_CCtx_addHeaderComment(
            cctx_.get(), comment1.data(), comment1.size()));
    EXPECT_ZS_VALID(ZL_CCtx_addHeaderComment(
            cctx_.get(), comment2.data(), comment2.size()));
    auto dst = cctx_.compressSerial(data);
    auto zfi = ZL_FrameInfo_create(dst.data(), dst.size());
    auto r   = ZL_RES_value(ZL_FrameInfo_getComment(zfi));
    std::string reconstructed((const char*)r.data, r.size);
    EXPECT_EQ(comment2, reconstructed);
    ZL_FrameInfo_free(zfi);
}

TEST_F(CompressTest, CompressMultipleTimesCommentRead)
{
    std::string data(1000, 'a');
    std::string comment = "comment";

    EXPECT_ZS_VALID(ZL_CCtx_addHeaderComment(
            cctx_.get(), comment.data(), comment.size()));
    // Compressing clears the comment in the cctx
    auto dst = cctx_.compressSerial(data);
    auto zfi = ZL_FrameInfo_create(dst.data(), dst.size());
    auto r   = ZL_RES_value(ZL_FrameInfo_getComment(zfi));
    std::string reconstructed((const char*)r.data, r.size);
    EXPECT_EQ(comment, reconstructed);
    ZL_FrameInfo_free(zfi);
    // Try again after message has been cleared
    dst = cctx_.compressSerial(data);
    zfi = ZL_FrameInfo_create(dst.data(), dst.size());
    r   = ZL_RES_value(ZL_FrameInfo_getComment(zfi));
    std::string emptyComment((const char*)r.data, r.size);
    // Expect no comment since sticky parameters are disabled
    EXPECT_EQ(emptyComment.size(), 0);
    ZL_FrameInfo_free(zfi);
}

TEST_F(CompressTest, CompressCommentIsZeroedInCctxAfterCompression)
{
    std::string data(1000, 'a');
    std::string comment = "comment";

    EXPECT_ZS_VALID(ZL_CCtx_addHeaderComment(
            cctx_.get(), comment.data(), comment.size()));
    // Compressing clears the comment in the cctx
    auto dst = cctx_.compressSerial(data);
    auto r   = CCTX_getHeaderComment(cctx_.get());
    EXPECT_EQ(r.size, 0);
}

TEST_F(CompressTest, CompressTooLongCommentFails)
{
    std::string comment(ZL_MAX_HEADER_COMMENT_SIZE_LIMIT + 1, 'a');
    EXPECT_ZS_ERROR(ZL_CCtx_addHeaderComment(
            cctx_.get(), comment.data(), comment.size()));
}

TEST_F(CompressTest, OlderFormatVersionCommentDoesNotChangeFrameHeader)
{
    std::string data(1000, 'a');
    std::string comment      = "comment";
    std::string emptyComment = "";
    cctx_.setParameter(CParam::FormatVersion, ZL_COMMENT_VERSION_MIN - 1);
    EXPECT_ZS_VALID(ZL_CCtx_addHeaderComment(
            cctx_.get(), comment.data(), comment.size()));
    auto dst = cctx_.compressSerial(data);
    // Clear comment
    EXPECT_ZS_VALID(ZL_CCtx_addHeaderComment(
            cctx_.get(), emptyComment.data(), emptyComment.size()));
    auto dst2 = cctx_.compressSerial(data);
    // Ensure that the frame is identical regardless of the comment
    EXPECT_EQ(dst, dst2);
}

} // namespace
