// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <stdint.h>
#include <memory>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "openzl/cpp/poly/Span.hpp"
#include "openzl/zl_localParams.h"

namespace openzl {

/**
 * Provides a safe wrapper on top of ZL_LocalParams that reference stability of
 * the parameters, and eases dynamically appending params.
 */
class LocalParams {
   public:
    LocalParams() = default;

    LocalParams(const LocalParams& other);
    LocalParams(LocalParams&&) = default;

    LocalParams& operator=(const LocalParams& other);
    LocalParams& operator=(LocalParams&&) = default;

    ~LocalParams() = default;

    /**
     * Copies parameters from @p params, except that duplicate keys are
     * disallowed. Each parameter, no matter the type, must have a unique key.
     *
     * @throws if the parameters have duplicate keys, or on allocation failures.
     */
    explicit LocalParams(const ZL_LocalParams& params);

    const ZL_LocalParams* get() const
    {
        return &params_;
    }
    const ZL_LocalParams& operator*() const
    {
        return *get();
    }
    const ZL_LocalParams* operator->() const
    {
        return get();
    }

    void addIntParam(ZL_IntParam param);
    void addIntParam(int key, int value);

    /// NOTE: Copies the param immediately
    void addCopyParam(ZL_CopyParam param);
    /// NOTE: Copies the param immediately
    void addCopyParam(int key, const void* valuePtr, size_t valueSize);
    template <typename T>
    void addCopyParam(int key, const T& value)
    {
        static_assert(
                std::is_standard_layout<T>{} && std::is_trivial<T>{},
                "Must be POD");
        addCopyParam(key, &value, sizeof(value));
    }

    void addRefParam(ZL_RefParam param);
    void addRefParam(int key, const void* ref);

    poly::span<const ZL_IntParam> getIntParams() const
    {
        return intParams_;
    }

    poly::span<const ZL_CopyParam> getCopyParams() const
    {
        return copyParams_;
    }

    poly::span<const ZL_RefParam> getRefParams() const
    {
        return refParams_;
    }

   private:
    void insertKeyOrThrow(int key);

    ZL_LocalParams params_{};
    std::vector<ZL_IntParam> intParams_;
    std::vector<ZL_CopyParam> copyParams_;
    std::vector<ZL_RefParam> refParams_;
    std::unordered_set<int> keys_;
    std::vector<std::unique_ptr<uint8_t[]>> storage_;
};

} // namespace openzl
