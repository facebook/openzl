// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/zl_compress.h"

namespace openzl {
enum class CParam {
    StickyParameters      = ZL_CParam_stickyParameters,
    CompressionLevel      = ZL_CParam_compressionLevel,
    DecompressionLevel    = ZL_CParam_decompressionLevel,
    FormatVersion         = ZL_CParam_formatVersion,
    PermissiveCompression = ZL_CParam_permissiveCompression,
    CompressedChecksum    = ZL_CParam_compressedChecksum,
    ContentChecksum       = ZL_CParam_contentChecksum,
    StoreOnExpansion      = ZL_CParam_storeOnExpansion,
    MinStreamSize         = ZL_CParam_minStreamSize,
};
}
