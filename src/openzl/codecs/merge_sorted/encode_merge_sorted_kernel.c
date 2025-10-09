// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/merge_sorted/encode_merge_sorted_kernel.h"

#include "openzl/common/errors_internal.h"
#include "openzl/common/set.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/utils.h"

ZL_DECLARE_SET_TYPE(PQSet, uint32_t);

typedef struct {
    uint32_t size;
    uint32_t heap[64];
    PQSet set;
} PriorityQueue;

static size_t Detail_PQ_parent(size_t idx)
{
    return (idx - 1) / 2;
}

static size_t Detail_PQ_left(size_t idx)
{
    return (2 * idx) + 1;
}

static size_t Detail_PQ_right(size_t idx)
{
    return (2 * idx) + 2;
}

static void Detail_PQ_swap(uint32_t* x, uint32_t* y)
{
    uint32_t xv = *x;
    *x          = *y;
    *y          = xv;
}

static void Detail_PQ_heapifyUp(PriorityQueue* pq, size_t idx)
{
    size_t parent = Detail_PQ_parent(idx);
    while (idx != 0 && pq->heap[parent] > pq->heap[idx]) {
        Detail_PQ_swap(&pq->heap[parent], &pq->heap[idx]);
        idx    = parent;
        parent = Detail_PQ_parent(idx);
    }
}

static void Detail_PQ_heapifyDown(PriorityQueue* pq, size_t idx)
{
    uint32_t* const heap = pq->heap;
    size_t const size    = pq->size;
    size_t const left    = Detail_PQ_left(idx);
    size_t const right   = Detail_PQ_right(idx);
    size_t min           = idx;

    min = left < size && heap[left] < heap[min] ? left : min;
    min = right < size && heap[right] < heap[min] ? right : min;

    if (min != idx) {
        Detail_PQ_swap(&heap[min], &heap[idx]);
        Detail_PQ_heapifyDown(pq, min);
    }
}

static void PQ_insert(PriorityQueue* pq, uint32_t val)
{
    assert(pq->size < ZL_ARRAY_SIZE(pq->heap));
    assert(PQSet_size(&pq->set) < ZL_ARRAY_SIZE(pq->heap));
    PQSet_Insert insert = PQSet_insertVal(&pq->set, val);
    assert(!insert.badAlloc);
    if (insert.inserted) {
        size_t const idx = pq->size++;
        pq->heap[idx]    = val;
        Detail_PQ_heapifyUp(pq, idx);
    }
}

static uint32_t PQ_popMin(PriorityQueue* pq)
{
    assert(pq->size > 0);
    uint32_t const min = pq->heap[0];

    bool const erased = PQSet_eraseVal(&pq->set, min);
    ZL_ASSERT(erased);

    --pq->size;

    if (pq->size > 0) {
        pq->heap[0] = pq->heap[pq->size];
        Detail_PQ_heapifyDown(pq, 0);
    }

    return min;
}

static bool PQ_init(PriorityQueue* pq, size_t nbSrcs)
{
    pq->size = 0;
    assert(nbSrcs <= ZL_ARRAY_SIZE(pq->heap));
    pq->set = PQSet_create((uint32_t)nbSrcs + 1);
    return PQSet_reserve(
            &pq->set, (uint32_t)nbSrcs, /* guaranteeNoAllocations */ true);
}

static bool PQ_isEmpty(PriorityQueue const* pq)
{
    return pq->size == 0;
}

static void PQ_destroy(PriorityQueue* pq)
{
    PQSet_destroy(&pq->set);
}

ZL_FORCE_INLINE ZL_Report ZL_MergeSorted_merge(
        PriorityQueue* pq,
        char* bitsets,
        uint32_t* merged,
        const uint32_t** restrict srcStarts,
        const uint32_t** restrict srcEnds,
        size_t nbSrcs,
        size_t kBitsetWidth)
{
    assert(kBitsetWidth <= 8);
    assert(nbSrcs <= kBitsetWidth * 8);
    ZL_RET_R_IF_NOT(allocation, PQ_init(pq, nbSrcs));

    const uint32_t* srcs[64];
    memcpy((void*)srcs, (const void*)srcStarts, sizeof(*srcs) * nbSrcs);

    for (size_t i = 0; i < nbSrcs; ++i) {
        if (srcs[i] != srcEnds[i]) {
            PQ_insert(pq, srcs[i][0]);
        }
    }

    // Tell the compiler our trip count is <= kBitSetWidth * 8.
    nbSrcs                = ZL_MIN(nbSrcs, kBitsetWidth * 8);
    size_t nbUniqueValues = 0;
    while (!PQ_isEmpty(pq)) {
        uint32_t const min = PQ_popMin(pq);
        uint64_t bitset    = 0;
        uint64_t const one = 1;
        for (size_t i = 0; i < nbSrcs; ++i) {
            uint32_t const* src          = srcs[i];
            uint32_t const* const srcEnd = srcEnds[i];
            assert(src == srcEnd || src[0] >= min);
            if (src != srcEnd && src[0] == min) {
                bitset |= (one << i);
                ++src;
                if (src != srcEnd) {
                    assert(src[0] > min);
                    PQ_insert(pq, src[0]);
                }
                srcs[i] = src;
            }
        }
        ZL_writeN(
                &bitsets[nbUniqueValues * kBitsetWidth], bitset, kBitsetWidth);
        merged[nbUniqueValues] = min;
        assert(nbUniqueValues == 0 || merged[nbUniqueValues - 1] < min);
        ++nbUniqueValues;
    }
    return ZL_returnValue(nbUniqueValues);
}

ZL_Report ZL_MergeSorted_merge8x32(
        uint8_t* bitsets,
        uint32_t* merged,
        uint32_t const** srcs,
        uint32_t const** srcEnds,
        size_t nbSrcs)
{
    PriorityQueue pq;
    ZL_Report ret = ZL_MergeSorted_merge(
            &pq, (char*)bitsets, merged, srcs, srcEnds, nbSrcs, 1);
    PQ_destroy(&pq);
    return ret;
}

ZL_Report ZL_MergeSorted_merge16x32(
        uint16_t* bitsets,
        uint32_t* merged,
        uint32_t const** srcs,
        uint32_t const** srcEnds,
        size_t nbSrcs)
{
    PriorityQueue pq;
    ZL_Report ret = ZL_MergeSorted_merge(
            &pq, (char*)bitsets, merged, srcs, srcEnds, nbSrcs, 2);
    PQ_destroy(&pq);
    return ret;
}

ZL_Report ZL_MergeSorted_merge32x32(
        uint32_t* bitsets,
        uint32_t* merged,
        uint32_t const** srcs,
        uint32_t const** srcEnds,
        size_t nbSrcs)
{
    PriorityQueue pq;
    ZL_Report ret = ZL_MergeSorted_merge(
            &pq, (char*)bitsets, merged, srcs, srcEnds, nbSrcs, 4);
    PQ_destroy(&pq);
    return ret;
}

ZL_Report ZL_MergeSorted_merge64x32(
        uint64_t* bitsets,
        uint32_t* merged,
        uint32_t const** restrict srcs,
        uint32_t const** restrict srcEnds,
        size_t nbSrcs)
{
    PriorityQueue pq;
    ZL_Report ret = ZL_MergeSorted_merge(
            &pq, (char*)bitsets, merged, srcs, srcEnds, nbSrcs, 8);
    PQ_destroy(&pq);
    return ret;
}
