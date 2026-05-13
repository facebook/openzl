// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <algorithm>

#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/operation_context.h"
#include "openzl/zl_errors.h"

#ifndef ZSTRONG_COMMON_ERRORS_INTERNAL_H
#    error "Must include errors_internal.h"
#endif

#include "tests/unittest/common/test_errors_in_c.h"

namespace {

struct Foo {
    int val;
};

ZL_RESULT_DECLARE_TYPE(Foo);

constexpr Foo kFoo{};

struct Bar {
    int val;
};

ZL_RESULT_DECLARE_TYPE(Bar);

ZL_ErrorInfo opCtxEI(const ZL_OperationContext& opCtx)
{
    ZL_DynamicErrorInfo* dy =
            VECTOR_AT(opCtx.errorInfos, VECTOR_SIZE(opCtx.errorInfos) - 1);
    return ZL_EI_fromDy(dy);
}

TEST(ErrorsTest, errorCodeToString)
{
    EXPECT_NE(ZL_ErrorCode_toString(ZL_ErrorCode_GENERIC), nullptr);
}

TEST(ErrorsTest, errorCreation)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    auto report = ZL_REPORT_ERROR(allocation, "fail! %d", 12345);
    ZL_E_print(ZL_RES_error(report));
    EXPECT_TRUE(ZL_isError(report));
    report = ZL_REPORT_ERROR(allocation, "fail!");
    EXPECT_TRUE(ZL_isError(report));
    ZL_E_print(ZL_RES_error(report));
    report = ZL_REPORT_ERROR(allocation);
    EXPECT_TRUE(ZL_isError(report));
    ZL_E_print(ZL_RES_error(report));
}

TEST(ErrorsTest, requireChokeOnError)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
    auto report = ZL_REPORT_ERROR(allocation, "fail! %d", 12345);
    EXPECT_TRUE(ZL_isError(report));
    EXPECT_DEATH(ZL_REQUIRE_SUCCESS(report, "oops!"), "");
}

