// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * \file
 *
 * This file implements some helpers for macro expansion, to avoid empty vararg
 * lists, which are illegal.
 */

#ifndef ZSTRONG_ZS2_MACRO_HELPERS_H
#define ZSTRONG_ZS2_MACRO_HELPERS_H

#define ZS_MACRO_CONCAT_INNER(a, b) a##b
#define ZS_MACRO_CONCAT(a, b) ZS_MACRO_CONCAT_INNER(a, b)

#define ZS_MACRO_QUOTE_INNER(a) #a
#define ZS_MACRO_QUOTE(a) ZS_MACRO_QUOTE_INNER(a)

#define ZS_MACRO_EXPAND(...) __VA_ARGS__
#define ZS_MACRO_PAD_SELECT_33RD_INNER( \
        _1,                             \
        _2,                             \
        _3,                             \
        _4,                             \
        _5,                             \
        _6,                             \
        _7,                             \
        _8,                             \
        _9,                             \
        _10,                            \
        _11,                            \
        _12,                            \
        _13,                            \
        _14,                            \
        _15,                            \
        _16,                            \
        _17,                            \
        _18,                            \
        _19,                            \
        _20,                            \
        _21,                            \
        _22,                            \
        _23,                            \
        _24,                            \
        _25,                            \
        _26,                            \
        _27,                            \
        _28,                            \
        _29,                            \
        _30,                            \
        _31,                            \
        _32,                            \
        _33,                            \
        ...)                            \
    _33
#define ZS_MACRO_PAD_SELECT_33RD(...) \
    ZS_MACRO_EXPAND(ZS_MACRO_PAD_SELECT_33RD_INNER(__VA_ARGS__))
#define ZS_MACRO_PAD1_SUFFIX(...) \
    ZS_MACRO_PAD_SELECT_33RD(     \
            __VA_ARGS__,          \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _FIXED,               \
            _NOMSG,               \
            _UNREACHABLE)
#define ZS_MACRO_PAD2_SUFFIX(...) \
    ZS_MACRO_PAD_SELECT_33RD(     \
            __VA_ARGS__,          \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _FIXED,               \
            _NOMSG,               \
            _NOT_ENOUGH_ARGS,     \
            _UNREACHABLE)
#define ZS_MACRO_PAD3_SUFFIX(...) \
    ZS_MACRO_PAD_SELECT_33RD(     \
            __VA_ARGS__,          \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _FIXED,               \
            _NOMSG,               \
            _NOT_ENOUGH_ARGS,     \
            _NOT_ENOUGH_ARGS,     \
            _UNREACHABLE)
#define ZS_MACRO_PAD4_SUFFIX(...) \
    ZS_MACRO_PAD_SELECT_33RD(     \
            __VA_ARGS__,          \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _FIXED,               \
            _NOMSG,               \
            _NOT_ENOUGH_ARGS,     \
            _NOT_ENOUGH_ARGS,     \
            _NOT_ENOUGH_ARGS,     \
            _UNREACHABLE)
#define ZS_MACRO_PAD5_SUFFIX(...) \
    ZS_MACRO_PAD_SELECT_33RD(     \
            __VA_ARGS__,          \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _MSG,                 \
            _FIXED,               \
            _NOMSG,               \
            _NOT_ENOUGH_ARGS,     \
            _NOT_ENOUGH_ARGS,     \
            _NOT_ENOUGH_ARGS,     \
            _NOT_ENOUGH_ARGS,     \
            _UNREACHABLE)

/**
 * The case in which a formatted message (i.e., 2+ args) is already present.
 * Nothing needs to be added.
 */
#define ZS_MACRO_PAD_MSG

/**
 * The case in which an unformatted message (i.e., 1 arg) is already present.
 * We don't add anything, although this means that the formatting parameters
 * are empty, and therefore the format string can't be directly taken as a
 * named parameter separate from the formatting parameters.
 */
#define ZS_MACRO_PAD_FIXED

