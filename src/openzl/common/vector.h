// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMMON_VECTOR_H
#define ZSTRONG_COMMON_VECTOR_H

#include <string.h>
#include "openzl/common/allocation.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/overflow.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_macro_helpers.h"

ZL_BEGIN_C_DECLS

/*************************************
 * The vector library is a lightweight header-only implementation of a vector.
 *
 * Basic usage:
 * ====================================
 * Declare a vector for type `T` using `VECTOR(T)`.
 * Declare a vector of pointers `T*` using `VECTOR_POINTERS(T)`.
 * You can then operate on this vector using `VECTOR_` macros.
 *
 * Note that `T` cannot include any special characters, this means that
 * `VECTOR(void*)` or `VECTOR_POINTERS(void*)` wouldn't work.
 * If you do need to use a pattern like this, use typedef to alias the pointer
 * type.
 *
 * Usage example:
 * ------------------
 * ```
 * size_t createPointersVector(void *p1, void *p2, void *p3) {
 *      // We want to use a vector of void* pointers
 *      VECTOR_POINTERS(void) pointers;
 *
 *      // initialize an empty vector with a maximum capacity of 100 elements
 *      VECTOR_INIT(pointers, 100);
 *
 *      // Add items to the vector, assert that operation is successful
 *      assert(VECTOR_PUSHBACK(vector, p1));
 *      assert(VECTOR_PUSHBACK(vector, p2));
 *
 *      // assign value to a specific location
 *      VECTOR_AT(vector, 0) = p3;
 *
 *      // get the vector's size
 *      const size_t size = VECTOR_SIZE(vector);
 *      // its also possible to access size directly with vector._generic.size,
 *
 *      // free all memory owner by the vector
 *      VECTOR_DESTROY(vector);
 *      return size;
 * }
 * ```
 *
 * Declaring a new VECTOR type:
 * ====================================
 * Before using `VECTOR(T)` we must declare a type to represent this vector.
 * This can be done using `DECLARE_VECTOR_TYPE(T)` for scalars or
 * `DECLARE_VECTOR_POINTERS_TYPE(T)` for pointers
 * Please place any `DECLARE_VECTOR_TYPE` of a common type in the bottom of
 * this file.
 * Any specialized vectors should be declared next to the declaration of the
 * type they contain.
 *
 * Implementation details:
 * ====================================
 * `GenericVector` is the type most of the internals operate on, it's simple
 * generalized form of vector that has size, capacity and an untyped pointer
 * to data.
 * `VECTOR(T)` aliases `GenericVector` (as `_generic`) and a similar structure
 * with a typed pointer.
 * We can then implement most of the functionality on `GenericVector` with a
 * thin layer of translation macros. At the same time we get to enjoy strong
 * typing, so the compiler will recognize that vectors of different types
 * are not the same.
 * Vector grows by a factor of 2 up to the size of 512 and by a factor of 1.25
 * from that point onwards.
 *
 *************************************/

/*************************************
 * Public API:
 *************************************/

/*
 * VECTOR:
 * Translates a scalar typename into its vector typename.
 */
#define VECTOR(type) ZS_MACRO_CONCAT(VECTOR_OF_, type)

/*
 * VECTOR_POINTERS:
 * Translates a scalar typename into a typename of vector of pointers to this
 * scalar type.
 */
#define VECTOR_POINTERS(type) ZS_MACRO_CONCAT(VECTOR(type), _pointers)

/*
 * VECTOR_CONST_POINTERS:
 * Translates a scalar typename into a typename of vector of const pointers to
 * this scalar type.
 */
#define VECTOR_CONST_POINTERS(type) \
    ZS_MACRO_CONCAT(VECTOR(type), _const_pointers)

/*
 * DECLARE_VECTOR_TYPE:
 * Declares a new vector type for the given typename. Create a definition for
 * `VECTOR(type)`.
 */
#define DECLARE_VECTOR_TYPE(type) _DECLARE_VECTOR_TYPE(type, VECTOR(type))

/*
 * DECLARE_VECTOR_POINTERS_TYPE:
 * Declares a new pointers vector type for the given typename. Create a
 * definition for `VECTOR_POINTERS(type)`.
 */
#define DECLARE_VECTOR_POINTERS_TYPE(type) \
    _DECLARE_VECTOR_TYPE(type*, VECTOR_POINTERS(type))

