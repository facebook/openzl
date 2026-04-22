// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_DICT_MATERIALIZER_CTX_H
#define OPENZL_DICT_MATERIALIZER_CTX_H

#include "openzl/common/allocation.h"       // Arena
#include "openzl/detail/zl_error_context.h" // ZL_OperationContext

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Concrete definition of the ZL_Materializer context struct.
 * Forward-declared in zl_opaque_types.h as `struct ZL_Materializer_s`.
 * Both :dict and :compress need visibility into this layout.
 */
struct ZL_Materializer_s {
    Arena* persistentArena;
    Arena* scratchArena;
    const void* opaquePtr;
    ZL_OperationContext* opCtx;
} /* typedef'ed to ZL_Materializer in zl_opaque_types.h */;

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_DICT_MATERIALIZER_CTX_H
