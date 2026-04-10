// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_DICT_DICT_CONSTANTS_H
#define OPENZL_DICT_DICT_CONSTANTS_H

#include <stddef.h> /* size_t */

/**
 * Shared wire-format constants for dict and bundle serialization.
 *
 * These values define the on-disk/on-wire layout described in dict.h
 * and bundle.h and must stay in sync with those format comments.
 */

#define ZL_DICT_MAGIC 0x4944CCDAU /* Little-endian "ÚÌDI" */
#define ZL_DICT_HEADER_SIZE \
    (4 + 32 + 4 + 1 + 4) /* magic + id + codec + codecType + dictSize = 45 */

#define ZL_BUNDLEINFO_MAGIC 0x4942CCDAU /* Little-endian "ÚÌBI" */
#define ZL_BUNDLE_HEADER_SIZE \
    (4 + 32 + 1 + 4) /* magic + bundleID + isFatBundle + numDicts = 41 */

#define ZL_UNIQUE_ID_SIZE 32 /* sizeof(ZL_UniqueID.bytes) */

#define ZL_MAX_DICTS_PER_BUNDLE 0xFFFF /* 2^16 - 1 */

#define ZL_DICT_INDEX_NONE (SIZE_MAX) /* no dict associated */

#endif /* OPENZL_DICT_DICT_CONSTANTS_H */