TEST(ErrorsTest, retIfs)
{
    {
        auto f = [](int path) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            switch (path) {
                case 0:
                    return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
                case 1:
                    ZL_ERR(GENERIC, "fail! %d", 1234);
                case 2:
                    ZL_ERR(GENERIC, "fail!");
                case 3:
                    ZL_ERR(GENERIC);
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
        ZL_OperationContext opCtx{};
        ZL_OC_init(&opCtx);
        auto f = [&](bool succeed) mutable {
            ZL_RESULT_DECLARE_SCOPE(Foo, &opCtx);
            ZL_Report report;
            if (succeed) {
                report = ZL_returnValue(1234);
            } else {
                report = ZL_REPORT_ERROR(corruption, "foo %d", 1234);
            }
            ZL_ERR_IF_ERR(report, "bar %d", 5678);
            ZL_ERR_IF_ERR(report, "bar");
            ZL_ERR_IF_ERR(report);
            return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
        };
        EXPECT_FALSE(ZL_RES_isError(f(true)));
        EXPECT_TRUE(ZL_RES_isError(f(false)));

        auto res = f(false);
        std::string err_str{ ZL_E_str(ZL_RES_error(res)) };
        EXPECT_NE(err_str.find("foo 1234"), std::string::npos) << err_str;
        EXPECT_NE(err_str.find("bar 5678"), std::string::npos) << err_str;

        ZL_OC_destroy(&opCtx);
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

TEST(ErrorsTest, errorForwardingTransportsSourceErrorInfo)
{
    {
        ZL_OperationContext opCtx{};
        ZL_OC_init(&opCtx);

        auto makeReportWithoutContext = []() {
            ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
            return ZL_REPORT_ERROR(corruption, "foo %d", 1234);
        };
        auto makeReportWithStContext = []() mutable {
            ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
            ZL_ERR(corruption, "foo %d", 1234);
        };
        auto makeReportWithDyContext = [&opCtx]() mutable {
            ZL_RESULT_DECLARE_SCOPE_REPORT(&opCtx);
            return ZL_REPORT_ERROR(corruption, "foo %d", 1234);
        };

        auto addFrameWithoutContext =
                [](auto result, std::string fmt, std::string msg) -> auto {
            ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
            ZL_RES_error(result) = ZL_E_ADDFRAME(
                    ZL_RES_error(result),
                    ZL_EE_EMPTY,
                    fmt.c_str(),
                    msg.c_str());
            return result;
        };
        auto addFrameWithContext =
                [&](auto result, std::string fmt, std::string msg) -> auto {
            ZL_RESULT_DECLARE_SCOPE_REPORT(&opCtx);
            ZL_RES_error(result) = ZL_E_ADDFRAME(
                    ZL_RES_error(result),
                    ZL_EE_EMPTY,
                    fmt.c_str(),
                    msg.c_str());
            return result;
        };

        auto retIfErrWithoutContext = [](ZL_Report report) {
            ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
            ZL_ERR_IF_ERR(report, "bar %d", 5678);
            ZL_ERR_IF_ERR(report, "bar");
            ZL_ERR_IF_ERR(report);
            return ZL_returnValue(1234);
        };
        auto retIfErrWithContext = [&opCtx](ZL_Report report) mutable {
            ZL_RESULT_DECLARE_SCOPE_REPORT(&opCtx);
            ZL_ERR_IF_ERR(report, "bar %d", 5678);
            ZL_ERR_IF_ERR(report, "bar");
            ZL_ERR_IF_ERR(report);
            return ZL_returnValue(1234);
        };

        enum class Mode {
            EMPTY,
            STATIC,
            DYNAMIC,
        };

        const std::map<Mode, std::string> mode_names{ {
                { Mode::EMPTY, "EMPTY" },
                { Mode::STATIC, "STATIC" },
                { Mode::DYNAMIC, "DYNAMIC" },
        } };

        auto check_report = [&](ZL_Report rep, Mode mode) {
            EXPECT_TRUE(ZL_RES_isError(rep));
            const auto& err = ZL_RES_error(rep);
            const std::string err_str{ ZL_E_str(err) };
            const auto code = ZL_E_code(err);
            EXPECT_EQ(code, ZL_ErrorCode_corruption);

            EXPECT_NE(
                    err_str.find(ZL_ErrorCode_toString(code)),
                    std::string::npos)
                    << mode_names.at(mode) << "\n"
                    << err_str;

            switch (mode) {
                case Mode::EMPTY:
                    // None of the error message is available.
                    EXPECT_EQ(err_str.find("foo"), std::string::npos)
                            << mode_names.at(mode) << "\n"
                            << err_str;
                    break;
                case Mode::STATIC:
                    // The unformatted error message is available.
                    EXPECT_NE(err_str.find("foo %d"), std::string::npos)
                            << mode_names.at(mode) << "\n"
                            << err_str;
                    break;
                case Mode::DYNAMIC:
                    // The formatted error message is available.
                    EXPECT_NE(err_str.find("foo 1234"), std::string::npos)
                            << mode_names.at(mode) << "\n"
                            << err_str;
                    break;
            }
        };

        auto check_result = [&](const ZL_Report result,
                                const Mode foo_mode,
                                const Mode fwd_mode1,
                                const Mode bar_mode,
                                const Mode fwd_mode2) {
            const auto effective_fwd_mode = std::max({ foo_mode, fwd_mode1 });
            const auto effective_bar_mode = bar_mode == Mode::EMPTY
                    ? Mode::EMPTY
                    : std::max({ foo_mode, fwd_mode1, bar_mode });
            const auto effective_end_mode =
                    std::max({ foo_mode, fwd_mode1, bar_mode, fwd_mode2 });

            EXPECT_TRUE(ZL_RES_isError(result));
            const auto& err = ZL_RES_error(result);
            const std::string err_str{ ZL_E_str(err) };
            const auto code = ZL_E_code(err);
            EXPECT_EQ(code, ZL_ErrorCode_corruption)
                    << ZL_ErrorCode_toString(code);

            const auto expect_err_desc = effective_end_mode == Mode::DYNAMIC
                    || !(foo_mode == Mode::EMPTY && bar_mode == Mode::STATIC);
            EXPECT_EQ(
                    err_str.find(ZL_ErrorCode_toString(code))
                            != std::string::npos,
                    expect_err_desc)
                    << mode_names.at(foo_mode) << " "
                    << mode_names.at(fwd_mode1) << " "
                    << mode_names.at(bar_mode) << " "
                    << mode_names.at(fwd_mode2) << "\n"
                    << err_str;

            switch (foo_mode) {
                case Mode::EMPTY:
                    // None of the error message is available.
                    EXPECT_EQ(err_str.find("foo"), std::string::npos)
                            << mode_names.at(foo_mode) << " "
                            << mode_names.at(fwd_mode1) << " "
                            << mode_names.at(bar_mode) << " "
                            << mode_names.at(fwd_mode2) << "\n"
                            << err_str;
                    break;
                case Mode::STATIC:
                    // The unformatted error message is available.
                    EXPECT_NE(err_str.find("foo %d"), std::string::npos)
                            << mode_names.at(foo_mode) << " "
                            << mode_names.at(fwd_mode1) << " "
                            << mode_names.at(bar_mode) << " "
                            << mode_names.at(fwd_mode2) << "\n"
                            << err_str;
                    break;
                case Mode::DYNAMIC:
                    // The formatted error message is available.
                    EXPECT_NE(err_str.find("foo 1234"), std::string::npos)
                            << mode_names.at(foo_mode) << " "
                            << mode_names.at(fwd_mode1) << " "
                            << mode_names.at(bar_mode) << " "
                            << mode_names.at(fwd_mode2) << "\n"
                            << err_str;
                    break;
            }

            if (fwd_mode1 != Mode::EMPTY) {
                switch (effective_fwd_mode) {
                    case Mode::EMPTY:
                    case Mode::STATIC:
                        // The frame isn't added.
                        EXPECT_EQ(
                                err_str.find("first frame"), std::string::npos)
                                << mode_names.at(foo_mode) << " "
                                << mode_names.at(fwd_mode1) << " "
                                << mode_names.at(bar_mode) << " "
                                << mode_names.at(fwd_mode2) << "\n"
                                << err_str;
                        break;
                    case Mode::DYNAMIC:
                        // The formatted frame message is available.
                        EXPECT_NE(
                                err_str.find("first frame yup"),
                                std::string::npos)
                                << mode_names.at(foo_mode) << " "
                                << mode_names.at(fwd_mode1) << " "
                                << mode_names.at(bar_mode) << " "
                                << mode_names.at(fwd_mode2) << "\n"
                                << err_str;
                        break;
                }
            }

            switch (effective_bar_mode) {
                case Mode::EMPTY:
                    // None of the error message is available.
                    EXPECT_EQ(err_str.find("bar"), std::string::npos)
                            << mode_names.at(foo_mode) << " "
                            << mode_names.at(fwd_mode1) << " "
                            << mode_names.at(bar_mode) << " "
                            << mode_names.at(fwd_mode2) << "\n"
                            << err_str;
                    break;
                case Mode::STATIC:
                    if (foo_mode == Mode::EMPTY) {
                        // The unformatted bar error message replaced the empty
                        // existing message.
                        EXPECT_NE(err_str.find("bar %d"), std::string::npos)
                                << mode_names.at(foo_mode) << " "
                                << mode_names.at(fwd_mode1) << " "
                                << mode_names.at(bar_mode) << " "
                                << mode_names.at(fwd_mode2) << "\n"
                                << err_str;
                    } else {
                        // The error message is statically foo and bar couldn't
                        // be added.
                        EXPECT_EQ(err_str.find("bar"), std::string::npos)
                                << mode_names.at(foo_mode) << " "
                                << mode_names.at(fwd_mode1) << " "
                                << mode_names.at(bar_mode) << " "
                                << mode_names.at(fwd_mode2) << "\n"
                                << err_str;
                    }
                    break;
                case Mode::DYNAMIC:
                    // The formatted error message is available.
                    EXPECT_NE(err_str.find("bar 5678"), std::string::npos)
                            << mode_names.at(foo_mode) << " "
                            << mode_names.at(fwd_mode1) << " "
                            << mode_names.at(bar_mode) << " "
                            << mode_names.at(fwd_mode2) << "\n"
                            << err_str;
                    break;
            }

            if (fwd_mode2 != Mode::EMPTY) {
                switch (effective_end_mode) {
                    case Mode::EMPTY:
                    case Mode::STATIC:
                        // The frame isn't added.
                        EXPECT_EQ(
                                err_str.find("second frame"), std::string::npos)
                                << mode_names.at(foo_mode) << " "
                                << mode_names.at(fwd_mode1) << " "
                                << mode_names.at(bar_mode) << " "
                                << mode_names.at(fwd_mode2) << "\n"
                                << err_str;
                        break;
                    case Mode::DYNAMIC:
                        // The formatted frame message is available.
                        EXPECT_NE(
                                err_str.find("second frame yup"),
                                std::string::npos)
                                << mode_names.at(foo_mode) << " "
                                << mode_names.at(fwd_mode1) << " "
                                << mode_names.at(bar_mode) << " "
                                << mode_names.at(fwd_mode2) << "\n"
                                << err_str;
                        break;
                }
            }

            // EXPECT_TRUE(false) << err_str;
        };

        auto run_test = [&](Mode create_mode,
                            Mode first_frame_mode,
                            Mode forward_mode,
                            Mode second_frame_mode) {
            ZL_Report rep;
            switch (create_mode) {
                case Mode::EMPTY:
                    rep = makeReportWithoutContext();
                    break;
                case Mode::STATIC:
                    rep = makeReportWithStContext();
                    break;
                case Mode::DYNAMIC:
                    rep = makeReportWithDyContext();
                    break;
            }

            check_report(rep, create_mode);

            switch (first_frame_mode) {
                case Mode::EMPTY:
                    // Don't do anything
                    break;
                case Mode::STATIC:
                    rep = addFrameWithoutContext(rep, "first frame %s", "yup");
                    break;
                case Mode::DYNAMIC:
                    rep = addFrameWithContext(rep, "first frame %s", "yup");
                    break;
            }

            switch (forward_mode) {
                case Mode::EMPTY:
                    // Don't do anything
                    break;
                case Mode::STATIC:
                    rep = retIfErrWithoutContext(rep);
                    break;
                case Mode::DYNAMIC:
                    rep = retIfErrWithContext(rep);
                    break;
            }

            switch (second_frame_mode) {
                case Mode::EMPTY:
                    // Don't do anything
                    break;
                case Mode::STATIC:
                    rep = addFrameWithoutContext(rep, "second frame %s", "yup");
                    break;
                case Mode::DYNAMIC:
                    rep = addFrameWithContext(rep, "second frame %s", "yup");
                    break;
            }

            check_result(
                    rep,
                    create_mode,
                    first_frame_mode,
                    forward_mode,
                    second_frame_mode);
        };

        const std::vector<Mode> modes{
            { Mode::EMPTY, Mode::STATIC, Mode::DYNAMIC }
        };
        for (const auto create_mode : modes) {
            for (const auto first_frame_mode : modes) {
                for (const auto forward_mode : modes) {
                    for (const auto second_frame_mode : modes) {
                        run_test(
                                create_mode,
                                first_frame_mode,
                                forward_mode,
                                second_frame_mode);
                    }
                }
            }
        }

        ZL_OC_destroy(&opCtx);
    }
}

TEST(ErrorsTest, ErrorInfoWorks)
{
    ZL_OperationContext opCtx{};
    ZL_OC_init(&opCtx);
    {
        ZL_ErrorContext scopeCtx{ &opCtx, { .nodeID = { 5 } } };

        // Create an error with a context
        auto error = ZL_E_create(
                nullptr,
                &scopeCtx,
                "MyFile",
                "MyFunc",
                42,
                ZL_ErrorCode_corruption,
                "MyFmtString %d",
                350);

        // Check that the fields are set as expected
        EXPECT_NE(ZL_E_dy(error), nullptr);
        EXPECT_EQ(
                ZL_E_dy(error),
                ZL_OC_getError(&opCtx, ZL_ErrorCode_corruption));
        EXPECT_EQ(ZL_EE_code(error._info), ZL_ErrorCode_corruption);
        EXPECT_EQ(ZL_EE_message(error._info), std::string("MyFmtString 350"));
        EXPECT_EQ(ZL_EE_nbStackFrames(error._info), size_t(1));
        EXPECT_EQ(ZL_EE_stackFrame(error._info, 0).file, std::string("MyFile"));
        EXPECT_EQ(ZL_EE_stackFrame(error._info, 0).func, std::string("MyFunc"));
        EXPECT_EQ(ZL_EE_stackFrame(error._info, 0).line, 42);
        EXPECT_EQ(
                ZL_EE_stackFrame(error._info, 0).message,
                std::string("MyFmtString 350"));
        EXPECT_EQ(ZL_EE_graphContext(error._info).nodeID.nid, 5u);
        EXPECT_EQ(ZL_EE_graphContext(error._info).graphID.gid, 0u);
        EXPECT_EQ(ZL_EE_graphContext(error._info).transformID, 0u);

        // Change the graph context, only graphID / transformID should be set
        scopeCtx.graphCtx.nodeID.nid  = 6;
        scopeCtx.graphCtx.graphID.gid = 7;
        scopeCtx.graphCtx.transformID = 8;

        error = ZL_E_addFrame(
                &scopeCtx,
                error,
                {},
                "MyFile2",
                "MyFunc2",
                100,
                "MyFmtString2");

        EXPECT_NE(ZL_E_dy(error), nullptr);
        EXPECT_EQ(ZL_EE_code(error._info), ZL_ErrorCode_corruption);
        EXPECT_EQ(ZL_EE_message(error._info), std::string("MyFmtString 350"));
        EXPECT_EQ(ZL_EE_nbStackFrames(error._info), size_t(2));
        EXPECT_EQ(ZL_EE_stackFrame(error._info, 0).line, 42);
        EXPECT_EQ(
                ZL_EE_stackFrame(error._info, 1).file, std::string("MyFile2"));
        EXPECT_EQ(
                ZL_EE_stackFrame(error._info, 1).func, std::string("MyFunc2"));
        EXPECT_EQ(ZL_EE_stackFrame(error._info, 1).line, 100);
        EXPECT_EQ(
                ZL_EE_stackFrame(error._info, 1).message,
                std::string("Forwarding error: MyFmtString2"));
        EXPECT_EQ(ZL_EE_graphContext(error._info).nodeID.nid, 5u);
        EXPECT_EQ(ZL_EE_graphContext(error._info).graphID.gid, 7u);
        EXPECT_EQ(ZL_EE_graphContext(error._info).transformID, 8u);

        EXPECT_NE(ZL_EE_str(error._info), nullptr);
        EXPECT_EQ(ZL_EE_str(error._info), ZL_E_str(error));
        ZL_EE_log(error._info, ZL_LOG_LVL_DEBUG);
        ZL_E_log(error, ZL_LOG_LVL_DEBUG);
        ZL_E_print(error);

        // Clear the error context & test the fields
        ZL_EE_clear(opCtxEI(opCtx));

        EXPECT_EQ(ZL_EE_code(opCtxEI(opCtx)), ZL_ErrorCode_no_error);
        EXPECT_EQ(ZL_EE_message(opCtxEI(opCtx)), nullptr);
        EXPECT_EQ(ZL_EE_nbStackFrames(opCtxEI(opCtx)), size_t(0));
        EXPECT_EQ(ZL_EE_graphContext(error._info).nodeID.nid, 0u);
        EXPECT_EQ(ZL_EE_graphContext(error._info).graphID.gid, 0u);
        EXPECT_EQ(ZL_EE_graphContext(error._info).transformID, 0u);

        // Create another error without the nodeID set

        scopeCtx.graphCtx.nodeID.nid  = 0;
        scopeCtx.graphCtx.graphID.gid = 1;
        scopeCtx.graphCtx.transformID = 2;

        error = ZL_E_create(
                nullptr,
                &scopeCtx,
                "MyFile",
                "MyFunc",
                42,
                ZL_ErrorCode_allocation,
                "MyFmtString %d",
                350);

        EXPECT_NE(ZL_E_dy(error), nullptr);
        EXPECT_EQ(
                ZL_E_dy(error),
                ZL_OC_getError(&opCtx, ZL_ErrorCode_allocation));
        EXPECT_EQ(ZL_EE_code(error._info), ZL_ErrorCode_allocation);
        EXPECT_EQ(ZL_EE_message(error._info), std::string("MyFmtString 350"));
        EXPECT_EQ(ZL_EE_nbStackFrames(error._info), size_t(1));
        EXPECT_EQ(ZL_EE_stackFrame(error._info, 0).file, std::string("MyFile"));
        EXPECT_EQ(ZL_EE_stackFrame(error._info, 0).func, std::string("MyFunc"));
        EXPECT_EQ(ZL_EE_stackFrame(error._info, 0).line, 42);
        EXPECT_EQ(
                ZL_EE_stackFrame(error._info, 0).message,
                std::string("MyFmtString 350"));
        EXPECT_EQ(ZL_EE_graphContext(error._info).nodeID.nid, 0u);
        EXPECT_EQ(ZL_EE_graphContext(error._info).graphID.gid, 1u);
        EXPECT_EQ(ZL_EE_graphContext(error._info).transformID, 2u);

        // Override the nodeID
        scopeCtx.graphCtx.nodeID.nid  = 3;
        scopeCtx.graphCtx.graphID.gid = 0;
        scopeCtx.graphCtx.transformID = 0;

        error = ZL_E_addFrame(
                &scopeCtx,
                error,
                {},
                "MyFile2",
                "MyFile2",
                100,
                "MyFmtString2");

        EXPECT_EQ(ZL_EE_graphContext(error._info).nodeID.nid, 3u);
        EXPECT_EQ(ZL_EE_graphContext(error._info).graphID.gid, 1u);
        EXPECT_EQ(ZL_EE_graphContext(error._info).transformID, 2u);
    }

    {
        ZL_OC_clearErrors(&opCtx);

        // Create an error without a context
        auto error = ZL_E_create(
                nullptr,
                nullptr,
                "MyFile",
                "MyFunc",
                42,
                ZL_ErrorCode_allocation,
                "MyFmtString %d",
                350);

        EXPECT_EQ(ZL_E_dy(error), nullptr);
        EXPECT_EQ(ZL_OC_numErrors(&opCtx), 0u);

        // Add a frame without a context
        error = ZL_E_addFrame(
                nullptr, error, {}, "MyFile2", "MyFile2", 100, "MyFmtString2");

        EXPECT_EQ(ZL_E_dy(error), nullptr);
        EXPECT_EQ(ZL_OC_numErrors(&opCtx), 0u);

        {
            // Add a frame with a context
            ZL_ErrorContext scopeCtx{ &opCtx };

            // Add a frame with a context
            error = ZL_E_addFrame(
                    &scopeCtx, error, {}, "MyFile3", "MyFile3", 300, "Fmt3");

            EXPECT_NE(ZL_E_dy(error), nullptr);
            EXPECT_EQ(ZL_OC_numErrors(&opCtx), 1u);
            EXPECT_EQ(ZL_EE_code(error._info), ZL_ErrorCode_allocation);
            EXPECT_EQ(
                    ZL_EE_message(error._info),
                    std::string("Attaching to pre-existing error: Fmt3"));
            EXPECT_EQ(ZL_EE_nbStackFrames(error._info), size_t(1));
            EXPECT_EQ(
                    ZL_EE_stackFrame(error._info, 0).message,
                    std::string("Attaching to pre-existing error: Fmt3"));
        }

        // Add a frame without a context, but already in error
        error = ZL_E_addFrame(
                nullptr, error, {}, "MyFile4", "MyFile4", 400, "Fmt4");

        EXPECT_NE(ZL_E_dy(error), nullptr);
        EXPECT_EQ(ZL_EE_code(error._info), ZL_ErrorCode_allocation);
        EXPECT_EQ(ZL_EE_nbStackFrames(error._info), size_t(2));
        EXPECT_EQ(
                ZL_EE_stackFrame(error._info, 1).message,
                std::string("Forwarding error: Fmt4"));
    }

    ZL_OC_destroy(&opCtx);
}

static void testStaticErrorInfo(ZL_Error& e, const std::string& needle = "")
{
    {
        auto st = ZL_E_st(e);
        EXPECT_NE(st, nullptr);
        EXPECT_EQ(st->code, ZL_ErrorCode_corruption);
        EXPECT_NE(st->fmt, nullptr);
        EXPECT_NE(st->file, nullptr);
        EXPECT_NE(st->func, nullptr);
        EXPECT_NE(st->line, 0);

        EXPECT_EQ(ZL_EE_code(e._info), ZL_ErrorCode_corruption);
        EXPECT_EQ(ZL_EE_message(e._info), st->fmt);
        EXPECT_EQ(ZL_EE_nbStackFrames(e._info), (size_t)1);

        auto frame = ZL_EE_stackFrame(e._info, 0);
        EXPECT_EQ(frame.file, st->file);
        EXPECT_EQ(frame.func, st->func);
        EXPECT_EQ(frame.line, st->line);
        EXPECT_EQ(frame.message, st->fmt);

        EXPECT_EQ(ZL_EE_str(e._info), st->fmt);
        EXPECT_EQ(ZL_E_str(e), st->fmt);

        auto str = std::string(ZL_E_str(e));
        EXPECT_NE(str.find(needle), std::string::npos);
    }

    {
        ZL_OperationContext opCtx{};
        ZL_OC_init(&opCtx);

        ZL_ErrorContext scopeCtx{ &opCtx, { .nodeID = { 5 } } };

        e = ZL_E_addFrame(
                &scopeCtx, e, {}, "MyFile", "MyFunc", 123, "MoarTxt %d", 1234);

        auto str = std::string(ZL_E_str(e));
        EXPECT_NE(str.find(needle), std::string::npos);

        ZL_OC_destroy(&opCtx);
    }
}

TEST(ErrorsTest, StaticErrorInfo)
{
    {
        auto f = [](int path) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            switch (path) {
                case 0:
                    ZL_ERR(corruption);
                case 1:
                    ZL_ERR(corruption, "BeepBeep!");
                case 2:
                    ZL_ERR(corruption, "BeepBeep %d", 1234);
                default:
                    throw std::runtime_error("!");
            }
        };

        auto e = ZL_RES_error(f(0));
        testStaticErrorInfo(e);
        e = ZL_RES_error(f(1));
        testStaticErrorInfo(e, "BeepBeep!");
        e = ZL_RES_error(f(2));
        testStaticErrorInfo(e, "BeepBeep %d");
    }
    {
        auto f = [](bool succeed) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            ZL_ERR_IF(!succeed, corruption, "BeepBeep!");
            return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
        };

        auto r = f(false);
        auto e = ZL_RES_error(r);

        testStaticErrorInfo(e);
    }
    {
        auto f = [](int path) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            const auto condition_expression = true;
            switch (path) {
                case 0:
                    ZL_ERR_IF(condition_expression, corruption);
                    break;
                case 1:
                    ZL_ERR_IF(condition_expression, corruption, "BeepBeep!");
                    break;
                case 2:
                    ZL_ERR_IF(
                            condition_expression,
                            corruption,
                            "BeepBeep %d",
                            1234);
                    break;
                default:
                    throw std::runtime_error("!");
            }
        };

        auto e = ZL_RES_error(f(0));
        testStaticErrorInfo(e, "condition_expression");
        e = ZL_RES_error(f(1));
        testStaticErrorInfo(e, "BeepBeep!");
        e = ZL_RES_error(f(2));
        testStaticErrorInfo(e, "BeepBeep %d");
    }
    {
        auto f = [](int path) {
            ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
            const auto val1 = 1;
            const auto val2 = 2;
            switch (path) {
                case 0:
                    ZL_ERR_IF_NE(val1, val2, corruption);
                    break;
                case 1:
                    ZL_ERR_IF_NE(val1, val2, corruption, "BeepBeep!");
                    break;
                case 2:
                    ZL_ERR_IF_NE(val1, val2, corruption, "BeepBeep %d", 1234);
                    break;
                default:
                    throw std::runtime_error("!");
            }
        };

        auto e = ZL_RES_error(f(0));
        testStaticErrorInfo(e, "val1 != val2");
        e = ZL_RES_error(f(1));
        testStaticErrorInfo(e, "BeepBeep!");
        e = ZL_RES_error(f(2));
        testStaticErrorInfo(e, "BeepBeep %d");
    }
}

