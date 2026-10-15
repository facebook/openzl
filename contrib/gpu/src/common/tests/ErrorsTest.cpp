// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "contrib/gpu/src/common/errors.hpp"
#include "contrib/gpu/src/common/tests/error_testing.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/detail/Portability.hpp"

namespace openzl::gpu {
namespace {

/// @returns The message that @p fn throws, without the call-site prefix that
/// the macros add before C++20.
template <typename Fn>
std::string thrownCheckMessage(Fn&& fn)
{
    std::string message = thrownMessage(std::forward<Fn>(fn));
    if constexpr (!openzl::detail::kHasSourceLocation) {
        const std::string file = __FILE__ ":";
        EXPECT_EQ(message.rfind(file, 0), 0) << message;
        message.erase(0, message.find(": ", file.size()) + 2);
    }
    return message;
}

TEST(ErrorsTest, PassingChecksDoNotThrow)
{
    const size_t one = 1;
    ZL_THROW_IF(false, corruption);
    ZL_THROW_IF_NOT(true, corruption);
    ZL_THROW_IF_NULL(&one, corruption);
    ZL_THROW_IF_NE(one, 1, corruption);
    ZL_THROW_IF_GT(one, 1, corruption, "with a message");
}

TEST(ErrorsTest, FailedCheckThrowsItsCodeAndMessage)
{
    EXPECT_EQ(
            thrownErrorCode([] { ZL_THROW_IF(true, srcSize_tooSmall); }),
            ZL_ErrorCode_srcSize_tooSmall);
    EXPECT_EQ(
            thrownCheckMessage([] {
                ZL_THROW_IF(1 + 1 == 2, corruption, "header is truncated");
            }),
            "Check `1 + 1 == 2' failed: header is truncated");
}

TEST(ErrorsTest, FailedComparisonReportsBothValues)
{
    const size_t numInputs = 3;
    EXPECT_EQ(
            thrownErrorCode([&] { ZL_THROW_IF_NE(numInputs, 2, corruption); }),
            ZL_ErrorCode_corruption);
    EXPECT_EQ(
            thrownCheckMessage(
                    [&] { ZL_THROW_IF_NE(numInputs, 2, corruption); }),
            "Check `numInputs != 2' failed where lhs = 3, rhs = 2");
}

TEST(ErrorsTest, FailedPointerComparisonReportsAddresses)
{
    const int values[2]{};
    const int* const first    = &values[0];
    const int* const second   = &values[1];
    const std::string message = thrownCheckMessage(
            [&] { ZL_THROW_IF_NE(first, second, corruption); });
    EXPECT_EQ(
            message.rfind("Check `first != second' failed where lhs = 0x", 0),
            0)
            << message;
}

void testFunction() {}

TEST(ErrorsTest, FailedFunctionPointerComparisonReportsAddress)
{
    void (*const function)()  = &testFunction;
    const std::string message = thrownCheckMessage(
            [&] { ZL_THROW_IF_NE(function, nullptr, corruption); });
    EXPECT_EQ(
            message.rfind(
                    "Check `function != nullptr' failed where lhs = 0x", 0),
            0)
            << message;
}

TEST(ErrorsTest, ThrowErrorReplacesNoErrorWithGeneric)
{
    EXPECT_EQ(
            thrownErrorCode([] { throwError(ZL_ErrorCode_no_error, "bad"); }),
            ZL_ErrorCode_GENERIC);
}

TEST(ErrorsTest, ComparisonsMatchTheCoreMacros)
{
    // A GPU check must reject exactly what its core counterpart rejects, also
    // when the operands differ in signedness. Both macros compare mixed-sign
    // operands here on purpose, which -Wsign-compare reports.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
    const auto coreRejectsLess = [](auto lhs, auto rhs) {
        const auto check = [&]() -> ZL_Report {
            ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
            ZL_ERR_IF_LT(lhs, rhs, corruption);
            return ZL_returnSuccess();
        };
        return ZL_isError(check());
    };
    const auto coreRejectsEqual = [](auto lhs, auto rhs) {
        const auto check = [&]() -> ZL_Report {
            ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
            ZL_ERR_IF_EQ(lhs, rhs, corruption);
            return ZL_returnSuccess();
        };
        return ZL_isError(check());
    };
    const auto gpuRejectsLess = [](auto lhs, auto rhs) {
        return thrownErrorCode([&] { ZL_THROW_IF_LT(lhs, rhs, corruption); })
                .has_value();
    };
    const auto gpuRejectsEqual = [](auto lhs, auto rhs) {
        return thrownErrorCode([&] { ZL_THROW_IF_EQ(lhs, rhs, corruption); })
                .has_value();
    };
#pragma GCC diagnostic pop

    const int negative = -1;
    const size_t zero  = 0;
    const size_t max   = static_cast<size_t>(-1);
    EXPECT_EQ(gpuRejectsLess(negative, zero), coreRejectsLess(negative, zero));
    EXPECT_EQ(gpuRejectsLess(zero, negative), coreRejectsLess(zero, negative));
    EXPECT_EQ(gpuRejectsEqual(negative, max), coreRejectsEqual(negative, max));
    EXPECT_EQ(gpuRejectsEqual(max, negative), coreRejectsEqual(max, negative));
}

TEST(ErrorsTest, FailedComparisonFormatsNullAndFloatingPoint)
{
    const int* const missing = nullptr;
    const double ratio       = 1.5;
    EXPECT_NE(
            thrownMessage([&] {
                ZL_THROW_IF_EQ(missing, nullptr, corruption);
            }).find("rhs = nullptr"),
            std::string::npos);
    EXPECT_EQ(
            thrownCheckMessage([&] { ZL_THROW_IF_GT(ratio, 1.0, corruption); }),
            "Check `ratio > 1.0' failed where lhs = 1.5, rhs = 1");
    const double nearlyOne = 1.0000001;
    EXPECT_EQ(
            thrownCheckMessage(
                    [&] { ZL_THROW_IF_GT(nearlyOne, 1.0, corruption); }),
            "Check `nearlyOne > 1.0' failed where lhs = 1.0000001000000001, rhs = 1");
}

TEST(ErrorsTest, ComparisonCopiesOperandsFromTemporaries)
{
    // The vector dies at the end of the operand expression. A reference to its
    // element would dangle before the comparison runs.
    ZL_THROW_IF_NE(std::vector<int>{ 7 }[0], 7, corruption);
}

TEST(ErrorsTest, ComparisonEvaluatesEachOperandOnce)
{
    int lhsCalls = 0;
    int rhsCalls = 0;
    ZL_THROW_IF_GT(++lhsCalls, ++rhsCalls, corruption);
    EXPECT_EQ(lhsCalls, 1);
    EXPECT_EQ(rhsCalls, 1);
}

TEST(ErrorsTest, ExceptionNamesTheFailedCheckLine)
{
    const int expectedLine = __LINE__ + 2;
    try {
        ZL_THROW_IF(true, corruption);
    } catch (const Exception& e) {
        const std::string callSite = std::string{ __FILE__ } + ":"
                + std::to_string(expectedLine) + ":";
        EXPECT_NE(std::string{ e.what() }.find(callSite), std::string::npos)
                << e.what();
        return;
    }
    FAIL() << "ZL_THROW_IF did not throw";
}

} // namespace
} // namespace openzl::gpu
