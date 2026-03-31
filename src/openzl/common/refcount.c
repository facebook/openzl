// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/refcount.h"

#include <string.h> // memcpy
#include "openzl/common/allocation.h"
#include "openzl/zl_errors.h"

struct ZS2_Refcount_Control {
    void* ptr;
    ZL_Refcount_FreeFn freeFn;
    void* freeState;
    int count;
    ALLOC_CustomFree freeCtrlFn;
    void* freeCtrlState;
};

ZL_Report ZL_Refcount_init(
        ZL_Refcount* rc,
        void* ptr,
        const ALLOC_CustomAllocation* ctrlAlloc,
        ZL_Refcount_FreeFn freeFn,
        void* opaque)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (ctrlAlloc == NULL) {
        // just a reference, no ownership
        rc->_ref = NULL;
    } else {
        // managed reference => free MUST be defined
        ZL_ASSERT_NN(freeFn);
        ZL_ASSERT_NN(ctrlAlloc->sfree);
        struct ZS2_Refcount_Control* const ctrl =
                ctrlAlloc->malloc(ctrlAlloc->opaque, sizeof(*ctrl));
        ZL_ERR_IF_NULL(ctrl, allocation);

        ctrl->ptr           = ptr;
        ctrl->count         = 1;
        ctrl->freeFn        = freeFn;
        ctrl->freeState     = opaque;
        ctrl->freeCtrlFn    = ctrlAlloc->sfree;
        ctrl->freeCtrlState = ctrlAlloc->opaque;

        rc->_ref = ctrl;
    }
    rc->_ptr     = ptr;
    rc->_mutable = true;

    return ZL_returnSuccess();
}

static void freeFn(void* opaque, void* ptr)
{
    ZL_ASSERT_NULL(opaque);
    ZL_free(ptr);
}

static void* mallocFn(void* opaque, size_t s)
{
    ZL_ASSERT_NULL(opaque);
    return ZL_malloc(s);
}

static const ALLOC_CustomAllocation ZS2_Refcount_defaultAllocation = {
    .malloc = mallocFn,
    .sfree  = freeFn,
    .opaque = NULL
};

ZL_Report ZL_Refcount_initMalloc(ZL_Refcount* rc, void* ptr)
{
    return ZL_Refcount_init(
            rc, ptr, &ZS2_Refcount_defaultAllocation, freeFn, NULL);
}

ZL_Report ZL_Refcount_initConstRef(ZL_Refcount* rc, const void* ptr)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    void* ptrMut;
    memcpy(&ptrMut, &ptr, sizeof(ptr));
    ZL_ERR_IF_ERR(ZL_Refcount_init(rc, ptrMut, NULL, NULL, NULL));
    rc->_mutable = false;
    ZL_ASSERT(!ZL_Refcount_mutable(rc));
    return ZL_returnSuccess();
}

ZL_Report ZL_Refcount_initMutRef(ZL_Refcount* rc, void* ptr)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_ERR(ZL_Refcount_init(rc, ptr, NULL, NULL, NULL));
    rc->_mutable = true;
    ZL_ASSERT(ZL_Refcount_mutable(rc));
    return ZL_returnSuccess();
}

// simple redirectors, just for type matching
static void* ZS2_Refcount_arenaMalloc(void* arena, size_t s)
{
    return ALLOC_Arena_malloc(arena, s);
}
static void ZS2_Refcount_arenaFree(void* arena, void* p)
{
    ALLOC_Arena_free(arena, p);
}

void* ZL_Refcount_inArena(ZL_Refcount* rc, Arena* arena, size_t s)
{
    void* const buffer = ALLOC_Arena_malloc(arena, s);
    if (buffer == NULL) {
        // Note(@Cyan): could be worth passing to some error context
        ZL_DLOG(ERROR,
                "ZL_Refcount_inArena: Failed allocation of buffer of size %zu",
                s);
        return NULL;
    }
    const ALLOC_CustomAllocation ca = { ZS2_Refcount_arenaMalloc,
                                        ZS2_Refcount_arenaFree,
                                        arena };
    ZL_Report const rcir =
            ZL_Refcount_init(rc, buffer, &ca, ZS2_Refcount_arenaFree, arena);
    if (ZL_isError(rcir)) {
        // Note(@Cyan): could be worth passing to some error context
        ZL_DLOG(ERROR,
                "ZL_Refcount_inArena: error initializing buffer: %s",
                ZL_ErrorCode_toString(ZL_RES_code(rcir)));
        ALLOC_Arena_free(arena, buffer);
        return NULL;
    }
    return buffer;
}

void ZL_Refcount_destroy(ZL_Refcount* rc)
{
    if (ZL_Refcount_null(rc))
        return;
    if (rc->_ref) {
        // Owned buffer: check for free
        ZL_ASSERT_NN(rc->_ref);
        ZL_ASSERT_NN(rc->_ref->freeFn);
        ZL_ASSERT_NN(rc->_ref->freeCtrlFn);
        ZL_ASSERT_GT(rc->_ref->count, 0);
        --rc->_ref->count;
        if (rc->_ref->count == 0) {
            (rc->_ref->freeFn)(rc->_ref->freeState, rc->_ref->ptr);
            (rc->_ref->freeCtrlFn)(rc->_ref->freeCtrlState, rc->_ref);
        }
        rc->_ref = NULL;
    }
    rc->_ptr = NULL;
}

bool ZL_Refcount_null(ZL_Refcount const* rc)
{
    // Note(@Cyan): rc->_ref == NULL is now a valid scenario,
    // when @rc just references an external buffer that it doesn't track.
    // Consequently, the below test is true if @rc references the NULL pointer.
    // Returning true in this case sounds reasonable for a `_null()` test,
    // but if the test actually meant `_isInitialized()`,
    // then the result is no longer correct.
    return rc->_ptr == NULL && rc->_ref == NULL;
}

bool ZL_Refcount_mutable(ZL_Refcount const* rc)
{
    ZL_ASSERT_NN(rc);
    return rc->_mutable && (rc->_ref == NULL || rc->_ref->count == 1);
}

void ZL_Refcount_constify(ZL_Refcount* rc)
{
    rc->_mutable = false;
    ZL_ASSERT(!ZL_Refcount_mutable(rc));
}

ZL_Refcount ZL_Refcount_copy(const ZL_Refcount* rc)
{
    if (ZL_Refcount_null(rc)) {
        return *rc;
    }
    if (rc->_ref) {
        ZL_ASSERT_GT(rc->_ref->count, 0);
        rc->_ref->count += 1;
    }
    ZL_Refcount copy = *rc;
    ZL_ASSERT(!ZL_Refcount_mutable(rc));
    ZL_ASSERT(!ZL_Refcount_mutable(&copy));
    return copy;
}

ZL_Refcount ZL_Refcount_aliasPtr(const ZL_Refcount* rc, void* ptr)
{
    ZL_Refcount alias = ZL_Refcount_copy(rc);
    alias._ptr        = ptr;
    return alias;
}

ZL_Refcount ZL_Refcount_aliasOffset(const ZL_Refcount* rc, size_t offset)
{
    if (!rc->_ptr)
        ZL_ASSERT_EQ(offset, 0);
    return ZL_Refcount_aliasPtr(rc, rc->_ptr ? (char*)rc->_ptr + offset : NULL);
}
