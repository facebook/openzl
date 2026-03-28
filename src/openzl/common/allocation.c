// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/allocation.h"
#include "openzl/common/assertion.h"
#include "openzl/common/limits.h"
#include "openzl/shared/overflow.h"
#include "openzl/shared/utils.h"

#include <stdalign.h>
#include <stdlib.h> // malloc, free
#include <string.h> // memset

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION

__attribute__((weak)) bool ZS2_malloc_should_fail(size_t size);

#endif

void* ZL_malloc(size_t s)
{
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    if (ZS2_malloc_should_fail && ZS2_malloc_should_fail(s)) {
        return NULL;
    }
    // Cap allocations at 100MB for fuzzer builds
    if (s > 100 * 1024 * 1024) {
        return NULL;
    }
#endif
    return malloc(s);
}

void* ZL_calloc(size_t s)
{
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    if (ZS2_malloc_should_fail && ZS2_malloc_should_fail(s)) {
        return NULL;
    }
    // Cap allocations at 100MB for fuzzer builds
    if (s > 100 * 1024 * 1024) {
        return NULL;
    }
#endif
    return calloc(1, s);
}

void* ZL_realloc(void* ptr, size_t size)
{
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    if (ZS2_malloc_should_fail && ZS2_malloc_should_fail(size)) {
        return NULL;
    }
#endif
    return realloc(ptr, size);
}

void ZL_free(void* p)
{
    free(p);
}

void ZL_zeroes(void* p, size_t s)
{
    memset(p, 0, s);
}

/*
 * Arena internals and API:
 */

void* ALLOC_Arena_malloc(Arena* arena, size_t size)
{
    ZL_ASSERT_NN(arena);
    ZL_ASSERT_NN(arena->malloc);
    return arena->malloc(arena, size);
}
void* ALLOC_Arena_calloc(Arena* arena, size_t size)
{
    ZL_ASSERT_NN(arena);
    ZL_ASSERT_NN(arena->calloc);
    return arena->calloc(arena, size);
}
void* ALLOC_Arena_realloc(Arena* arena, void* ptr, size_t newSize)
{
    ZL_ASSERT_NN(arena);
    ZL_ASSERT_NN(arena->realloc);
    return arena->realloc(arena, ptr, newSize);
}
void ALLOC_Arena_free(Arena* arena, void* ptr)
{
    ZL_ASSERT_NN(arena);
    ZL_ASSERT_NN(arena->free);
    arena->free(arena, ptr);
}
void ALLOC_Arena_freeAll(Arena* arena)
{
    ZL_DLOG(BLOCK, "ALLOC_Arena_freeAll (address:%p)", arena);
    if (arena == NULL)
        return;
    ZL_ASSERT_NN(arena->freeAll);
    arena->freeAll(arena);
}
void ALLOC_Arena_freeArena(Arena* arena)
{
    ZL_DLOG(OBJ, "ALLOC_Arena_freeArena (address:%p)", arena);
    if (arena == NULL)
        return;
    ZL_ASSERT_NN(arena->freeArena);
    arena->freeArena(arena);
}

size_t ALLOC_Arena_memAllocated(const Arena* arena)
{
    if (arena == NULL)
        return 0;
    ZL_ASSERT_NN(arena->memAllocated);
    return arena->memAllocated(arena);
}

size_t ALLOC_Arena_memUsed(const Arena* arena)
{
    if (arena == NULL)
        return 0;
    ZL_ASSERT_NN(arena->memUsed);
    return arena->memUsed(arena);
}

/*
 * ==============================================================
 * HeapArena:
 * ==============================================================
 */

typedef union {
    struct {
        size_t index;
        size_t size;
    }; // Anonymous struct (C11)
    char _padding[16];
} HeapMeta;

DECLARE_VECTOR_POINTERS_TYPE(HeapMeta)

typedef struct {
    Arena base;
    VECTOR_POINTERS(HeapMeta) ptrs;
} HeapArena;

/**
 * Implements the shared code for malloc and calloc.
 * @param meta A pointer to a HeapMeta object, or NULL if allocation failed.
 *             It should be (sizeof(HeapMeta) + size) bytes.
 * @param size The user requested size of the allocation.
 * @returns The pointer or NULL on failure. On failure @p meta is freed.
 */
