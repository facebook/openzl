// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/kernels/encode_thrift_kernel.h"

#include "openzl/common/errors_internal.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_errors.h"

static uint64_t ZL_zigZagDecode64(uint64_t value)
{
    return (uint64_t)((value >> 1) ^ -(value & 0x1));
}

static uint32_t ZL_zigZagDecode32(uint32_t value)
{
    return (uint32_t)((value >> 1) ^ -(value & 0x1));
}

static ZL_Report ZL_ThriftKernel_validateContainerSize(
        size_t numKeys,
        size_t numValues,
        size_t srcSize)
{
    // All thrift types in an array / map take at least 1 byte, no matter the
    // type. So an upper bound on the number of possible elements is srcSize
    size_t const numElts = numKeys + numValues;
    ZL_RET_R_IF_GT(
            node_invalid_input,
            numElts,
            srcSize,
            "Container size is larger than the remaining source size allows!");
    return ZL_returnSuccess();
}

static ZL_Report ZL_ThriftKernel_decodeMapHeader(
        uint8_t const** ip,
        uint8_t const* iend,
        uint8_t expectedKeyType,
        uint8_t expectedValueType)
{
    ZL_RESULT_OF(uint64_t) size = ZL_varintDecode32Strict(ip, iend);
    ZL_RET_R_IF_ERR(size);
    if (ZL_RES_value(size) > 0) {
        ZL_RET_R_IF_EQ(srcSize_tooSmall, *ip, iend);
        uint8_t const keyType   = **ip >> 4;
        uint8_t const valueType = **ip & 0xF;
        if (expectedKeyType != 0x0)
            ZL_RET_R_IF_NE(node_invalid_input, keyType, expectedKeyType);
        if (expectedValueType != 0x0)
            ZL_RET_R_IF_NE(node_invalid_input, valueType, expectedValueType);
        ++*ip;
    }
    return ZL_returnValue((size_t)ZL_RES_value(size));
}

static ZL_Report ZL_ThriftKernel_validateMapHeader(
        uint8_t const** ip,
        uint8_t const* iend,
        uint8_t expectedKeyType,
        uint8_t expectedValueType,
        size_t expectedSize)
{
    ZL_Report const size = ZL_ThriftKernel_decodeMapHeader(
            ip, iend, expectedKeyType, expectedValueType);
    ZL_RET_R_IF_ERR(size);
    ZL_RET_R_IF_NE(node_invalid_input, ZL_RES_value(size), expectedSize);
    return ZL_returnSuccess();
}

static ZL_Report ZL_ThriftKernel_decodeI32(
        uint8_t const** ip,
        uint8_t const* iend)
{
    ZL_RESULT_OF(uint64_t) ret = ZL_varintDecode32Strict(ip, iend);
    ZL_RET_R_IF_ERR(ret);
    return ZL_returnValue(ZL_zigZagDecode32((uint32_t)ZL_RES_value(ret)));
}

static ZL_RESULT_OF(uint64_t)
        ZL_ThriftKernel_decodeI64(uint8_t const** ip, uint8_t const* iend)
{
    ZL_RESULT_OF(uint64_t) ret = ZL_varintDecode64Strict(ip, iend);
    ZL_RET_T_IF_ERR(uint64_t, ret);
    ZL_RES_value(ret) = ZL_zigZagDecode64(ZL_RES_value(ret));
    return ret;
}

static ZL_Report ZL_ThriftKernel_decodeArrayHeader(
        uint8_t const** ip,
        uint8_t const* iend,
        uint8_t expectedType)
{
    ZL_RET_R_IF_EQ(srcSize_tooSmall, *ip, iend);
    uint8_t const type = **ip & 0xF;
    if (expectedType != 0x0)
        ZL_RET_R_IF_NE(node_invalid_input, type, expectedType);
    size_t size = **ip >> 4;
    ++*ip;
    if (size == 0xF) {
        ZL_RESULT_OF(uint64_t) ret = ZL_varintDecode32Strict(ip, iend);
        ZL_RET_R_IF_ERR(ret);
        size = ZL_RES_value(ret);
        ZL_RET_R_IF_LT(node_invalid_input, size, 15);
    }
    return ZL_returnValue(size);
}

