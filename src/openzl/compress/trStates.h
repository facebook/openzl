// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_TRSTATES_H
#define ZSTRONG_COMPRESS_TRSTATES_H

#include "openzl/common/map.h"
#include "openzl/compress/cnode.h" // CNode
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" // ZL_CodecStateManager

ZL_BEGIN_C_DECLS

ZL_DECLARE_MAP_TYPE(CachedStatesMap, ZL_CodecStateManager, void*);

typedef struct {
    CachedStatesMap states;
} CachedStates;

void TRS_init(CachedStates* trs);

void TRS_destroy(CachedStates* trs);

/** Returns memory currently retained by @p trs. Accepts NULL. */
size_t TRS_sizeof(const CachedStates* trs);

// Accessors

/* look for a cached state for @cnode,
 * return it if present,
 * create and cache a new state if not */
void* TRS_getCodecState(CachedStates* trs, const CNode* cnode);

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_TRSTATES_H
