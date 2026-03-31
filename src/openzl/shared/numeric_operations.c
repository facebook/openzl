// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/shared/numeric_operations.h"
#include <stdint.h>
#include "openzl/common/assertion.h"
#include "openzl/common/debug.h"
#include "openzl/shared/mem.h" // MEM_IS_ALIGNED

/* This version is safe of overflow,
 * as it would take an array of > 4 billions values
 * all very large to reach the limit of a `uint64_t`. */
uint64_t NUMOP_sumArray32(const uint32_t array[], size_t arraySize)
{
    if (arraySize == 0)
        return 0;
    ZL_ASSERT_NN(array);
    uint64_t total = 0;
    for (size_t n = 0; n < arraySize; n++) {
        total += array[n];
    }
    return total;
}

/* This version checks overflow at debug time only (assert()). */
size_t NUMOP_sumArrayST(const size_t array[], size_t arraySize)
{
    if (arraySize == 0)
        return 0;
    ZL_ASSERT_NN(array);
    size_t total = 0;
    for (size_t n = 0; n < arraySize; n++) {
        ZL_ASSERT_GE(total + array[n], total); // overflow check
        total += array[n];
    }
    return total;
}

size_t NUMOP_findMaxST(const size_t array[], size_t arraySize)
{
    if (arraySize)
        ZL_ASSERT_NN(array);
    size_t max = 0;
    for (size_t n = 0; n < arraySize; n++) {
        if (max < array[n])
            max = array[n];
    }
    return max;
}

uint8_t NUMOP_findMaxU8(const uint8_t array[], size_t arraySize)
{
    if (arraySize)
        ZL_ASSERT_NN(array);
    uint8_t max = 0;
    for (size_t n = 0; n < arraySize; n++) {
        if (max < array[n])
            max = array[n];
    }
    return max;
}

uint32_t NUMOP_findMaxArr32(const uint32_t array32[], size_t arraySize)
{
    if (arraySize)
        ZL_ASSERT_NN(array32);
    uint32_t max = 0;
    for (size_t n = 0; n < arraySize; n++) {
        if (max < array32[n])
            max = array32[n];
    }
    return max;
}

int NUMOP_underLimit(const unsigned* array, size_t arraySize, unsigned limit)
{
    int underLimit = 1;
    if (arraySize)
        ZL_ASSERT_NN(array);
    for (size_t n = 0; n < arraySize; n++) {
        underLimit &= (array[n] < limit);
    }
    return underLimit;
}

int NUMOP_underLimitU8(const uint8_t* arrayU8, size_t arraySize, unsigned limit)
{
    int underLimit = 1;
    if (arraySize)
        ZL_ASSERT_NN(arrayU8);
    for (size_t n = 0; n < arraySize; n++) {
        underLimit &= (arrayU8[n] < limit);
    }
    return underLimit;
}

int NUMOP_underLimitU16(
        const uint16_t* arrayU16,
        size_t arraySize,
        unsigned limit)
{
    int underLimit = 1;
    if (arraySize)
        ZL_ASSERT_NN(arrayU16);
    for (size_t n = 0; n < arraySize; n++) {
        underLimit &= (arrayU16[n] < limit);
    }
    return underLimit;
}

size_t NUMOP_numericWidthForValue(uint64_t maxValue)
{
    if (maxValue < 256)
        return 1;
    if (maxValue < 65536)
        return 2;
    if (maxValue <= (uint32_t)(-1))
        return 4;
    return 8;
}

size_t NUMOP_numericWidthForArrayST(const size_t array[], size_t arraySize)
{
    size_t const max = NUMOP_findMaxST(array, arraySize);
    return NUMOP_numericWidthForValue(max);
}

size_t NUMOP_numericWidthForArray32(const uint32_t array32[], size_t arraySize)
{
    uint32_t const max = NUMOP_findMaxArr32(array32, arraySize);
    ZL_STATIC_ASSERT(sizeof(uint32_t) <= sizeof(size_t), "");
    return NUMOP_numericWidthForValue(max);
}

