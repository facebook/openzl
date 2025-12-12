// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string_view>

#include <gtest/gtest.h>

#include "openzl/zl_public_nodes.h"
#include "tests/utils.h"
#include "tests/zstrong/test_zstrong_fixture.h"

using namespace ::testing;

namespace openzl::tests {
namespace {
/**
 * These test cases test common scenarios that the user may run into where we
 * want Zstrong to be able to provide helpful error messages. These are mostly
 * to document the common errors, but should also help make sure we don't
 * regress in our messages.
 */
class HelpfulErrorMessagesTest : public Test {
   public:
    ~HelpfulErrorMessagesTest() override
    {
        clear();
    }

    void clear()
    {
        if (cctx_ != nullptr) {
            ZL_CCtx_free(cctx_);
            cctx_ = nullptr;
        }
        if (cgraph_ != nullptr) {
            ZL_Compressor_free(cgraph_);
            cgraph_ = nullptr;
        }
        if (dctx_ != nullptr) {
            ZL_DCtx_free(dctx_);
            dctx_ = nullptr;
        }
    }

    void SetUp() override
    {
        clear();
        cctx_   = ZL_CCtx_create();
        dctx_   = ZL_DCtx_create();
        cgraph_ = ZL_Compressor_create();
    }

    std::string getCCtxErrorMessage(ZL_Report report)
    {
        if (!ZL_isError(report)) {
            throw std::runtime_error("Should be failure");
        }
        return ZL_CCtx_getErrorContextString(cctx_, report);
    }

    std::string getCGraphErrorMessage(ZL_Report report)
    {
        if (!ZL_isError(report)) {
            throw std::runtime_error("Should be failure");
        }
        return ZL_Compressor_getErrorContextString(cgraph_, report);
    }

    std::string getDCtxErrorMessage(ZL_Report report)
    {
        if (!ZL_isError(report)) {
            throw std::runtime_error("Should be failure");
        }
        return ZL_DCtx_getErrorContextString(dctx_, report);
    }

    bool errorMessageHasSubstr(
            std::string_view errorMessage,
            std::string_view substr)
    {
        return errorMessage.find(substr) != std::string::npos;
    }

    std::string compressAndReturnErrorMessage(std::string_view data)
    {
        std::string compressed(ZL_compressBound(data.size()), '\0');
        const auto report = ZL_CCtx_compress(
                cctx_,
                compressed.data(),
                compressed.size(),
                data.data(),
                data.size());
        return getCCtxErrorMessage(report);
    }

    ZL_Compressor* cgraph_{ nullptr };
    ZL_CCtx* cctx_{ nullptr };
    ZL_DCtx* dctx_{ nullptr };
};

TEST_F(HelpfulErrorMessagesTest, TestFormatVersionNotSet)
{
    ASSERT_ZS_VALID(
            ZL_Compressor_selectStartingGraphID(cgraph_, ZL_GRAPH_STORE));
    ASSERT_ZS_VALID(ZL_CCtx_refCompressor(cctx_, cgraph_));
    auto message = compressAndReturnErrorMessage("hello world");
    EXPECT_TRUE(errorMessageHasSubstr(message, "Format version is not set"));
    EXPECT_TRUE(errorMessageHasSubstr(message, "_formatVersion"));
}

TEST_F(HelpfulErrorMessagesTest, TestStartingGraphIDNotSet)
{
    auto message = getCCtxErrorMessage(ZL_CCtx_refCompressor(cctx_, cgraph_));
    EXPECT_TRUE(errorMessageHasSubstr(message, "starting graph ID is not set"));
    EXPECT_TRUE(
            errorMessageHasSubstr(message, "Compressor_selectStartingGraphID"));
}

TEST_F(HelpfulErrorMessagesTest, TestGetErrorContextOnWrongObject)
{
    constexpr auto kExpectedError =
            "Error does not belong to this context object";
    {
        auto report        = ZL_CCtx_compress(cctx_, nullptr, 0, nullptr, 0);
        auto cctxMessage   = getCCtxErrorMessage(report);
        auto cgraphMessage = getCGraphErrorMessage(report);
        auto dctxMessage   = getDCtxErrorMessage(report);
        EXPECT_TRUE(errorMessageHasSubstr(cgraphMessage, kExpectedError));
        EXPECT_FALSE(errorMessageHasSubstr(cctxMessage, kExpectedError));
        EXPECT_TRUE(errorMessageHasSubstr(dctxMessage, kExpectedError));
    }
    {
        auto report        = ZL_DCtx_decompress(dctx_, nullptr, 0, nullptr, 0);
        auto cctxMessage   = getCCtxErrorMessage(report);
        auto cgraphMessage = getCGraphErrorMessage(report);
        auto dctxMessage   = getDCtxErrorMessage(report);
        EXPECT_TRUE(errorMessageHasSubstr(cgraphMessage, kExpectedError));
        EXPECT_TRUE(errorMessageHasSubstr(cctxMessage, kExpectedError));
        EXPECT_FALSE(errorMessageHasSubstr(dctxMessage, kExpectedError));
    }
    {
        auto report =
                ZL_Compressor_selectStartingGraphID(cgraph_, ZL_GRAPH_ILLEGAL);
        auto cctxMessage   = getCCtxErrorMessage(report);
        auto cgraphMessage = getCGraphErrorMessage(report);
        auto dctxMessage   = getDCtxErrorMessage(report);
        EXPECT_FALSE(errorMessageHasSubstr(cgraphMessage, kExpectedError));
        EXPECT_TRUE(errorMessageHasSubstr(cctxMessage, kExpectedError));
        EXPECT_TRUE(errorMessageHasSubstr(dctxMessage, kExpectedError));
    }
}

} // namespace
} // namespace openzl::tests