TEST(ErrorsTest, StaticInfoStringContainsPercentSymbol)
{
    auto f = [](bool succeed) {
        ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
        int x = !succeed;
        ZL_ERR_IF(x % 2, corruption);
        return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
    };

    auto r = f(false);
    auto e = ZL_RES_error(r);

    auto st = ZL_E_st(e);
    EXPECT_NE(st, nullptr);

    {
        auto str = std::string(ZL_E_str(e));
        EXPECT_NE(str.find("x % 2"), std::string::npos);
    }

    ZL_OperationContext opCtx{};
    ZL_OC_init(&opCtx);
    ZL_ErrorContext scopeCtx{ &opCtx, {} };

    e = ZL_E_addFrame(&scopeCtx, e, {}, "a", "b", 123, "c %d", 1234);

    {
        auto str = std::string(ZL_E_str(e));
        EXPECT_NE(str.find("x % 2"), std::string::npos);
    }

    ZL_OC_destroy(&opCtx);
}

TEST(ErrorsTest, DynamicInfoStringContainsPercentSymbol)
{
    ZL_OperationContext opCtx{};
    ZL_OC_init(&opCtx);

    auto f = [&](bool succeed) mutable {
        ZL_RESULT_DECLARE_SCOPE(Foo, &opCtx);
        int x = !succeed;
        ZL_ERR_IF(x % 2, corruption);
        return ZL_RESULT_WRAP_VALUE(Foo, kFoo);
    };

    auto r = f(false);
    auto e = ZL_RES_error(r);

    auto st = ZL_E_st(e);
    EXPECT_EQ(st, nullptr);

    auto str = std::string(ZL_E_str(e));
    EXPECT_NE(str.find("x % 2"), std::string::npos);

    ZL_OC_destroy(&opCtx);
}

