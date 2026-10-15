// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "openzl/cpp/poly/SourceLocation.hpp"
#include "openzl/zl_errors.h"
#include "openzl/zl_portability.h"

/**
 * Host C++ error checks.
 *
 * These macros mirror the core `ZL_ERR_IF_*` macros, but they throw an
 * `openzl::Exception` instead of returning a `ZL_Report`. A GPU check can then
 * match the core decoder check that it reproduces line for line:
 *
 *     ZL_ERR_IF_GT(headerEnd, srcSize, corruption);   // core decoder
 *     ZL_THROW_IF_GT(headerEnd, srcSize, corruption); // GPU planner
 *
 * The error code is the suffix of a `ZL_ErrorCode_*` name. The optional last
 * argument must be a single string literal, so a passing check never builds a
 * message. Unlike the core macros, these take no printf-style arguments, and a
 * call that passes them does not compile. Each macro evaluates its operands
 * once. A comparison converts both operands to their common type first, as the
 * core macros do, so a GPU check and its core counterpart give the same result.
 *
 * Before C++20, `poly::source_location` is empty, so the message starts with
 * the `file:line: ` of the failed check, as core log lines do.
 *
 * Device code cannot throw. It must report errors through a status array.
 */

#define ZL_THROW_IF(cond, ...) ZL_THROW_IF_IMPL(cond, __VA_ARGS__, "", "")
#define ZL_THROW_IF_NOT(cond, ...) ZL_THROW_IF(!(cond), __VA_ARGS__)
#define ZL_THROW_IF_NULL(ptr, ...) ZL_THROW_IF((ptr) == nullptr, __VA_ARGS__)

#define ZL_THROW_IF_EQ(lhs, rhs, ...) \
    ZL_THROW_IF_CMP_IMPL(lhs, ==, rhs, __VA_ARGS__, "", "")
#define ZL_THROW_IF_NE(lhs, rhs, ...) \
    ZL_THROW_IF_CMP_IMPL(lhs, !=, rhs, __VA_ARGS__, "", "")
#define ZL_THROW_IF_LT(lhs, rhs, ...) \
    ZL_THROW_IF_CMP_IMPL(lhs, <, rhs, __VA_ARGS__, "", "")
#define ZL_THROW_IF_LE(lhs, rhs, ...) \
    ZL_THROW_IF_CMP_IMPL(lhs, <=, rhs, __VA_ARGS__, "", "")
#define ZL_THROW_IF_GT(lhs, rhs, ...) \
    ZL_THROW_IF_CMP_IMPL(lhs, >, rhs, __VA_ARGS__, "", "")
#define ZL_THROW_IF_GE(lhs, rhs, ...) \
    ZL_THROW_IF_CMP_IMPL(lhs, >=, rhs, __VA_ARGS__, "", "")

// The two trailing "" arguments keep `...` non-empty, which -pedantic requires
// before C++20, and supply the empty message when the caller gives none. So
// `...` holds one argument without a message and two with one. A third
// argument means printf-style arguments, which these macros reject.
#define ZL_THROW_IF_REJECT_FORMAT_ARGS(...)                                     \
    static_assert(                                                              \
            std::tuple_size_v<decltype(std::make_tuple(__VA_ARGS__))> <= 2,     \
            "ZL_THROW_IF* takes one optional string literal, not printf-style " \
            "arguments")

#define ZL_THROW_IF_IMPL(cond, code, message, ...)               \
    do {                                                         \
        ZL_THROW_IF_REJECT_FORMAT_ARGS(__VA_ARGS__);             \
        if (ZL_UNLIKELY(cond)) {                                 \
            ::openzl::gpu::detail::throwCheckFailed(             \
                    ZL_ErrorCode_##code,                         \
                    "Check `" #cond "' failed",                  \
                    "" message,                                  \
                    __FILE__,                                    \
                    __LINE__,                                    \
                    ::openzl::poly::source_location::current()); \
        }                                                        \
    } while (0)

