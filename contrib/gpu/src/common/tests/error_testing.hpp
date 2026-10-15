// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <optional>
#include <string>
#include <utility>

#include "openzl/cpp/Exception.hpp"
#include "openzl/zl_errors.h"

namespace openzl::gpu {

/**
 * @returns The code of the @c openzl::Exception that @p fn throws, or nullopt
 * when @p fn returns normally.
 *
 * Compare the result with the expected @c ZL_ErrorCode. A call that does not
 * throw then fails the assertion.
 */
template <typename Fn>
std::optional<ZL_ErrorCode> thrownErrorCode(Fn&& fn)
{
    try {
        std::forward<Fn>(fn)();
    } catch (const Exception& e) {
        return e.code();
    }
    return std::nullopt;
}

/**
 * @returns The message of the @c openzl::Exception that @p fn throws, or an
 * empty string when @p fn returns normally.
 */
template <typename Fn>
std::string thrownMessage(Fn&& fn)
{
    try {
        std::forward<Fn>(fn)();
    } catch (const Exception& e) {
        return std::string{ e.msg() };
    }
    return {};
}

} // namespace openzl::gpu
