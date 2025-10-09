// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/kernels/decode_thrift_kernel.h"

#include "openzl/common/errors_internal.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_errors.h"

ZL_FORCE_INLINE uint64_t ZL_ThriftKernel_zigzagEncode64(uint64_t value)
{
    return (value << 1) ^ (uint64_t)((int64_t)value >> 63);
}

ZL_FORCE_INLINE uint32_t ZL_ThriftKernel_zigzagEncode32(uint32_t value)
{
    return (value << 1) ^ (uint32_t)((int32_t)value >> 31);
}

ZL_FORCE_INLINE ZL_Report
ZL_ThriftKernel_serializeLength(uint32_t val, uint8_t** op, uint8_t* oend)
{
    size_t const capacity = (size_t)(oend - *op);
    if (capacity >= ZL_VARINT_FAST_OVERWRITE_32) {
        *op += ZL_varintEncode32Fast(val, *op);
        ZL_ASSERT_LE(*op, oend);
        return ZL_returnSuccess();
    } else if (capacity >= ZL_varintSize(val)) {
        *op += ZL_varintEncode(val, *op);
        ZL_ASSERT_LE(*op, oend);
        return ZL_returnSuccess();
    } else {
        ZL_RET_R_ERR(internalBuffer_tooSmall);
    }
}

ZL_FORCE_INLINE ZL_Report
ZL_ThriftKernel_serializeI64(uint64_t val, uint8_t** op, uint8_t* oend)
{
    uint64_t const zz     = ZL_ThriftKernel_zigzagEncode64(val);
    size_t const capacity = (size_t)(oend - *op);
    if (capacity >= ZL_VARINT_FAST_OVERWRITE_64) {
        *op += ZL_varintEncode64Fast(zz, *op);
        ZL_ASSERT_LE(*op, oend);
        return ZL_returnSuccess();
    } else if (capacity >= ZL_varintSize(zz)) {
        *op += ZL_varintEncode(zz, *op);
        ZL_ASSERT_LE(*op, oend);
        return ZL_returnSuccess();
    } else {
        ZL_RET_R_ERR(internalBuffer_tooSmall);
    }
}

ZL_FORCE_INLINE ZL_Report
ZL_ThriftKernel_serializeI32(uint32_t val, uint8_t** op, uint8_t* oend)
{
    uint32_t const zz = ZL_ThriftKernel_zigzagEncode32(val);
    return ZL_ThriftKernel_serializeLength(zz, op, oend);
}

ZL_FORCE_INLINE ZL_Report ZL_ThriftKernel_serializeMapHeader(
        uint8_t** op,
        uint8_t* oend,
        uint8_t keyType,
        uint8_t valueType,
        size_t size)
{
    ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeLength((uint32_t)size, op, oend));

    uint8_t const type = (uint8_t)((keyType << 4) | valueType);

    if (size > 0) {
        ZL_RET_R_IF_EQ(internalBuffer_tooSmall, *op, oend);
        **op = type;
        ++*op;
    }

    return ZL_returnSuccess();
}

ZL_FORCE_INLINE ZL_Report ZL_ThriftKernel_serializeArrayHeader(
        uint8_t** op,
        uint8_t* oend,
        uint8_t type,
        size_t size)
{
    uint8_t const lengthNibble = (uint8_t)(size >= 0xF ? 0xF : size);
    uint8_t const lengthType   = (uint8_t)(type | (lengthNibble << 4));

    ZL_RET_R_IF_EQ(internalBuffer_tooSmall, *op, oend);
    **op = lengthType;
    ++*op;

    if (size >= 0xF) {
        ZL_RET_R_IF_ERR(
                ZL_ThriftKernel_serializeLength((uint32_t)size, op, oend));
    }
    return ZL_returnSuccess();
}

ZL_FORCE_INLINE ZL_Report ZL_ThriftKernel_serializeArrayI64_inline(
        uint8_t** op,
        uint8_t* oend,
        uint64_t const* values,
        size_t arraySize)
{
    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_serializeArrayHeader(op, oend, 0x6, arraySize));

    for (size_t i = 0; i < arraySize; ++i) {
        ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeI64(values[i], op, oend));
    }

    return ZL_returnSuccess();
}

