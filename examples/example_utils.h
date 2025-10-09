// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_errors.h"

namespace zstrong {
namespace examples {

static constexpr int kExampleFormatVersion    = 16;
static constexpr int kExampleCompressionLevel = 6;

/// Wrapper around ZL_*_getErrorContextString()
inline const char* getErrorContextString(ZL_CCtx* ctx, ZL_Report report)
{
    return ZL_CCtx_getErrorContextString(ctx, report);
}

/// Wrapper around ZL_*_getErrorContextString()
inline const char* getErrorContextString(ZL_Compressor* ctx, ZL_Report report)
{
    return ZL_Compressor_getErrorContextString(ctx, report);
}

/// Wrapper around ZL_*_getErrorContextString()
inline const char* getErrorContextString(ZL_DCtx* ctx, ZL_Report report)
{
    return ZL_DCtx_getErrorContextString(ctx, report);
}

/// Wrapper around ZL_*_getErrorContextString()
inline const char* getErrorContextString(std::nullptr_t, ZL_Report report)
{
    return ZL_ErrorCode_toString(ZL_errorCode(report));
}

/// Helper that aborts if Zstrong returns an error, and otherwise returns the
/// size_t result.
template <typename Ctx>
size_t abortIfError(Ctx ctx, ZL_Report report)
{
    if (ZL_isError(report)) {
        const char* msg = getErrorContextString(ctx, report);
        fprintf(stderr, "Error: %s\n", msg);
        abort();
    }
    return ZL_validResult(report);
}

/// Helper that aborts if Zstrong returns an error, and otherwise returns the
/// size_t result.
inline size_t abortIfError(ZL_Report report)
{
    return abortIfError(nullptr, report);
}

/// Abort if the @p condition is true
inline void abortIf(bool condition, const char* msg = "unknown")
{
    if (condition) {
        fprintf(stderr, "Error: %s\n", msg);
        abort();
    }
}

/// Reads the data from @p filename into the output string
std::string readFile(const char* filename);

} // namespace examples
} // namespace zstrong
