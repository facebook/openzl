// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_ZS2_GRAPHS_H
#define ZSTRONG_ZS2_GRAPHS_H

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum {
    // 0 is reserved to mean "invalid"
    ZL_StandardGraphID_illegal = 0,
    // 1 is reserved for ZL_PrivateStandardGraphID_serial_store (private)
    ZL_StandardGraphID_store = 2,

    ZL_StandardGraphID_fse,
    ZL_StandardGraphID_huffman,
    ZL_StandardGraphID_entropy,
    ZL_StandardGraphID_constant,

    ZL_StandardGraphID_zstd,
    ZL_StandardGraphID_bitpack,
    ZL_StandardGraphID_flatpack,
    ZL_StandardGraphID_field_lz,

    ZL_StandardGraphID_compress_generic,
    ZL_StandardGraphID_select_generic_lz_backend,
    ZL_StandardGraphID_segment_numeric,
    ZL_StandardGraphID_select_numeric,
    ZL_StandardGraphID_clustering,
    ZL_StandardGraphID_try_parse_int,

    ZL_StandardGraphID_simple_data_description_language,
    ZL_StandardGraphID_simple_data_description_language_v2,

    ZL_StandardGraphID_lz4,

    ZL_StandardGraphID_public_end // last id, used to detect end of public
                                  // range
} ZL_StandardGraphID;

#if defined(__cplusplus)
} // extern "C"
#endif

#endif