/* Note (@yoniko) :
 * The following numeric conversion operations
 * have some close equivalent within rangePackEncode transform.
 * Consolidation might be possible,
 * but that their scope is not strictly equal,
 * and moving operations from transforms/ to common/ is unusual.
 * */
static void
writeNumeric_fromST(void* array, size_t numWidth, size_t pos, size_t in)
{
    ZL_ASSERT_NN(array);
    switch (numWidth) {
        case 1:
            ZL_ASSERT_LT(in, 256);
            ((uint8_t*)array)[pos] = (uint8_t)in;
            return;
        case 2:
            ZL_ASSERT_LT(in, 65536);
            ((uint16_t*)array)[pos] = (uint16_t)in;
            return;
        case 4:
            ZL_ASSERT_LE(in, (size_t)(uint32_t)(-1));
            ((uint32_t*)array)[pos] = (uint32_t)in;
            return;
        case 8: {
            ZL_STATIC_ASSERT(
                    sizeof(size_t) <= 8,
                    "only compatible with size_t max width 64-bit");
            ((uint64_t*)array)[pos] = (uint64_t)in;
            return;
        }
        default:
            ZL_ASSERT_FAIL("only numeric width 1,2,4,8 are allowed");
            return;
    }
}

void NUMOP_writeNumerics_fromST(
        void* array,
        size_t numWidth,
        const size_t srcST[],
        size_t nbValues)
{
    if (nbValues)
        ZL_ASSERT_NN(array);
    for (size_t n = 0; n < nbValues; n++) {
        writeNumeric_fromST(array, numWidth, n, srcST[n]);
    }
}

static size_t readNumeric_intoST(const void* array, size_t pos, size_t numWidth)
{
    ZL_ASSERT_NN(array);
    ZL_ASSERT(numWidth == 1 || numWidth == 2 || numWidth == 4 || numWidth == 8);
    ZL_ASSERT_EQ((size_t)array % numWidth, 0);
    switch (numWidth) {
        case 1:
            return ((const uint8_t*)array)[pos];
        case 2:
            return ((const uint16_t*)array)[pos];
        case 4:
            return ((const uint32_t*)array)[pos];
        case 8:
            ZL_ASSERT(sizeof(size_t) == 8);
            return ((const uint64_t*)array)[pos];
        default:
            ZL_ASSERT_FAIL("only numeric width 1,2,4,8 are allowed");
            return 0;
    }
}

// Note : this function is always successful
// (presuming its conditions are respected)
void NUMOP_writeST_fromNumerics(
        size_t* array,
        size_t nbValues,
        const void* srcNum,
        size_t numWidth)
{
    if (nbValues) {
        ZL_ASSERT_NN(array);
        ZL_ASSERT_NN(srcNum);
    }
    for (size_t n = 0; n < nbValues; n++) {
        array[n] = readNumeric_intoST(srcNum, n, numWidth);
    }
}

static void
writeNumeric_fromU32(void* array, size_t numWidth, size_t pos, unsigned in)
{
    ZL_ASSERT_NN(array);
    switch (numWidth) {
        case 1:
            ZL_ASSERT_LT(in, 256);
            ((uint8_t*)array)[pos] = (uint8_t)in;
            return;
        case 2:
            ZL_ASSERT_LT(in, 65536);
            ((uint16_t*)array)[pos] = (uint16_t)in;
            return;
        case 4: {
            ZL_STATIC_ASSERT(
                    sizeof(unsigned) <= 4,
                    "only compatible with unsigned max width 32-bit");
            ((uint32_t*)array)[pos] = (uint32_t)in;
            return;
        }
        case 8:
            ((uint64_t*)array)[pos] = (uint64_t)in;
            return;
        default:
            ZL_ASSERT_FAIL("only numeric width 1,2,4,8 are allowed");
            return;
    }
}

