// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_ZS2_NODES_H
#define ZSTRONG_ZS2_NODES_H

#if defined(__cplusplus)
extern "C" {
#endif

// The enum values below shall not be used directly.
// Users creating custom graphs shall use *solely*
// NodeID and GraphID MACROS above.
typedef enum {
    ZL_StandardNodeID_illegal = 0,
    // Note :1 used to be a reserved NodeID value,

    ZL_StandardNodeID_delta_int = 2,
    ZL_StandardNodeID_transpose_split,
    ZL_StandardNodeID_zigzag,
    ZL_StandardNodeID_dispatchN_byTag,

    ZL_StandardNodeID_float32_deconstruct,
    ZL_StandardNodeID_bfloat16_deconstruct,
    ZL_StandardNodeID_float16_deconstruct,

    ZL_StandardNodeID_field_lz,

    // Automatic down-conversion nodes
    ZL_StandardNodeID_convert_struct_to_serial,
    ZL_StandardNodeID_convert_num_to_struct_le,
    ZL_StandardNodeID_convert_num_to_serial_le,
    // Manual conversion nodes
    ZL_StandardNodeID_convert_serial_to_struct,
    ZL_StandardNodeID_convert_serial_to_struct2,
    ZL_StandardNodeID_convert_serial_to_struct4,
    ZL_StandardNodeID_convert_serial_to_struct8,
    ZL_StandardNodeID_convert_struct_to_num_le,
    ZL_StandardNodeID_convert_struct_to_num_be,
    ZL_StandardNodeID_convert_serial_to_num8,
    ZL_StandardNodeID_convert_serial_to_num_le16,
    ZL_StandardNodeID_convert_serial_to_num_le32,
    ZL_StandardNodeID_convert_serial_to_num_le64,
    ZL_StandardNodeID_convert_serial_to_num_be16,
    ZL_StandardNodeID_convert_serial_to_num_be32,
    ZL_StandardNodeID_convert_serial_to_num_be64,
    ZL_StandardNodeID_separate_string_components,

    // Unpack bits into numerics
    ZL_StandardNodeID_bitunpack,

    // Converts integer into smaller types and offsets from 0
    ZL_StandardNodeID_range_pack,

    ZL_StandardNodeID_merge_sorted,

    ZL_StandardNodeID_prefix,

    ZL_StandardNodeID_divide_by,

    ZL_StandardNodeID_dispatch_string,

    ZL_StandardNodeID_concat_serial,

    ZL_StandardNodeID_concat_num,

    ZL_StandardNodeID_concat_struct,

    ZL_StandardNodeID_concat_string,

    ZL_StandardNodeID_dedup_num,

    ZL_StandardNodeID_parse_int,

    ZL_StandardNodeID_interleave_string,

    ZL_StandardNodeID_tokenize_struct,
    ZL_StandardNodeID_tokenize_numeric,
    ZL_StandardNodeID_tokenize_string,

    ZL_StandardNodeID_quantize_offsets,
    ZL_StandardNodeID_quantize_lengths,

    ZL_StandardNodeID_public_end // last id, used to detect end of public range
} ZL_StandardNodeID;

#if defined(__cplusplus)
} // extern "C"
#endif

#endif
