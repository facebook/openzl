// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <istream>
#include <ostream>
#include <random>

#include "openzl/shared/bits.h"

namespace openzl::tests::datagen {

//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

template <size_t x>
struct log2 {
    enum { value = 1 + log2<x / 2>::value };
};

template <>
struct log2<1> {
    enum { value = 0 };
};

// from libcxx/include/iosfwd
template <class _CharT, class _Traits>
class save_flags {
    typedef std::basic_ios<_CharT, _Traits> __stream_type;
    typedef typename __stream_type::fmtflags fmtflags;

    __stream_type& __stream_;
    fmtflags __fmtflags_;
    _CharT __fill_;

   public:
    save_flags(const save_flags&)            = delete;
    save_flags& operator=(const save_flags&) = delete;

    inline explicit save_flags(__stream_type& __stream)
            : __stream_(__stream),
              __fmtflags_(__stream.flags()),
              __fill_(__stream.fill())
    {
    }
    inline ~save_flags()
    {
        __stream_.flags(__fmtflags_);
        __stream_.fill(__fill_);
    }
};

// from libcxx/include/__random/is_valid.h
// [rand.req.urng]/3:
// A class G meets the uniform random bit generator requirements if G models
// uniform_random_bit_generator, invoke_result_t<G&> is an unsigned integer
// type, and G provides a nested typedef-name result_type that denotes the same
// type as invoke_result_t<G&>. (In particular, reject URNGs with signed
// result_types; our distributions cannot handle such generator types.)

template <class, class = void>
struct __libcpp_random_is_valid_urng : std::false_type {};
template <class _Gp>
struct __libcpp_random_is_valid_urng<
        _Gp,
        std::enable_if_t<
                std::is_unsigned<typename _Gp::result_type>::value
                && std::is_same<
                        decltype(std::declval<_Gp&>()()),
                        typename _Gp::result_type>::value>> : std::true_type {};

// from libcxx/include/__random/uniform_int_distribution.h
template <class _Engine, class _UIntType>
class __independent_bits_engine {
   public:
    // types
    typedef _UIntType result_type;

   private:
    typedef typename _Engine::result_type _Engine_result_type;
    typedef std::conditional_t<
            sizeof(_Engine_result_type) <= sizeof(result_type),
            result_type,
            _Engine_result_type>
            _Working_result_type;

    _Engine& __e_;
    size_t __w_;
    size_t __w0_;
    size_t __n_;
    size_t __n0_;
    _Working_result_type __y0_;
    _Working_result_type __y1_;
    _Engine_result_type __mask0_;
    _Engine_result_type __mask1_;

    // in the LLVM code, this is _Working_result_type, but this doesn't work on
    // mac because the standard mt19937 limits overflows over to 0
    static constexpr const uint64_t _Rp =
            _Engine::max() - _Engine::min() + (1ull);
    static constexpr const size_t __m = log2<_Rp>::value;
    static constexpr const size_t _WDt =
            std::numeric_limits<_Working_result_type>::digits;
    static constexpr const size_t _EDt =
            std::numeric_limits<_Engine_result_type>::digits;

   public:
    // constructors and seeding functions
    inline __independent_bits_engine(_Engine& __e, size_t __w);

    // generating functions
    inline result_type operator()()
    {
        return __eval(std::integral_constant<bool, _Rp != 0>());
    }

   private:
    inline result_type __eval(std::false_type);
    inline result_type __eval(std::true_type);
};

template <class _Engine, class _UIntType>
__independent_bits_engine<_Engine, _UIntType>::__independent_bits_engine(
        _Engine& __e,
        size_t __w)
        : __e_(__e), __w_(__w)
{
    __n_  = __w_ / __m + (__w_ % __m != 0);
    __w0_ = __w_ / __n_;
    if (_Rp == 0)
        __y0_ = _Rp;
    else if (__w0_ < _WDt)
        __y0_ = (_Rp >> __w0_) << __w0_;
    else
        __y0_ = 0;
    if (_Rp - __y0_ > __y0_ / __n_) {
        ++__n_;
        __w0_ = __w_ / __n_;
        if (__w0_ < _WDt)
            __y0_ = (_Rp >> __w0_) << __w0_;
        else
            __y0_ = 0;
    }
    __n0_ = __n_ - __w_ % __n_;
    if (__w0_ < _WDt - 1)
        __y1_ = (_Rp >> (__w0_ + 1)) << (__w0_ + 1);
    else
        __y1_ = 0;
    __mask0_ = __w0_ > 0 ? _Engine_result_type(~0) >> (_EDt - __w0_)
                         : _Engine_result_type(0);
    __mask1_ = __w0_ < _EDt - 1
            ? _Engine_result_type(~0) >> (_EDt - (__w0_ + 1))
            : _Engine_result_type(~0);
}

template <class _Engine, class _UIntType>
inline _UIntType __independent_bits_engine<_Engine, _UIntType>::__eval(
        std::false_type)
{
    return static_cast<result_type>(__e_() & __mask0_);
}

template <class _Engine, class _UIntType>
_UIntType __independent_bits_engine<_Engine, _UIntType>::__eval(std::true_type)
{
    const size_t __w_rt = std::numeric_limits<result_type>::digits;
    result_type __sp    = 0;
    for (size_t __k = 0; __k < __n0_; ++__k) {
        _Engine_result_type __u;
        do {
            __u = __e_() - _Engine::min();
        } while (__u >= __y0_);
        if (__w0_ < __w_rt)
            __sp <<= __w0_;
        else
            __sp = 0;
        __sp += __u & __mask0_;
    }
    for (size_t __k = __n0_; __k < __n_; ++__k) {
        _Engine_result_type __u;
        do {
            __u = __e_() - _Engine::min();
        } while (__u >= __y1_);
        if (__w0_ < __w_rt - 1)
            __sp <<= __w0_ + 1;
        else
            __sp = 0;
        __sp += __u & __mask1_;
    }
    return __sp;
}

template <class _IntType = int>
class uniform_int_distribution {
    static_assert(
            std::is_integral<_IntType>::value,
            "IntType must be a supported integer type");

