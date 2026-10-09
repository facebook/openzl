// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_ZL_LOGGING_H
#define OPENZL_ZL_LOGGING_H

#include <stdarg.h>

#include "openzl/zl_portability.h" // ZL_NOEXCEPT_FUNC_PTR

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Receives one formatted OpenZL log record.
 *
 * @p file is NULL and @p line is zero for raw log records. @p prefix may be
 * NULL. The callback may consume @p args during the call, but must not retain
 * it. The callback must be thread-safe and may be called concurrently. The
 * callback and @p opaque must remain valid until the handler is replaced or
 * reset.
 */
typedef void (*ZL_LogHandler)(
        void* opaque,
        const char* file,
        int line,
        const char* prefix,
        const char* format,
        va_list args) ZL_NOEXCEPT_FUNC_PTR;

/**
 * Replaces the process-wide log handler.
 *
 * Passing NULL disables logging. Configure the handler before using OpenZL
 * concurrently; changing it while another thread is logging is unsupported.
 */
void ZL_setLogHandler(ZL_LogHandler handler, void* opaque);

/**
 * Restores the default handler selected when OpenZL was built.
 *
 * The default writes to stderr when OPENZL_ENABLE_STDERR_LOGGING is enabled,
 * and discards logs otherwise.
 */
void ZL_resetLogHandler(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // OPENZL_ZL_LOGGING_H
