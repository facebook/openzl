// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CUSTOM_TRANSFORMS_THRIFT_KERNELS_DECODE_THRIFT_KERNEL_H
#define ZSTRONG_CUSTOM_TRANSFORMS_THRIFT_KERNELS_DECODE_THRIFT_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/zl_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * All of these functions assume that you have an upper bound on the output
 * size. This means that the compressor should likely store the original
 * serialized size in the header. This assumption makes allocation simpler.
 *
 * These functions produce compliant thrift, and are an exact reverse of the
 * encoder. They are resilent to corruption, and will fail if they detect it.
 */

/// @returns the number of bytes written into @p dst.
ZL_Report ZS2_ThriftKernel_serializeMapI32Float(
        void* dst,
        size_t dstCapacity,
        uint32_t const* keys,
        uint32_t const* floats,
        size_t mapSize);

/// @returns the number of bytes written into @p dst.
ZL_Report ZS2_ThriftKernel_serializeMapI32ArrayFloat(
        void* dst,
        size_t dstCapacity,
        uint32_t const* keys,
        uint32_t const* lengths,
        size_t mapSize,
        uint32_t const** innerValuesPtr,
        uint32_t const* innerValuesEnd);

/// @returns the number of bytes written into @p dst.
ZL_Report ZS2_ThriftKernel_serializeMapI32ArrayI64(
        void* dst,
        size_t dstCapacity,
        uint32_t const* keys,
        uint32_t const* lengths,
        size_t mapSize,
        uint64_t const** innerValuesPtr,
        uint64_t const* innerValuesEnd);

/// @returns the number of bytes written into @p dst.
ZL_Report ZS2_ThriftKernel_serializeMapI32ArrayArrayI64(
        void* dst,
        size_t dstCapacity,
        uint32_t const* keys,
        uint32_t const* lengths,
        size_t mapSize,
        uint32_t const** innerLengthsPtr,
        uint32_t const* innerLengthsEnd,
        uint64_t const** innerinnerValuesPtr,
        uint64_t const* innerinnerValuesEnd);

/// @returns the number of bytes written into @p dst.
ZL_Report ZS2_ThriftKernel_serializeMapI32MapI64Float(
        void* dst,
        size_t dstCapacity,
        uint32_t const* keys,
        uint32_t const* lengths,
        size_t mapSize,
        uint64_t const** innerKeysPtr,
        uint64_t const* innerKeysEnd,
        uint32_t const** innerValuesPtr,
        uint32_t const* innerValuesEnd);

/// @returns the number of bytes written into @p dst.
ZL_Report ZS2_ThriftKernel_serializeArrayI64(
        void* dst,
        size_t dstCapacity,
        uint64_t const* values,
        size_t arraySize);

/// @returns the number of bytes written into @p dst.
ZL_Report ZS2_ThriftKernel_serializeArrayI32(
        void* dst,
        size_t dstCapacity,
        uint32_t const* values,
        size_t arraySize);

/// @returns the number of bytes written into @p dst.
ZL_Report ZS2_ThriftKernel_serializeArrayFloat(
        void* dst,
        size_t dstCapacity,
        uint32_t const* values,
        size_t arraySize);

#ifdef __cplusplus
}
#endif

#endif
