// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_GCPARAMS_H
#define ZSTRONG_GCPARAMS_H

#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/* Note : Global params are listed in ZL_CParam,
 *        defined within zstrong/zs2_compress.h
 */

#include "openzl/common/allocation.h" // Arena
#include "openzl/zl_common_types.h"   // ZL_TernaryParam
#include "openzl/zl_compress.h"       // ZL_CParam
#include "openzl/zl_graph_api.h"      // ZL_RuntimeGraphParameters
#include "openzl/zl_opaque_types.h"   // ZL_GraphID
#include "openzl/zl_reflection.h"

/* Design note :
 * value `0` means "not set".
 * Values used at compression level are set in this priority order :
 * CCtx > CGraph > Default
 */
typedef struct {
    /// Compression level (higher = better compression, slower speed)
    /// Range: typically 1-9, with 6 being the default
    /// (ZL_COMPRESSIONLEVEL_DEFAULT) Controls the trade-off between compression
    /// ratio and speed
    int compressionLevel;

    /// Decompression level (higher = faster decompression, may affect format
    /// choices) Range: typically 1-9, with 3 being the default
    /// (ZL_DECOMPRESSIONLEVEL_DEFAULT)
    int decompressionLevel;

    /// ZStrong format version to use for encoding
    /// Must be a supported format version (checked via
    /// ZL_isFormatVersionSupported) between ZL_FORMATVERSION_MIN and
    /// ZL_FORMATVERSION_MAX Validated to be non-zero during finalization
    uint32_t formatVersion;

    /// Controls behavior when compression stage fails (e.g., type mismatches)
    /// ZL_TernaryParam_disable: Strict mode - fail on errors
    /// ZL_TernaryParam_enable: Permissive mode - fall back to generic
    /// compression ZL_TernaryParam_auto (default): Currently treated as disable
    ZL_TernaryParam permissiveCompression;

    /// Enable checksum of the compressed frame for corruption detection
    /// ZL_TernaryParam_enable: Include compressed checksum
    /// ZL_TernaryParam_disable: Skip compressed checksum
    /// ZL_TernaryParam_auto (default): Currently treated as enable
    /// Automatically disabled for format versions <= 3
    ZL_TernaryParam compressedChecksum;

    /// Enable checksum of the uncompressed content for end-to-end validation
    /// ZL_TernaryParam_enable: Include content checksum
    /// ZL_TernaryParam_disable: Skip content checksum
    /// ZL_TernaryParam_auto (default): Currently treated as enable
    /// Automatically disabled for format versions <= 3
    ZL_TernaryParam contentChecksum;

    /// Controls whether chunks that expand during compression
    /// are automatically replaced with STORE (anti-inflation guard).
    /// ZL_TernaryParam_enable: Replace expanded chunks with STORE (default)
    /// ZL_TernaryParam_disable: Keep compressed output even if it expanded
    /// ZL_TernaryParam_auto: Resolves to enable via defaults
    ZL_TernaryParam storeOnExpansion;

    /// Minimum stream size threshold for automatic storage without compression
    /// Streams smaller than this size are stored directly to avoid expansion
    /// Default: ZL_MINSTREAMSIZE_DEFAULT (10 bytes)
    /// Set to negative value to completely disable auto-store feature
    unsigned minStreamSize;

    /// Preserve parameters across compression sessions (CCtx level only)
    /// 0 (default): Reset parameters after each session
    /// 1: Keep parameters sticky across sessions
    /// Only meaningful at CCtx level, ignored at CGraph level
    int stickyParameters;

    /// Internal flag indicating if explicit starting graph is set
    /// 0: Use default graph selection
    /// 1: Use explicitly set via GCParams_selectStartingGraphID, cleared via
    /// GCParams_resetStartingGraphID
    int explicitStart;

    /// Graph ID to use as explicit starting point (when explicitStart is set)
    /// Only valid when explicitStart != 0
    /// Set to ZL_GRAPH_ILLEGAL when not in use
    ZL_GraphID startingGraphID;

    /// Runtime graph parameters for the explicit starting graph (optional)
    /// Only valid when explicitStart != 0
    /// Can be NULL even when explicitStart is set
    /// Transferred via arena allocation into session arena
    const ZL_RuntimeGraphParameters* rgp;
} GCParams;

// All defaults for Global parameters
extern const GCParams GCParams_default;

/// Sets a global compression parameter in the GCParams structure
/// @param gcparams Pointer to GCParams structure to modify
/// @param gcparam Parameter ID (ZL_CParam enum value)
/// @param value New value for the parameter
/// @returns ZL_returnSuccess() on success, error on invalid parameter ID or
/// unsupported format version
/// @note formatVersion parameter validates that the version is supported
/// @note All other parameters accept the provided value without bounds checking
/// (TODO: add bounds)
ZL_Report
GCParams_setParameter(GCParams* gcparams, ZL_CParam gcparam, int value);

/// Sets an explicit starting graph ID with optional runtime parameters
/// When set, compression will use this specific graph instead of default
/// selection
/// @param gcparams Pointer to GCParams structure to modify
/// @param graphid Graph ID to use as the starting point
/// @param rgp Runtime graph parameters (can be NULL), transferred via arena if
/// provided
/// @param arena Required arena for parameter transfer when rgp is not NULL
/// @returns ZL_returnSuccess() on success
/// @note Sets explicitStart flag to 1 and stores the graph ID and parameters
ZL_Report GCParams_setStartingGraphID(
        GCParams* gcparams,
        ZL_GraphID graphid,
        const ZL_RuntimeGraphParameters* rgp,
        Arena* arena);