ZL_Report ZL_ThriftKernel_serializeMapI32Float(
        void* dst,
        size_t dstCapacity,
        uint32_t const* keys,
        uint32_t const* floats,
        size_t mapSize)
{
    uint8_t* const ostart = (uint8_t*)dst;
    uint8_t* const oend   = ostart + dstCapacity;
    uint8_t* op           = ostart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_serializeMapHeader(&op, oend, 0x5, 0xD, mapSize));

    // TODO: Unroll to remove bounds checks, speed up varint encoding
    for (size_t i = 0; i < mapSize; ++i) {
        ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeI32(keys[i], &op, oend));
        ZL_RET_R_IF_GT(internalBuffer_tooSmall, 4, (size_t)(oend - op));
        ZL_writeBE32(op, floats[i]);
        op += 4;
    }

    return ZL_returnValue((size_t)(op - ostart));
}

ZL_Report ZL_ThriftKernel_serializeMapI32ArrayFloat(
        void* dst,
        size_t dstCapacity,
        uint32_t const* keys,
        uint32_t const* lengths,
        size_t mapSize,
        uint32_t const** innerValuesPtr,
        uint32_t const* innerValuesEnd)
{
    uint8_t* const ostart = (uint8_t*)dst;
    uint8_t* const oend   = ostart + dstCapacity;
    uint8_t* op           = ostart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_serializeMapHeader(&op, oend, 0x5, 0x9, mapSize));

    for (size_t i = 0; i < mapSize; ++i) {
        ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeI32(keys[i], &op, oend));

        size_t const arraySize = lengths[i];
        ZL_RET_R_IF_GT(
                srcSize_tooSmall,
                arraySize,
                (size_t)(innerValuesEnd - *innerValuesPtr));
        ZL_Report const arrayBytes = ZL_ThriftKernel_serializeArrayFloat(
                op, (size_t)(oend - op), *innerValuesPtr, arraySize);
        ZL_RET_R_IF_ERR(arrayBytes);
        op += ZL_validResult(arrayBytes);
        *innerValuesPtr += arraySize;
    }
    assert(*innerValuesPtr <= innerValuesEnd);

    return ZL_returnValue((size_t)(op - ostart));
}

ZL_Report ZL_ThriftKernel_serializeMapI32ArrayI64(
        void* dst,
        size_t dstCapacity,
        uint32_t const* keys,
        uint32_t const* lengths,
        size_t mapSize,
        uint64_t const** innerValuesPtr,
        uint64_t const* innerValuesEnd)
{
    uint8_t* const ostart = (uint8_t*)dst;
    uint8_t* const oend   = ostart + dstCapacity;
    uint8_t* op           = ostart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_serializeMapHeader(&op, oend, 0x5, 0x9, mapSize));

    for (size_t i = 0; i < mapSize; ++i) {
        ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeI32(keys[i], &op, oend));

        size_t const arraySize = lengths[i];
        ZL_RET_R_IF_GT(
                srcSize_tooSmall,
                arraySize,
                (size_t)(innerValuesEnd - *innerValuesPtr));
        ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeArrayI64_inline(
                &op, oend, *innerValuesPtr, arraySize));
        *innerValuesPtr += arraySize;
    }
    assert(*innerValuesPtr <= innerValuesEnd);

    return ZL_returnValue((size_t)(op - ostart));
}

ZL_Report ZL_ThriftKernel_serializeMapI32ArrayArrayI64(
        void* dst,
        size_t dstCapacity,
        uint32_t const* keys,
        uint32_t const* lengths,
        size_t mapSize,
        uint32_t const** innerLengthsPtr,
        uint32_t const* innerLengthsEnd,
        uint64_t const** innerInnerValuesPtr,
        uint64_t const* innerInnerValuesEnd)
{
    uint8_t* const ostart = (uint8_t*)dst;
    uint8_t* const oend   = ostart + dstCapacity;
    uint8_t* op           = ostart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_serializeMapHeader(&op, oend, 0x5, 0x9, mapSize));

    for (size_t i = 0; i < mapSize; ++i) {
        ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeI32(keys[i], &op, oend));

        size_t const arraySize = lengths[i];
        ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeArrayHeader(
                &op, oend, 0x9, arraySize));

        ZL_RET_R_IF_GT(
                srcSize_tooSmall,
                arraySize,
                (size_t)(innerLengthsEnd - *innerLengthsPtr));
        for (size_t j = 0; j < arraySize; ++j) {
            size_t const innerArraySize = (*innerLengthsPtr)[j];
            ZL_RET_R_IF_GT(
                    srcSize_tooSmall,
                    innerArraySize,
                    (size_t)(innerInnerValuesEnd - *innerInnerValuesPtr));
            ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeArrayI64_inline(
                    &op, oend, *innerInnerValuesPtr, innerArraySize));
            *innerInnerValuesPtr += innerArraySize;
        }
        *innerLengthsPtr += arraySize;
    }
    assert(*innerLengthsPtr <= innerLengthsEnd);
    assert(*innerInnerValuesPtr <= innerInnerValuesEnd);

    return ZL_returnValue((size_t)(op - ostart));
}

