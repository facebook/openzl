// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMMON_NUMERIC_OPERATIONS_H
#define ZSTRONG_COMMON_NUMERIC_OPERATIONS_H

#include <stdint.h>
#include "openzl/common/errors_internal.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

size_t NUMOP_sumArrayST(const size_t array[], size_t arraySize);
uint64_t NUMOP_sumArray32(const uint32_t array[], size_t arraySize);

size_t NUMOP_findMaxST(const size_t array[], size_t arraySize);
uint8_t NUMOP_findMaxU8(const uint8_t array[], size_t arraySize);
uint32_t NUMOP_findMaxArr32(const uint32_t array32[], size_t arraySize);

int NUMOP_underLimit(const unsigned array[], size_t arraySize, unsigned limit);
int NUMOP_underLimitU8(const uint8_t arr8[], size_t arraySize, unsigned limit);
int NUMOP_underLimitU16(
        const uint16_t arr16[],
        size_t arraySize,
        unsigned limit);

/// Copies data from @p src to @p dst, with the bytes swapped.
/// @note No alignment is assumed on @p src or @p dst.
void NUMOP_byteswap8(void* dst, const void* src, size_t nbElts);

/// Copies data from @p src to @p dst, with the bytes swapped.
/// @note No alignment is assumed on @p src or @p dst.
void NUMOP_byteswap16(void* dst, const void* src, size_t nbElts);

/// Copies data from @p src to @p dst, with the bytes swapped.
/// @note No alignment is assumed on @p src or @p dst.
void NUMOP_byteswap32(void* dst, const void* src, size_t nbElts);

/// Copies data from @p src to @p dst, with the bytes swapped.
/// @note No alignment is assumed on @p src or @p dst.
void NUMOP_byteswap64(void* dst, const void* src, size_t nbElts);

/// Copies data from @p src to @p dst, with the bytes swapped.
/// @note No alignment is assumed on @p src or @p dst.
void NUMOP_byteswap(void* dst, const void* src, size_t nbElts, size_t eltSize);

/**
 * @returns the minimal integer size in bytes that fits the given value.
 * Possible return values are 1/2/4/8.
 */
size_t NUMOP_numericWidthForValue(uint64_t maxValue);
size_t NUMOP_numericWidthForArrayST(const size_t array[], size_t arraySize);
size_t NUMOP_numericWidthForArray32(const uint32_t array32[], size_t arraySize);

void NUMOP_writeNumerics_fromST(
        void* array,
        size_t numWidth,
        const size_t srcST[],
        size_t nbValues);
void NUMOP_writeST_fromNumerics(
        size_t* array,
        size_t nbValues,
        const void* srcNum,
        size_t numWidth);

void NUMOP_writeNumerics_fromU32(
        void* array,
        size_t numWidth,
        const uint32_t src32[],
        size_t nbValues);

/**
 * @returns an error if any value in @p srcNum does not fit in a uint32_t.
 */
ZL_Report NUMOP_write32_fromNumerics(
        uint32_t* array32,
        size_t nbValues,
        const void* srcNum,
        size_t numWidth);

ZL_END_C_DECLS

#endif // ZSTRONG_COMMON_NUMERIC_OPERATIONS_H