/**
 * The case in which no message was passed. Add an effectively empty string.
 * Note that this construction is required as opposed to simply passing `""`
 * because the compiler will complain about the obvious uselessness if it sees
 * you do `*printf("")`.
 */
#define ZS_MACRO_PAD_NOMSG , "%s", ""

#define ZS_MACRO_PAD1_ARGS(...) \
    ZS_MACRO_CONCAT(ZS_MACRO_PAD, ZS_MACRO_PAD1_SUFFIX(__VA_ARGS__))
#define ZS_MACRO_PAD2_ARGS(...) \
    ZS_MACRO_CONCAT(ZS_MACRO_PAD, ZS_MACRO_PAD2_SUFFIX(__VA_ARGS__))
#define ZS_MACRO_PAD3_ARGS(...) \
    ZS_MACRO_CONCAT(ZS_MACRO_PAD, ZS_MACRO_PAD3_SUFFIX(__VA_ARGS__))
#define ZS_MACRO_PAD4_ARGS(...) \
    ZS_MACRO_CONCAT(ZS_MACRO_PAD, ZS_MACRO_PAD4_SUFFIX(__VA_ARGS__))
#define ZS_MACRO_PAD5_ARGS(...) \
    ZS_MACRO_CONCAT(ZS_MACRO_PAD, ZS_MACRO_PAD5_SUFFIX(__VA_ARGS__))

#define ZS_MACRO_PAD1_INNER(macro, ...) ZS_MACRO_EXPAND(macro(__VA_ARGS__))
#define ZS_MACRO_PAD2_INNER(macro, ...) ZS_MACRO_EXPAND(macro(__VA_ARGS__))
#define ZS_MACRO_PAD3_INNER(macro, ...) ZS_MACRO_EXPAND(macro(__VA_ARGS__))
#define ZS_MACRO_PAD4_INNER(macro, ...) ZS_MACRO_EXPAND(macro(__VA_ARGS__))
#define ZS_MACRO_PAD5_INNER(macro, ...) ZS_MACRO_EXPAND(macro(__VA_ARGS__))

/**
 * These macros are designed to handle the case where, after N required
 * arguments, a macro wants to take an optional formatted string.
 */

#define ZS_MACRO_PAD1(macro, ...) \
    ZS_MACRO_PAD1_INNER(macro, __VA_ARGS__ ZS_MACRO_PAD1_ARGS(__VA_ARGS__))
#define ZS_MACRO_PAD2(macro, ...) \
    ZS_MACRO_PAD2_INNER(macro, __VA_ARGS__ ZS_MACRO_PAD2_ARGS(__VA_ARGS__))
#define ZS_MACRO_PAD3(macro, ...) \
    ZS_MACRO_PAD3_INNER(macro, __VA_ARGS__ ZS_MACRO_PAD3_ARGS(__VA_ARGS__))
#define ZS_MACRO_PAD4(macro, ...) \
    ZS_MACRO_PAD4_INNER(macro, __VA_ARGS__ ZS_MACRO_PAD4_ARGS(__VA_ARGS__))
#define ZS_MACRO_PAD5(macro, ...) \
    ZS_MACRO_PAD5_INNER(macro, __VA_ARGS__ ZS_MACRO_PAD5_ARGS(__VA_ARGS__))

#define ZS_MACRO_PAD_SELECT_1ST(arg, ...) arg

/**
 * This macro returns the first of one or more arguments.
 */
#define ZS_MACRO_1ST_ARG(...) \
    ZS_MACRO_PAD_SELECT_1ST(__VA_ARGS__, ZS_MACRO_PAD_NOT_ENOUGH_ARGS)

/**
 * These helpers are designed to help you print out a value of indeterminate
 * type. ZS_GENERIC_PRINTF_BUILD_FORMAT_*_ARG() inserts one or more printf
 * placeholder(s) into a string literal (they must all be the same type).
 * ZS_GENERIC_PRINTF_CAST() then casts the value to be compatible with the
 * selected formatting placeholder.
 *
 * In C++, we can't use C11's _Generic, so we fall back to the previous
 * behavior of casting everything to a `long long`.
 */