static void*
ALLOC_HeapArena_allocImpl(HeapArena* arena, HeapMeta* meta, size_t size)
{
    if (meta == NULL) {
        return NULL;
    }
    ZL_ASSERT_EQ((uintptr_t)meta % alignof(HeapMeta), 0);
    meta->index = VECTOR_SIZE(arena->ptrs);
    meta->size  = size;

    if (!VECTOR_PUSHBACK(arena->ptrs, meta)) {
        ZL_LOG(ERROR, "Failed to push ptr into HeapArena");
        ZL_free(meta);
        return NULL;
    }
    ZL_STATIC_ASSERT(
            sizeof(HeapMeta) == 16,
            "sizeof(HeapMeta) must be 16 to guarantee alignment");
    return (void*)(meta + 1);
}

static void* ALLOC_HeapArena_malloc(Arena* arena, size_t size)
{
    ZL_ASSERT_NN(arena);
    HeapArena* const heapArena = ZL_CONTAINER_OF(arena, HeapArena, base);
    size_t allocSize;
    if (ZL_overflowAddST(size, sizeof(HeapMeta), &allocSize)) {
        return NULL;
    }
    void* const ptr = ZL_malloc(allocSize);
    return ALLOC_HeapArena_allocImpl(heapArena, ptr, size);
}

static void* ALLOC_HeapArena_calloc(Arena* arena, size_t size)
{
    ZL_ASSERT_NN(arena);
    HeapArena* heapArena = ZL_CONTAINER_OF(arena, HeapArena, base);
    size_t allocSize;
    if (ZL_overflowAddST(size, sizeof(HeapMeta), &allocSize)) {
        return NULL;
    }
    void* const ptr = ZL_calloc(allocSize);
    return ALLOC_HeapArena_allocImpl(heapArena, ptr, size);
}

static void* ALLOC_HeapArena_realloc(Arena* arena, void* ptr, size_t newSize)
{
    ZL_ASSERT_NN(arena);
    if (ptr == NULL) {
        return ALLOC_HeapArena_malloc(arena, newSize);
    }
    HeapArena* heapArena    = ZL_CONTAINER_OF(arena, HeapArena, base);
    HeapMeta* const oldMeta = (HeapMeta*)ptr - 1;
    HeapMeta* const newMeta = ZL_realloc(oldMeta, sizeof(HeapMeta) + newSize);
    if (newMeta == NULL) {
        return NULL;
    }
    newMeta->size                              = newSize;
    VECTOR_AT(heapArena->ptrs, newMeta->index) = newMeta;
    return (void*)(newMeta + 1);
}

static void ALLOC_HeapArena_freeSegment(Arena* arena, void* ptr)
{
    if (ptr == NULL)
        return;
    ZL_ASSERT_NN(arena);
    HeapArena* const heapArena = ZL_CONTAINER_OF(arena, HeapArena, base);
    HeapMeta* const meta       = (HeapMeta*)ptr - 1;

    const size_t index   = meta->index;
    const size_t numPtrs = VECTOR_SIZE(heapArena->ptrs);
    ZL_ASSERT_LT(index, numPtrs);
    ZL_ASSERT_EQ(VECTOR_AT(heapArena->ptrs, index), meta);

    // Unconditionally move last element to this position
    // This is a no-op when index == numPtrs - 1, but avoid a branch
    HeapMeta* const last = VECTOR_AT(heapArena->ptrs, numPtrs - 1);
    ZL_ASSERT_EQ(last->index, numPtrs - 1);
    VECTOR_AT(heapArena->ptrs, index) = last;
    last->index                       = index;

    // Remove last element
    VECTOR_POPBACK(heapArena->ptrs);

    ZL_free(meta);
}

static void ALLOC_HeapArena_freeAllSegments(Arena* arena)
{
    ZL_DLOG(BLOCK, "ALLOC_HeapArena_freeAllSegments (address:%p)", arena);
    if (arena == NULL)
        return;
    HeapArena* const heapArena = ZL_CONTAINER_OF(arena, HeapArena, base);
    const size_t numPtrs       = VECTOR_SIZE(heapArena->ptrs);
    for (size_t i = 0; i < numPtrs; ++i) {
        ZL_free(VECTOR_AT(heapArena->ptrs, i));
    }
    VECTOR_CLEAR(heapArena->ptrs);
}

static void ALLOC_HeapArena_destroy(HeapArena* arena)
{
    ZL_ASSERT_NN(arena);
    ALLOC_HeapArena_freeAllSegments(&arena->base);
    ZL_ASSERT_EQ(VECTOR_SIZE(arena->ptrs), 0);
    VECTOR_DESTROY(arena->ptrs);
}

