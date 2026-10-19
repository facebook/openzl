// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <type_traits>
#include <utility>
#include <variant>

#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_portability.h"

namespace openzl::gpu {

/**
 * A value of type @p T, or a @c ZL_Error.
 *
 * Use @c Result<T> for a return value that @c ZL_RESULT_OF cannot hold.
 * @c ZL_RESULT_DECLARE_TYPE builds a C union, so it holds only trivial types,
 * and not a type with a @c std::vector member. Use @c ZL_RESULT_OF for a
 * trivial type.
 *
 * A @c Result<T> converts from an error @c ZL_Report. So a function that
 * returns @c Result<T> declares @c ZL_RESULT_DECLARE_SCOPE_REPORT and uses the
 * core @c ZL_ERR_IF_*, @c ZL_ERR, and @c ZL_TRY_LET_CONST macros unchanged:
 *
 *     Result<Plan> makePlan(ZL_DCtx* dctx)
 *     {
 *         ZL_RESULT_DECLARE_SCOPE_REPORT(dctx);
 *         ZL_ERR_IF_NULL(dctx, parameter_invalid);
 *         Plan plan;
 *         // ...
 *         return plan;
 *     }
 *
 * A caller forwards an error with @c ZL_ERR_IF_ERR(result.report()).
 *
 * A @c Result<T> keeps an error's code and static info, but not its dynamic
 * info. Dynamic info lives in the operation context that created the error,
 * often a @c ZL_DCtx that the returning function frees. So a function that
 * frees a @c ZL_DCtx can return that context's errors as @c Result<T>, and a
 * caller never reads or writes the freed context through them.
 */
template <typename T>
class ZL_NODISCARD Result {
    static_assert(
            !std::is_trivially_copyable_v<T>,
            "Use ZL_Report or ZL_RESULT_OF for a trivially copyable type");

   public:
    // Under C++17 rules, `return local;` moves the local only into a
    // constructor whose parameter is `T&&`. A by-value parameter copies it.
    // No `const T&` overload exists, so an accidental copy does not compile.
    /* implicit */ Result(T&& value)
            : storage_(std::in_place_index<0>, std::move(value))
    {
    }

    /**
     * Holds the error in @p report. A success @p report becomes a
     * @c logicError, which stops debug builds as the core does.
     */
    /* implicit */ Result(ZL_Report report)
            : storage_(std::in_place_index<1>, errorOf(report))
    {
    }

    bool isError() const
    {
        return storage_.index() == 1;
    }

    /// @returns The error, or a success @c ZL_Report if this holds a value.
    ZL_Report report() const
    {
        if (!isError()) {
            return ZL_returnSuccess();
        }
        return ZL_RESULT_WRAP_ERROR(size_t, *std::get_if<1>(&storage_));
    }

    /// Requires that this holds a value, and aborts if it holds an error.
    T& value() &
    {
        ZL_REQUIRE(!isError(), "Result<T>::value() needs a value");
        return *std::get_if<0>(&storage_);
    }

    /// Requires that this holds a value, and aborts if it holds an error.
    const T& value() const&
    {
        ZL_REQUIRE(!isError(), "Result<T>::value() needs a value");
        return *std::get_if<0>(&storage_);
    }

    /// Requires that this holds a value, and aborts if it holds an error.
    T&& value() &&
    {
        ZL_REQUIRE(!isError(), "Result<T>::value() needs a value");
        return std::move(*std::get_if<0>(&storage_));
    }

   private:
    static ZL_Error errorOf(ZL_Report report)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(nullptr);
        if (!ZL_RES_isError(report)) {
            return ZL_E(logicError, "Result<T> needs an error ZL_Report");
        }
        ZL_Error error = ZL_RES_error(report);
        // ZL_E_dy reads only the tag bit of the info pointer, so this is safe
        // even if the context that owns the info is already freed.
        if (ZL_E_dy(error) != nullptr) {
            ZL_E_clearInfo(&error);
        }
        return error;
    }

    std::variant<T, ZL_Error> storage_;
};

} // namespace openzl::gpu