#if !defined(__cplusplus)
#    define ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG(expr, prefix, middle, suffix)          \
        _Generic(                                                                       \
                ((expr) - (expr)),                                                      \
                ptrdiff_t: _Generic(                                                    \
                        (expr),                                                         \
                        int: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(                \
                                "(int) ", "%ld", prefix, middle, suffix),               \
                        long: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(               \
                                "(long) ", "%lld", prefix, middle, suffix),             \
                        long long: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(          \
                                "(long long) ",                                         \
                                "%lld",                                                 \
                                prefix,                                                 \
                                middle,                                                 \
                                suffix),                                                \
                        default: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(            \
                                "(pointer) ", "%p", prefix, middle, suffix)),           \
                default: _Generic(                                                      \
                        (expr),                                                         \
                        char: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(               \
                                "(char) ", "%#04x", prefix, middle, suffix),            \
                        signed char: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(        \
                                "(signed char) ",                                       \
                                "%hhd",                                                 \
                                prefix,                                                 \
                                middle,                                                 \
                                suffix),                                                \
                        unsigned char: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(      \
                                "(unsigned char) ",                                     \
                                "%hhu",                                                 \
                                prefix,                                                 \
                                middle,                                                 \
                                suffix),                                                \
                        short: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(              \
                                "(short) ", "%hd", prefix, middle, suffix),             \
                        unsigned short: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(     \
                                "(unsigned short) ",                                    \
                                "%hu",                                                  \
                                prefix,                                                 \
                                middle,                                                 \
                                suffix),                                                \
                        int: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(                \
                                "(int) ", "%d", prefix, middle, suffix),                \
                        unsigned int: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(       \
                                "(unsigned int) ",                                      \
                                "%u",                                                   \
                                prefix,                                                 \
                                middle,                                                 \
                                suffix),                                                \
                        long: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(               \
                                "(long) ", "%lld", prefix, middle, suffix),             \
                        unsigned long: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(      \
                                "(unsigned long) ",                                     \
                                "%llu",                                                 \
                                prefix,                                                 \
                                middle,                                                 \
                                suffix),                                                \
                        long long: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(          \
                                "(long long) ",                                         \
                                "%lld",                                                 \
                                prefix,                                                 \
                                middle,                                                 \
                                suffix),                                                \
                        unsigned long long: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER( \
                                "(unsigned long long) ",                                \
                                "%llu",                                                 \
                                prefix,                                                 \
                                middle,                                                 \
                                suffix),                                                \
                        float: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(              \
                                "(float) ", "%f", prefix, middle, suffix),              \
                        double: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(             \
                                "(double) ", "%f", prefix, middle, suffix),             \
                        long double: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(        \
                                "(long double) ",                                       \
                                "%llf",                                                 \
                                prefix,                                                 \
                                middle,                                                 \
                                suffix),                                                \
                        default: ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(            \
                                "(unknown) ",                                           \
                                "%lld",                                                 \
                                prefix,                                                 \
                                middle,                                                 \
                                suffix)))
#    define ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG_INNER(     \
            type_name, placeholder, prefix, middle, suffix) \
        prefix type_name placeholder middle type_name placeholder suffix
#    define ZS_GENERIC_PRINTF_CAST(expr) (expr)
#else
#    define ZS_GENERIC_PRINTF_BUILD_FORMAT_2_ARG(expr, prefix, middle, suffix) \
        prefix "(unknown) "                                                    \
               "%lld" middle                                                   \
               "(unknown) "                                                    \
               "%lld" suffix
#    define ZS_GENERIC_PRINTF_CAST(expr) (long long)(expr)
#endif

#endif // ZSTRONG_ZS2_MACRO_HELPERS_H
