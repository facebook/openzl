// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <map>
#include "openzl/cpp/CParam.hpp"
#include "openzl/cpp/CompressIntrospectionHooks.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/Input.hpp"
#include "openzl/cpp/detail/NonNullUniqueCPtr.hpp"
#include "openzl/cpp/poly/Optional.hpp"
#include "openzl/cpp/poly/Span.hpp"
#include "openzl/cpp/poly/StringView.hpp"
#include "openzl/zl_opaque_types.h"

namespace openzl {
// TODO(terrelln): Move this?
size_t compressBound(size_t totalSize);

namespace visualizer {

class CompressionTraceHooks; // forward declaration

} // namespace visualizer

class CCtx {
   public:
    /**
     * Creates a new CCtx object owned by the @p CCtx object.
     * @throws on allocation failure.
     */
    CCtx();

    CCtx(const CCtx&) = delete;
    // Can't be declared default in the header because the forward-declared
    // CompressionTraceHooks is an incomplete type here.
    CCtx(CCtx&&) noexcept; /* = default; */

    CCtx& operator=(const CCtx&) = delete;
    CCtx& operator=(CCtx&&) noexcept; /* = default; */

    ~CCtx(); /* = default; */

    /// @returns pointer to the underlying ZL_CCtx* object.
    ZL_CCtx* get()
    {
        return cctx_.get();
    }
    /// @returns const pointer to the underlying ZL_CCtx* object.
    const ZL_CCtx* get() const
    {
        return cctx_.get();
    }

    void refCompressor(const Compressor& compressor);

    void setParameter(CParam param, int value);
    int getParameter(CParam) const;
    void resetParameters();

    size_t compress(poly::span<char> output, poly::span<const Input> inputs);
    std::string compress(poly::span<const Input> inputs);

    size_t compressOne(poly::span<char> output, const Input& input);
    std::string compressOne(const Input& input);

    size_t compressSerial(poly::span<char> output, poly::string_view input);
    std::string compressSerial(poly::string_view input);

    poly::string_view getErrorContextString(ZL_Error error) const;

    template <typename ResultType>
    poly::string_view getErrorContextString(ResultType result) const
    {
        return getErrorContextString(ZL_RES_error(result));
    }

    template <typename ResultType>
    typename ResultType::ValueType unwrap(
            ResultType result,
            poly::string_view msg = {},
            poly::source_location location =
                    poly::source_location::current()) const
    {
        return openzl::unwrap(
                result, std::move(msg), this, std::move(location));
    }

    void selectStartingGraph(
            GraphID graph,
            const poly::optional<GraphParameters>& params = poly::nullopt);

    void selectStartingGraph(
            const Compressor& compressor,
            GraphID graph,
            const poly::optional<GraphParameters>& params = poly::nullopt);

    void writeTraces(bool enabled, bool streamPreview = true);

    /**
     * @returns a pair of the latest trace, and a map from internal stream IDs
     * to a pair of the raw stream buffer, and a buffer to the string lengths of
     * the corresponding stream if it is a string stream, otherwise "".
     */
    std::pair<
            poly::string_view,
            std::map<
                    std::string,
                    std::pair<poly::string_view, poly::string_view>>>
    getLatestTrace();

   protected:
    CCtx(ZL_CCtx* cctx, detail::NonNullUniqueCPtr<ZL_CCtx>::DeleterFn deleter);

   private:
    detail::NonNullUniqueCPtr<ZL_CCtx> cctx_;
    std::unique_ptr<CompressIntrospectionHooks> visHooks_{ nullptr };
};

class CCtxRef : public CCtx {
   public:
    explicit CCtxRef(ZL_CCtx* cctx) : CCtx(cctx, nullptr) {}
};

} // namespace openzl
