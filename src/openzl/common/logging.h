// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * \file
 *
 * This file defines and configures a number of macros that can be used to
 * conditionally log debug information.
 */

#ifndef ZS_COMMON_LOGGING_H
#define ZS_COMMON_LOGGING_H

#include <stdarg.h> // va_list

#include "openzl/common/debug_level.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_logging.h"

ZL_BEGIN_C_DECLS

#ifndef ZL_ENABLE_LOG
#    define ZL_ENABLE_LOG (ZL_DBG_LVL >= 2)
#endif

#ifndef ZL_ENABLE_DLOG
/* Inherit from ZL_ENABLE_LOG, so -DZS_ENABLE_LOG=0 also turns this off. */
#    define ZL_ENABLE_DLOG ((ZL_DBG_LVL >= 4) && ZL_ENABLE_LOG)
#endif

/* Here are the various predefined logging severities, from most to least
 * important. They are used with the various logging macros defined below like
 * this, for example:
 *
 * ZL_LOG(WARN, "%s", arg1);
 */

#define ZL_LOG_LVL_ALWAYS (1 << 0)
#define ZL_LOG_LVL_ERROR (1 << 1)
#define ZL_LOG_LVL_WARN (1 << 2)
#define ZL_LOG_LVL_DEBUG (1 << 3)
#define ZL_LOG_LVL_V (1 << 4)
#define ZL_LOG_LVL_V1 (1 << 5)
#define ZL_LOG_LVL_V2 (1 << 6)
#define ZL_LOG_LVL_V3 (1 << 7)
#define ZL_LOG_LVL_V4 (1 << 8)
#define ZL_LOG_LVL_V5 (1 << 9)
#define ZL_LOG_LVL_V6 (1 << 10)
#define ZL_LOG_LVL_V7 (1 << 11)
#define ZL_LOG_LVL_V8 (1 << 12)
#define ZL_LOG_LVL_V9 (1 << 13)

/* These aliases attach semantics to the verbose levels, with more frequent
 * objects associated with higher log levels.
 */

#define ZL_LOG_LVL_OBJ ZL_LOG_LVL_V1
#define ZL_LOG_LVL_FRAME ZL_LOG_LVL_V2
#define ZL_LOG_LVL_BLOCK ZL_LOG_LVL_V3
#define ZL_LOG_LVL_TRANSFORM ZL_LOG_LVL_V4
#define ZL_LOG_LVL_STREAM ZL_LOG_LVL_V5
#define ZL_LOG_LVL_SEQ ZL_LOG_LVL_V6
#define ZL_LOG_LVL_POS ZL_LOG_LVL_V7
#define ZL_LOG_LVL_MAX ZL_LOG_LVL_V9

/* These macros provide helper values for configuring ZL_g_logLevel. */

#define ZL_LOG_NONE 0
#define ZL_LOG_ALL ZL_LOG_LVL_V9

/**
 * The ZL_LOG_LVL macro configures the default logging level at runtime. This
 * default can be modified at runtime, e.g., presumably with `-v` in the CLI.
 * Only log statements at levels greater than or equal to the configured value
 * will be printed.
 *
 * Note that only log statements that are compiled in (controlled by
 * ZL_ENABLE_LOG and ZL_ENABLE_DLOG) can be enabled by this configuration.
 */
#ifndef ZL_LOG_LVL
#    ifdef NDEBUG
#        define ZL_LOG_LVL ZL_LOG_LVL_ALWAYS // TODO
#    else
#        define ZL_LOG_LVL ZL_LOG_LVL_DEBUG // TODO
#    endif
#endif