static ZL_Report ZL_ThriftKernel_validateArrayHeader(
        uint8_t const** ip,
        uint8_t const* iend,
        uint8_t expectedType,
        size_t expectedSize)
{
    ZL_Report size = ZL_ThriftKernel_decodeArrayHeader(ip, iend, expectedType);
    ZL_RET_R_IF_ERR(size);
    ZL_RET_R_IF_NE(node_invalid_input, ZL_RES_value(size), expectedSize);
    return ZL_returnSuccess();
}

static ZL_Report ZL_ThriftKernel_deserializeVarints64(
        uint64_t* values,
        uint8_t const** ip,
        uint8_t const* iend,
        size_t nbValues)
{
    for (size_t i = 0; i < nbValues; ++i) {
        ZL_RESULT_OF(uint64_t) ret = ZL_ThriftKernel_decodeI64(ip, iend);
        ZL_RET_R_IF_ERR(ret);
        values[i] = ZL_RES_value(ret);
    }
    return ZL_returnSuccess();
}

ZL_Report ZL_ThriftKernel_deserializeMapI32Float(
        uint32_t* keys,
        uint32_t* floats,
        void const* src,
        size_t srcSize,
        size_t mapSize)
{
    uint8_t const* const istart = (uint8_t const*)src;
    uint8_t const* const iend   = istart + srcSize;
    uint8_t const* ip           = istart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_validateMapHeader(&ip, iend, 0x5, 0xD, mapSize));

    // Optimization: Run for ((iend - ip) / 9) iters without bounds checks, then
    // repeat.
    // Optimization: Branch based on the previous id, expect the same
    // length, expect sorted keys
    for (size_t i = 0; i < mapSize; ++i) {
        ZL_Report const key = ZL_ThriftKernel_decodeI32(&ip, iend);
        ZL_RET_R_IF_ERR(key);
        keys[i] = (uint32_t)ZL_RES_value(key);
        ZL_RET_R_IF_GT(srcSize_tooSmall, 4, (size_t)(iend - ip));
        floats[i] = ZL_readBE32(ip);
        ip += 4;
    }

    ZL_ASSERT_SUCCESS(
            ZL_ThriftKernel_validateContainerSize(mapSize, mapSize, srcSize));

    return ZL_returnValue((size_t)(ip - istart));
}

/// @returns the number of bytes consumed from the source.
ZL_Report ZL_ThriftKernel_deserializeMapI32ArrayFloat(
        uint32_t* keys,
        uint32_t* lengths,
        ZL_ThriftKernel_DynamicOutput32 innerValues,
        void const* src,
        size_t srcSize,
        size_t mapSize)
{
    uint8_t const* const istart = (uint8_t const*)src;
    uint8_t const* const iend   = istart + srcSize;
    uint8_t const* ip           = istart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_validateMapHeader(&ip, iend, 0x5, 0x9, mapSize));

    ZL_ThriftKernel_Slice32 values = { NULL, NULL };
    for (size_t i = 0; i < mapSize; ++i) {
        ZL_RESULT_OF(uint64_t) ret = ZL_varintDecode32Strict(&ip, iend);
        ZL_RET_R_IF_ERR(ret);
        keys[i] = ZL_zigZagDecode32((uint32_t)ZL_RES_value(ret));

        // Decode array
        ZL_Report const arraySize =
                ZL_ThriftKernel_decodeArrayHeader(&ip, iend, 0xD);
        ZL_RET_R_IF_ERR(arraySize);
        // TODO: This could be better
        ZL_RET_R_IF_NOT(
                node_invalid_input, ZL_uintFits(ZL_RES_value(arraySize), 4));
        lengths[i] = (uint32_t)ZL_RES_value(arraySize);

        ZL_RET_R_IF_GT(srcSize_tooSmall, 4 * lengths[i], (size_t)(iend - ip));

        for (uint32_t pos = 0, end = lengths[i]; pos < end;) {
            if (values.ptr == values.end) {
                values = innerValues.next(innerValues.opaque, i, mapSize);
            }
            size_t const toCopy =
                    ZL_MIN(end - pos, (size_t)(values.end - values.ptr));
            for (size_t k = 0; k < toCopy; ++k) {
                *values.ptr++ = ZL_readBE32(ip);
                ip += 4;
            }
            pos += toCopy;
        }
    }
    innerValues.finish(innerValues.opaque, values.ptr);

    ZL_ASSERT_SUCCESS(
            ZL_ThriftKernel_validateContainerSize(mapSize, mapSize, srcSize));

    return ZL_returnValue((size_t)(ip - istart));
}