/*
 * DECLARE_VECTOR_CONST_POINTERS_TYPE:
 * Declares a new const pointers vector type for the given typename. Create a
 * definition for `VECTOR_CONST_POINTERS(type)`.
 */
#define DECLARE_VECTOR_CONST_POINTERS_TYPE(type) \
    _DECLARE_VECTOR_TYPE(type const*, VECTOR_CONST_POINTERS(type))

/*
 * VECTOR_GENERIC_POINTER:
 * Returns a GenericVector pointer for the given vector.
 */
#define VECTOR_GENERIC_POINTER(vec) (&(vec)._generic)

/*
 * VECTOR_ELEMENT_SIZE:
 * Return the size in bytes of an element in the vector.
 */
#define VECTOR_ELEMENT_SIZE(vec) (sizeof(*(vec)._typed.data))

/*
 * VECTOR_INIT:
 * Initializes `vec` to an empty vector with a maximum capacity of
 * `max_capacity` elements.
 */
#define VECTOR_INIT(vec, max_capacity) \
    GenericVector_init(VECTOR_GENERIC_POINTER(vec), NULL, max_capacity);

/*
 * VECTOR_INIT:
 * Initializes `vec` to an empty vector with a maximum capacity of
 * `max_capacity` elements in `arena`. The vector may be destroyed
 * with VECTOR_DESTROY(), or the memory may be implicitly released
 * by freeing all memory owned by `arena`.
 */
#define VECTOR_INIT_IN_ARENA(vec, arena, max_capacity) \
    GenericVector_init(VECTOR_GENERIC_POINTER(vec), (arena), max_capacity);

/*
 * VECTOR_EMPTY:
 * Initializes an empty vector as an expression.
 */
#define VECTOR_EMPTY(max_capacity) \
    { ._generic = GenericVector_empty(max_capacity) }

/*
 * VECTOR_RESET:
 * Clears vector and releases all memory owned by it.
 * After this operation `vec` will be an empty vector.
 */
#define VECTOR_RESET(vec) GenericVector_reset(VECTOR_GENERIC_POINTER(vec))

/*
 * VECTOR_DESTROY:
 * Destroys a vector and releases all memory owned by it.
 * After this operation `vec` will be an unusable vector (limited to 0
 * elements).
 */
#define VECTOR_DESTROY(vec) GenericVector_destroy(VECTOR_GENERIC_POINTER(vec))

/*
 * VECTOR_RESERVE:
 * Reserves capacity for the vector, if `vec` already has the required
 * capacity does nothing.
 * Returns the new capacity, if it's smaller than requested then the operation
 * failed and the vector should remain unchanged.
 */
#define VECTOR_RESERVE(vec, reservationSize) \
    GenericVector_reserve(                   \
            VECTOR_GENERIC_POINTER(vec),     \
            VECTOR_ELEMENT_SIZE(vec),        \
            (reservationSize))

/*
 * VECTOR_CLEAR:
 * Clears the vector without shrinking the capacity.
 */
#define VECTOR_CLEAR(vec) (void)VECTOR_RESIZE((vec), 0)

/*
 * VECTOR_RESIZE:
 * Resizes vector to specified size increasing capacity if needed.
 * If requested size is bigger than existing,
 * the newly added elements are zero initialized.
 * Returns the new size,
 * if it's smaller than requested then
 * the operation failed and the vector should remain unchanged.
 * This operation is guaranteed to succeed if the requested size is smaller
 * than the current size.
 */
#define VECTOR_RESIZE(vec, size) \
    GenericVector_resize(        \
            VECTOR_GENERIC_POINTER(vec), VECTOR_ELEMENT_SIZE(vec), size, true)

/*
 * VECTOR_RESIZE_UNINITIALIZED
 * Resizes the vector without initializing the values.
 */
#define VECTOR_RESIZE_UNINITIALIZED(vec, size) \
    GenericVector_resize(                      \
            VECTOR_GENERIC_POINTER(vec),       \
            VECTOR_ELEMENT_SIZE(vec),          \
            size,                              \
            false)

/*
 * VECTOR_POPBACK:
 * Pops the last item in the vector.
 * Asserts that vector has at least one item.
 */
#define VECTOR_POPBACK(vec) GenericVector_popBack(VECTOR_GENERIC_POINTER(vec))

