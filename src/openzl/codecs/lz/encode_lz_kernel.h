// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZS_ENCODE_LZ_KERNEL_H
#define ZS_ENCODE_LZ_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/codecs/partition/common_partition.h"

#define ZL_LZ_LIT_OVER_LENGTH 32
#define ZL_LZ_MIN_MATCH 4
// All offsets must be strictly less than this.
#define ZL_LZ_MAX_OFFSET_U16 (1u << 16)
#define ZL_LZ_MAX_OFFSET_U32 ZL_PARTITION_MAX_PARTITION_SIZE_FOR_UNROLL2

typedef struct {
    uint8_t* literals;
    size_t literalsCapacity;
    size_t numLiterals;

    void* offsets;
    size_t offsetWidth;
    uint16_t* literalLengths;
    uint16_t* matchLengths;
    size_t sequencesCapacity;
    size_t numSequences;
} ZL_Lz_OutSequences;

/**
 * Returns the maximum number of sequences that the encoder can produce
 * for an input of srcSize bytes.
 */
size_t ZL_Lz_maxNumSequences(size_t srcSize);

/// @returns The table log for the given window log.
uint32_t ZL_Lz_tableLog(uint32_t windowLog);

/**
 * Encodes src[0..srcSize) into LZ sequences.
 *
 * All output buffers in dst must be pre-allocated by the caller:
 *   - dst->literals: at least srcSize bytes
 *   - dst->offsets, dst->literalLengths, dst->matchLengths:
 *     at least ZL_Lz_maxNumSequences(srcSize) elements each
 *
 * hashTableMem must point to at least
 * ZS_FastTable_tableSize(ZL_LZ_TABLE_LOG) bytes of writable memory.
 *
 * dst->numLiterals and dst->numSequences are set to the number of
 * output literals and sequences respectively.
 */
void ZL_Lz_encode(
        ZL_Lz_OutSequences* dst,
        const uint8_t* src,
        size_t srcSize,
        void* hashTableMem,
        uint32_t windowLog,
        int acceleration);

#endif
