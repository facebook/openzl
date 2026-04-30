// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "openzl/cpp/poly/Optional.hpp"
#include "openzl/cpp/poly/SourceLocation.hpp"
#include "openzl/cpp/poly/StringView.hpp"

#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_compressor_serialization.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_errors.h"

namespace openzl {

class Exception : public std::runtime_error {
   public:
    explicit Exception(
            poly::string_view msg,
            poly::source_location location = poly::source_location::current());

    /// @see ExceptionBuilder
    Exception(
            poly::string_view msg,
            poly::optional<ZL_Error> error,
            poly::string_view errorContext,
            poly::source_location location);

    poly::string_view msg() const noexcept
    {
        return msg_;
    }

    poly::optional<ZL_ErrorCode> code() const noexcept
    {
        if (error_) {
            return ZL_E_code(error_.value());
        } else {
            return {};
        }
    }

    template <typename Ctx>
    ZL_Error toError(
            Ctx* ctx,
            poly::source_location this_location =
                    poly::source_location::current()) const noexcept
    {
        if (!error_) {
            return ZL_E_create(
                    nullptr,
                    ZL_GET_DEFAULT_ERROR_CONTEXT(ctx),
                    location_.file_name(),
                    location_.function_name(),
                    location_.line(),
                    ZL_ErrorCode_GENERIC,
                    "C++ OpenZL Exception: %s",
                    what());
        }
        auto e            = error_.value();
        const auto* opCtx = ZL_GET_OPERATION_CONTEXT(ctx);
        if (ZL_OperationContext_ownsError(opCtx, e)) {
            e = ZL_E_addFrame(
                    nullptr,
                    e,
                    ZL_EE_EMPTY,
                    location_.file_name(),
                    location_.function_name(),
                    location_.line(),
                    "Converting to C++ OpenZL Exception: %s",
                    msg_.c_str());
            e = ZL_E_addFrame(
                    nullptr,
                    e,
                    ZL_EE_EMPTY,
                    this_location.file_name(),
                    this_location.function_name(),
                    this_location.line(),
                    "Converting back to ZL_Error");
        }
        return e;
    }

    template <typename Ctx>
    ZL_Error toError(
            Ctx& ctx,
            poly::source_location this_location =
                    poly::source_location::current()) const noexcept
    {
        return toError(ctx.get(), std::move(this_location));
    }

    ZL_Error toError(
            poly::source_location this_location =
                    poly::source_location::current()) const noexcept
    {
        return toError((ZL_CCtx*)nullptr, std::move(this_location));
    }

    poly::string_view errorContext() const noexcept
    {
        return errorContext_;
    }

    const poly::source_location& location() const noexcept
    {
        return location_;
    }

   private:
    std::string msg_;
    poly::optional<ZL_Error> error_;
    std::string errorContext_;
    poly::source_location location_;
};

class ExceptionBuilder {
   public:
    explicit ExceptionBuilder(
            poly::string_view msg          = {},
            poly::source_location location = poly::source_location::current())
            : msg_(std::move(msg)), location_(std::move(location))
    {
    }

    template <typename ResultT>
    ExceptionBuilder&& withResult(ResultT result) && noexcept
    {
        if (ZL_RES_isError(result)) {
            // ExceptionBuilder should only be called with one call to one of
            // withResult() or withErrorCode().
            assert(!error_);
            error_ = ZL_RES_error(result);
        }
        return std::move(*this);
    }

    ExceptionBuilder&& withErrorContext(
            poly::string_view errorContext) && noexcept
    {
        errorContext_ = errorContext;
        return std::move(*this);
    }

    // Must come *after* withResult()
    // Also works with raw ctx objects via specializations below.
    template <typename Ctx>
    ExceptionBuilder&& addErrorContext(const Ctx* ctx) && noexcept
    {
        if (ctx != nullptr && error_.has_value()) {
            return std::move(*this).withErrorContext(
                    ctx->getErrorContextString(error_.value()));
        } else {
            return std::move(*this);
        }
    }