/*
 * VECTOR_PUSHBACK:
 * Pushes an element to the end of the vector,
 * increases the vector's capacity if needed.
 * This macro requires getting a pointer to `elem`
 * so it cannot be used with literals in C++.
 * Returns true on success and false on failure.
 */
#define VECTOR_PUSHBACK(vec, elem)       \
    GenericVector_pushBack(              \
            VECTOR_GENERIC_POINTER(vec), \
            VECTOR_ELEMENT_SIZE(vec),    \
            &(elem),                     \
            sizeof(elem))

/*
 * VECTOR_SIZE:
 * Returns the number of elements in `vec`.
 */
#define VECTOR_SIZE(vec) GenericVector_size(&(vec)._generic)

/*
 * VECTOR_CAPACITY:
 * Returns the capacity of the vector.
 */
#define VECTOR_CAPACITY(vec) GenericVector_capacity(&(vec)._generic)

/*
 * VECTOR_MAX_CAPACITY:
 * Returns the maximum capacity of the vector.
 */
#define VECTOR_MAX_CAPACITY(vec) GenericVector_maxCapacity(&(vec)._generic)

/*
 * VECTOR_DATA:
 * Returns a pointer to the vector's underlying storage.
 * Pay attention to not mutate the vector while using this pointer as
 * it's not guaranteed memory wouldn't move.
 */
#define VECTOR_DATA(vec) ((vec)._typed.data)

/*
 * VECTOR_AT:
 * Mutable accessor to the vector at a specific index.
 */
#define VECTOR_AT(vec, i) VECTOR_DATA(vec)[(i)]

/*************************************
 * Private API:
 *************************************/

#define _DECLARE_VECTOR_TYPED_INTERNAL_NAME(name) name##_typed_internal
#define _DECLARE_VECTOR_TYPE(type, name)                      \
    typedef struct {                                          \
        const uint32_t size;                                  \
        const uint32_t capacity;                              \
        const uint32_t max_capacity;                          \
        type* const data;                                     \
        Arena* const arena;                                   \
    } _DECLARE_VECTOR_TYPED_INTERNAL_NAME(name);              \
    typedef struct {                                          \
        union {                                               \
            _DECLARE_VECTOR_TYPED_INTERNAL_NAME(name) _typed; \
            GenericVector _generic;                           \
        };                                                    \
    } name;

typedef struct {
    uint32_t size;
    uint32_t capacity;
    uint32_t max_capacity;
    void* data;
    Arena* arena;
} GenericVector;

ZL_INLINE size_t GenericVector_maxCapacity(GenericVector const* vec)
{
    return vec->max_capacity;
}

ZL_INLINE size_t GenericVector_nextCapacity(GenericVector* vec)
{
    size_t newCapacity;
    if (vec->capacity == 0) {
        // We go straight to 4 elements to save on reallocating for 1 and 2
        // elements
        newCapacity = 4;
    } else if (vec->capacity >= 512) {
        // capacity>=512 -> capacity * 1.25
        newCapacity = ((size_t)(vec->capacity) * 5) / 4;
    } else {
        // 0<capacity<512 -> capacity * 2
        newCapacity = (size_t)vec->capacity * 2;
    }
    if (newCapacity < vec->capacity
        || newCapacity > GenericVector_maxCapacity(vec)) {
        // overflow or just over max
        return GenericVector_maxCapacity(vec);
    }
    return newCapacity;
}

ZL_INLINE void
GenericVector_init(GenericVector* vec, Arena* arena, size_t max_capacity)
{
    memset((void*)vec, 0, sizeof(GenericVector));
    ZL_ASSERT_LT(max_capacity, UINT32_MAX);
    vec->max_capacity = (uint32_t)max_capacity;
    vec->arena        = arena;
}

ZL_INLINE GenericVector GenericVector_empty(size_t max_capacity)
{
    GenericVector vec;
    memset((void*)&vec, 0, sizeof(GenericVector));
    ZL_ASSERT_LT(max_capacity, UINT32_MAX);
    vec.max_capacity = (uint32_t)max_capacity;
    return vec;
}

ZL_INLINE size_t GenericVector_size(GenericVector const* vec)
{
    return vec->size;
}

ZL_INLINE size_t GenericVector_capacity(GenericVector const* vec)
{
    return vec->capacity;
}

ZL_INLINE void* GenericVector_data(GenericVector const* vec)
{
    return vec->data;
}

