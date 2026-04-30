// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <assert.h>
#include <memory>

#include "openzl/zl_common_types.h"

namespace openzl {

template <typename T>
ZL_OpaquePtr moveToOpaquePtr(std::shared_ptr<const T> ptr)
{
    auto raw = ptr.get();
    return ZL_OpaquePtr{ .ptr = const_cast<void*>((const void*)raw),
                         .freeOpaquePtr =
                                 new std::shared_ptr<const T>(std::move(ptr)),
                         .freeFn = [](void* freeOpaquePtr,
                                      void* opaquePtr) noexcept {
                             assert(freeOpaquePtr != nullptr);
                             auto sharedPtr =
                                     static_cast<std::shared_ptr<const T>*>(
                                             freeOpaquePtr);
                             assert(sharedPtr->get() == opaquePtr);
                             sharedPtr->reset();
                             delete sharedPtr;
                         } };
}

template <typename T, typename D>
ZL_OpaquePtr moveToOpaquePtr(std::unique_ptr<T, D> ptr)
{
    return ZL_OpaquePtr{ .ptr    = ptr.release(),
                         .freeFn = [](void*, void* opaquePtr) noexcept {
                             std::unique_ptr<T, D> uniquePtr(
                                     static_cast<T*>(opaquePtr));
                             uniquePtr.reset();
                         } };
}

} // namespace openzl
