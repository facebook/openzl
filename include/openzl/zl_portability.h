// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * \file
 *
 * Defines portability macros needed for Zstrong's public interface.
 */

#ifndef ZSTRONG_ZS2_PORTABILITY_H
#define ZSTRONG_ZS2_PORTABILITY_H

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__has_c_attribute) && !defined(__cplusplus)
#    define ZL_HAS_C_ATTRIBUTE(x) __has_c_attribute(x)
#else
#    define ZL_HAS_C_ATTRIBUTE(x) (0)
#endif

#if defined(__has_cpp_attribute) && defined(__cplusplus)
#    define ZL_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#    define ZL_HAS_CPP_ATTRIBUTE(x) (0)
#endif

#ifdef __has_attribute
#    define ZL_HAS_ATTRIBUTE(x) __has_attribute(x)
#else
#    define ZL_HAS_ATTRIBUTE(x) 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#    define ZL_UNUSED_ATTR __attribute__((unused))
#else
#    define ZL_UNUSED_ATTR
#endif

/**
 * This is essentially the same as C99's inline,
 * but adds some compiler attribute to avoid warnings
 * when the inlined function is not used within the including unit.
 */
#define ZL_INLINE static inline ZL_UNUSED_ATTR

/**
 * Portable way to declare a struct or a function return's type as
 * nodiscard.
 * warn_unused_result is only used in clang, because gcc's version doesn't work
 * on structs.
 */
#if (ZL_HAS_C_ATTRIBUTE(nodiscard) && __STDC_VERSION__ >= 202311L) \
        || ZL_HAS_CPP_ATTRIBUTE(nodiscard)
#    define ZL_NODISCARD [[nodiscard]]
#elif defined(__clang__) && ZL_HAS_ATTRIBUTE(__warn_unused_result__)
#    define ZL_NODISCARD __attribute__((__warn_unused_result__))
#else
#    define ZL_NODISCARD
#endif

/**
 * Pure functions have no side effects and can depend only on their arguments
 * and global variables.
 */
#if ZL_HAS_ATTRIBUTE(pure)
#    define ZL_PURE_FN __attribute__((pure))
#else
#    define ZL_PURE_FN
#endif

/**
 * Const functions are pure functions that only depend on their arguments.
 */
#if ZL_HAS_ATTRIBUTE(const)
#    define ZL_CONST_FN __attribute__((const))
#else
#    define ZL_CONST_FN ZL_PURE_FN
#endif

/**
 * Functions passed into OpenZL core from C++ should be `noexcept`. The C++
 * standard, by omitting to describe a behavior, makes it by default undefined
 * behavior to throw an exception from C++ code into C code, even if that C
 * code was itself called by C++ code which has an appropriate handler for the
 * thrown exception.
 *
 * Even in the case where the undefined behavior monster doesn't come to eat
 * your sanity, and the compiler generates code such that the intermediate C
 * stack frames are successfully unwound between the thrown exception and an
 * enclosing handler, it is still unsafe to throw exceptions through OpenZL:
 * it will likely result in memory leaks and objects being left in inconsistent
 * states which make them unsafe to further operate on, including freeing them.
 *
 * Starting in C++17, `noexcept` is part of the type specification, and can
 * be used in typedefs as part of describing the type of a function pointer.
 * When we are in that mode, we append `noexcept` to our function pointer type
 * definitions to enforce that users comply with this.
 */
#if defined(__cplusplus) && __cplusplus >= 201703L
#    define ZL_NOEXCEPT_FUNC_PTR noexcept
#else
#    define ZL_NOEXCEPT_FUNC_PTR
#endif

/**
 * Portable way to mark a branch as unlikely. Currently only implemented using
 * `__builtin_expect` but can be expanded in the future.
 *
 * It's not 100% a drop-in replacement, since `__builtin_expect` takes a `long`
 * and returns that same `long`, whereas this collapses the value to a `bool`
 * (so as to avoid unsigned -> signed implicit conversion warnings on some
 * platforms...).
 */
#ifndef ZL_UNLIKELY
#    if defined(__GNUC__) || defined(__clang__)
#        define ZL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#    else
#        define ZL_UNLIKELY(x) (x)
#    endif
#endif

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_PORTABILITY_H
