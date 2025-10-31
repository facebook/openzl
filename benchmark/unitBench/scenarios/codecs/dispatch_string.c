// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "benchmark/unitBench/scenarios/codecs/dispatch_string.h"

#include "openzl/codecs/dispatch_string/decode_dispatch_string_kernel.h"
#include "openzl/codecs/dispatch_string/encode_dispatch_string_kernel.h"

/*
 * src for encode (dst for decode) is a packed buffer containing
 *    - u32: nbStrs
 *    - u32[]: strLens
 *    - u8[]: indices
 *    - char[]: raw string buffer
 * dst for encode (src for decode) is a packed buffer containing
 *    - u64: nbDsts  -- larger than strictly necessary to maintain alignment
 *    - size_t[]: dstNbStrs  (N.B.: this is machine dependent, so take care when
 *                            using generated output to decode!)
 *    - u32[][]: dstStrLens
 *    - u8[]: indices
 *    - char[][]: dstBuffers
 *
 * dstStrLens and dstBuffers are sparse. Each buffer is sized to fit the entire
 * input. See dispatchStringEncode_outSize() for more details.
 */
#define DISPATCH_STRING_NB_DSTS 8
size_t dispatchStringEncode_outSize(const void* src, size_t srcSize)
{
    // allocate enough space for each dst to contain the entire string buffer
    // and all of the strings. also allocate space for output indices list
    // and a {@DISPATCH_STRING_NB_DSTS}-length array to store the number of
    // strings in each dst
    const uint32_t nbStrs     = *(const uint32_t*)src;
    const size_t rawBufferLen = srcSize - sizeof(nbStrs)
            - nbStrs * (sizeof(uint32_t) + sizeof(uint8_t));
    return sizeof(uint32_t)
            + DISPATCH_STRING_NB_DSTS * sizeof(uint32_t)          // dstNbStrs
            + DISPATCH_STRING_NB_DSTS * nbStrs * sizeof(uint32_t) // dstStrLens
            + DISPATCH_STRING_NB_DSTS * rawBufferLen              // dstBuffers
            + nbStrs * sizeof(uint8_t);                           // indices
}

size_t dispatchStringEncode_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)customPayload;
    // prep function: verify packed src buffer corresponds to correct
    // string/indices. verify dst has enough space for all the resultant strings
    // and data

    // unpack src
    size_t nbStrs                = *(const uint32_t*)src;
    const uint32_t* srcStrLens   = (const uint32_t*)((const uint32_t*)src + 1);
    const uint8_t* outputIndices = (const uint8_t*)(srcStrLens + nbStrs);
    const void* srcStart         = outputIndices + nbStrs;

    // unpack dst
    uint64_t* nbDsts  = dst;
    size_t* dstNbStrs = (size_t*)(nbDsts + 1);
    uint32_t* dstStrLens[DISPATCH_STRING_NB_DSTS];
    uint8_t* indices;
    void* dstBuffers[DISPATCH_STRING_NB_DSTS];

    *nbDsts = DISPATCH_STRING_NB_DSTS;
    uint32_t* dstStrLensStart =
            ((uint32_t*)dst + 2
             + sizeof(size_t) / sizeof(uint32_t) * DISPATCH_STRING_NB_DSTS);
    for (unsigned i = 0u; i < DISPATCH_STRING_NB_DSTS; ++i) {
        dstStrLens[i] = dstStrLensStart + i * nbStrs;
    }
    indices = (uint8_t*)(dstStrLensStart + DISPATCH_STRING_NB_DSTS * nbStrs);
    char* dstBuffersStart = (char*)(indices + nbStrs);
    size_t bufferLen =
            srcSize - (size_t)((const char*)srcStart - (const char*)src);
    for (unsigned i = 0u; i < DISPATCH_STRING_NB_DSTS; ++i) {
        dstBuffers[i] = dstBuffersStart + i * bufferLen;
    }

    ZL_DispatchString_encode(
            DISPATCH_STRING_NB_DSTS,
            dstBuffers,
            dstStrLens,
            dstNbStrs,
            srcStart,
            srcStrLens,
            nbStrs,
            outputIndices);

    memcpy(indices, outputIndices, nbStrs);

    return dstCapacity;
}

size_t dispatchStringDecode_wrapper(
        const void* src,
        size_t srcSize,
        void* dst,
        size_t dstCapacity,
        void* customPayload)
{
    (void)srcSize;
    (void)dstCapacity;
    (void)customPayload;

    // unpack src
    const uint8_t nbSrcs              = (uint8_t)*(const uint64_t*)src;
    const uint64_t* srcNbStrsUnparsed = (const uint64_t*)src + 1;
    const uint32_t* srcStrLens[DISPATCH_STRING_NB_DSTS];
    const uint8_t* indices;
    const char* srcBuffers[DISPATCH_STRING_NB_DSTS];

    size_t nbStrs = 0;
    for (unsigned i = 0u; i < DISPATCH_STRING_NB_DSTS; ++i) {
        nbStrs += srcNbStrsUnparsed[i];
    }
    const uint32_t* srcStrLensStart =
            (const uint32_t*)((const uint32_t*)src + 2
                              + DISPATCH_STRING_NB_DSTS * 2);
    for (unsigned i = 0u; i < DISPATCH_STRING_NB_DSTS; ++i) {
        srcStrLens[i] = srcStrLensStart + i * nbStrs;
    }
    size_t totStrLen = 0;
    for (unsigned i = 0u; i < DISPATCH_STRING_NB_DSTS; ++i) {
        for (unsigned j = 0u; j < srcNbStrsUnparsed[i]; ++j) {
            totStrLen += srcStrLens[i][j];
        }
    }
    indices                     = (const uint8_t*)(srcStrLensStart
                               + DISPATCH_STRING_NB_DSTS * nbStrs);
    const char* srcBuffersStart = (const char*)(indices + nbStrs);
    for (unsigned i = 0u; i < DISPATCH_STRING_NB_DSTS; ++i) {
        srcBuffers[i] = srcBuffersStart + i * totStrLen;
    }

    // account for 32bit arch
    size_t srcNbStrs[DISPATCH_STRING_NB_DSTS];
    for (unsigned i = 0u; i < DISPATCH_STRING_NB_DSTS; ++i) {
        srcNbStrs[i] = srcNbStrsUnparsed[i];
    }

    // unpack dst
    *(uint32_t*)dst        = (uint32_t)nbStrs;
    uint32_t* dstStrLens   = (uint32_t*)dst + 1;
    uint8_t* outputIndices = (uint8_t*)(dstStrLens + nbStrs);
    void* dstBuffer        = outputIndices + nbStrs;

    ZL_DispatchString_decode(
            dstBuffer,
            dstStrLens,
            nbStrs,
            nbSrcs,
            srcBuffers,
            srcStrLens,
            srcNbStrs,
            indices);

    memcpy(outputIndices, indices, nbStrs);

    return sizeof(uint32_t) + nbStrs * sizeof(dstStrLens[0])
            + nbStrs * sizeof(outputIndices[0]) + totStrLen;
}