    ExceptionBuilder&& withErrorCode(ZL_ErrorCode code) && noexcept
    {
        // ExceptionBuilder should only be called with one call to one of
        // withResult() or withErrorCode().
        assert(!error_);
        error_ = (ZL_Error){ ._code = code, ._info = {} };
        return std::move(*this);
    }

    ExceptionBuilder&& addErrorContext(std::nullptr_t) && noexcept
    {
        return std::move(*this);
    }

    Exception build() && noexcept;

   private:
    poly::string_view msg_;
    poly::source_location location_;
    poly::optional<ZL_Error> error_;
    poly::string_view errorContext_;
};

// Declarations of alternate specializations of addErrorContext() for
// non-wrapped i.e. raw C context objects.
template <>
ExceptionBuilder&& ExceptionBuilder::addErrorContext(
        const ZL_CCtx* cctx) && noexcept;

template <>
ExceptionBuilder&& ExceptionBuilder::addErrorContext(
        const ZL_Compressor* compressor) && noexcept;

template <>
ExceptionBuilder&& ExceptionBuilder::addErrorContext(
        const ZL_DCtx* dctx) && noexcept;

template <>
ExceptionBuilder&& ExceptionBuilder::addErrorContext(
        const ZL_CompressorSerializer* serializer) && noexcept;

template <>
ExceptionBuilder&& ExceptionBuilder::addErrorContext(
        const ZL_CompressorDeserializer* deserializer) && noexcept;

template <>
ExceptionBuilder&& ExceptionBuilder::addErrorContext(
        const ZL_Graph* graph) && noexcept;

/**
 * Helper free function to (possibly) convert a result into an exception and
 * throw it.
 *
 * @returns the value in the result, if it contained a value.
 */
template <typename ResultType, typename Ctx = std::nullptr_t>
typename ResultType::ValueType unwrap(
        ResultType result,
        poly::string_view msg          = {},
        Ctx ctx                        = nullptr,
        poly::source_location location = poly::source_location::current())
{
    if (ZL_RES_isError(result)) {
        throw ExceptionBuilder(std::move(msg), std::move(location))
                .withResult(result)
                .addErrorContext(ctx)
                .build();
    }
    return std::move(ZL_RES_value(result));
}

class CCtx;
class Compressor;
class DCtx;

ZL_Error_Array get_warnings(const ZL_CCtx* const& cctx);
ZL_Error_Array get_warnings(const ZL_Compressor* const& compressor);
ZL_Error_Array get_warnings(const ZL_DCtx* const& dctx);
ZL_Error_Array get_warnings(const CCtx& cctx);
ZL_Error_Array get_warnings(const Compressor& compressor);
ZL_Error_Array get_warnings(const DCtx& dctx);

std::string warning_str(const ZL_CCtx* const& cctx, const ZL_Error& error);
std::string warning_str(
        const ZL_Compressor* const& compressor,
        const ZL_Error& error);
std::string warning_str(const ZL_DCtx* const& dctx, const ZL_Error& error);
std::string warning_str(const CCtx& cctx, const ZL_Error& error);
std::string warning_str(const Compressor& compressor, const ZL_Error& error);
std::string warning_str(const DCtx& dctx, const ZL_Error& error);

template <typename Ctx>
std::vector<std::pair<ZL_Error, std::string>> get_warning_strings(
        const Ctx& ctx)
{
    const ZL_Error_Array warnings = get_warnings(ctx);
    std::vector<std::pair<ZL_Error, std::string>> warnings_with_strings;
    for (size_t i = 0; i < warnings.size; i++) {
        const auto& warning = warnings.errors[i];
        auto wstr           = warning_str(ctx, warning);
        warnings_with_strings.emplace_back(warning, std::move(wstr));
    }
    return warnings_with_strings;
}

} // namespace openzl
