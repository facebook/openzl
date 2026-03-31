// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/zl_errors.h"

#ifdef ZSTRONG_COMMON_ERRORS_INTERNAL_H
#    error "Must not include errors_internal.h"
#endif

namespace {

struct Foo {};

ZL_RESULT_DECLARE_TYPE(Foo);

constexpr Foo kFoo;

TEST(PublicErrorsTest, errorCodeToString)
{
    EXPECT_NE(ZL_ErrorCode_toString(ZL_ErrorCode_GENERIC), nullptr);
}

TEST(PublicErrorsTest, errorCreation)
{
    auto report = ZL_REPORT_ERROR(allocation, "fail! %d", 12345);
    EXPECT_TRUE(ZL_isError(report));
    report = ZL_REPORT_ERROR(allocation, "fail!");
    EXPECT_TRUE(ZL_isError(report));
    report = ZL_REPORT_ERROR(allocation);
    EXPECT_TRUE(ZL_isError(report));
}

TEST(PublicErrorsTest, requireChokeOnError)
{
    auto report = ZL_REPORT_ERROR(allocation, "fail! %d", 12345);
    EXPECT_TRUE(ZL_isError(report));
}

TEST(PublicErrorsTest, retIfs)
{
    {
        auto f = [](int path) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            switch (path) {
                case 0:
                    return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
                    break;
                case 1:
                    ZL_ERR(GENERIC, "fail! %d", 1234);
                    break;
                case 2:
                    ZL_ERR(GENERIC, "fail!");
                    break;
                case 3:
                    ZL_ERR(GENERIC);
                    break;
                default:
                    throw std::runtime_error("!");
            }
        };
        EXPECT_FALSE(ZL_RES_isError(f(0)));
        EXPECT_TRUE(ZL_RES_isError(f(1)));
        EXPECT_TRUE(ZL_RES_isError(f(2)));
        EXPECT_TRUE(ZL_RES_isError(f(3)));
    }
    {
        auto f = [](bool succeed) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            ZL_ERR_IF(!succeed, GENERIC, "foo %d", 1234);
            ZL_ERR_IF(!succeed, GENERIC, "foo");
            ZL_ERR_IF(!succeed, GENERIC);
            return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
        };
        EXPECT_FALSE(ZL_RES_isError(f(true)));
        EXPECT_TRUE(ZL_RES_isError(f(false)));
    }
    {
        auto f = [](bool succeed) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            ZL_ERR_IF_NE(1, 2 - (int)succeed, GENERIC, "foo %d", 1234);
            ZL_ERR_IF_NE(1, 2 - (int)succeed, GENERIC, "foo");
            ZL_ERR_IF_NE(1, 2 - (int)succeed, GENERIC);
            return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
        };
        EXPECT_FALSE(ZL_RES_isError(f(true)));
        EXPECT_TRUE(ZL_RES_isError(f(false)));
    }
    {
        auto f = [](bool succeed) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            ZL_ERR_IF_EQ(1, 1 + (int)succeed, GENERIC, "foo %d", 1234);
            ZL_ERR_IF_EQ(1, 1 + (int)succeed, GENERIC, "foo");
            ZL_ERR_IF_EQ(1, 1 + (int)succeed, GENERIC);
            return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
        };
        EXPECT_FALSE(ZL_RES_isError(f(true)));
        EXPECT_TRUE(ZL_RES_isError(f(false)));
    }
    {
        auto f = [](bool succeed) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            ZL_ERR_IF_GE(2, 1 + (2 * (int)succeed), GENERIC, "foo %d", 1234);
            ZL_ERR_IF_GE(2, 1 + (2 * (int)succeed), GENERIC, "foo");
            ZL_ERR_IF_GE(2, 1 + (2 * (int)succeed), GENERIC);
            return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
        };
        EXPECT_FALSE(ZL_RES_isError(f(true)));
        EXPECT_TRUE(ZL_RES_isError(f(false)));
    }
    {
        auto f = [](bool succeed) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            ZL_ERR_IF_LE(1 + (2 * (int)succeed), 2, GENERIC, "foo %d", 1234);
            ZL_ERR_IF_LE(1 + (2 * (int)succeed), 2, GENERIC, "foo");
            ZL_ERR_IF_LE(1 + (2 * (int)succeed), 2, GENERIC);
            return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
        };
        EXPECT_FALSE(ZL_RES_isError(f(true)));
        EXPECT_TRUE(ZL_RES_isError(f(false)));
    }
    {
        auto f = [](bool succeed) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            ZL_ERR_IF_GT(2, 1 + (2 * (int)succeed), GENERIC, "foo %d", 1234);
            ZL_ERR_IF_GT(2, 1 + (2 * (int)succeed), GENERIC, "foo");
            ZL_ERR_IF_GT(2, 1 + (2 * (int)succeed), GENERIC);
            return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
        };
        EXPECT_FALSE(ZL_RES_isError(f(true)));
        EXPECT_TRUE(ZL_RES_isError(f(false)));
    }
    {
        auto f = [](bool succeed) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            ZL_ERR_IF_AND(true, !succeed, GENERIC, "foo %d", 1234);
            ZL_ERR_IF_AND(true, !succeed, GENERIC, "foo");
            ZL_ERR_IF_AND(true, !succeed, GENERIC);
            return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
        };
        EXPECT_FALSE(ZL_RES_isError(f(true)));
        EXPECT_TRUE(ZL_RES_isError(f(false)));
    }
    {
        auto f = [](bool succeed) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            ZL_ERR_IF_OR(false, !succeed, GENERIC, "foo %d", 1234);
            ZL_ERR_IF_OR(false, !succeed, GENERIC, "foo");
            ZL_ERR_IF_OR(false, !succeed, GENERIC);
            return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
        };
        EXPECT_FALSE(ZL_RES_isError(f(true)));
        EXPECT_TRUE(ZL_RES_isError(f(false)));
    }
    {
        auto f = [](bool succeed) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            ZL_Report report;
            if (succeed) {
                report = ZL_returnValue(1234);
            } else {
                report = ZL_REPORT_ERROR(corruption, "foo %d", 1234);
            }
            ZL_ERR_IF_ERR(report, "foo %d", 1234);
            ZL_ERR_IF_ERR(report, "foo");
            ZL_ERR_IF_ERR(report);
            return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
        };
        EXPECT_FALSE(ZL_RES_isError(f(true)));
        EXPECT_TRUE(ZL_RES_isError(f(false)));
    }
    {
        auto f = [](bool succeed) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            ZL_ERR_IF_NULL(succeed ? "foo" : nullptr, GENERIC, "foo %d", 1234);
            ZL_ERR_IF_NULL(succeed ? "foo" : nullptr, GENERIC, "foo");
            ZL_ERR_IF_NULL(succeed ? "foo" : nullptr, GENERIC);
            return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
        };
        EXPECT_FALSE(ZL_RES_isError(f(true)));
        EXPECT_TRUE(ZL_RES_isError(f(false)));
    }
    {
        auto f = [](bool succeed) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            ZL_ERR_IF_NN(!succeed ? "foo" : nullptr, GENERIC, "foo %d", 1234);
            ZL_ERR_IF_NN(!succeed ? "foo" : nullptr, GENERIC, "foo");
            ZL_ERR_IF_NN(!succeed ? "foo" : nullptr, GENERIC);
            return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
        };
        EXPECT_FALSE(ZL_RES_isError(f(true)));
        EXPECT_TRUE(ZL_RES_isError(f(false)));
    }
}

} // namespace
