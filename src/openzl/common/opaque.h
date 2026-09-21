// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMMON_OPAQUE_H
#define ZSTRONG_COMMON_OPAQUE_H

#include "openzl/common/vector.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_common_types.h"

ZL_BEGIN_C_DECLS

DECLARE_VECTOR_TYPE(ZL_OpaquePtr)

typedef struct ZL_OpaquePtrRegistry_s {
    VECTOR(ZL_OpaquePtr) ptrs;
} ZL_OpaquePtrRegistry;

void ZL_OpaquePtrRegistry_init(ZL_OpaquePtrRegistry* registry);
void ZL_OpaquePtrRegistry_destroy(ZL_OpaquePtrRegistry* registry);
void ZL_OpaquePtrRegistry_reset(ZL_OpaquePtrRegistry* registry);

/** Returns memory currently retained by @p registry. Accepts NULL. */
size_t ZL_OpaquePtrRegistry_sizeof(const ZL_OpaquePtrRegistry* registry);

ZL_Report ZL_OpaquePtrRegistry_register(
        ZL_OpaquePtrRegistry* registry,
        ZL_OpaquePtr ptr);

void ZL_OpaquePtr_free(ZL_OpaquePtr opaque);

ZL_END_C_DECLS

#endif