#define ZL_THROW_IF_CMP_IMPL(lhs, op, rhs, code, message, ...)   \
    do {                                                         \
        ZL_THROW_IF_REJECT_FORMAT_ARGS(__VA_ARGS__);             \
        const auto zlThrowIfLhs = (lhs);                         \
        const auto zlThrowIfRhs = (rhs);                         \
        if (ZL_UNLIKELY(zlThrowIfLhs op zlThrowIfRhs)) {         \
            ::openzl::gpu::detail::throwComparisonFailed(        \
                    ZL_ErrorCode_##code,                         \
                    "Check `" #lhs " " #op " " #rhs "' failed",  \
                    zlThrowIfLhs,                                \
                    zlThrowIfRhs,                                \
                    "" message,                                  \
                    __FILE__,                                    \
                    __LINE__,                                    \
                    ::openzl::poly::source_location::current()); \
        }                                                        \
    } while (0)

namespace openzl::gpu {

/**
 * Throws an @c openzl::Exception that carries @p code.
 *
 * Use this for an unconditional failure, such as an unknown enum value. Use a
 * `ZL_THROW_IF_*` macro for a check. @c ZL_GPU_decompress converts the
 * exception to a @c ZL_Report, so @p code is the code that C callers receive.
 * @c ZL_ErrorCode_no_error becomes @c ZL_ErrorCode_GENERIC.
 * Debug builds abort on @c ZL_ErrorCode_logicError, as the core does.
 */
[[noreturn]] void throwError(
        ZL_ErrorCode code,
        std::string_view message,
        poly::source_location location = poly::source_location::current());

namespace detail {

template <typename T>
constexpr bool kIsNonBoolInteger =
        std::is_integral_v<T> && !std::is_same_v<T, bool>;

std::string formatCheckValue(bool value);
std::string formatCheckValue(long long value);
std::string formatCheckValue(unsigned long long value);
std::string formatCheckValue(double value);
std::string formatCheckValue(const void* value);

/// @returns @p value as text, or a placeholder for a type without a format.
template <typename T>
std::string formatCheckValue(const T& value)
{
    if constexpr (std::is_enum_v<T>) {
        return formatCheckValue(std::underlying_type_t<T>(value));
    } else if constexpr (kIsNonBoolInteger<T> && std::is_signed_v<T>) {
        return formatCheckValue(static_cast<long long>(value));
    } else if constexpr (kIsNonBoolInteger<T>) {
        return formatCheckValue(static_cast<unsigned long long>(value));
    } else if constexpr (std::is_floating_point_v<T>) {
        return formatCheckValue(static_cast<double>(value));
    } else if constexpr (std::is_null_pointer_v<T>) {
        return "nullptr";
    } else if constexpr (
            std::is_pointer_v<T>
            && std::is_function_v<std::remove_pointer_t<T>>) {
        return formatCheckValue(reinterpret_cast<const void*>(value));
    } else if constexpr (std::is_pointer_v<T>) {
        return formatCheckValue(
                const_cast<const void*>(
                        static_cast<const volatile void*>(value)));
    } else {
        return "<unprintable>";
    }
}

[[noreturn]] void throwCheckFailed(
        ZL_ErrorCode code,
        std::string_view check,
        std::string_view message,
        const char* file,
        int line,
        poly::source_location location);

[[noreturn]] void throwFormattedComparisonFailed(
        ZL_ErrorCode code,
        std::string_view check,
        const std::string& lhs,
        const std::string& rhs,
        std::string_view message,
        const char* file,
        int line,
        poly::source_location location);

/// Formats both operands on the failure path only.
template <typename L, typename R>
[[noreturn]] void throwComparisonFailed(
        ZL_ErrorCode code,
        std::string_view check,
        const L& lhs,
        const R& rhs,
        std::string_view message,
        const char* file,
        int line,
        poly::source_location location)
{
    throwFormattedComparisonFailed(
            code,
            check,
            formatCheckValue(lhs),
            formatCheckValue(rhs),
            message,
            file,
            line,
            location);
}

} // namespace detail
} // namespace openzl::gpu
