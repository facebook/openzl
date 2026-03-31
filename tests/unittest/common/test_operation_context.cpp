// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/common/errors_internal.h"
#include "openzl/common/operation_context.h"
#include "openzl/common/scope_context.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_compressor_serialization.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_errors.h"

using namespace ::testing;

TEST(OperationContextTest, GetOperationContextCCtx)
{
    ZL_CCtx* cctx = ZL_CCtx_create();
    EXPECT_NE(ZL_GET_OPERATION_CONTEXT(cctx), nullptr);
    EXPECT_EQ(
            ZL_GET_OPERATION_CONTEXT(cctx), ZL_CCtx_getOperationContext(cctx));
    ZL_CCtx_free(cctx);
}

TEST(OperationContextTest, GetOperationContextCGraph)
{
    ZL_Compressor* cgraph = ZL_Compressor_create();
    EXPECT_NE(ZL_GET_OPERATION_CONTEXT(cgraph), nullptr);
    EXPECT_EQ(
            ZL_GET_OPERATION_CONTEXT(cgraph),
            ZL_Compressor_getOperationContext(cgraph));
    ZL_Compressor_free(cgraph);
}

TEST(OperationContextTest, GetOperationContextDCtx)
{
    ZL_DCtx* dctx = ZL_DCtx_create();
    EXPECT_NE(ZL_GET_OPERATION_CONTEXT(dctx), nullptr);
    EXPECT_EQ(
            ZL_GET_OPERATION_CONTEXT(dctx), ZL_DCtx_getOperationContext(dctx));
    ZL_DCtx_free(dctx);
}

TEST(OperationContextTest, GetOperationContextCompressorSerializer)
{
    ZL_CompressorSerializer* cser = ZL_CompressorSerializer_create();
    EXPECT_NE(ZL_GET_OPERATION_CONTEXT(cser), nullptr);
    EXPECT_EQ(
            ZL_GET_OPERATION_CONTEXT(cser),
            ZL_CompressorSerializer_getOperationContext(cser));
    ZL_CompressorSerializer_free(cser);
}

TEST(OperationContextTest, GetOperationContextCompressorDeserializer)
{
    ZL_CompressorDeserializer* cdeser = ZL_CompressorDeserializer_create();
    EXPECT_NE(ZL_GET_OPERATION_CONTEXT(cdeser), nullptr);
    EXPECT_EQ(
            ZL_GET_OPERATION_CONTEXT(cdeser),
            ZL_CompressorDeserializer_getOperationContext(cdeser));
    ZL_CompressorDeserializer_free(cdeser);
}

// Testing EICtx & DICtx is harder, omit it...

TEST(OperationContextTest, BasicUsage)
{
    ZL_OperationContext opCtx{};
    ZL_OC_init(&opCtx);

    ZL_ScopeContext scopeCtx = { &opCtx };

    EXPECT_EQ(ZL_OC_numErrors(&opCtx), 0u);
    EXPECT_EQ(ZL_OC_getError(&opCtx, ZL_ErrorCode_no_error), nullptr);
    EXPECT_EQ(ZL_OC_getError(&opCtx, ZL_ErrorCode_corruption), nullptr);

    ZL_OC_startOperation(&opCtx, ZL_Operation_compress);

    EXPECT_EQ(ZL_OC_numErrors(&opCtx), 0u);
    EXPECT_EQ(ZL_OC_getError(&opCtx, ZL_ErrorCode_no_error), nullptr);
    EXPECT_EQ(ZL_OC_getError(&opCtx, ZL_ErrorCode_corruption), nullptr);

    ZL_E_create(nullptr, &scopeCtx, "", "", 0, ZL_ErrorCode_corruption, "");

    EXPECT_EQ(ZL_OC_numErrors(&opCtx), 1u);
    EXPECT_EQ(ZL_OC_getError(&opCtx, ZL_ErrorCode_no_error), nullptr);
    EXPECT_NE(ZL_OC_getError(&opCtx, ZL_ErrorCode_corruption), nullptr);
    EXPECT_NE(ZL_OC_getError(&opCtx, ZL_ErrorCode_GENERIC), nullptr);
    EXPECT_EQ(
            ZL_OC_getError(&opCtx, ZL_ErrorCode_corruption),
            ZL_OC_getError(&opCtx, ZL_ErrorCode_corruption));

    ZL_OC_clearErrors(&opCtx);

    EXPECT_EQ(ZL_OC_numErrors(&opCtx), 0u);
    EXPECT_EQ(ZL_OC_getError(&opCtx, ZL_ErrorCode_no_error), nullptr);
    EXPECT_EQ(ZL_OC_getError(&opCtx, ZL_ErrorCode_corruption), nullptr);

    ZL_E_create(nullptr, &scopeCtx, "", "", 0, ZL_ErrorCode_corruption, "");

    EXPECT_EQ(ZL_OC_numErrors(&opCtx), 1u);
    EXPECT_NE(ZL_OC_getError(&opCtx, ZL_ErrorCode_corruption), nullptr);

    ZL_E_create(nullptr, &scopeCtx, "", "", 0, ZL_ErrorCode_allocation, "");

    EXPECT_EQ(ZL_OC_numErrors(&opCtx), 2u);
    EXPECT_NE(ZL_OC_getError(&opCtx, ZL_ErrorCode_allocation), nullptr);

    ZL_OC_startOperation(&opCtx, ZL_Operation_compress);

    EXPECT_EQ(ZL_OC_numErrors(&opCtx), 0u);
    EXPECT_EQ(ZL_OC_getError(&opCtx, ZL_ErrorCode_corruption), nullptr);

    ZL_OC_destroy(&opCtx);
}

