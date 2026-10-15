// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/common/errors.hpp"

#include <limits>
#include <locale>
#include <sstream>

#include "openzl/common/assertion.h"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/detail/Portability.hpp"

namespace openzl::gpu {

void throwError(
        ZL_ErrorCode code,
        std::string_view message,
        poly::source_location location)
{
    // ZL_ErrorCode_no_error would turn into a success ZL_Report at the C API.
    if (code == ZL_ErrorCode_no_error) {
        code = ZL_ErrorCode_GENERIC;
    }
    // logicError marks a broken internal invariant, so debug builds stop here,
    // as the core's ZL_E_create() does.
    ZL_ASSERT_NE(
            code,
            ZL_ErrorCode_logicError,
            "Logic error in: %.*s",
            static_cast<int>(message.size()),
            message.data());
    throw ExceptionBuilder(message, location).withErrorCode(code).build();
}

namespace detail {

std::string formatCheckValue(bool value)
{
    return value ? "true" : "false";
}

std::string formatCheckValue(long long value)
{
    return std::to_string(value);
}

std::string formatCheckValue(unsigned long long value)
{
    return std::to_string(value);
}

std::string formatCheckValue(double value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(std::numeric_limits<double>::max_digits10);
    out << value;
    return out.str();
}

std::string formatCheckValue(const void* value)
{
    std::ostringstream out;
    out << value;
    return out.str();
}

void throwCheckFailed(
        ZL_ErrorCode code,
        std::string_view check,
        std::string_view message,
        const char* file,
        int line,
        poly::source_location location)
{
    std::string text;
    // Exception::what() names the call site only when source_location is
    // available.
    if constexpr (!::openzl::detail::kHasSourceLocation) {
        text += file;
        text += ':';
        text += std::to_string(line);
        text += ": ";
    }
    text += check;
    if (!message.empty()) {
        text += ": ";
        text += message;
    }
    throwError(code, text, location);
}

void throwFormattedComparisonFailed(
        ZL_ErrorCode code,
        std::string_view check,
        const std::string& lhs,
        const std::string& rhs,
        std::string_view message,
        const char* file,
        int line,
        poly::source_location location)
{
    std::string text{ check };
    text += " where lhs = ";
    text += lhs;
    text += ", rhs = ";
    text += rhs;
    throwCheckFailed(code, text, message, file, line, location);
}

} // namespace detail
} // namespace openzl::gpu