TEST(ErrorsTest, CoerceInternalErrors)
{
    ZL_OperationContext opCtx{};
    ZL_OC_init(&opCtx);

    auto f = [&]() mutable {
        ZL_RESULT_DECLARE_SCOPE(Foo, &opCtx);
        ZL_ERR(dstCapacity_tooSmall, "oops");
    };

    auto g = [&](ZL_RESULT_OF(Foo) & res) {
        ZL_RESULT_DECLARE_SCOPE(Foo, &opCtx);
        ZL_ERR_IF_ERR_COERCE(res, "fail");
        return res;
    };

    auto r1 = f();
    auto e1 = ZL_RES_error(r1);
    EXPECT_EQ(ZL_E_code(e1), ZL_ErrorCode_dstCapacity_tooSmall);

    auto r2 = r1;
#if ZL_ENABLE_ASSERT
    ASSERT_DEATH({ r2 = g(r1); }, "");
#else
    r2      = g(r1);
    auto e2 = ZL_RES_error(r2);
    EXPECT_EQ(ZL_E_code(e2), ZL_ErrorCode_logicError);
#endif

    ZL_OC_destroy(&opCtx);
}

TEST(ErrorsTest, LogicErrorGoesBoom)
{
    auto f = [&]() mutable {
        ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
        ZL_ERR(logicError, "oops");
    };
    ZL_RESULT_OF(Foo) r;
#if ZL_ENABLE_ASSERT
    ASSERT_DEATH({ r = f(); }, "oops");
#else
    r = f();
#endif
    (void)r;
}

