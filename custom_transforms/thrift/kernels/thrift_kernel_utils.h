// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include <assert.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <span>

#include "custom_transforms/thrift/kernels/encode_thrift_kernel.h"

namespace zstrong::thrift {
namespace detail {
template <typename T>
struct DynamicOutputTypes {};

template <>
struct DynamicOutputTypes<uint32_t> {
    using CSlice = ZL_ThriftKernel_Slice32;
    using CType  = ZL_ThriftKernel_DynamicOutput32;
};

template <>
struct DynamicOutputTypes<uint64_t> {
    using CSlice = ZL_ThriftKernel_Slice64;
    using CType  = ZL_ThriftKernel_DynamicOutput64;
};
} // namespace detail

template <typename T, typename Impl>
class DynamicOutputBase {
   public:
    using ValueType = T;
    using CSlice    = typename detail::DynamicOutputTypes<ValueType>::CSlice;
    using CType     = typename detail::DynamicOutputTypes<ValueType>::CType;

    CType asCType()
    {
        return CType{ this, cNext, cFinish };
    }

   private:
    static CSlice cNext(void* opaque, size_t i, size_t size)
    {
        auto impl = static_cast<Impl*>(opaque);
        auto span = impl->next(i, size);
        return { span.data(), span.data() + span.size() };
    }

    static void cFinish(void* opaque, ValueType* ptr)
    {
        auto impl = static_cast<Impl*>(opaque);
        return impl->finish(ptr);
    }
};

/**
 * C++ helper class implementing ZL_ThriftKernel_DynamicOutput{32,64}.
 * You construct the class, pass the return value of .asCType() to C,
 * then call .written() to get the vector that is written.
 */
template <typename T>
class VectorDynamicOutput
        : public DynamicOutputBase<T, VectorDynamicOutput<T>> {
   public:
    explicit VectorDynamicOutput(
            size_t minChunkSize = 1024,
            size_t maxChunkSize = 1024 * 1024)
            : minChunkSize_(minChunkSize), maxChunkSize_(maxChunkSize)
    {
    }

    std::vector<T>&& written() &&
    {
        assert(storage_.size() == written_);
        return std::move(storage_);
    }

   private:
    friend class DynamicOutputBase<T, VectorDynamicOutput<T>>;

    std::span<T> next(size_t i, size_t size)
    {
        // Commit the previous chunk
        written_ = storage_.size();

        // Try to guess how much to allocate based on our progress so far.
        // But clamp at min/max chunk size to make sure our memory allocation
        // isn't crazy.
        size_t const expected = i == 0
                ? 0
                : (size_t)((double)written_ * (double)size / (double)i);
        size_t const actual   = std::clamp(
                expected, written_ + minChunkSize_, written_ + maxChunkSize_);
        storage_.resize(actual);

        return { storage_.data() + written_,
                 storage_.data() + storage_.size() };
    }

    void finish(T* ptr)
    {
        if (ptr == NULL) {
            assert(storage_.empty());
            assert(written_ == 0);
            return;
        }

        assert(ptr >= storage_.data() + written_);
        size_t const size = (size_t)(ptr - storage_.data());
        assert(size >= written_);
        assert(size <= storage_.size());
        written_ = size;
        storage_.resize(size);
    }

    std::vector<T> storage_;
    size_t written_{ 0 };
    size_t minChunkSize_;
    size_t maxChunkSize_;
};

template <typename T>
class ZeroCopyDynamicOutput
        : public DynamicOutputBase<T, ZeroCopyDynamicOutput<T>> {
   public:
    explicit ZeroCopyDynamicOutput(
            size_t minChunkSize = 1024,
            size_t maxChunkSize = 1024 * 1024)
            : minChunkSize_(minChunkSize), maxChunkSize_(maxChunkSize)
    {
        assert(minChunkSize > 0);
    }

    size_t size() const
    {
        return size_;
    }

    size_t nbytes() const
    {
        return size_ * sizeof(T);
    }

    void copyToBuffer(void* dst, size_t dstCapacity) const
    {
        if (dstCapacity < nbytes()) {
            throw std::runtime_error("not enough output space!");
        }
        uint8_t* op      = (uint8_t*)dst;
        size_t remaining = size();
        for (auto const& [ptr, size] : storage_) {
            assert(size > 0);
            size_t const toCopy = std::min(remaining, size);
            std::memcpy(op, ptr.get(), toCopy * sizeof(T));
            op += toCopy * sizeof(T);
            remaining -= toCopy;
        }
        assert(remaining == 0);
    }

   private:
    friend class DynamicOutputBase<T, ZeroCopyDynamicOutput<T>>;

    std::span<T> next(size_t i, size_t size)
    {
        if (!storage_.empty()) {
            size_ += storage_.back().second;
        }

        // Try to guess how much to allocate based on our progress so far.
        // But clamp at min/max chunk size to make sure our memory allocation
        // isn't crazy.
        size_t const expected = i == 0
                ? 0
                : (size_t)((double)this->size() * (double)(size - i)
                           / (double)i);
        size_t const actual =
                std::clamp(expected, minChunkSize_, maxChunkSize_);
        storage_.emplace_back(std::make_unique<T[]>(actual), actual);

        auto ptr = storage_.back().first.get();

        return { ptr, actual };
    }

    void finish(T* ptr)
    {
        if (ptr == NULL) {
            assert(storage_.empty());
            assert(size_ == 0);
            return;
        }
        assert(!storage_.empty());

        auto const& [data, size] = storage_.back();
        assert(ptr >= data.get());
        assert(ptr <= data.get() + size);
        size_t const written = (size_t)(ptr - data.get());

        size_ += written;
    }

    std::vector<std::pair<std::unique_ptr<T[]>, size_t>> storage_;
    size_t size_{ 0 };
    size_t minChunkSize_;
    size_t maxChunkSize_;
};

} // namespace zstrong::thrift
