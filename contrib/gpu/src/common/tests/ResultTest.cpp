// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <vector>

#include "contrib/gpu/src/common/result.hpp"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_errors.h"

namespace openzl::gpu {
namespace {

Result<std::vector<int>> makeElements(size_t count)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    ZL_ERR_IF_GT(count, 3, srcSize_tooLarge);
    return std::vector<int>(count, 7);
}

ZL_Report countElements(size_t count)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    const Result<std::vector<int>> elements = makeElements(count);
    ZL_ERR_IF_ERR(elements.report());
    return ZL_returnValue(elements.value().size());
}

Result<std::vector<int>> makeElementsFromReport(ZL_Report count)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    ZL_TRY_LET_CONST(size_t, validCount, count);
    return std::vector<int>(validCount, 7);
}

TEST(ResultTest, HoldsValue)
{
    const Result<std::vector<int>> result = makeElements(2);

    ASSERT_FALSE(result.isError());
    EXPECT_FALSE(ZL_isError(result.report()));
    EXPECT_EQ(result.value(), (std::vector<int>{ 7, 7 }));
}

Result<std::unique_ptr<int>> makeOwnedInt(int value)
{
    std::unique_ptr<int> owned = std::make_unique<int>(value);
    return owned;
}

TEST(ResultTest, MovesOutMoveOnlyValue)
{
    // This fails to compile if `return owned;` or `value() &&` copies the
    // value.
    Result<std::unique_ptr<int>> result = makeOwnedInt(5);

    const std::unique_ptr<int> value = std::move(result).value();

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 5);
}

TEST(ResultTest, KeepsErrorCodeOfFailedCheck)
{
    const Result<std::vector<int>> result = makeElements(4);

    ASSERT_TRUE(result.isError());
    EXPECT_EQ(ZL_errorCode(result.report()), ZL_ErrorCode_srcSize_tooLarge);
}

TEST(ResultTest, ValueOfAnErrorAborts)
{
    // This fails if value() reads the storage of an error instead of stopping,
    // including in builds that compile out ZL_ASSERT.
    const Result<std::vector<int>> result = makeElements(4);

    ASSERT_TRUE(result.isError());
    EXPECT_DEATH((void)result.value(), "needs a value");
}

TEST(ResultTest, ForwardsErrorThroughErrIfErr)
{
    EXPECT_EQ(ZL_errorCode(countElements(4)), ZL_ErrorCode_srcSize_tooLarge);
    EXPECT_EQ(ZL_validResult(countElements(3)), 3);
}

TEST(ResultTest, ForwardsErrorThroughTryLetConst)
{
    const Result<std::vector<int>> result =
            makeElementsFromReport(ZL_returnError(ZL_ErrorCode_corruption));

    ASSERT_TRUE(result.isError());
    EXPECT_EQ(ZL_errorCode(result.report()), ZL_ErrorCode_corruption);
}

TEST(ResultTest, SuccessReportIsLogicError)
{
    // A success ZL_Report holds no error, so the result cannot carry one.
#if ZL_ENABLE_ASSERT
    EXPECT_DEATH(
            { Result<std::vector<int>> result{ ZL_returnSuccess() }; },
            "Logic error");
#else
    const Result<std::vector<int>> result{ ZL_returnSuccess() };
    ASSERT_TRUE(result.isError());
    EXPECT_EQ(ZL_errorCode(result.report()), ZL_ErrorCode_logicError);
#endif
}

// Frees the DCTX that holds its error's dynamic info before it returns.
Result<std::vector<int>> failInFreedContext()
{
    const std::unique_ptr<ZL_DCtx, decltype(&ZL_DCtx_free)> dctx{
        ZL_DCtx_create(), ZL_DCtx_free
    };
    ZL_RESULT_DECLARE_SCOPE_REPORT(dctx.get());
    ZL_ERR(corruption, "reported from a DCTX");
}

TEST(ResultTest, DropsInfoOfFreedContext)
{
    // This fails if the result keeps a pointer into the freed DCTX. ASan
    // reports the read in ZL_E_str.
    const Result<std::vector<int>> result = failInFreedContext();

    ASSERT_TRUE(result.isError());
    EXPECT_STREQ(
            ZL_E_str(ZL_RES_error(result.report())),
            ZL_ErrorCode_toString(ZL_ErrorCode_corruption));
}

TEST(ResultTest, KeepsStaticInfo)
{
    // An error created without a context points to static info, which never
    // dangles. This fails if the result drops it.
    const Result<std::vector<int>> result = makeElements(4);

    ASSERT_TRUE(result.isError());
    EXPECT_STRNE(
            ZL_E_str(ZL_RES_error(result.report())),
            ZL_ErrorCode_toString(ZL_ErrorCode_srcSize_tooLarge));
}

} // namespace
} // namespace openzl::gpu