TEST(ErrorsTest, BinaryTestArgTypesDeducedInC)
{
    // ZS_g_enableLeakyErrorAllocations = 1;

    ZL_OperationContext opCtx{};
    ZL_OC_init(&opCtx);

    ZL_Report report =
            ZS2_test_errors_binary_arg_types_deduced_in_c_inner(&opCtx);
    if (ZL_RES_isError(report)) {
        ZL_E_print(ZL_RES_error(report));
    }

    EXPECT_FALSE(ZL_RES_isError(report));

    ZL_OC_destroy(&opCtx);
}

TEST(ErrorsTest, TrySet)
{
    const auto inner = [](bool succeed) -> ZL_RESULT_OF(Foo) {
        ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
        ZL_ERR_IF(!succeed, GENERIC);
        const Foo foo{
            .val = 1234,
        };
        return ZL_WRAP_VALUE(foo);
    };

    const auto outer = [&inner](bool succeed) -> ZL_RESULT_OF(Bar) {
        ZL_RESULT_DECLARE_SCOPE(Bar, nullptr);
        Foo var;
        ZL_TRY_SET(Foo, var, inner(succeed));
        EXPECT_TRUE(succeed);
        const Bar bar{
            .val = var.val,
        };
        return ZL_WRAP_VALUE(bar);
    };

    auto res = outer(false);
    EXPECT_TRUE(ZL_RES_isError(res));

    res = outer(true);
    EXPECT_FALSE(ZL_RES_isError(res));
    EXPECT_EQ(ZL_RES_value(res).val, 1234);
}

