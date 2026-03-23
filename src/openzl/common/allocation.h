// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMMON_ALLOCATION_H
#define ZSTRONG_COMMON_ALLOCATION_H

#include <stddef.h> // size_t
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

// Static default allocation functions
void* ZL_malloc(size_t size);
void* ZL_calloc(size_t size);
void* ZL_realloc(void* ptr, size_t size);
void ZL_free(void* ptr);

// Fill target memory area with zeroes
void ZL_zeroes(void* ptr, size_t size);

// Description Structure for custom Allocation
typedef void* (*ALLOC_CustomMalloc)(void* opaque, size_t size)
        ZL_NOEXCEPT_FUNC_PTR;
typedef void (*ALLOC_CustomFree)(void* opaque, void* ptr) ZL_NOEXCEPT_FUNC_PTR;
typedef struct {
    ALLOC_CustomMalloc malloc;
    ALLOC_CustomFree sfree;
    void* opaque;
} ALLOC_CustomAllocation;

/* ============================================== */
/* =====   Arena   ===== */
/* ============================================== */
/*
 * The Arena type defines interfaces that
 * manage groups of allocated memory objects,
 * using custom allocators functions.
 * The key property is freeAll, which is expected to release all memory
 * allocated previously in the same Arena.
 */
typedef struct Arena_s Arena;

// Arena contract definition
struct Arena_s {
    // Allocate an uninitialized memory object associated to the arena
    void* (*malloc)(Arena* arena, size_t size)ZL_NOEXCEPT_FUNC_PTR;
    // Allocate a memory object initialized to zeroes associated to the arena
    void* (*calloc)(Arena* arena, size_t size)ZL_NOEXCEPT_FUNC_PTR;
    // Realloc
    void* (*realloc)(Arena* arena, void* ptr, size_t newSize);
    // Frees the memory object associated to the arena
    // Note: trying to free a memory object *not* associated to the arena is UB
    void (*free)(Arena* arena, void* ptr) ZL_NOEXCEPT_FUNC_PTR;
    // Frees all memory objects allocated by this arena instance
    void (*freeAll)(Arena* arena) ZL_NOEXCEPT_FUNC_PTR;
    // Frees the arena and all associated memory objects
    void (*freeArena)(Arena* arena) ZL_NOEXCEPT_FUNC_PTR;
    // Returns memory currently allocated by @arena
    size_t (*memAllocated)(const Arena* arena) ZL_NOEXCEPT_FUNC_PTR;
    // Returns memory currently in use by @arena
    size_t (*memUsed)(const Arena* arena) ZL_NOEXCEPT_FUNC_PTR;
};

/* ALLOC_Arena_malloc:
 * Allocates uninitialized memory using `arena`. */
void* ALLOC_Arena_malloc(Arena* arena, size_t size);

/* ALLOC_Arena_calloc:
 * Allocates zeroed memory using `arena`. */
void* ALLOC_Arena_calloc(Arena* arena, size_t size);

/**
 * realloc() memory.
 *
 * @note Growable containers should allocate all their growable memory
 * with realloc(), even the first allocation where @p ptr == NULL. This
 * tells the arena that this allocation is likely to change in size.
 */
void* ALLOC_Arena_realloc(Arena* arena, void* ptr, size_t newSize);

/* ALLOC_Arena_free:
 * Frees memory allocated by `arena` at address `ptr`.
 * Arena can still be used afterwards. */
void ALLOC_Arena_free(Arena* arena, void* ptr);

/* ALLOC_Arena_freeAll:
 * Frees all memory allocated by `arena`. */
void ALLOC_Arena_freeAll(Arena* arena);

/* ALLOC_Arena_freeArena:
 * Frees the arena. Also free all associated memory segments. */
void ALLOC_Arena_freeArena(Arena* arena);

/*
 * HeapArena:
 * This is a simple arena that uses the system's heap and keeps track of
 * allocations so `freeAll` and `freeArena` can remove all allocated data.
 * @returns NULL on failure.
 */