static void ALLOC_HeapArena_freeArena(Arena* arena)
{
    ZL_DLOG(OBJ, "ALLOC_HeapArena_freeArena (address:%p)", arena);
    if (arena == NULL) {
        return;
    }
    HeapArena* const heapArena = ZL_CONTAINER_OF(arena, HeapArena, base);
    ALLOC_HeapArena_destroy(heapArena);

    ZL_free(arena);
}

static size_t ALLOC_HeapArena_countMem(const HeapArena* heapArena)
{
    if (heapArena == NULL)
        return 0;
    size_t totalMem      = 0;
    const size_t numPtrs = VECTOR_SIZE(heapArena->ptrs);
    for (size_t i = 0; i < numPtrs; ++i) {
        totalMem += VECTOR_AT(heapArena->ptrs, i)->size;
    }
    return totalMem;
}

static size_t ALLOC_HeapArena_memAllocated(const Arena* arena)
{
    if (arena == NULL)
        return 0;
    const HeapArena* const heapArena =
            ZL_CONST_CONTAINER_OF(arena, HeapArena, base);
    return ALLOC_HeapArena_countMem(heapArena);
}

static size_t ALLOC_HeapArena_memUsed(const Arena* arena)
{
    return ALLOC_Arena_memAllocated(arena);
}

static const Arena kHeapArena = {
    .malloc       = ALLOC_HeapArena_malloc,
    .calloc       = ALLOC_HeapArena_calloc,
    .realloc      = ALLOC_HeapArena_realloc,
    .free         = ALLOC_HeapArena_freeSegment,
    .freeAll      = ALLOC_HeapArena_freeAllSegments,
    .freeArena    = ALLOC_HeapArena_freeArena,
    .memAllocated = ALLOC_HeapArena_memAllocated,
    .memUsed      = ALLOC_HeapArena_memUsed,
};
static void ALLOC_HeapArena_init(HeapArena* a)
{
    a->base = kHeapArena;
    VECTOR_INIT(a->ptrs, ZL_CONTAINER_SIZE_LIMIT);
}

Arena* ALLOC_HeapArena_create(void)
{
    HeapArena* const arena = ZL_malloc(sizeof(HeapArena));
    if (arena) {
        ALLOC_HeapArena_init(arena);
    }
    return &arena->base;
}

/*
 * ==============================================================
 * StackArena:
 * ==============================================================
 * This allocator tries to create one Primary Buffer per Arena
 * and assigns Slices from it to future requests from this Arena.
 * It is designed to reduce stress on malloc/free,
 * in scenario where Arenas are maintained and re-used over multiple sessions.
 *
 * When there is not enough room in the primary buffer for a new slice,
 * create additional buffers with malloc/free, as usual.
 * Primary Buffer is then resized dynamically as needed.
 */

typedef struct StackArena_s StackArena;

struct StackArena_s {
    Arena base; // must be first member
    void* primaryBuffer;
    size_t pBuffCapacity;
    size_t pBuffUsed;
    size_t wouldHaveNeeded; // Tracks amount of memory allocated outside of
                            // @primaryBuffer
    size_t sessionUsage;    // Tracks amount of memory requested from this arena
                            // before a reset
    size_t wasted;          // pBuffCapacity * nbTimesUsedWastefully,
                            // to trigger a size-down event
    HeapArena heapBackup;
};