   public:
    // types
    typedef _IntType result_type;

    class param_type {
        result_type __a_;
        result_type __b_;

       public:
        typedef uniform_int_distribution distribution_type;

        inline explicit param_type(
                result_type __a = 0,
                result_type __b = std::numeric_limits<result_type>::max())
                : __a_(__a), __b_(__b)
        {
        }

        inline result_type a() const
        {
            return __a_;
        }
        inline result_type b() const
        {
            return __b_;
        }

        inline friend bool operator==(
                const param_type& __x,
                const param_type& __y)
        {
            return __x.__a_ == __y.__a_ && __x.__b_ == __y.__b_;
        }
        inline friend bool operator!=(
                const param_type& __x,
                const param_type& __y)
        {
            return !(__x == __y);
        }
    };

   private:
    param_type __p_;

   public:
    // constructors and reset functions
    inline uniform_int_distribution() : uniform_int_distribution(0) {}
    inline explicit uniform_int_distribution(
            result_type __a,
            result_type __b = std::numeric_limits<result_type>::max())
            : __p_(param_type(__a, __b))
    {
    }
    inline explicit uniform_int_distribution(const param_type& __p) : __p_(__p)
    {
    }
    inline void reset() {}

    // generating functions
    template <class _URNG>
    inline result_type operator()(_URNG& __g)
    {
        return (*this)(__g, __p_);
    }
    template <class _URNG>
    inline result_type operator()(_URNG& __g, const param_type& __p);

    // property functions
    inline result_type a() const
    {
        return __p_.a();
    }
    inline result_type b() const
    {
        return __p_.b();
    }

    inline param_type param() const
    {
        return __p_;
    }
    inline void param(const param_type& __p)
    {
        __p_ = __p;
    }

    inline result_type min() const
    {
        return a();
    }
    inline result_type max() const
    {
        return b();
    }

    inline friend bool operator==(
            const uniform_int_distribution& __x,
            const uniform_int_distribution& __y)
    {
        return __x.__p_ == __y.__p_;
    }
    inline friend bool operator!=(
            const uniform_int_distribution& __x,
            const uniform_int_distribution& __y)
    {
        return !(__x == __y);
    }
};

template <class _IntType>
template <class _URNG>
typename uniform_int_distribution<_IntType>::result_type
uniform_int_distribution<_IntType>::operator()(
        _URNG& __g,
        const param_type& __p)
{
    //   static_assert(__libcpp_random_is_valid_urng<_URNG>::value, "");
    typedef std::conditional_t<
            sizeof(result_type) <= sizeof(uint32_t),
            uint32_t,
            std::make_unsigned_t<result_type>>
            _UIntType;
    const _UIntType __rp =
            _UIntType(__p.b()) - _UIntType(__p.a()) + _UIntType(1);
    if (__rp == 1)
        return __p.a();
    const size_t __dt = std::numeric_limits<_UIntType>::digits;
    typedef __independent_bits_engine<_URNG, _UIntType> _Eng;
    if (__rp == 0)
        return static_cast<result_type>(_Eng(__g, __dt)());
    size_t __w = __dt - ZL_clz32(__rp) - 1;
    if ((__rp & (std::numeric_limits<_UIntType>::max() >> (__dt - __w))) != 0)
        ++__w;
    _Eng __e(__g, __w);
    _UIntType __u;
    do {
        __u = __e();
    } while (__u >= __rp);
    return static_cast<result_type>(__u + __p.a());
}

template <class _CharT, class _Traits, class _IT>
inline std::basic_ostream<_CharT, _Traits>& operator<<(
        std::basic_ostream<_CharT, _Traits>& __os,
        const uniform_int_distribution<_IT>& __x)
{
    save_flags<_CharT, _Traits> __lx(__os);
    typedef std::basic_ostream<_CharT, _Traits> _Ostream;
    __os.flags(_Ostream::dec | _Ostream::left);
    _CharT __sp = __os.widen(' ');
    __os.fill(__sp);
    return __os << __x.a() << __sp << __x.b();
}

template <class _CharT, class _Traits, class _IT>
inline std::basic_istream<_CharT, _Traits>& operator>>(
        std::basic_istream<_CharT, _Traits>& __is,
        uniform_int_distribution<_IT>& __x)
{
    typedef uniform_int_distribution<_IT> _Eng;
    typedef typename _Eng::result_type result_type;
    typedef typename _Eng::param_type param_type;
    save_flags<_CharT, _Traits> __lx(__is);
    typedef std::basic_istream<_CharT, _Traits> _Istream;
    __is.flags(_Istream::dec | _Istream::skipws);
    result_type __a;
    result_type __b;
    __is >> __a >> __b;
    if (!__is.fail())
        __x.param(param_type(__a, __b));
    return __is;
}

// from libcxx/include/__random/uniform_real_distribution.h
template <class _RealType = double>
class uniform_real_distribution {
    static_assert(
            std::is_floating_point<_RealType>::value,
            "RealType must be a supported floating-point type");

