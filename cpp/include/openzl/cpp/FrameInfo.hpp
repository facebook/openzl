// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/cpp/Type.hpp"
#include "openzl/cpp/detail/NonNullUniqueCPtr.hpp"
#include "openzl/cpp/poly/StringView.hpp"
#include "openzl/zl_decompress.h"

namespace openzl {

class FrameInfo {
   public:
    explicit FrameInfo(poly::string_view compressed);

    FrameInfo(const FrameInfo&) = delete;
    FrameInfo(FrameInfo&&)      = default;

    FrameInfo& operator=(const FrameInfo&) = delete;
    FrameInfo& operator=(FrameInfo&&)      = default;

    ~FrameInfo() = default;

    const ZL_FrameInfo* get() const
    {
        return info_.get();
    }

    size_t formatVersion() const;
    size_t numOutputs() const;
    Type outputType(size_t index) const;
    size_t outputContentSize(size_t index) const;
    poly::string_view comment() const;

    template <typename ResultType>
    typename ResultType::ValueType unwrap(
            ResultType result,
            poly::string_view msg = {},
            poly::source_location location =
                    poly::source_location::current()) const
    {
        return openzl::unwrap(
                result, std::move(msg), nullptr, std::move(location));
    }

   private:
    detail::NonNullUniqueCPtr<ZL_FrameInfo> info_;
};

} // namespace openzl
