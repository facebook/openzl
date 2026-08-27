// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_TOOLS_WASM_OPENZL_WASM_H
#define OPENZL_TOOLS_WASM_OPENZL_WASM_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/zl_errors_types.h" // ZL_ErrorCode

#if defined(__cplusplus)
extern "C" {
#endif

void* openzl_wasm_malloc(size_t size);
void openzl_wasm_free(void* buf);

/**
 * Returns the string descriptor for a ZL_ErrorCode.
 */
const char* openzl_wasm_errorString(ZL_ErrorCode code);

/**
 * Compressor profiles that the current wasm build offers.
 *
 * These values key the profile table in openzl_wasm.cpp, and is mirrored in
 * js/wasm_api.js.
 */
typedef enum {
    OPENZL_WASM_PROFILE_SERIAL = 0,
    OPENZL_WASM_PROFILE_U8     = 1,
    OPENZL_WASM_PROFILE_I8     = 2,
    OPENZL_WASM_PROFILE_U16    = 3,
    OPENZL_WASM_PROFILE_I16    = 4,
    OPENZL_WASM_PROFILE_U32    = 5,
    OPENZL_WASM_PROFILE_I32    = 6,
    OPENZL_WASM_PROFILE_U64    = 7,
    OPENZL_WASM_PROFILE_I64    = 8,
    OPENZL_WASM_PROFILE_COUNT
} openzl_wasm_Profile;

/**
 * @returns The name of @p profile, e.g. "u32", or NULL if it is out of range.
 * Exposed so language bindings can derive their profile tables from this.
 */
const char* openzl_wasm_profileName(openzl_wasm_Profile profile);

/**
 * Serializes the graph for @p profile.
 *
 * The serialized compressor is pinned to ZL_MAX_FORMAT_VERSION, so it should
 * not be persisted across builds.
 *
 * @param profile  openzl_wasm_Profile that user wants to use
 * @param outBuf   On success, an owned buffer of serialized bytes, release
 *                 with openzl_wasm_free().
 * @param outSize  On success, the length of @p outBuf.
 * @returns        ZL_ErrorCode_no_error on success.
 */
ZL_ErrorCode openzl_wasm_getSerializedCompressor(
        openzl_wasm_Profile profile,
        uint8_t** outBuf,
        size_t* outSize);

/**
 * Compresses @p src with the compressor serialized in @p compressor, as
 * returned by openzl_wasm_getSerializedCompressor().
 *
 * @param compressor      Serialized compressor bytes.
 * @param compressorSize  Size of @p compressor.
 * @param src             Source bytes, may be NULL when @p srcSize is 0.
 * @param srcSize         Size of @p src.
 * @param outBuf          On success, an owned frame, release with
 *                        openzl_wasm_free().
 * @param outSize         On success, the length of @p outBuf.
 * @returns               ZL_ErrorCode_no_error on success.
 */
ZL_ErrorCode openzl_wasm_compress(
        const uint8_t* compressor,
        size_t compressorSize,
        const uint8_t* src,
        size_t srcSize,
        uint8_t** outBuf,
        size_t* outSize);

/**
 * Reads the decompressed size recorded in the frame header.
 *
 * It is exposed for callers that want the size before committing to the
 * decompression.
 *
 * @param outSize  On success, the recorded size. 0 is a valid answer.
 * @returns ZL_ErrorCode_no_error on success.
 */
ZL_ErrorCode openzl_wasm_getDecompressedSize(
        const uint8_t* src,
        size_t srcSize,
        size_t* outSize);

/**
 * Decompresses the frame in @p src.
 *
 * @param outBuf   On success, an owned buffer; release with
 *                 openzl_wasm_free(). Non-NULL even when @p outSize is 0.
 * @param outSize  On success, the length of @p outBuf.
 * @returns ZL_ErrorCode_no_error on success.
 */
ZL_ErrorCode openzl_wasm_decompress(
        const uint8_t* src,
        size_t srcSize,
        uint8_t** outBuf,
        size_t* outSize);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_TOOLS_WASM_OPENZL_WASM_H