ZL_INLINE void* GenericVector_reallocData(GenericVector* vec, size_t newSize)
{
    if (vec->arena != NULL) {
        return ALLOC_Arena_realloc(vec->arena, vec->data, newSize);
    } else {
        return ZL_realloc(vec->data, newSize);
    }
}

ZL_NODISCARD ZL_INLINE size_t GenericVector_reserve(
        GenericVector* vec,
        size_t elementSize,
        size_t reservationSize)
{
    ZL_ASSERT_GT(GenericVector_maxCapacity(vec), 0);
    if (reservationSize <= vec->capacity
        || reservationSize > GenericVector_maxCapacity(vec)) {
        return vec->capacity;
    }

    // Ensure that we don't enter a N^2 scenario by growing to at least
    // to the next capacity when we grow.
    size_t const nextCapacity = GenericVector_nextCapacity(vec);
    if (reservationSize < nextCapacity) {
        reservationSize = nextCapacity;
    }

    if (nextCapacity > GenericVector_maxCapacity(vec)) {
        return vec->capacity;
    }

    size_t totalSize;
    if (ZL_overflowMulST(elementSize, reservationSize, &totalSize)) {
        return vec->capacity;
    }
    void* newPtr = GenericVector_reallocData(vec, totalSize);
    if (newPtr) {
        vec->data     = newPtr;
        vec->capacity = (uint32_t)reservationSize;
    }
    return vec->capacity;
}

ZL_NODISCARD ZL_INLINE size_t GenericVector_resize(
        GenericVector* vec,
        size_t elementSize,
        size_t size,
        bool initialize)
{
    if (size <= vec->size) {
        vec->size = (uint32_t)size;
        return vec->size;
    }

    if (GenericVector_reserve(vec, elementSize, size) < size) {
        return vec->size;
    }

    // memset the new bytes to zero.
    if (initialize) {
        char* const data        = (char*)vec->data;
        size_t const oldBytes   = vec->size * elementSize;
        size_t const addedBytes = size * elementSize - oldBytes;
        memset(data + oldBytes, 0, addedBytes);
    }

    vec->size = (uint32_t)size;
    return size;
}

ZL_NODISCARD ZL_INLINE bool GenericVector_pushBack(
        GenericVector* vec,
        size_t vectorElementSize,
        const void* element,
        size_t elementSize)
{
    (void)vectorElementSize; // unused outside of the assert
    ZL_ASSERT_EQ(vectorElementSize, elementSize);
    ZL_ASSERT_GT(GenericVector_maxCapacity(vec), 0);
    if (vec->size >= vec->capacity) {
        // we need to grow
        const size_t newCapacity = GenericVector_nextCapacity(vec);
        if (newCapacity == vec->capacity
            || GenericVector_reserve(vec, elementSize, newCapacity)
                    < newCapacity) {
            // Failed to increase capacity
            return false;
        }
    }
    memcpy((void*)((uint8_t*)vec->data + elementSize * vec->size),
           element,
           elementSize);
    vec->size += 1;
    return true;
}

ZL_INLINE void GenericVector_popBack(GenericVector* vec)
{
    ZL_ASSERT_GT(GenericVector_size(vec), 0);
    vec->size--;
}

ZL_INLINE void GenericVector_freeData(GenericVector* vec)
{
    if (vec->arena != NULL) {
        ALLOC_Arena_free(vec->arena, vec->data);
    } else {
        ZL_free(vec->data);
    }
}

ZL_INLINE void GenericVector_destroy(GenericVector* vec)
{
    GenericVector_freeData(vec);
    GenericVector_init(vec, NULL, 0);
}

ZL_INLINE void GenericVector_reset(GenericVector* vec)
{
    GenericVector_freeData(vec);
    GenericVector_init(vec, vec->arena, vec->max_capacity);
}

/*************************************
 * Common vector types:
 *************************************/
DECLARE_VECTOR_TYPE(size_t)
DECLARE_VECTOR_TYPE(uint8_t)
DECLARE_VECTOR_TYPE(uint32_t)
DECLARE_VECTOR_TYPE(int32_t)
DECLARE_VECTOR_TYPE(char)
DECLARE_VECTOR_POINTERS_TYPE(void)

ZL_END_C_DECLS

#endif /* ZSTRONG_COMMON_VECTOR_H */