ZL_Report ZL_ThriftKernel_serializeMapI32MapI64Float(
        void* dst,
        size_t dstCapacity,
        uint32_t const* keys,
        uint32_t const* lengths,
        size_t mapSize,
        uint64_t const** innerKeysPtr,
        uint64_t const* innerKeysEnd,
        uint32_t const** innerValuesPtr,
        uint32_t const* innerValuesEnd)
{
    uint8_t* const ostart = (uint8_t*)dst;
    uint8_t* const oend   = ostart + dstCapacity;
    uint8_t* op           = ostart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_serializeMapHeader(&op, oend, 0x5, 0xB, mapSize));

    ZL_RET_R_IF_NE(
            corruption,
            (size_t)(innerKeysEnd - *innerKeysPtr),
            (size_t)(innerValuesEnd - *innerValuesPtr),
            "Keys and values must be the same length!");

    for (size_t i = 0; i < mapSize; ++i) {
        ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeI32(keys[i], &op, oend));

        size_t const innerMapSize = lengths[i];
        ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeMapHeader(
                &op, oend, 0x6, 0xD, innerMapSize));

        ZL_RET_R_IF_GT(
                srcSize_tooSmall,
                innerMapSize,
                (size_t)(innerKeysEnd - *innerKeysPtr));
        for (size_t j = 0; j < innerMapSize; ++j) {
            ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeI64(
                    (*innerKeysPtr)[j], &op, oend));
            ZL_RET_R_IF_GT(internalBuffer_tooSmall, 4, (size_t)(oend - op));
            ZL_writeBE32(op, (*innerValuesPtr)[j]);
            op += 4;
        }
        *innerKeysPtr += innerMapSize;
        *innerValuesPtr += innerMapSize;
    }
    assert(*innerKeysPtr <= innerKeysEnd);
    assert(*innerValuesPtr <= innerValuesEnd);

    return ZL_returnValue((size_t)(op - ostart));
}

ZL_Report ZL_ThriftKernel_serializeArrayI64(
        void* dst,
        size_t dstCapacity,
        uint64_t const* values,
        size_t arraySize)
{
    uint8_t* const ostart = (uint8_t*)dst;
    uint8_t* const oend   = ostart + dstCapacity;
    uint8_t* op           = ostart;

    ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeArrayI64_inline(
            &op, oend, values, arraySize));

    return ZL_returnValue((size_t)(op - ostart));
}

ZL_Report ZL_ThriftKernel_serializeArrayI32(
        void* dst,
        size_t dstCapacity,
        uint32_t const* values,
        size_t arraySize)
{
    uint8_t* const ostart = (uint8_t*)dst;
    uint8_t* const oend   = ostart + dstCapacity;
    uint8_t* op           = ostart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_serializeArrayHeader(&op, oend, 0x5, arraySize));

    for (size_t i = 0; i < arraySize; ++i) {
        ZL_RET_R_IF_ERR(ZL_ThriftKernel_serializeI32(values[i], &op, oend));
    }

    return ZL_returnValue((size_t)(op - ostart));
}

ZL_Report ZL_ThriftKernel_serializeArrayFloat(
        void* dst,
        size_t dstCapacity,
        uint32_t const* values,
        size_t arraySize)
{
    uint8_t* const ostart = (uint8_t*)dst;
    uint8_t* const oend   = ostart + dstCapacity;
    uint8_t* op           = ostart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_serializeArrayHeader(&op, oend, 0xD, arraySize));

    ZL_RET_R_IF_GT(internalBuffer_tooSmall, arraySize * 4, (size_t)(oend - op));
    for (size_t i = 0; i < arraySize; ++i) {
        ZL_writeBE32(op + 4 * i, values[i]);
    }
    op += 4 * arraySize;

    return ZL_returnValue((size_t)(op - ostart));
}
