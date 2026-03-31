// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/Exception.hpp"

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"

namespace openzl {
namespace {
std::string formatMsg(
        std::string_view msg,
        const poly::optional<ZL_ErrorCode>& code,
        poly::string_view errorContext,
        const poly::source_location& location)
{
    std::string out;

    if (!msg.empty()) {
        out += "Message: ";
        out += msg;
        out += '\n';
    }
    if (code.has_value()) {
        out += "OpenZL error code: ";
        out += std::to_string(code.value());
        out += "\nOpenZL error string: ";
        out += ZL_ErrorCode_toString(code.value());
        out += "\n";
    }
    if (!errorContext.empty()) {
        out += "OpenZL error context: ";
        out += errorContext;
        out += "\n";
    }
    if constexpr (detail::kHasSourceLocation) {
        out += "\nLocation: ";
        out += location.function_name();
        out += " @ ";
        out += location.file_name();
        out += ":";
        out += std::to_string(location.line());
        out += ":";
        out += std::to_string(location.column());
        out += "\n";
    }
    for (auto& c : out) {
        if (c < 0) {
            c = '?';
        }
    }
    return out;
}
} // namespace

Exception::Exception(poly::string_view msg, poly::source_location location)
        : Exception(std::move(msg), poly::nullopt, {}, std::move(location))
{
}

Exception::Exception(
        poly::string_view msg,
        poly::optional<ZL_ErrorCode> code,
        poly::string_view errorContext,
        poly::source_location location)
        : std::runtime_error(formatMsg(msg, code, errorContext, location)),
          msg_(msg),
          code_(std::move(code)),
          errorContext_(errorContext),
          location_(std::move(location))
{
}

template <>
ExceptionBuilder&& ExceptionBuilder::addErrorContext<ZL_CCtx>(
        ZL_CCtx const* const ctx) && noexcept
{
    if (ctx != nullptr && error_.has_value()) {
        return std::move(*this).withErrorContext(
                ZL_CCtx_getErrorContextString_fromError(ctx, error_.value()));
    } else {
        return std::move(*this);
    }
}

template <>
ExceptionBuilder&& ExceptionBuilder::addErrorContext<ZL_Compressor>(
        ZL_Compressor const* const ctx) && noexcept
{
    if (ctx != nullptr && error_.has_value()) {
        return std::move(*this).withErrorContext(
                ZL_Compressor_getErrorContextString_fromError(
                        ctx, error_.value()));
    } else {
        return std::move(*this);
    }
}

template <>
ExceptionBuilder&& ExceptionBuilder::addErrorContext<ZL_DCtx>(
        ZL_DCtx const* const ctx) && noexcept
{
    if (ctx != nullptr && error_.has_value()) {
        return std::move(*this).withErrorContext(
                ZL_DCtx_getErrorContextString_fromError(ctx, error_.value()));
    } else {
        return std::move(*this);
    }
}

template <>
ExceptionBuilder&& ExceptionBuilder::addErrorContext<ZL_CompressorSerializer>(
        ZL_CompressorSerializer const* const ctx) && noexcept
{
    if (ctx != nullptr && error_.has_value()) {
        return std::move(*this).withErrorContext(
                ZL_CompressorSerializer_getErrorContextString_fromError(
                        ctx, error_.value()));
    } else {
        return std::move(*this);
    }
}

template <>
ExceptionBuilder&& ExceptionBuilder::addErrorContext<ZL_CompressorDeserializer>(
        ZL_CompressorDeserializer const* const ctx) && noexcept
{
    if (ctx != nullptr && error_.has_value()) {
        return std::move(*this).withErrorContext(
                ZL_CompressorDeserializer_getErrorContextString_fromError(
                        ctx, error_.value()));
    } else {
        return std::move(*this);
    }
}

Exception ExceptionBuilder::build() && noexcept
{
    poly::optional<ZL_ErrorCode> code;
    if (error_.has_value()) {
        code.emplace(ZL_E_code(error_.value()));
    }
    return Exception(msg_, code, errorContext_, std::move(location_));
}

ZL_Error_Array get_warnings(const ZL_CCtx* const& cctx)
{
    return ZL_CCtx_getWarnings(cctx);
}

ZL_Error_Array get_warnings(const ZL_Compressor* const& compressor)
{
    return ZL_Compressor_getWarnings(compressor);
}

ZL_Error_Array get_warnings(const ZL_DCtx* const& dctx)
{
    return ZL_DCtx_getWarnings(dctx);
}

ZL_Error_Array get_warnings(const CCtx& cctx)
{
    return get_warnings(cctx.get());
}

ZL_Error_Array get_warnings(const Compressor& compressor)
{
    return get_warnings(compressor.get());
}

ZL_Error_Array get_warnings(const DCtx& dctx)
{
    return get_warnings(dctx.get());
}

std::string warning_str(const ZL_CCtx* const& cctx, const ZL_Error& error)
{
    return std::string{ ZL_CCtx_getErrorContextString_fromError(cctx, error) };
}

std::string warning_str(
        const ZL_Compressor* const& compressor,
        const ZL_Error& error)
{
    return std::string{ ZL_Compressor_getErrorContextString_fromError(
            compressor, error) };
}

std::string warning_str(const ZL_DCtx* const& dctx, const ZL_Error& error)
{
    return std::string{ ZL_DCtx_getErrorContextString_fromError(dctx, error) };
}

std::string warning_str(const CCtx& cctx, const ZL_Error& error)
{
    return warning_str(cctx.get(), error);
}

std::string warning_str(const Compressor& compressor, const ZL_Error& error)
{
    return warning_str(compressor.get(), error);
}

std::string warning_str(const DCtx& dctx, const ZL_Error& error)
{
    return warning_str(dctx.get(), error);
}
} // namespace openzl
