// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <random>
#include <type_traits>

// MSVC's std::uniform_int_distribution, binomial_distribution,
// geometric_distribution don't support uint8_t, int8_t, etc. per the C++
// standard N4950 [rand.req.genl]/1.5. GCC/Clang are more permissive and allow
// these types.
//
// Solution: On MSVC, promote small types to short/unsigned short, use std::,
// then cast back. This is simple and avoids potential bugs in custom
// implementations.

namespace openzl::tests::datagen {

// Helper: map small types to larger ones that MSVC supports
template <typename T>
struct promoted_int_type {
    using type = std::conditional_t<
            sizeof(T) < sizeof(short),
            std::conditional_t<std::is_signed_v<T>, short, unsigned short>,
            T>;
};
template <typename T>
using promoted_int_type_t = typename promoted_int_type<T>::type;

#if defined(_MSC_VER)

// Wrapper for uniform_int_distribution that handles small types on MSVC
template <typename IntType>
class compat_uniform_int_distribution {
    using LargerType = promoted_int_type_t<IntType>;
    std::uniform_int_distribution<LargerType> dist_;

   public:
    using result_type = IntType;

    compat_uniform_int_distribution() : dist_() {}
    explicit compat_uniform_int_distribution(
            IntType a,
            IntType b = std::numeric_limits<IntType>::max())
            : dist_(static_cast<LargerType>(a), static_cast<LargerType>(b))
    {
    }

    template <typename Gen>
    IntType operator()(Gen& g)
    {
        return static_cast<IntType>(dist_(g));
    }

    IntType min() const
    {
        return static_cast<IntType>(dist_.min());
    }
    IntType max() const
    {
        return static_cast<IntType>(dist_.max());
    }
};

// Wrapper for binomial_distribution that handles small types on MSVC
template <typename IntType>
class compat_binomial_distribution {
    using LargerType = promoted_int_type_t<IntType>;
    std::binomial_distribution<LargerType> dist_;

   public:
    using result_type = IntType;

    compat_binomial_distribution() = default;
    explicit compat_binomial_distribution(IntType t, double p = 0.5)
            : dist_(static_cast<LargerType>(t), p)
    {
    }

    template <typename Gen>
    IntType operator()(Gen& g)
    {
        return static_cast<IntType>(dist_(g));
    }
};

// Wrapper for geometric_distribution that handles small types on MSVC
template <typename IntType>
class compat_geometric_distribution {
    using LargerType = promoted_int_type_t<IntType>;
    std::geometric_distribution<LargerType> dist_;

   public:
    using result_type = IntType;

    compat_geometric_distribution() = default;
    explicit compat_geometric_distribution(double p) : dist_(p) {}

    template <typename Gen>
    IntType operator()(Gen& g)
    {
        return static_cast<IntType>(dist_(g));
    }
};

#else
// On GCC/Clang, use std:: which already supports these types

template <typename IntType>
using compat_uniform_int_distribution = std::uniform_int_distribution<IntType>;

template <typename IntType>
using compat_binomial_distribution = std::binomial_distribution<IntType>;

template <typename IntType>
using compat_geometric_distribution = std::geometric_distribution<IntType>;

#endif

} // namespace openzl::tests::datagen