TEST(ErrorsTest, TryLet)
{
    const auto inner = [](bool succeed) -> ZL_RESULT_OF(Foo) {
        ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
        ZL_ERR_IF(!succeed, GENERIC);
        const Foo foo{
            .val = 1234,
        };
        return ZL_WRAP_VALUE(foo);
    };

    const auto outer = [&inner](bool succeed) -> ZL_RESULT_OF(Bar) {
        ZL_RESULT_DECLARE_SCOPE(Bar, nullptr);
        ZL_TRY_LET(Foo, var, inner(succeed));
        EXPECT_TRUE(succeed);
        var.val++;
        const Bar bar{
            .val = var.val,
        };
        return ZL_WRAP_VALUE(bar);
    };

    auto res = outer(false);
    EXPECT_TRUE(ZL_RES_isError(res));

    res = outer(true);
    EXPECT_FALSE(ZL_RES_isError(res));
    EXPECT_EQ(ZL_RES_value(res).val, 1235);
}

TEST(ErrorsTest, TryLetConst)
{
    const auto inner = [](bool succeed) -> ZL_RESULT_OF(Foo) {
        ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
        ZL_ERR_IF(!succeed, GENERIC);
        const Foo foo{
            .val = 1234,
        };
        return ZL_WRAP_VALUE(foo);
    };

    const auto outer = [&inner](bool succeed) -> ZL_RESULT_OF(Bar) {
        ZL_RESULT_DECLARE_SCOPE(Bar, nullptr);
        ZL_TRY_LET_CONST(Foo, var, inner(succeed));
        EXPECT_TRUE(succeed);
        const Bar bar{
            .val = var.val,
        };
        return ZL_WRAP_VALUE(bar);
    };

    auto res = outer(false);
    EXPECT_TRUE(ZL_RES_isError(res));

    res = outer(true);
    EXPECT_FALSE(ZL_RES_isError(res));
    EXPECT_EQ(ZL_RES_value(res).val, 1234);
}