   public:
    // types
    typedef _RealType result_type;

    class param_type {
        result_type __a_;
        result_type __b_;

       public:
        typedef uniform_real_distribution distribution_type;

        inline explicit param_type(result_type __a = 0, result_type __b = 1)
                : __a_(__a), __b_(__b)
        {
        }

        inline result_type a() const
        {
            return __a_;
        }
        inline result_type b() const
        {
            return __b_;
        }

        friend inline bool operator==(
                const param_type& __x,
                const param_type& __y)
        {
            return __x.__a_ == __y.__a_ && __x.__b_ == __y.__b_;
        }
        friend inline bool operator!=(
                const param_type& __x,
                const param_type& __y)
        {
            return !(__x == __y);
        }
    };

   private:
    param_type __p_;

   public:
    // constructors and reset functions
    inline uniform_real_distribution() : uniform_real_distribution(0) {}
    inline explicit uniform_real_distribution(
            result_type __a,
            result_type __b = 1)
            : __p_(param_type(__a, __b))
    {
    }
    inline explicit uniform_real_distribution(const param_type& __p) : __p_(__p)
    {
    }
    inline void reset() {}

    // generating functions
    template <class _URNG>
    inline result_type operator()(_URNG& __g)
    {
        return (*this)(__g, __p_);
    }
    template <class _URNG>
    inline result_type operator()(_URNG& __g, const param_type& __p);

    // property functions
    inline result_type a() const
    {
        return __p_.a();
    }
    inline result_type b() const
    {
        return __p_.b();
    }

    inline param_type param() const
    {
        return __p_;
    }
    inline void param(const param_type& __p)
    {
        __p_ = __p;
    }

    inline result_type min() const
    {
        return a();
    }
    inline result_type max() const
    {
        return b();
    }

    friend inline bool operator==(
            const uniform_real_distribution& __x,
            const uniform_real_distribution& __y)
    {
        return __x.__p_ == __y.__p_;
    }
    friend inline bool operator!=(
            const uniform_real_distribution& __x,
            const uniform_real_distribution& __y)
    {
        return !(__x == __y);
    }
};

template <class _RealType>
template <class _URNG>
inline typename uniform_real_distribution<_RealType>::result_type
uniform_real_distribution<_RealType>::operator()(
        _URNG& __g,
        const param_type& __p)
{
    static_assert(__libcpp_random_is_valid_urng<_URNG>::value, "");
    return (__p.b() - __p.a())
            * std::generate_canonical<
                    _RealType,
                    std::numeric_limits<_RealType>::digits>(__g)
            + __p.a();
}

template <class _CharT, class _Traits, class _RT>
inline std::basic_ostream<_CharT, _Traits>& operator<<(
        std::basic_ostream<_CharT, _Traits>& __os,
        const uniform_real_distribution<_RT>& __x)
{
    save_flags<_CharT, _Traits> __lx(__os);
    typedef std::basic_ostream<_CharT, _Traits> _OStream;
    __os.flags(
            _OStream::dec | _OStream::left | _OStream::fixed
            | _OStream::scientific);
    _CharT __sp = __os.widen(' ');
    __os.fill(__sp);
    return __os << __x.a() << __sp << __x.b();
}

template <class _CharT, class _Traits, class _RT>
inline std::basic_istream<_CharT, _Traits>& operator>>(
        std::basic_istream<_CharT, _Traits>& __is,
        uniform_real_distribution<_RT>& __x)
{
    typedef uniform_real_distribution<_RT> _Eng;
    typedef typename _Eng::result_type result_type;
    typedef typename _Eng::param_type param_type;
    save_flags<_CharT, _Traits> __lx(__is);
    typedef std::basic_istream<_CharT, _Traits> _Istream;
    __is.flags(_Istream::dec | _Istream::skipws);
    result_type __a;
    result_type __b;
    __is >> __a >> __b;
    if (!__is.fail())
        __x.param(param_type(__a, __b));
    return __is;
}

// end LLVM project

} // namespace openzl::tests::datagen
