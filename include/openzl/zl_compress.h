// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_ZS2_COMPRESS_H
#define ZSTRONG_ZS2_COMPRESS_H

#include <stddef.h>                  // size_t
#include "openzl/zl_errors.h"        // ZL_Report, ZL_isError()
#include "openzl/zl_introspection.h" // ZL_CompressIntrospectionHooks
#include "openzl/zl_opaque_types.h"  // ZL_CCtx, ZL_TypedRef
#include "openzl/zl_portability.h"   // ZL_INLINE
#include "openzl/zl_version.h"

#if defined(__cplusplus)
extern "C" {
#endif

// ----------------------------------------------
// One-pass contextless compression
// ----------------------------------------------

/**
 * This function is disabled because it is confusing users who think
 * Zstrong is a drop in replacement for Zstd.
 * If this is something you are sure you want, use ZL_CCtx_compress().
 *
 * Delegates processing to ZS2_profile_undefined profile.
 * Note 1 : Currently ignores compressionLevel.
 *          In the future, compressionLevel may trigger different back ends.
 * Note 2 : @compressionLevel is the only parameter
 *          both available and compulsory.
 */
// ZL_Report ZS2_compress(
//         void* dst,
//         size_t dstCapacity,
//         const void* src,
//         size_t srcSize,
//         int compressionLevel);

#define ZL_COMPRESSBOUND(s) (((s) * 2) + 512 + 8)
/**
 * Provides the upper bound for the compressed size needed to ensure
 * that compressing @p totalSrcSize is successful. When compressing
 * multiple inputs, @p totalSrcSize must be the sum of the size of each input.
 *
 * @param totalSrcSize The sum of all input sizes
 * @returns The upper bound of the compressed size
 *
 * @note This is a very large over-estimation, to be tightened later
 */
ZL_INLINE size_t ZL_compressBound(size_t totalSrcSize)
{
    return ZL_COMPRESSBOUND(totalSrcSize);
}

// ----------------------------------------------------
// Typeless compression with explicit state management
// ----------------------------------------------------

// Compression state management
ZL_CCtx* ZL_CCtx_create(void);
void ZL_CCtx_free(ZL_CCtx* cctx);

/// The list of global compression parameters
typedef enum {
    /// Only meaningful at CCtx level (ignored at CGraph level)
    /// By default, parameters are reset between compression sessions
    /// setting this parameter to 1 keep the parameters across compression
    /// sessions.
    ZL_CParam_stickyParameters = 1,

    /// Scale amplitude to determine
    ZL_CParam_compressionLevel = 2,

    /// Scale amplitude to determine
    ZL_CParam_decompressionLevel = 3,

    /// Sets the format version number to use for encoding.
    /// See @ZL_getDefaultEncodingVersion for details.
    /// @default 0 means use format version ZL_getDefaultEncodingVersion().
    ZL_CParam_formatVersion = 4,

    /// Select behavior when an internal compression stage fails.
    /// For example, when expecting an array of 32-bit integers,
    /// but the input size is not a clean multiple of 4.
    /// Strict mode stops at such stage and outputs an error.
    /// Permissive mode engages a generic backup compression mechanism,
    /// to successfully complete compression, at the cost of efficiency.
    /// At the time of this writing, backup is ZL_GRAPH_COMPRESS_GENERIC.
    /// Valid values for this parameter use the ZS2_cv3_* format.
    /// @default 0 currently means strict mode. This may change in the
    /// future.
    ZL_CParam_permissiveCompression = 5,

    /// Enable checksum of the compressed frame.
    /// This is useful to check for corruption that happens after
    /// compression.
    /// Valid values for this parameter use the ZS2_cv3_* format.
    /// @default 0 currently means checksum, might change in the future.
    ZL_CParam_compressedChecksum = 6,

    /// Enable checksum of the uncompressed content contained in the frame.
    /// This is useful to check for corruption that happens after
    /// compression,
    /// or corruption introduced during (de)compression. However, it cannot
    /// distinguish the two alone. In order to determine whether it is
    /// corruption or a bug in the ZStrong library, you have to enable both
    /// compressed and content checksums.
    /// Valid values for this parameter use the ZS2_cv3_* format.
    /// @default 0 currently means checksum, might change in the future.
    ZL_CParam_contentChecksum = 7,

    /// Any time an internal data Stream becomes smaller than this size,
    /// it gets STORED immediately, without further processing.
    /// This reduces processing time, improves decompression speed, and
    /// reduce
    /// risks of data expansion.
    /// Note(@Cyan): follows convention that setting 0 means "default", aka
    /// ZL_MINSTREAMSIZE_DEFAULT.
    /// Therefore, in order to completely disable the "automatic store"
    /// feature,
    /// one must pass a negative threshold value.
    ZL_CParam_minStreamSize = 11,

    // Other possible parameters (ideas) :
    //  - Backup when a node errors out (continue with generic LZ, or error
    //  out)
    //    + includes backup when a conversion error happens
    //  - Maximum memory budget for compression, and then for decompression
    //  - strict conversion mode (requires explicit type conversions in the
    //  graph)
} ZL_CParam;

// Publishing list of default Values
// Note : values still opened to debate, not finalized
#define ZL_COMPRESSIONLEVEL_DEFAULT 6
#define ZL_DECOMPRESSIONLEVEL_DEFAULT 3
#define ZL_MINSTREAMSIZE_DEFAULT 10

/**
 * @brief Sets a global compression parameter via the CCtx.
 *
 * @param gcparam The global compression parameter to set
 * @param value The value to set the global compression parameter to
 * @returns A ZL_Report containing the result of the operation
 *
 * @note Parameters set via CCtx have higher priority than parameters set via
 * CGraph.
 * @note By default, parameters set via CCtx are reset at the end of the
 * compression session. To preserve them across sessions, set
 * stickyParameters=1.
 */
ZL_Report ZL_CCtx_setParameter(ZL_CCtx* cctx, ZL_CParam gcparam, int value);

/**
 * @brief Reads a compression parameter's configured value from the CCtx.
 *
 * @param gcparam The global compression parameter to read
 * @returns The value set for the given parameter (0 if unset or does not
 * exist)
 */
int ZL_CCtx_getParameter(const ZL_CCtx* cctx, ZL_CParam gcparam);

/**
 * @brief Resets the parameters in the cctx to a blank state.
 *
 * @note Useful when unsure if ZL_CParam_stickyParameters is set to 1.
 */
ZL_Report ZL_CCtx_resetParameters(ZL_CCtx* cctx);

#include "openzl/zl_data.h" // ZL_DataArenaType
/**
 * Sets the Arena for Data* objects in the CCtx.
 * This frees the previous Data Arena and creates a new one.
 * This choice remains sticky, until set again.
 * The default Data Arena is HeapArena.
 *
 * @param sat The Data Arena type to set
 *
 * @note This is an advanced (experimental) parameter.
 */
ZL_Report ZL_CCtx_setDataArena(ZL_CCtx* cctx, ZL_DataArenaType sat);

/**
 * @brief A one-shot (blocking) compression function.
 *
 * @param dst The destination buffer to write the compressed data to
 * @param dstCapacity The capacity of the destination buffer
 * @param src The source buffer to compress
 * @param srcSize The size of the source buffer
 *
 * @returns The number of bytes written into @p dst, if successful. Otherwise,
 * returns an error.
 */
ZL_Report ZL_CCtx_compress(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize);

/**
 * Gets the error context for a given ZL_Report. This context is useful for
 * debugging and for submitting bug reports to Zstrong developers.
 *
 * @param report The report to get the error context for
 *
 * @returns A verbose error string containing context about the error that
 * occurred.
 *
 * @note: This string is stored within the @p cctx and is only valid for the
 * lifetime of the @p cctx.
 */
const char* ZL_CCtx_getErrorContextString(
        const ZL_CCtx* cctx,
        ZL_Report report);
/**
 * See ZL_CCtx_getErrorContextString()
 *
 * @param error: The error to get the context for
 */
const char* ZL_CCtx_getErrorContextString_fromError(
        const ZL_CCtx* cctx,
        ZL_Error error);

/**
 * Gets the warnings that were encountered during the lifetime of the
 * most recent compression operation.
 *
 * @returns The array of warnings encountered
 *
 * @note The array's and the errors' lifetimes are valid until the next
 * compression operation.
 */
ZL_Error_Array ZL_CCtx_getWarnings(const ZL_CCtx* cctx);

/**
 * @brief Attach introspection hooks to the CCtx.
 *
 * The supplied functions in @p hooks will be called at specified waypoints
 * during compression. These functions are expected to be pure observer
 * functions only. Attempts to modify the intermediate structures exposed at
 * these waypoints will almost certainly cause data corruption!
 *
 * @note This copies the content of the hooks struct into the CCtx. The caller
 * is responsible for maintaining the lifetime of the objects in the hook.
 *
 * @note This will only do something if the library is compiled with the
 * ALLOW_INTROSPECTION option. Otherwise, all the hooks will be no-ops.
 */
ZL_Report ZL_CCtx_attachIntrospectionHooks(
        ZL_CCtx* cctx,
        const ZL_CompressIntrospectionHooks* hooks);

/**
 * Detach any introspection hooks currently attached to the CCtx.
 */
ZL_Report ZL_CCtx_detachAllIntrospectionHooks(ZL_CCtx* cctx);

// ----------------------------------------------------
// Typed inputs
// ----------------------------------------------------

/**
 * Compresses a single typed input presented as a
 * `ZL_TypedRef`. See below for TypedRef* object creation.
 *
 * @param dst The destination buffer to write the compressed data to
 * @param dstCapacity The capacity of the destination buffer
 * @param input: The input to compress
 *
 * @returns The number of bytes written into @p dst, if successful. Otherwise,
 * returns an error.
 */
ZL_Report ZL_CCtx_compressTypedRef(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const ZL_TypedRef* input);

/**
 * Compresses multiple typed inputs , presented as an
 * array of `ZL_TypedRef`. See below for TypedRef* object creation.
 *
 * @param dst The destination buffer to write the compressed data to
 * @param dstCapacity The capacity of the destination buffer
 * @param inputs: The inputs to compress
 * @param nbInputs: The number of inputs to compress
 *
 * @returns The number of bytes written into @p dst, if successful. Otherwise,
 * returns an error.
 *
 * @note These inputs will be regenerated together in the same order at
 * decompression time.
 */
ZL_Report ZL_CCtx_compressMultiTypedRef(
        ZL_CCtx* cctx,
        void* dst,
        size_t dstCapacity,
        const ZL_TypedRef* inputs[],
        size_t nbInputs);

/* ZL_TypedRef* is an object that references an input buffer
 * and tag it with additional Type information */

/**
 * Creates a `ZL_TypedRef` that represents a regular buffer of bytes.
 *
 * @param src The reference buffer
 * @param srcSize The size of the reference buffer
 *
 * @returns A `ZL_TypedRef*` of type `ZL_Type_serial`.
 */
ZL_TypedRef* ZL_TypedRef_createSerial(const void* src, size_t srcSize);

/**
 * Creates a `ZL_TypedRef` that represents a concatenated list of fields of a
 * fixed size of @p structWidth.
 *
 * @p structWidth can be any size > 0. Even odd sizes (13, 17, etc.) are
 * allowed. All fields are considered concatenated back to back. There is no
 * alignment requirement.
 *
 * @param start The start of the reference buffer
 * @param structWidth The width of each element in the reference buffer.
 * @param structCount The number of elements in the input buffer. The total
 * size will be @p structWidth * @p structCount.
 *
 * @note Struct in this case is just short-hand for fixed-size-fields. It's
 * not limited to C-style structures.
 */
ZL_TypedRef* ZL_TypedRef_createStruct(
        const void* start,
        size_t structWidth,
        size_t structCount);

/**
 * Creates a `ZL_TypedRef` that references an array of numeric values,
 * employing the local host's endianness.
 * Supported widths are 1, 2, 4, and 8 and the input array must be properly
 * aligned (in local ABI).
 *
 * @param start The start of the reference array
 * @param numWidth The width of the numeric values
 * @param numCount The number of elements in the input array. The total size
 * will be @p numWidth * @p numCount.
 *
 */
ZL_TypedRef*
ZL_TypedRef_createNumeric(const void* start, size_t numWidth, size_t numCount);

/**
 * Creates a `ZL_TypedRef` referencing a "flat-strings" representation. All
 * "strings" are concatenated into @p strBuffer and their lengths are stored in
 * a @p strLens array.
 *
 * @param strBuffer The data buffer
 * @param bufferSize The size of the data buffer
 * @param strLengths The lengths array
 * @param nbStrings The number of strings (i.e. the size of the lengths array)
 *
 * @note String is just short-hand for variable-size-fields. It's not limited
 * to null-terminated ascii strings. A string can be any blob of bytes,
 * including some containing 0-value bytes, because length is explicit.
 */
ZL_TypedRef* ZL_TypedRef_createString(
        const void* strBuffer,
        size_t bufferSize,
        const uint32_t* strLens,
        size_t nbStrings);

/**
 * Adds header comment to the compressed frame for the following compression.
 * The message will be overridden if added a second time. The message is erased
 * from the cctx at the end of each compression.
 *
 * @note A comment of size 0 clears the comment field.
 *
 * @param comment The comment to add. The comment is copied and stored in the
 * cctx.
 * @param commentSize The size of the comment or 0 to clear the comment.
 */
ZL_Report ZL_CCtx_addHeaderComment(
        ZL_CCtx* cctx,
        const void* comment,
        size_t commentSize);

/**
 * Frees the given `ZL_TypedRef`.
 *
 * @param tref the object to free
 *
 * @note All ZL_TypedRef* objects of any type are released by the same method
 */
void ZL_TypedRef_free(ZL_TypedRef* tref);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_COMPRESS_H