TEST(ErrorsTest, DeclaredRetValUnary)
{
    ZL_OperationContext opCtx{};
    ZL_OC_init(&opCtx);
    const auto inner = [&opCtx](bool succeed) -> ZL_RESULT_OF(Foo) {
        ZL_RESULT_DECLARE_SCOPE(Foo, &opCtx);

        ZL_ERR_IF_NOT(succeed, corruption, "Eep! %d", 1234);

        return ZL_WRAP_VALUE(kFoo);
    };

    auto res = inner(true);
    EXPECT_FALSE(ZL_RES_isError(res));

    res = inner(false);
    EXPECT_TRUE(ZL_RES_isError(res));
    const std::string errstr{ ZL_E_str(ZL_RES_error(res)) };
    EXPECT_NE(errstr.find("Eep!"), std::string::npos);
    EXPECT_NE(
            errstr.find(ZL_ErrorCode_corruption__desc_str), std::string::npos);
    EXPECT_NE(errstr.find("1234"), std::string::npos);

    ZL_OC_destroy(&opCtx);
}

TEST(ErrorsTest, DeclaredRetValBinary)
{
    ZL_OperationContext opCtx{};
    ZL_OC_init(&opCtx);
    const auto inner = [&opCtx](bool succeed) -> ZL_RESULT_OF(Foo) {
        ZL_RESULT_DECLARE_SCOPE(Foo, &opCtx);

        ZL_ERR_IF_NE(succeed, true, corruption, "Eep! %d", 1234);

        return ZL_WRAP_VALUE(kFoo);
    };

    auto res = inner(true);
    EXPECT_FALSE(ZL_RES_isError(res));

    res = inner(false);
    EXPECT_TRUE(ZL_RES_isError(res));
    const std::string errstr{ ZL_E_str(ZL_RES_error(res)) };
    EXPECT_NE(errstr.find("Eep!"), std::string::npos);
    EXPECT_NE(
            errstr.find(ZL_ErrorCode_corruption__desc_str), std::string::npos);
    EXPECT_NE(errstr.find("1234"), std::string::npos);

    ZL_OC_destroy(&opCtx);
}

