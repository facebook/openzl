// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_COMMON_REFCOUNT_H
#define ZSTRONG_COMMON_REFCOUNT_H

#include "openzl/common/allocation.h" // ALLOC_CustomAllocation
#include "openzl/common/assertion.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

// ZL_Refcount_FreeFn: free function to invoke once refcount reaches 0
typedef void (*ZL_Refcount_FreeFn)(void* opaque, void* ptr)
        ZL_NOEXCEPT_FUNC_PTR;

/**
 * An object that manages a reference counted pointer with a custom free
 * function. If the original pointer is mutable, this object provides a mutable
 * pointer when the reference count is one, and an immutable pointer when the
 * reference count is greater than one.
 *
 * WARNING: This object is NOT thread safe, except for get().
 */
typedef struct {
    void* _ptr;
    struct ZL_Refcount_Control* _ref;
    bool _mutable;
} ZL_Refcount;

/**
 * Takes ownership of @p ptr and frees it with `(*freeFn)(opaque, ptr)`.
 *
 * @p ptr The pointer to reference count.
 * @p ctrlAlloc Used to manage ZL_Refcount_Control* lifetime.
 * If == NULL, @p ptr is considered an externally managed memory,
 * and just referenced (no Free operation will be triggered).
 * @p freeBufferFn The function used to free the pointer on reaching 0.
 * @p opaque An optional state pointer to pass to @p freeFn .
 *
 * @returns Success or an error on allocation failure
 */
ZL_Report ZL_Refcount_init(
        ZL_Refcount* rc,
        void* ptr,
        const ALLOC_CustomAllocation* ctrlAlloc,
        ZL_Refcount_FreeFn freeBufferFn,
        void* opaque);

/// Initializes the reference with a pointer that has been created with
/// malloc(). ZL_Refcount_Control* will be allocated with malloc() too.
ZL_Report ZL_Refcount_initMalloc(ZL_Refcount* rc, void* ptr);

/// Initializes the reference with a constant reference that will not be freed.
/// This object is never mutable.
/// WARNING: The reference must outlive the reference counted object.
ZL_Report ZL_Refcount_initConstRef(ZL_Refcount* rc, void const* ptr);

/// Initializes the reference with a mutable reference that will not be freed.
/// WARNING: The reference must outlive the reference counted object.
ZL_Report ZL_Refcount_initMutRef(ZL_Refcount* rc, void* ptr);

/// Helper function, which **allocates** a buffer of size @s within provided
/// @arena, and also allocated the control structure ZL_Refcount_Control* in
/// the same Arena, for compatibility with Arena's freeAll().
/// @return NULL on error.
void* ZL_Refcount_inArena(ZL_Refcount* rc, Arena* arena, size_t s);

/// Destroys the refcounted pointer and if it is the last instance frees it.
void ZL_Refcount_destroy(ZL_Refcount* rc);

/// @returns true iff the @p rc is mutable, which happens when the reference
/// count is one and it is not marked as immutable.
bool ZL_Refcount_mutable(ZL_Refcount const* rc);

/// @returns true iff @p rc is NULL (zero initialized).
bool ZL_Refcount_null(ZL_Refcount const* rc);

/// Marks the @p rc instance as immutable. Does not affect other references.
/// NOTE: Any ZL_Refcount that has a reference count > 1 is always immutable.
void ZL_Refcount_constify(ZL_Refcount* rc);

/**
 * Make a copy of @p rc and increment the reference count.
 */
ZL_Refcount ZL_Refcount_copy(ZL_Refcount const* rc);

/**
 * Alias @p rc by incrementing its refcount, but pointing to @p ptr.
 *
 * This is useful to get a pointer to a subobject of @p rc that shares
 * its lifetime. E.g. getting a pointer to a member variable of a struct.
 */
ZL_Refcount ZL_Refcount_aliasPtr(ZL_Refcount const* rc, void* ptr);

/**
 * Alias @p rc by incrementing its refcount, but adding @p offset to the
 * pointer.
 *
 * Helper function to get a an aliasing pointer to an offset @p offset bytes
 * past ZL_Refcount_get(rc).
 */
ZL_Refcount ZL_Refcount_aliasOffset(ZL_Refcount const* rc, size_t offset);

/// Get an immutable pointer to @p rc.
ZL_INLINE void const* ZL_Refcount_get(ZL_Refcount const* rc)
{
    return rc->_ptr;
}

/**
 * Get a mutable pointer to @p rc.
 *
 * @pre @p rc must be a mutable pointer.
 */
ZL_INLINE void* ZL_Refcount_getMut(ZL_Refcount* rc)
{
    ZL_ASSERT(ZL_Refcount_mutable(rc));
    return rc->_ptr;
}

ZL_END_C_DECLS

#endif
