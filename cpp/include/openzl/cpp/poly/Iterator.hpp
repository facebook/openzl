// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <initializer_list>
#include <iterator>

#include "openzl/cpp/detail/Portability.hpp"

namespace openzl {
namespace poly {

namespace impl {
template <class C>
auto data(C& c) -> decltype(c.data())
{
    return c.data();
}

template <class C>
constexpr auto data(const C& c) -> decltype(c.data())
{
    return c.data();
}

template <class T, std::size_t N>
constexpr T* data(T (&array)[N]) noexcept
{
    return array;
}

template <class E>
constexpr const E* data(std::initializer_list<E> il) noexcept
{
    return il.begin();
}

template <class C>
std::size_t size(const C& c)
{
    return c.size();
}

template <class T, std::size_t N>
constexpr std::size_t size(T (&array)[N]) noexcept
{
    (void)array;
    return N;
}

} // namespace impl

#if ZL_CPP_HAS_NONMEMBER_CONTAINER_ACCESS
using std::data;
using std::size;
#else
using impl::data;
using impl::size;
#endif

} // namespace poly
} // namespace openzl