Arena* ALLOC_HeapArena_create(void);

/*
 * StackArena:
 * This arena implements a "stack allocator", centered on one Primary Buffer.
 * Memory objects are allocated as Slices into this Primary Buffer.
 *
 * When there is not enough room in the Primary Buffer,
 * StackArena employs a HeapArena as backup,
 * and since it's not possible to safely increase Primary Buffer's size without
 * modifying pointer addresses.
 * Then, at next session (a session ends with an invocation to
 * ALLOC_Arena_freeAll()), the Primary Buffer is speculatively resized based on
 * previous session's needs.
 * Overtime, given an homogeneous workload, it's expected that the Primary
 * Buffer will stabilize on a memory budget suitable for all sessions on the
 * same workload.
 * Conversely, if the Primary Buffer has been oversized due to one exceptional
 * job, StackArena will detect it and dynamically size it down to better reflect
 * the more general session's needs.
 *
 * This allocation strategy is designed to reduce stress on malloc/free and the
 * page manager.
 */
Arena* ALLOC_StackArena_create(void);

// Maximum size of StackArena's Primary buffer
// Beyond that amount, requests are served by a HeapArena backup
#define ALLOC_STACK_SIZE_MAX \
    (1U << 30) // could be candidate as compilation variable in the future

/* =============================== */
/*      Accessors   */
/* =============================== */

/* @return the amount of memory currently allocated by @arena */
size_t ALLOC_Arena_memAllocated(const Arena* arena);

/* @return the amount of memory currently in use in @arena
 * necessarily <= memAllocated */
size_t ALLOC_Arena_memUsed(const Arena* arena);

/* =============================== */
/*      Macro Helpers   */
/* =============================== */

// Note: these macros require including the error header.
// They only work if the host function has declared ZL_RESULT_DECLARE_SCOPE.
#define ALLOC_CHECKED(_type, _var, _mallocf, _count)                    \
    _type* _var;                                                        \
    {                                                                   \
        size_t _allocSize;                                              \
        ZL_ERR_IF(                                                      \
                ZL_overflowMulST(sizeof(_type), (_count), &_allocSize), \
                allocation);                                            \
        _var = (_mallocf)(_allocSize);                                  \
    }                                                                   \
    ZL_ERR_IF_NULL(                                                     \
            _var,                                                       \
            allocation,                                                 \
            "cannot allocate buffer of %zu bytes using %s",             \
            (_count) * sizeof(_type),                                   \
            #_mallocf)

#define ALLOC_MALLOC_CHECKED(_type, _var, _count) \
    ALLOC_CHECKED(_type, _var, ZL_malloc, _count)

#define ALLOC_ARENA_CHECKED(_type, _var, _mallocf, _count, _arena)      \
    _type* _var;                                                        \
    {                                                                   \
        size_t _allocSize;                                              \
        ZL_ERR_IF(                                                      \
                ZL_overflowMulST(sizeof(_type), (_count), &_allocSize), \
                allocation);                                            \
        _var = (_mallocf)(_arena, _allocSize);                          \
    }                                                                   \
    ZL_ERR_IF_NULL(                                                     \
            _var,                                                       \
            allocation,                                                 \
            "cannot allocate buffer of %zu bytes using %s",             \
            (_count) * sizeof(_type),                                   \
            #_arena)

#define ALLOC_ARENA_MALLOC_CHECKED(_type, _var, _count, _arena) \
    ALLOC_ARENA_CHECKED(_type, _var, ALLOC_Arena_malloc, _count, _arena)

#define ALLOC_ARENA_CALLOC_CHECKED(_type, _var, _count, _arena) \
    ALLOC_ARENA_CHECKED(_type, _var, ALLOC_Arena_calloc, _count, _arena)

ZL_END_C_DECLS

#endif // ZSTRONG_COMMON_ALLOCATION_H