void NUMOP_writeNumerics_fromU32(
        void* array,
        size_t numWidth,
        const uint32_t srcU[],
        size_t nbValues)
{
    if (nbValues) {
        ZL_ASSERT_NN(array);
        ZL_ASSERT_NN(srcU);
    }
    for (size_t n = 0; n < nbValues; n++) {
        writeNumeric_fromU32(array, numWidth, n, srcU[n]);
    }
}

static void convertArray_1to4(uint32_t* dst32, const uint8_t* src8, size_t size)
{
    for (size_t n = 0; n < size; n++) {
        dst32[n] = src8[n];
    }
}

static void
convertArray_2to4(uint32_t* dst32, const uint16_t* src16, size_t size)
{
    for (size_t n = 0; n < size; n++) {
        dst32[n] = src16[n];
    }
}

static ZL_Report
convertArray_8to4(uint32_t* dst32, const uint64_t* src64, size_t size)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    for (size_t n = 0; n < size; n++) {
        // Note : need better overflow control
        ZL_ERR_IF_GE(
                src64[n],
                (uint64_t)1 << 32,
                integerOverflow,
                "uint64_t value is too large for uint32_t");
        dst32[n] = (uint32_t)src64[n];
    }
    return ZL_returnSuccess();
}

ZL_Report NUMOP_write32_fromNumerics(
        uint32_t* array,
        size_t nbValues,
        const void* srcNum,
        size_t numWidth)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    if (!nbValues)
        return ZL_returnSuccess();
    ZL_ASSERT_NN(array);
    switch (numWidth) {
        case 1:
            convertArray_1to4(array, (const uint8_t*)srcNum, nbValues);
            return ZL_returnSuccess();
        case 2:
            convertArray_2to4(array, (const uint16_t*)srcNum, nbValues);
            return ZL_returnSuccess();
        case 4:
            memcpy(array, srcNum, nbValues * 4);
            return ZL_returnSuccess();
        case 8:
            return convertArray_8to4(array, (const uint64_t*)srcNum, nbValues);
        default:
            ZL_ERR(logicError, "only numeric width 1,2,4,8 are allowed");
    }
}

void NUMOP_byteswap8(void* dst, const void* src, size_t nbElts)
{
    if (nbElts > 0) {
        memcpy(dst, src, nbElts);
    }
}

void NUMOP_byteswap16(void* dst, const void* src, size_t nbElts)
{
    char* dst8       = (char*)dst;
    const char* src8 = (const char*)src;
    for (size_t n = 0; n < nbElts; n++) {
        ZL_write16(dst8 + 2 * n, ZL_swap16(ZL_read16(src8 + 2 * n)));
    }
}

void NUMOP_byteswap32(void* dst, const void* src, size_t nbElts)
{
    char* dst8       = (char*)dst;
    const char* src8 = (const char*)src;
    for (size_t n = 0; n < nbElts; n++) {
        ZL_write32(dst8 + 4 * n, ZL_swap32(ZL_read32(src8 + 4 * n)));
    }
}

void NUMOP_byteswap64(void* dst, const void* src, size_t nbElts)
{
    char* dst8       = (char*)dst;
    const char* src8 = (const char*)src;
    for (size_t n = 0; n < nbElts; n++) {
        ZS_write64(dst8 + 8 * n, ZL_swap64(ZL_read64(src8 + 8 * n)));
    }
}

void NUMOP_byteswap(void* dst, const void* src, size_t nbElts, size_t eltWidth)
{
    ZL_ASSERT(eltWidth == 1 || eltWidth == 2 || eltWidth == 4 || eltWidth == 8);
    if (eltWidth == 1) {
        NUMOP_byteswap8(dst, src, nbElts);
    } else if (eltWidth == 2) {
        NUMOP_byteswap16(dst, src, nbElts);
    } else if (eltWidth == 4) {
        NUMOP_byteswap32(dst, src, nbElts);
    } else {
        NUMOP_byteswap64(dst, src, nbElts);
    }
}