static void* ALLOC_StackArena_malloc(Arena* arena, size_t size)
{
    ZL_ASSERT_NN(arena);
    StackArena* const pba = ZL_CONTAINER_OF(arena, StackArena, base);
    ZL_ASSERT_GE(pba->pBuffCapacity, pba->pBuffUsed);
    size_t pBuffAvailable         = pba->pBuffCapacity - pba->pBuffUsed;
    static const size_t alignment = 16; // could become a variable in the future
    size_t const neededSize       = size + (alignment - 1);
    pba->sessionUsage += neededSize;

    if ((pba->pBuffUsed == 0) /* first request in session */
        && ((pBuffAvailable < neededSize) /* insufficient capacity for first request */
            || (pBuffAvailable < pba->wouldHaveNeeded) /* insufficient capacity for previous session */)) {
        /* let's resize the primary buffer, which is too small */
        ZL_free(pba->primaryBuffer);
        // We are going to reuse primaryBuffer for all future allocation in this
        // arena, so make it worthwhile (essentially 1 page - classic malloc
        // metadata), this will save a bunch of bump up if many small buffers
        // are requested.
        static const size_t pBuffSizeMin =
                4080; // could become a compilation constant in the future
        static const size_t pBuffSizeMax = ALLOC_STACK_SIZE_MAX;
        size_t const toAllocate =
                ZL_MAX(ZL_MAX(pba->wouldHaveNeeded, neededSize), pBuffSizeMin);
        if (toAllocate <= pBuffSizeMax) {
            pba->primaryBuffer = ZL_malloc(toAllocate);
        } else {
            /* request too large : do not allocate primaryBuffer
             * request will be taken care of by HeapArena backup */
            pba->primaryBuffer   = NULL;
            pba->wouldHaveNeeded = ZL_MIN(pba->wouldHaveNeeded, pBuffSizeMax);
        }
        if (pba->primaryBuffer) {
            pba->pBuffCapacity   = toAllocate;
            pba->pBuffUsed       = neededSize;
            pba->wouldHaveNeeded = toAllocate;
            ZL_ASSERT_EQ(
                    (size_t)pba->primaryBuffer % alignment,
                    0); /* note : this alignment method will have to change if
                           requested alignment is larger than base allocation of
                           primaryBuffer */
            return pba->primaryBuffer;
        }
        /* allocation failed */
        pba->pBuffCapacity   = 0;
        pba->pBuffUsed       = 0;
        pBuffAvailable       = 0;
        pba->wouldHaveNeeded = 0;
    }

    /* second+ request, or allocation of primary buffer failed */
    if (pBuffAvailable >= neededSize) {
        /* enough space in primary buffer -> assign a slice */
        size_t const start = ((pba->pBuffUsed + (alignment - 1)) / alignment)
                * alignment; /* ensure start position is properly aligned */
        ZL_ASSERT_GE(start, pba->pBuffUsed);
        ZL_ASSERT_LE(start + size, pba->pBuffCapacity);
        ZL_ASSERT_NN(pba->primaryBuffer);
        ZL_ASSERT_EQ(
                (size_t)pba->primaryBuffer % alignment,
                0); /* note : this alignment method will have to change if
                       requested alignment is larger than base allocation of
                       primaryBuffer */
        void* const r  = (char*)pba->primaryBuffer + start;
        pba->pBuffUsed = start + size;
        return r;
    }

    /* not enough space in primaryBuffer :
     * assign backup heap memory for this session
     * and track necessary space, for next session */
    ZL_ASSERT_GE(pba->wouldHaveNeeded, pba->pBuffCapacity);
    pba->wouldHaveNeeded += neededSize;
    return ALLOC_Arena_malloc(&pba->heapBackup.base, size);
}

static void* ALLOC_StackArena_calloc(Arena* arena, size_t size)
{
    ZL_ASSERT_NN(arena);
    StackArena* pba = ZL_CONTAINER_OF(arena, StackArena, base);
    /* note: could be optimized later, by a call to `calloc()` instead of
     * `malloc()` when reaching heapBackup */
    void* const r = ALLOC_Arena_malloc(&pba->base, size);
    if (r == NULL)
        return NULL;
    ZL_zeroes(r, size);
    return r;
}

static bool ALLOC_StackArena_inPrimaryBuffer(StackArena* arena, void* ptr)
{
    if (arena->primaryBuffer == NULL) {
        return false;
    }
    void* const pBuffEnd = (char*)arena->primaryBuffer + arena->pBuffCapacity;
    return (ptr >= arena->primaryBuffer && ptr < pBuffEnd);
}

static void* ALLOC_StackArena_realloc(Arena* arena, void* ptr, size_t newSize)
{
    ZL_ASSERT_NN(arena);
    StackArena* const pba = ZL_CONTAINER_OF(arena, StackArena, base);
    if (ALLOC_StackArena_inPrimaryBuffer(pba, ptr)) {
        // TODO: If ptr was the most recently allocated pointer, we could
        // realloc in-place. This optimization would make sense if we also
        // supported freeing the most recently allocated pointer.
        //
        // For now, we always copy into the heap arena.
        void* newPtr = ALLOC_Arena_malloc(&pba->heapBackup.base, newSize);
        if (newPtr == NULL) {
            return NULL;
        }
        // The old size isn't stored so copy based on the new size, and the
        // largest size it could've possibly been.
        ZL_ASSERT_NN(ptr);
        char* const pBuffEnd = (char*)pba->primaryBuffer + pba->pBuffCapacity;
        const size_t toCopy  = ZL_MIN((size_t)(pBuffEnd - (char*)ptr), newSize);
        memcpy(newPtr, ptr, toCopy);
        return newPtr;
    } else {
        return ALLOC_HeapArena_realloc(&pba->heapBackup.base, ptr, newSize);
    }
}

