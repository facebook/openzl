// Copyright (c) Meta Platforms, Inc. and affiliates.

/* zs2_dtransform_legacy.h
 * These are older Transforms that are no longer used.
 * They remain supported for backward compatibility.
 * They can also serve as simple examples to learn how to write Transforms.
 **/

#ifndef ZSTRONG_ZL_DTRANSFORM_LEGACY_H
#define ZSTRONG_ZL_DTRANSFORM_LEGACY_H

// basic definitions
#include "openzl/zl_buffer.h"       // ZL_XBuffer
#include "openzl/zl_ctransform.h"   // ZL_TypedGraphDesc
#include "openzl/zl_decompress.h"   // ZL_DCtx
#include "openzl/zl_errors.h"       // ZL_Report, ZL_isError()
#include "openzl/zl_opaque_types.h" // ZL_IDType
#include "openzl/zl_portability.h"  // ZL_NOEXCEPT_FUNC_PTR

#if defined(__cplusplus)
extern "C" {
#endif

/* Custom pipe transform decoder declaration.
 *
 * This is a mirror of ZL_PipeEncoderDesc within "zs2_ctransform.h" .
 * It declares a custom pipe decoder,
 * which features 1 serialized data stream as input,
 * and 1 serialized data stream as output.
 *
 * This declaration is then employed to register the custom decoder
 * into an active Decompression State (DCtx),
 * using ZL_DCtx_registerPipeDecoder().
 *
 * The decoder has a companion function @dstBound_f,
 * which tells zstrong how much memory to allocate for the destination buffer.
 * @dstBound_f can be NULL, in which case, it's assumed that
 * destination buffer needs as much capacity as input size.
 *
 * The amount of data produced into the destination buffer
 * must be provided as @return value.
 * The function does not have to use the whole destination buffer,
 * it may use less, but it cannot use more.
 * Any @return value > dstCapacity will be interpreted as an error.
 */

typedef size_t (*ZL_PipeDstCapacityFn)(const void* src, size_t srcSize)
        ZL_NOEXCEPT_FUNC_PTR;
typedef size_t (*ZL_PipeDecoderFn)(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize) ZL_NOEXCEPT_FUNC_PTR;

typedef struct {
    ZL_IDType CTid;
    ZL_PipeDstCapacityFn dstBound_f;
    ZL_PipeDecoderFn transform_f;
    const char* name; // Optional display name, for debugging purposes. Allowed
                      // to be NULL.
} ZL_PipeDecoderDesc;

/**
 * Register a custom decoder transform, should it be needed during
 * decompression of a zstrong frame.
 * A decoder transform is expected to reverse the encoding transform of same
 * @CTid.
 * This function is specialized for the registration of simple pipe decoder
 * transforms. Counterpart to ZL_Compressor_registerPipeEncoder().
 */
ZL_Report ZL_DCtx_registerPipeDecoder(
        ZL_DCtx* dctx,
        const ZL_PipeDecoderDesc* dtd);

/*
 * Custom split decoder transform declaration.
 *
 * This is a mirror of ZL_SplitEncoderDesc within "zs2_ctransform.h" .
 * The encoding side features 1 serialized data stream as input,
 * and N serialized data stream as output.
 * Below API declares the corresponding specialized custom decoder,
 * which does the reverse operation, joining N inputs into 1 output.
 *
 * This declaration is then employed to register the custom decoder
 * into an active Decompression State (DCtx), with
 * ZL_DCtx_registerSplitDecoder().
 *
 * The decoder has a companion function @dstBound_f,
 * which tells zstrong how much memory to allocate for the destination buffer.
 * Note: @dstBound_f **can't be NULL**. It must be defined.
 *
 * The amount of data produced into the destination buffer
 * must be provided as @return value.
 * The function does not have to use the whole destination buffer,
 * it may use less, but it cannot use more.
 * Any @return value > dstCapacity will be interpreted as an error.
 *
 * Design note : functions must assume that the size of src[] arrays
 * is == @nbInputStreams
 */

typedef size_t (*ZL_SplitDstCapacityFn)(const ZL_RBuffer src[])
        ZL_NOEXCEPT_FUNC_PTR;
typedef size_t (*ZL_SplitDecoderFn)(ZL_WBuffer dst, const ZL_RBuffer src[])
        ZL_NOEXCEPT_FUNC_PTR;

typedef struct {
    ZL_IDType CTid;
    size_t nbInputStreams;
    ZL_SplitDstCapacityFn dstBound_f;
    ZL_SplitDecoderFn transform_f;
    const char* name;
} ZL_SplitDecoderDesc;

/**
 * Register a custom split decoder transform.
 * This is supposed to be the reverse of the encoding transform of same @CTid.
 * Counterpart to ZL_registerSplitTransform().
 */
ZL_Report ZL_DCtx_registerSplitDecoder(
        ZL_DCtx* dctx,
        const ZL_SplitDecoderDesc* dtd);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZL_DTRANSFORM_LEGACY_H