/* We define a number of logging macros below:
 *
 * - ZL_LOG(level, ...)
 * - ZL_DLOG(level, ...)
 * - ZL_RLOG(level, ...)
 * - ZL_RDLOG(level, ...)
 * - ZL_FLOG(level, ...)
 * - ZL_FDLOG(level, ...)
 * - ZL_FRLOG(level, ...)
 * - ZL_FRDLOG(level, ...)
 * - ZL_VLOG(level, fmt, args)
 * - ZL_VDLOG(level, fmt, args)
 * - ZL_VRLOG(level, fmt, args)
 * - ZL_VRDLOG(level, fmt, args)
 * - ZL_VFLOG(level, fmt, args)
 * - ZL_VFDLOG(level, fmt, args)
 * - ZL_VFRLOG(level, fmt, args)
 * - ZL_VFRDLOG(level, fmt, args)
 * - ZL_LOG_IF(cond, level, ...)
 * - ZL_DLOG_IF(cond, level, ...)
 * - ZL_RLOG_IF(cond, level, ...)
 * - ZL_RDLOG_IF(cond, level, ...)
 * - ZL_FLOG_IF(cond, level, ...)
 * - ZL_FDLOG_IF(cond, level, ...)
 * - ZL_FRLOG_IF(cond, level, ...)
 * - ZL_FRDLOG_IF(cond, level, ...)
 * - ZL_VLOG_IF(cond, level, fmt, args)
 * - ZL_VDLOG_IF(cond, level, fmt, args)
 * - ZL_VRLOG_IF(cond, level, fmt, args)
 * - ZL_VRDLOG_IF(cond, level, fmt, args)
 * - ZL_VFLOG_IF(cond, level, fmt, args)
 * - ZL_VFDLOG_IF(cond, level, fmt, args)
 * - ZL_VFRLOG_IF(cond, level, fmt, args)
 * - ZL_VFRDLOG_IF(cond, level,fmt, args)
 *
 * The baseline version is ZL_LOG(). This logs the provided arguments to
 * stderr via fprintf(), with the filename, line number, and severity, as well
 * as a newline at the end.
 *
 * An 'R' in the macro name signifies "raw" logging, which only logs the
 * provided format string with arguments, no filename/line number/severity/
 * newline fanciness.
 *
 * A 'D' in the macro name signifies "debug" logging. These statements are
 * only compiled into debug builds, and are excluded from optimized builds.
 * These statements are therefore intended to be used in places where even an
 * inactive log statement would induce a performance hit.
 *
 * An 'F' in the macro name signifies "frame" logging. It accepts an additional
 * 3 parameters: const char* file, const char* func, and int line. In non-'F'
 * variants, these parameters are auto-populated from the __FILE__, __func__,
 * and __LINE__ pseudo-macros. These variants allow the use of explicitly
 * provided values instead. E.g., when performing logging in a helper function
 * that should be attributed to the caller's stack frame. Note that this frame
 * info is discarded/ignored in 'R' logging statements, since they don't print
 * it.
 *
 * A 'V' in the macro name signifies that it accepts a `va_list` of additional
 * arguments, rather than directly taking a variable number of arguments.
 * (These variants are created to be invoked in functions that themselves take
 * a variable number of arguments and want to pass that on.)
 *
 * Adding an "_IF" causes the macro to take another argument, a condition to
 * evaluate. Only if true will the log be executed.
 */

#include <stdio.h>

#define ZL_LOG(level, ...) ZL_LOG_IF(1, level, __VA_ARGS__)
#define ZL_DLOG(level, ...) ZL_DLOG_IF(1, level, __VA_ARGS__)
#define ZL_RLOG(level, ...) ZL_RLOG_IF(1, level, __VA_ARGS__)
#define ZL_RDLOG(level, ...) ZL_RDLOG_IF(1, level, __VA_ARGS__)

#define ZL_VLOG(level, fmt, args) ZL_VLOG_IF(1, level, fmt, args)
#define ZL_VDLOG(level, fmt, args) ZL_VDLOG_IF(1, level, fmt, args)
#define ZL_VRLOG(level, fmt, args) ZL_VRLOG_IF(1, level, fmt, args)
#define ZL_VRDLOG(level, fmt, args) ZL_VRDLOG_IF(1, level, fmt, args)

#define ZL_FLOG(level, ...) ZL_FLOG_IF(1, level, __VA_ARGS__)
#define ZL_FDLOG(level, ...) ZL_FDLOG_IF(1, level, __VA_ARGS__)
#define ZL_FRLOG(level, ...) ZL_FRLOG_IF(1, level, __VA_ARGS__)
#define ZL_FRDLOG(level, ...) ZL_FRDLOG_IF(1, level, __VA_ARGS__)

#define ZL_VFLOG(level, ...) ZL_VFLOG_IF(1, level, __VA_ARGS__)
#define ZL_VFDLOG(level, ...) ZL_VFDLOG_IF(1, level, __VA_ARGS__)
#define ZL_VFRLOG(level, ...) ZL_VFRLOG_IF(1, level, __VA_ARGS__)
#define ZL_VFRDLOG(level, ...) ZL_VFRDLOG_IF(1, level, __VA_ARGS__)