TEST(ErrorsTest, EmptyDeclaredRetVal)
{
    const auto inner = [](bool succeed) -> ZL_RESULT_OF(Foo) {
        ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);

        ZL_ERR_IF_NE(succeed, true, corruption, "Eep! %d", 1234);

        return ZL_WRAP_VALUE(kFoo);
    };

    auto res = inner(true);
    EXPECT_FALSE(ZL_RES_isError(res));

    res = inner(false);
    EXPECT_TRUE(ZL_RES_isError(res));
    const std::string errstr{ ZL_E_str(ZL_RES_error(res)) };
    EXPECT_NE(errstr.find("Eep!"), std::string::npos);
    EXPECT_NE(
            errstr.find(ZL_ErrorCode_corruption__desc_str), std::string::npos);
    EXPECT_EQ(errstr.find("1234"), std::string::npos);
}

TEST(ErrorsTest, EmptyDeclaredGetsDynInPassing)
{
    ZL_OperationContext opCtx{};
    ZL_OC_init(&opCtx);
    const auto inner = [](bool succeed) -> ZL_RESULT_OF(Foo) {
        ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);

        ZL_ERR_IF_NE(succeed, true, corruption, "Eep! %d", 1234);

        return ZL_WRAP_VALUE(kFoo);
    };

    const auto outer = [&opCtx](ZL_RESULT_OF(Foo) res) -> ZL_RESULT_OF(Foo) {
        ZL_RESULT_DECLARE_SCOPE(Foo, &opCtx);

        ZL_ERR_IF_ERR(res, "Fwd! %d", 5678);

        return ZL_WRAP_VALUE(ZL_RES_value(res));
    };

    auto res = outer(inner(true));
    EXPECT_FALSE(ZL_RES_isError(res));

    res = outer(inner(false));
    EXPECT_TRUE(ZL_RES_isError(res));
    const std::string errstr{ ZL_E_str(ZL_RES_error(res)) };
    EXPECT_NE(errstr.find("Eep!"), std::string::npos);
    EXPECT_NE(
            errstr.find(ZL_ErrorCode_corruption__desc_str), std::string::npos);
    EXPECT_EQ(errstr.find("1234"), std::string::npos);
    EXPECT_NE(errstr.find("Fwd!"), std::string::npos);
    EXPECT_NE(errstr.find("5678"), std::string::npos);

    ZL_OC_destroy(&opCtx);
}

TEST(ErrorsTest, TrySetNew)
{
    const auto inner = [](bool succeed) -> ZL_RESULT_OF(Foo) {
        ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
        ZL_ERR_IF(!succeed, GENERIC);
        const Foo foo{
            .val = 1234,
        };
        return ZL_WRAP_VALUE(foo);
    };

    const auto outer = [&inner](bool succeed) -> ZL_RESULT_OF(Bar) {
        ZL_RESULT_DECLARE_SCOPE(Bar, nullptr);
        Foo var;
        ZL_TRY_SET(Foo, var, inner(succeed));
        EXPECT_TRUE(succeed);
        const Bar bar{
            .val = var.val,
        };
        return ZL_WRAP_VALUE(bar);
    };

    auto res = outer(false);
    EXPECT_TRUE(ZL_RES_isError(res));

    res = outer(true);
    EXPECT_FALSE(ZL_RES_isError(res));
    EXPECT_EQ(ZL_RES_value(res).val, 1234);
}

TEST(ErrorsTest, TryLetNew)
{
    const auto inner = [](bool succeed) -> ZL_RESULT_OF(Foo) {
        ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
        ZL_ERR_IF(!succeed, GENERIC);
        const Foo foo{
            .val = 1234,
        };
        return ZL_WRAP_VALUE(foo);
    };

    const auto outer = [&inner](bool succeed) -> ZL_RESULT_OF(Bar) {
        ZL_RESULT_DECLARE_SCOPE(Bar, nullptr);
        ZL_TRY_LET(Foo, var, inner(succeed));
        EXPECT_TRUE(succeed);
        const Bar bar{
            .val = var.val,
        };
        return ZL_WRAP_VALUE(bar);
    };

    auto res = outer(false);
    EXPECT_TRUE(ZL_RES_isError(res));

    res = outer(true);
    EXPECT_FALSE(ZL_RES_isError(res));
    EXPECT_EQ(ZL_RES_value(res).val, 1234);
}

TEST(ErrorsTest, TryLetConstNew)
{
    const auto inner = [](bool succeed) -> ZL_RESULT_OF(Foo) {
        ZL_RESULT_DECLARE_SCOPE(Foo, nullptr);
        ZL_ERR_IF(!succeed, GENERIC);
        const Foo foo{
            .val = 1234,
        };
        return ZL_WRAP_VALUE(foo);
    };

    const auto outer = [&inner](bool succeed) -> ZL_RESULT_OF(Bar) {
        ZL_RESULT_DECLARE_SCOPE(Bar, nullptr);
        ZL_TRY_LET_CONST(Foo, var, inner(succeed));
        EXPECT_TRUE(succeed);
        const Bar bar{
            .val = var.val,
        };
        return ZL_WRAP_VALUE(bar);
    };

    auto res = outer(false);
    EXPECT_TRUE(ZL_RES_isError(res));

    res = outer(true);
    EXPECT_FALSE(ZL_RES_isError(res));
    EXPECT_EQ(ZL_RES_value(res).val, 1234);
}

} // namespace