static ZL_Report ZL_ThriftKernel_deserializeInnerArrayI64(
        ZL_ThriftKernel_DynamicOutput64 innerValues,
        ZL_ThriftKernel_Slice64* values,
        uint32_t* length,
        size_t keyIdx,
        size_t mapSize,
        uint8_t const** ip,
        uint8_t const* iend)
{
    ZL_Report const arraySize =
            ZL_ThriftKernel_decodeArrayHeader(ip, iend, 0x6);
    ZL_RET_R_IF_ERR(arraySize);
    // TODO: This could be better...
    ZL_RET_R_IF_NOT(
            node_invalid_input, ZL_uintFits(ZL_RES_value(arraySize), 4));
    *length = (uint32_t)ZL_RES_value(arraySize);

    for (size_t pos = 0, end = *length; pos < end;) {
        if (values->ptr == values->end) {
            *values = innerValues.next(innerValues.opaque, keyIdx, mapSize);
        }
        size_t const toCopy =
                ZL_MIN(end - pos, (size_t)(values->end - values->ptr));
        ZL_RET_R_IF_ERR(ZL_ThriftKernel_deserializeVarints64(
                values->ptr, ip, iend, toCopy));
        values->ptr += toCopy;
        pos += toCopy;
    }
    return ZL_returnSuccess();
}

ZL_Report ZL_ThriftKernel_deserializeMapI32ArrayI64(
        uint32_t* keys,
        uint32_t* lengths,
        ZL_ThriftKernel_DynamicOutput64 innerValues,
        void const* src,
        size_t srcSize,
        size_t mapSize)
{
    uint8_t const* const istart = (uint8_t const*)src;
    uint8_t const* const iend   = istart + srcSize;
    uint8_t const* ip           = istart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_validateMapHeader(&ip, iend, 0x5, 0x9, mapSize));

    ZL_ThriftKernel_Slice64 values = { NULL, NULL };
    for (size_t i = 0; i < mapSize; ++i) {
        ZL_RESULT_OF(uint64_t) ret = ZL_varintDecode32Strict(&ip, iend);
        ZL_RET_R_IF_ERR(ret);
        keys[i] = ZL_zigZagDecode32((uint32_t)ZL_RES_value(ret));

        ZL_RET_R_IF_ERR(ZL_ThriftKernel_deserializeInnerArrayI64(
                innerValues, &values, &lengths[i], i, mapSize, &ip, iend));
    }
    innerValues.finish(innerValues.opaque, values.ptr);

    ZL_ASSERT_SUCCESS(
            ZL_ThriftKernel_validateContainerSize(mapSize, mapSize, srcSize));

    return ZL_returnValue((size_t)(ip - istart));
}