#define ZL_LOG_IF(cond, level, ...) \
    ZL_FLOG_IF(cond, level, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define ZL_DLOG_IF(cond, level, ...) \
    ZL_FDLOG_IF(cond, level, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define ZL_RLOG_IF(cond, level, ...) \
    ZL_FRLOG_IF(cond, level, __FILE__, __func__, __LINE__, __VA_ARGS__)
#define ZL_RDLOG_IF(cond, level, ...) \
    ZL_FRDLOG_IF(cond, level, __FILE__, __func__, __LINE__, __VA_ARGS__)

#define ZL_VLOG_IF(cond, level, fmt, args) \
    ZL_VFLOG_IF(cond, level, __FILE__, __func__, __LINE__, fmt, args)
#define ZL_VDLOG_IF(cond, level, fmt, args) \
    ZL_VFDLOG_IF(cond, level, __FILE__, __func__, __LINE__, fmt, args)
#define ZL_VRLOG_IF(cond, level, fmt, args) \
    ZL_VFRLOG_IF(cond, level, __FILE__, __func__, __LINE__, fmt, args)
#define ZL_VRDLOG_IF(cond, level, fmt, args) \
    ZL_VFRDLOG_IF(cond, level, __FILE__, __func__, __LINE__, fmt, args)

#define ZL_FLOG_IF(cond, level, file, func, line, ...) \
    ZL_LOG_IMPL(                                       \
            ZL_ENABLE_LOG,                             \
            cond,                                      \
            level,                                     \
            ZL_LOG_func,                               \
            file,                                      \
            func,                                      \
            line,                                      \
            __VA_ARGS__)
#define ZL_FDLOG_IF(cond, level, file, func, line, ...) \
    ZL_LOG_IMPL(                                        \
            ZL_ENABLE_DLOG,                             \
            cond,                                       \
            level,                                      \
            ZL_LOG_func,                                \
            file,                                       \
            func,                                       \
            line,                                       \
            __VA_ARGS__)
#define ZL_FRLOG_IF(cond, level, file, func, line, ...) \
    ZL_LOG_IMPL(ZL_ENABLE_LOG, cond, level, ZL_RLOG_func, __VA_ARGS__)
#define ZL_FRDLOG_IF(cond, level, file, func, line, ...) \
    ZL_LOG_IMPL(ZL_ENABLE_DLOG, cond, level, ZL_RLOG_func, __VA_ARGS__)

#define ZL_VFLOG_IF(cond, level, file, func, line, fmt, args) \
    ZL_LOG_IMPL(                                              \
            ZL_ENABLE_LOG,                                    \
            cond,                                             \
            level,                                            \
            ZL_VLOG_func,                                     \
            file,                                             \
            func,                                             \
            line,                                             \
            fmt,                                              \
            args)
#define ZL_VFDLOG_IF(cond, level, file, func, line, fmt, args) \
    ZL_LOG_IMPL(                                               \
            ZL_ENABLE_DLOG,                                    \
            cond,                                              \
            level,                                             \
            ZL_VLOG_func,                                      \
            file,                                              \
            func,                                              \
            line,                                              \
            fmt,                                               \
            args)
#define ZL_VFRLOG_IF(cond, level, file, func, line, fmt, args) \
    ZL_LOG_IMPL(ZL_ENABLE_LOG, cond, level, ZL_VRLOG_func, fmt, args)
#define ZL_VFRDLOG_IF(cond, level, file, func, line, fmt, args) \
    ZL_LOG_IMPL(ZL_ENABLE_DLOG, cond, level, ZL_VRLOG_func, fmt, args)

#define ZL_LOG_IFNONEMPTY(level, prefix, ...) \
    ZL_LOG_IMPL(                              \
            ZL_ENABLE_LOG,                    \
            1,                                \
            level,                            \
            ZL_LOG_func_if_nonempty,          \
            __FILE__,                         \
            __func__,                         \
            __LINE__,                         \
            prefix,                           \
            __VA_ARGS__)

#define ZL_DLOG_IFNONEMPTY(level, prefix, ...) \
    ZL_LOG_IMPL(                               \
            ZL_ENABLE_DLOG,                    \
            1,                                 \
            level,                             \
            ZL_LOG_func_if_nonempty,           \
            __FILE__,                          \
            __func__,                          \
            __LINE__,                          \
            prefix,                            \
            __VA_ARGS__)

/**************************
 * Implementation Details *
 **************************/

/**
 * Translates a logging level shortname, e.g. WARN, into the full macro.
 */
#define ZL_LOG_ADD_PREFIX(name) ZL_LOG_LVL_##name

#define ZL_LOG_IMPL(enabled, cond, level, impl, ...)                      \
    (((enabled) && (cond) && (ZL_LOG_ADD_PREFIX(level) <= ZL_g_logLevel)) \
             ? impl(__VA_ARGS__)                                          \
             : (void)0)

void ZL_LOG_func(
        const char* file,
        const char* func,
        int line,
        const char* fmt,
        ...);

void ZL_VLOG_func(
        const char* file,
        const char* func,
        int line,
        const char* fmt,
        va_list args);

void ZL_LOG_func_if_nonempty(
        const char* file,
        const char* func,
        int line,
        const char* prefix,
        const char* fmt,
        ...);

void ZL_VLOG_func_if_nonempty(
        const char* file,
        const char* func,
        int line,
        const char* prefix,
        const char* fmt,
        va_list args);

void ZL_RLOG_func(const char* fmt, ...);

void ZL_VRLOG_func(const char* fmt, va_list args);

/**
 * This global is only declared here. It's defined in debug.c. Its default
 * value is ZSTD_LOG_LEVEL. It can however be modified at runtime.
 */
extern int ZL_g_logLevel;

/**************************
 * String generation *
 **************************/
// Note : could also be a separate .h file, to include here

// Useful to print a string ptr that might be NULL, like node->name.
// NULL will be automatically replaced by "" (empty string).
#define STR_REPLACE_NULL(s) (((s) == NULL) ? "" : (s))

ZL_END_C_DECLS

#endif // ZS_COMMON_LOGGING_H
