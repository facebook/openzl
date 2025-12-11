// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_ZS2_VERSION_H
#define ZSTRONG_ZS2_VERSION_H

#include <stddef.h>

#include "openzl/zl_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZL_LIBRARY_VERSION_MAJOR 0
#define ZL_LIBRARY_VERSION_MINOR 1
#define ZL_LIBRARY_VERSION_PATCH 3

#define ZL_LIBRARY_VERSION_NUMBER                                      \
    (ZL_LIBRARY_VERSION_MAJOR * 10000 + ZL_LIBRARY_VERSION_MINOR * 100 \
     + ZL_LIBRARY_VERSION_PATCH * 1)

/**
 * The frame fomat version tells zstrong which frame format to
 * encode or decode. This is important to ensure forward and
 * backward compatibility with previous releases. The frame
 * format number must be bumped if any of the following happen
 * between releases:
 * - The frame format is changed (e.g. frame header changes)
 * - A standard Node is added or removed
 * - A standard Node changes its public ID
 * - A standard transform makes a breaking change to its format
 *
 * The following are not allowed to be changed:
 * - Standard transforms must not change their graph
 *   description. E.g. They must maintain the same number of
 *   inputs & outputs, and the same type for eacy input & output.
 *   To change the type of the transform, you must add a new
 *   transform and delete the old one.
 */

/// The minimum supported version for encoding & decoding.
/// Older versions cannot be decompressed.
///
/// WARNING: Be extremely careful when updating this number!
/// If there is still data encoded in version X, changing this
/// to x + 1 will make ZStrong refuse to decompress version X.
/// You must be certain that no data still exists in version X
/// before bumping this number to X+1.
#define ZL_MIN_FORMAT_VERSION (8)

/// The maximum supported version for encoding & decoding.
///
/// It is safe to bump this number when we make breaking
/// format changes. But note that once a library with
/// max format version X is released, we must support X
/// through our support window.
#define ZL_MAX_FORMAT_VERSION (23)

/// Minimum wire format version required to support chunking.
#define ZL_CHUNK_VERSION_MIN (21)

/// Minimum wire format version required to support typed input.
#define ZL_TYPED_INPUT_VERSION_MIN (14)

/**
 * @returns The current encoding version number.
 * This version number is used when the version
 * number is unset.
 *
 * To use a fixed version number for encoding,
 * grab the current version number using this
 * function, and then pass it as a constant to
 * ZL_CParam_formatVersion.
 *
 * NOTE: We currently only offer the ability to
 * encode with older versions for a very limited
 * period, so a new release will eventually
 * remove support for encoding with any fixed
 * version number. If you need long term
 * support for a version, please reach out to
 * the data_compression team, since that isn't
 * currently supported.
 */
unsigned ZL_getDefaultEncodingVersion(void);

/**
 * Reads the magic number from the frame and returns the
 * format version.
 *
 * @returns The format version of the frame, or an error
 * if the frame isn't large enough, or it has the wrong
 * magic number, or if the format version is not supported.
 */
ZL_Report ZL_getFormatVersionFromFrame(void const* src, size_t srcSize);

/**
 * Defined to 1 in the fbcode build of OpenZL and 0 in all other cases.
 */
#ifndef ZL_IS_FBCODE
#    define ZL_IS_FBCODE 0
#endif

/**
 * Defined to 1 in the fbcode release branch of OpenZL and 0 in all other cases.
 */
#ifndef ZL_FBCODE_IS_RELEASE
#    define ZL_FBCODE_IS_RELEASE 0
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_VERSION_H