ZL_Report ZL_ThriftKernel_deserializeMapI32ArrayArrayI64(
        uint32_t* keys,
        uint32_t* lengths,
        ZL_ThriftKernel_DynamicOutput32 innerLengths,
        ZL_ThriftKernel_DynamicOutput64 innerInnerValues,
        void const* src,
        size_t srcSize,
        size_t mapSize)
{
    uint8_t const* const istart = (uint8_t const*)src;
    uint8_t const* const iend   = istart + srcSize;
    uint8_t const* ip           = istart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_validateMapHeader(&ip, iend, 0x5, 0x9, mapSize));

    ZL_ThriftKernel_Slice64 valuesSlice  = { NULL, NULL };
    ZL_ThriftKernel_Slice32 lengthsSlice = { NULL, NULL };
    for (size_t i = 0; i < mapSize; ++i) {
        ZL_RESULT_OF(uint64_t) ret = ZL_varintDecode32Strict(&ip, iend);
        ZL_RET_R_IF_ERR(ret);
        keys[i] = ZL_zigZagDecode32((uint32_t)ZL_RES_value(ret));

        ZL_Report const arraySize =
                ZL_ThriftKernel_decodeArrayHeader(&ip, iend, 0x9);
        ZL_RET_R_IF_ERR(arraySize);
        // TODO: This could be better...
        ZL_RET_R_IF_NOT(
                node_invalid_input, ZL_uintFits(ZL_RES_value(arraySize), 4));
        lengths[i] = (uint32_t)ZL_RES_value(arraySize);

        for (size_t j = 0, end = lengths[i]; j < end; ++j) {
            if (lengthsSlice.ptr == lengthsSlice.end) {
                lengthsSlice =
                        innerLengths.next(innerLengths.opaque, i, mapSize);
            }
            ZL_RET_R_IF_ERR(ZL_ThriftKernel_deserializeInnerArrayI64(
                    innerInnerValues,
                    &valuesSlice,
                    lengthsSlice.ptr,
                    i,
                    mapSize,
                    &ip,
                    iend));
            ++lengthsSlice.ptr;
        }
    }
    innerInnerValues.finish(innerInnerValues.opaque, valuesSlice.ptr);
    innerLengths.finish(innerLengths.opaque, lengthsSlice.ptr);

    ZL_ASSERT_SUCCESS(
            ZL_ThriftKernel_validateContainerSize(mapSize, mapSize, srcSize));

    return ZL_returnValue((size_t)(ip - istart));
}

ZL_Report ZL_ThriftKernel_deserializeMapI32MapI64Float(
        uint32_t* keys,
        uint32_t* lengths,
        ZL_ThriftKernel_DynamicOutput64 innerKeys,
        ZL_ThriftKernel_DynamicOutput32 innerValues,
        void const* src,
        size_t srcSize,
        size_t mapSize)
{
    uint8_t const* const istart = (uint8_t const*)src;
    uint8_t const* const iend   = istart + srcSize;
    uint8_t const* ip           = istart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_validateMapHeader(&ip, iend, 0x5, 0xB, mapSize));

    ZL_ThriftKernel_Slice64 keysSlice   = { NULL, NULL };
    ZL_ThriftKernel_Slice32 valuesSlice = { NULL, NULL };
    for (size_t i = 0; i < mapSize; ++i) {
        ZL_RESULT_OF(uint64_t) ret = ZL_varintDecode32Strict(&ip, iend);
        ZL_RET_R_IF_ERR(ret);
        keys[i] = ZL_zigZagDecode32((uint32_t)ZL_RES_value(ret));

        ZL_Report const innerMapSize =
                ZL_ThriftKernel_decodeMapHeader(&ip, iend, 0x6, 0xD);
        ZL_RET_R_IF_ERR(innerMapSize);
        // TODO: This could be better
        ZL_RET_R_IF_NOT(
                node_invalid_input, ZL_uintFits(ZL_RES_value(innerMapSize), 4));
        lengths[i] = (uint32_t)ZL_RES_value(innerMapSize);

        for (size_t j = 0, end = ZL_RES_value(innerMapSize); j < end; ++j) {
            if (keysSlice.ptr == keysSlice.end) {
                keysSlice = innerKeys.next(innerKeys.opaque, i, mapSize);
            }
            if (valuesSlice.ptr == valuesSlice.end) {
                valuesSlice = innerValues.next(innerValues.opaque, i, mapSize);
            }

            ret = ZL_varintDecode64Strict(&ip, iend);
            ZL_RET_R_IF_ERR(ret);
            *keysSlice.ptr++ = ZL_zigZagDecode64(ZL_RES_value(ret));

            ZL_RET_R_IF_GT(srcSize_tooSmall, 4, (size_t)(iend - ip));
            *valuesSlice.ptr++ = ZL_readBE32(ip);
            ip += 4;
        }
    }
    innerKeys.finish(innerKeys.opaque, keysSlice.ptr);
    innerValues.finish(innerValues.opaque, valuesSlice.ptr);

    ZL_ASSERT_SUCCESS(
            ZL_ThriftKernel_validateContainerSize(mapSize, mapSize, srcSize));

    return ZL_returnValue((size_t)(ip - istart));
}