/// Clears the explicit starting graph configuration
/// @param gcparams Pointer to GCParams structure to modify
/// @returns ZL_returnSuccess() on success
/// @note Resets explicitStart to 0, startingGraphID to ZL_GRAPH_ILLEGAL, and
/// rgp to NULL
ZL_Report GCParams_resetStartingGraphID(GCParams* gcparams);

/// Applies default values to unset parameters (value == 0) in the destination
/// @param dst Target GCParams structure to update
/// @param defaults Source GCParams structure containing default values
/// @note Only parameters with value 0 in dst are overwritten with values from
/// defaults
/// @note stickyParameter is intentionally NOT overridden by defaults
/// @note Applied parameters: compressionLevel, decompressionLevel,
/// permissiveCompression,
///       formatVersion, compressedChecksum, contentChecksum,
///       storeOnExpansion, minStreamSize
void GCParams_applyDefaults(GCParams* dst, const GCParams* defaults);

/// Finalizes and validates the parameters, resolving incompatibilities where
/// possible
/// @param gcparams Pointer to GCParams structure to finalize
/// @returns ZL_returnSuccess() on success, error if parameters are invalid
/// @note Validates that formatVersion is set (non-zero), returns
/// formatVersion_notSet if zero
/// @note This function must be called before using the parameters for
/// compression
ZL_Report GCParams_finalize(GCParams* gcparams);

/// Retrieves the value of a specific parameter
/// @param gcparams Pointer to GCParams structure to query
/// @param paramId Parameter ID to retrieve (ZL_CParam enum value)
/// @returns The parameter value, or 0 for invalid/unknown parameter IDs
/// @note All parameter values are returned as int, with appropriate casting for
/// enums
int GCParams_getParameter(const GCParams* gcparams, ZL_CParam paramId);

/// Checks if an explicit starting graph has been configured
/// @param gcparams Pointer to GCParams structure to query
/// @returns Non-zero if explicit start is set, 0 otherwise
/// @note This is separate from ZL_CParam because explicitStart is
/// private/internal
int GCParams_explicitStartSet(const GCParams* gcparams);

/// Retrieves the explicit starting graph ID
/// @param gcparams Pointer to GCParams structure to query
/// @returns The configured starting graph ID
/// @pre Must only be called after verifying explicitStart != 0 via
/// GCParams_explicitStartSet
/// @note Will assert if called when explicitStart is 0
ZL_GraphID GCParams_explicitStart(const GCParams* gcparams);

/// Retrieves the runtime parameters for the explicit starting graph
/// @param gcparams Pointer to GCParams structure to query
/// @returns Pointer to runtime graph parameters, or NULL if not set
/// @pre Must only be called after verifying explicitStart != 0 via
/// GCParams_explicitStartSet
/// @note Will assert if called when explicitStart is 0
/// @note Return value can be NULL even when explicitStart is set
const ZL_RuntimeGraphParameters* GCParams_startParams(const GCParams* gcparams);

/// Copies all parameter values from source to destination GCParams structure
/// @param dst Destination GCParams structure to copy into
/// @param src Source GCParams structure to copy from
/// @note Performs a complete memory copy of the entire structure (including
/// explicit start parameters)
/// @note After copying, dst will be an exact replica of src, including any
/// explicit graph settings
void GCParams_copy(GCParams* dst, const GCParams* src);

/// Iterates through all non-zero parameters in the GCParams structure, calling
/// a callback for each
/// @param gcparams Pointer to GCParams structure to iterate over
/// @param callback Function to call for each parameter (receives opaque
/// pointer, parameter ID, and value)
/// @param opaque User-provided pointer passed through to the callback function
/// @returns ZL_returnSuccess() on success, or first error returned by callback
/// @note Only parameters with non-zero values are reported to the callback
/// @note Callback signature: ZL_Report (*callback)(void* opaque, ZL_CParam
/// param, int value)
/// @note Used for parameter introspection and serialization workflows
ZL_Report GCParams_forEachParam(
        const GCParams* gcparams,
        ZL_Compressor_ForEachParamCallback callback,
        void* opaque);

/// Converts a parameter name string to its corresponding ZL_CParam enum value
/// @param param Null-terminated string containing the parameter name (e.g.,
/// "compressionLevel")
/// @returns ZL_Result containing the ZL_CParam value on success, or error if
/// string doesn't match any parameter
/// @note Performs exact string matching against canonical parameter names
/// @note Supported parameter names: "stickyParameters", "compressionLevel",
/// "decompressionLevel",
///       "formatVersion", "permissiveCompression", "compressedChecksum",
///       "contentChecksum", "storeOnExpansion", "minStreamSize"
/// @note Use ZL_validResult() to extract the parameter ID from a successful
/// result
ZL_Report GCParams_strToParam(const char* param);

/// Converts a ZL_CParam enum value to its corresponding canonical string name
/// @param param Parameter ID to convert to string
/// @returns Pointer to null-terminated canonical parameter name string, or NULL
/// if param is invalid
/// @note Returns the primary/canonical name for each parameter (first name in
/// the names array)
/// @note Returned string is statically allocated and does not need to be freed
/// @note Used for parameter serialization, debugging, and user-friendly output
const char* GCParams_paramToStr(ZL_CParam param);

ZL_END_C_DECLS

#endif // ZSTRONG_GCPARAMS_H
