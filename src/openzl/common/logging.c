// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/logging.h"

#include <stdarg.h> // va_list, va_start, va_end

#include "openzl/zl_config.h"

int ZL_g_logLevel = ZL_LOG_LVL;

#if ZL_ENABLE_STDERR_LOGGING
static void ZL_stderrLogHandler(
        void* opaque,
        const char* file,
        int line,
        const char* prefix,
        const char* fmt,
        va_list args)
{
    (void)opaque;
    if (file != NULL) {
        fprintf(stderr, "%s:%d: ", file, line);
    }
    if (prefix != NULL) {
        fputs(prefix, stderr);
    }
    vfprintf(stderr, fmt, args);
    if (file != NULL) {
        fprintf(stderr, "\n");
    }
}
#    define ZL_DEFAULT_LOG_HANDLER ZL_stderrLogHandler
#else
#    define ZL_DEFAULT_LOG_HANDLER NULL
#endif

static ZL_LogHandler ZL_g_logHandler = ZL_DEFAULT_LOG_HANDLER;
static void* ZL_g_logOpaque;

void ZL_setLogHandler(ZL_LogHandler handler, void* opaque)
{
    ZL_g_logOpaque  = opaque;
    ZL_g_logHandler = handler;
}

void ZL_resetLogHandler(void)
{
    ZL_g_logOpaque  = NULL;
    ZL_g_logHandler = ZL_DEFAULT_LOG_HANDLER;
}

static void ZL_emitLog(
        const char* file,
        int line,
        const char* prefix,
        const char* fmt,
        va_list args)
{
    ZL_LogHandler const handler = ZL_g_logHandler;
    if (handler != NULL) {
        handler(ZL_g_logOpaque, file, line, prefix, fmt, args);
    }
}

// Note: requires -Wno-format-nonliteral to build without a warning.

void ZL_LOG_func(
        const char* file,
        const char* func,
        int line,
        const char* fmt,
        ...)
{
    va_list args;
    va_start(args, fmt);
    ZL_VLOG_func(file, func, line, fmt, args);
    va_end(args);
}

void ZL_LOG_func_if_nonempty(
        const char* file,
        const char* func,
        int line,
        const char* prefix,
        const char* fmt,
        ...)
{
    va_list args;
    va_start(args, fmt);
    ZL_VLOG_func_if_nonempty(file, func, line, prefix, fmt, args);
    va_end(args);
}

void ZL_VLOG_func(
        const char* file,
        const char* func,
        int line,
        const char* fmt,
        va_list args)
{
    (void)func;
    ZL_emitLog(file, line, NULL, fmt, args);
}

void ZL_VLOG_func_if_nonempty(
        const char* file,
        const char* func,
        int line,
        const char* prefix,
        const char* fmt,
        va_list args)
{
    (void)func;
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (len > 0) {
        ZL_emitLog(file, line, prefix, fmt, args);
    }
}

// Note: requires -Wno-format-nonliteral to build without a warning.
void ZL_RLOG_func(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ZL_VRLOG_func(fmt, args);
    va_end(args);
}

void ZL_VRLOG_func(const char* fmt, va_list args)
{
    ZL_emitLog(NULL, 0, NULL, fmt, args);
}