ZL_Report ZL_ThriftKernel_deserializeArrayI64(
        uint64_t* values,
        void const* src,
        size_t srcSize,
        size_t arraySize)
{
    uint8_t const* const istart = (uint8_t const*)src;
    uint8_t const* const iend   = istart + srcSize;
    uint8_t const* ip           = istart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_validateArrayHeader(&ip, iend, 0x6, arraySize));
    ZL_RET_R_IF_ERR(ZL_ThriftKernel_deserializeVarints64(
            values, &ip, iend, arraySize));

    ZL_ASSERT_SUCCESS(
            ZL_ThriftKernel_validateContainerSize(0, arraySize, srcSize));

    return ZL_returnValue((size_t)(ip - istart));
}

ZL_Report ZL_ThriftKernel_deserializeArrayI32(
        uint32_t* values,
        void const* src,
        size_t srcSize,
        size_t arraySize)
{
    uint8_t const* const istart = (uint8_t const*)src;
    uint8_t const* const iend   = istart + srcSize;
    uint8_t const* ip           = istart;

    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_validateArrayHeader(&ip, iend, 0x5, arraySize));
    for (size_t i = 0; i < arraySize; ++i) {
        ZL_Report ret = ZL_ThriftKernel_decodeI32(&ip, iend);
        ZL_RET_R_IF_ERR(ret);
        values[i] = (uint32_t)ZL_RES_value(ret);
    }

    ZL_ASSERT_SUCCESS(
            ZL_ThriftKernel_validateContainerSize(0, arraySize, srcSize));

    return ZL_returnValue((size_t)(ip - istart));
}

ZL_Report ZL_ThriftKernel_deserializeArrayFloat(
        uint32_t* values,
        void const* src,
        size_t srcSize,
        size_t arraySize)
{
    uint8_t const* const istart = (uint8_t const*)src;
    uint8_t const* const iend   = istart + srcSize;
    uint8_t const* ip           = istart;

    // Validate the array header
    ZL_RET_R_IF_ERR(
            ZL_ThriftKernel_validateArrayHeader(&ip, iend, 0xD, arraySize));

    // Copy the floats
    ZL_RET_R_IF_GT(srcSize_tooSmall, arraySize * 4, (size_t)(iend - ip));
    for (size_t i = 0; i < arraySize; ++i) {
        ZL_write32(values + i, ZL_readBE32(ip + 4 * i));
    }

    ip += arraySize * 4;

    ZL_ASSERT_SUCCESS(
            ZL_ThriftKernel_validateContainerSize(0, arraySize, srcSize));

    return ZL_returnValue((size_t)(ip - istart));
}

ZL_Report ZL_ThriftKernel_getMapSize(void const* src, size_t srcSize)
{
    uint8_t const* const istart = (uint8_t const*)src;
    uint8_t const* const iend   = istart + srcSize;
    uint8_t const* ip           = istart;

    // Read the size from the header
    ZL_Report const size =
            ZL_ThriftKernel_decodeMapHeader(&ip, iend, 0x0, 0x0);
    ZL_RET_R_IF_ERR(size);
    // Validate the size against an upper bound
    ZL_RET_R_IF_ERR(ZL_ThriftKernel_validateContainerSize(
            ZL_validResult(size), ZL_validResult(size), srcSize));

    return size;
}

ZL_Report ZL_ThriftKernel_getArraySize(void const* src, size_t srcSize)
{
    uint8_t const* const istart = (uint8_t const*)src;
    uint8_t const* const iend   = istart + srcSize;
    uint8_t const* ip           = istart;

    // Validate the array header
    ZL_Report const size = ZL_ThriftKernel_decodeArrayHeader(&ip, iend, 0x0);
    ZL_RET_R_IF_ERR(size);
    // Validate the size against an upper bound
    ZL_RET_R_IF_ERR(ZL_ThriftKernel_validateContainerSize(
            0, ZL_validResult(size), srcSize));

    return size;
}
