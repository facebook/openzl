// Copyright (c) Meta Platforms, Inc. and affiliates.

// Public API
// List of standard nodes provided by OpenZL
// to create custom graphs.
//
// Design note :
// This file not only declares standard nodes for the compiler.
// Documentation details about Codecs are provided in their respective header.

#ifndef ZSTRONG_ZS2_PUBLIC_NODES_H
#define ZSTRONG_ZS2_PUBLIC_NODES_H

#include "openzl/codecs/zl_ace.h"                  // IWYU pragma: export
#include "openzl/codecs/zl_bitpack.h"              // IWYU pragma: export
#include "openzl/codecs/zl_bitunpack.h"            // IWYU pragma: export
#include "openzl/codecs/zl_brute_force_selector.h" // IWYU pragma: export
#include "openzl/codecs/zl_concat.h"               // IWYU pragma: export
#include "openzl/codecs/zl_constant.h"             // IWYU pragma: export
#include "openzl/codecs/zl_conversion.h"           // IWYU pragma: export
#include "openzl/codecs/zl_dedup.h"                // IWYU pragma: export
#include "openzl/codecs/zl_delta.h"                // IWYU pragma: export
#include "openzl/codecs/zl_dispatch.h"             // IWYU pragma: export
#include "openzl/codecs/zl_divide_by.h"            // IWYU pragma: export
#include "openzl/codecs/zl_entropy.h"              // IWYU pragma: export
#include "openzl/codecs/zl_field_lz.h"             // IWYU pragma: export
#include "openzl/codecs/zl_flatpack.h"             // IWYU pragma: export
#include "openzl/codecs/zl_float_deconstruct.h"    // IWYU pragma: export
#include "openzl/codecs/zl_generic.h"              // IWYU pragma: export
#include "openzl/codecs/zl_illegal.h"              // IWYU pragma: export
#include "openzl/codecs/zl_interleave.h"           // IWYU pragma: export
#include "openzl/codecs/zl_lz4.h"                  // IWYU pragma: export
#include "openzl/codecs/zl_merge_sorted.h"         // IWYU pragma: export
#include "openzl/codecs/zl_parse_int.h"            // IWYU pragma: export
#include "openzl/codecs/zl_prefix.h"               // IWYU pragma: export
#include "openzl/codecs/zl_quantize.h"             // IWYU pragma: export
#include "openzl/codecs/zl_range_pack.h"           // IWYU pragma: export
#include "openzl/codecs/zl_sddl.h"                 // IWYU pragma: export
#include "openzl/codecs/zl_split.h"                // IWYU pragma: export
#include "openzl/codecs/zl_split_by_struct.h"      // IWYU pragma: export
#include "openzl/codecs/zl_store.h"                // IWYU pragma: export
#include "openzl/codecs/zl_tokenize.h"             // IWYU pragma: export
#include "openzl/codecs/zl_transpose.h"            // IWYU pragma: export
#include "openzl/codecs/zl_zigzag.h"               // IWYU pragma: export
#include "openzl/codecs/zl_zstd.h"                 // IWYU pragma: export

#endif // ZSTRONG_ZS2_PUBLIC_NODES_H