static void ALLOC_StackArena_freeSegment(Arena* arena, void* ptr)
{
    if (ptr == NULL)
        return;
    ZL_ASSERT_NN(arena);
    StackArena* const pba = ZL_CONTAINER_OF(arena, StackArena, base);
    if (ALLOC_StackArena_inPrimaryBuffer(pba, ptr)) {
        /* this ptr owns a slice within primaryBuffer -> don't free anything
         */
        return;
    }
    /* this ptr is presumed tracked by heapBackup */
    ALLOC_HeapArena_freeSegment(&pba->heapBackup.base, ptr);
}

#define PBUFF_USAGE_MIN(s) ((s) / 2) // arbitrary threshold

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
#    define PBUFF_SIZEDOWN_THRESHOLD (1U << 14)
#else
#    define PBUFF_SIZEDOWN_THRESHOLD (1U << 30) // arbitrary threshold
#endif

static void ALLOC_StackArena_freeAllSegments(Arena* arena)
{
    ZL_DLOG(BLOCK, "ALLOC_StackArena_freeAllSegments (address:%p)", arena);
    if (arena == NULL)
        return;
    StackArena* const pba = ZL_CONTAINER_OF(arena, StackArena, base);
    pba->pBuffUsed        = 0;
    ALLOC_HeapArena_freeAllSegments(&pba->heapBackup.base);
    if (pba->sessionUsage < PBUFF_USAGE_MIN(pba->pBuffCapacity)) {
        pba->wasted += pba->pBuffCapacity;
    } else {
        pba->wasted = 0;
    }
    if (pba->wasted > PBUFF_SIZEDOWN_THRESHOLD) {
        // size down the Primary buffer -> it has been too big for a while
        pba->pBuffCapacity /= 2;
        // Use realloc to improve odd of keeping current buffer in place.
        void* const newPBuffer =
                ZL_realloc(pba->primaryBuffer, pba->pBuffCapacity);
        if (newPBuffer != NULL) {
            pba->primaryBuffer = newPBuffer;
        } else {
            // Failed realloc: Just give up the primary buffer
            ZL_free(pba->primaryBuffer);
            pba->primaryBuffer = NULL;
            pba->pBuffCapacity = 0;
        }
        pba->wasted = 0;
    }
    pba->pBuffUsed    = 0;
    pba->sessionUsage = 0;
}

static void ALLOC_StackArena_freeArena(Arena* arena)
{
    if (arena == NULL)
        return;
    ZL_ASSERT_NN(arena->freeAll);
    arena->freeAll(arena); /* note: freeAll doesn't free the primaryBuffer */
    StackArena* const pba = ZL_CONTAINER_OF(arena, StackArena, base);
    ALLOC_HeapArena_destroy(&pba->heapBackup);
    ZL_free(pba->primaryBuffer);
    ZL_free(pba);
}

static size_t ALLOC_StackArena_memAllocated(const Arena* arena)
{
    if (arena == NULL)
        return 0;
    const StackArena* const stackArena =
            ZL_CONST_CONTAINER_OF(arena, StackArena, base);
    return stackArena->pBuffCapacity
            + ALLOC_HeapArena_countMem(&stackArena->heapBackup);
}

static size_t ALLOC_StackArena_memUsed(const Arena* arena)
{
    if (arena == NULL)
        return 0;
    const StackArena* const stackArena =
            ZL_CONST_CONTAINER_OF(arena, StackArena, base);
    return stackArena->pBuffUsed
            + ALLOC_HeapArena_countMem(&stackArena->heapBackup);
}

static const Arena kStackArena = {
    .malloc       = ALLOC_StackArena_malloc,
    .calloc       = ALLOC_StackArena_calloc,
    .realloc      = ALLOC_StackArena_realloc,
    .free         = ALLOC_StackArena_freeSegment,
    .freeAll      = ALLOC_StackArena_freeAllSegments,
    .freeArena    = ALLOC_StackArena_freeArena,
    .memAllocated = ALLOC_StackArena_memAllocated,
    .memUsed      = ALLOC_StackArena_memUsed,
};
Arena* ALLOC_StackArena_create(void)
{
    StackArena* const arena = ZL_calloc(sizeof(*arena));
    if (arena == NULL)
        return NULL;
    arena->base = kStackArena;
    ALLOC_HeapArena_init(&arena->heapBackup);
    return &arena->base;
}
