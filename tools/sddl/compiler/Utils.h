// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tools/sddl/compiler/Exception.h"
#include "tools/sddl/compiler/Source.h"

namespace openzl::sddl::detail {

template <typename Ptr>
typename Ptr::element_type& some(const Ptr& ptr)
{
    if (!ptr) {
        throw InvariantViolation("Got NULL pointer! Panic!");
    }
    return *ptr;
}

template <typename Ptr>
SourceLocation maybe_loc(const Ptr& ptr)
{
    return ptr ? ptr->loc() : SourceLocation::null();
}

template <typename Collection>
SourceLocation join_locs(const Collection& nodes)
{
    auto loc = SourceLocation::null();
    for (const auto& node : nodes) {
        loc += maybe_loc(node);
    }
    return loc;
}

// Workaround for missing constexpr discard of static_assert(false) in pre-CWG
// Issue 2518 implementations of C++.
template <typename>
constexpr bool dependent_false_v = false;

} // namespace openzl::sddl::detail