TEST(OperationContextTest, Warnings)
{
    ZL_OperationContext opCtx{};
    ZL_OC_init(&opCtx);

    ZL_ScopeContext scopeCtx = { &opCtx };

    EXPECT_EQ(ZL_OC_numErrors(&opCtx), 0u);
    EXPECT_EQ(ZL_OC_numWarnings(&opCtx), 0u);

    ZL_OC_startOperation(&opCtx, ZL_Operation_compress);

    {
        auto e1 = ZL_E_create(
                nullptr,
                &scopeCtx,
                "file.c",
                "func",
                123,
                ZL_ErrorCode_corruption,
                "foo %d",
                1234);

        EXPECT_EQ(ZL_OC_numErrors(&opCtx), 1u);
        EXPECT_EQ(ZL_OC_numWarnings(&opCtx), 0u);

        auto dy1 = ZL_E_dy(e1);
        EXPECT_NE(dy1, nullptr);
        EXPECT_EQ(dy1, ZL_OC_getError(&opCtx, ZL_ErrorCode_corruption));

        ZL_OC_markAsWarning(&opCtx, e1);

        EXPECT_EQ(ZL_OC_numErrors(&opCtx), 1u);
        EXPECT_EQ(ZL_OC_numWarnings(&opCtx), 1u);

        EXPECT_EQ(ZL_E_dy(ZL_OC_getWarning(&opCtx, 0)), dy1);
    }

    {
        auto e2 = ZL_E_create(
                nullptr,
                &scopeCtx,
                "file.c",
                "func",
                123,
                ZL_ErrorCode_corruption,
                "foo %d",
                1234);

        EXPECT_EQ(ZL_OC_numErrors(&opCtx), 2u);
        EXPECT_EQ(ZL_OC_numWarnings(&opCtx), 1u);

        auto dy2 = ZL_E_dy(e2);
        EXPECT_NE(dy2, nullptr);
        EXPECT_EQ(dy2, ZL_OC_getError(&opCtx, ZL_ErrorCode_corruption));

        ZL_OC_markAsWarning(&opCtx, e2);

        EXPECT_EQ(ZL_OC_numErrors(&opCtx), 2u);
        EXPECT_EQ(ZL_OC_numWarnings(&opCtx), 2u);

        EXPECT_EQ(ZL_E_dy(ZL_OC_getWarning(&opCtx, 1)), dy2);
        EXPECT_NE(
                ZL_E_dy(ZL_OC_getWarning(&opCtx, 0)),
                ZL_E_dy(ZL_OC_getWarning(&opCtx, 1)));
    }

    {
        // Coerce dynamic info
        auto e3 = ZL_E_create(
                nullptr,
                &scopeCtx,
                "file.c",
                "func",
                123,
                ZL_ErrorCode_corruption,
                "foo %d",
                1234);

        EXPECT_EQ(ZL_OC_numErrors(&opCtx), 3u);
        EXPECT_EQ(ZL_OC_numWarnings(&opCtx), 2u);

        ZL_E_convertToWarning(&opCtx, e3);

        EXPECT_EQ(ZL_OC_numErrors(&opCtx), 3u);
        EXPECT_EQ(ZL_OC_numWarnings(&opCtx), 3u);

        EXPECT_EQ(ZL_E_dy(ZL_OC_getWarning(&opCtx, 2)), ZL_E_dy(e3));
    }

    {
        // Coerce static info and convert into dynamic
        auto e4 = ZL_RES_error([]() {
            ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
            ZL_ERR(corruption, "qwerty %d", 1234);
        }());

        EXPECT_EQ(ZL_E_dy(e4), nullptr);

        EXPECT_EQ(ZL_OC_numErrors(&opCtx), 3u);
        EXPECT_EQ(ZL_OC_numWarnings(&opCtx), 3u);

        ZL_E_convertToWarning(&opCtx, e4);

        EXPECT_EQ(ZL_OC_numErrors(&opCtx), 4u);
        EXPECT_EQ(ZL_OC_numWarnings(&opCtx), 4u);

        EXPECT_NE(ZL_E_dy(ZL_OC_getWarning(&opCtx, 3)), nullptr);

        auto new_e = ZL_OC_getWarning(&opCtx, 3);
        EXPECT_NE(
                std::string(ZL_E_str(new_e)).find("qwerty"), std::string::npos);
    }

    ZL_OC_destroy(&opCtx);
}
