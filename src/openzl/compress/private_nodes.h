// Copyright (c) Meta Platforms, Inc. and affiliates.

// Private API
// declaring standard nodes used internally by zstrong engine
// but which are not considered useful to be exposed to the public side.
// The idea is to "guide" users towards useful concepts and declaration.

#ifndef ZSTRONG_PRIVATE_NODES_H
#define ZSTRONG_PRIVATE_NODES_H

#include "openzl/zl_public_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Private Nodes
// Typically needed for later Standard Graphs definition,
// but not exposed to the public side.

// Note : some of these transforms should be better documented.

// clang-format off

/* The zstd Node supports advanced compression parameters,
 * both at registration and compression times,
 * via the ZS2_IntParams interface.
 * All advanced parameters that can be set via the ZSTD_CCtx_setParameter() can be set this way,
 * and take precedence over Global parameters in case of conflict.
 * For example, { ZSTD_c_compressionLevel, 1 } will set the compression level to 1.
 * Note: by default, the `zstd` compression level is the same as the Zstrong Global compression level.
 * Exception:
 * Some parameters (ZSTD_c_format, ZSTD_c_contentSizeFlag) cannot be changed,
 * in order to remain compatible with the decoder side.
 * If these parameters are specified within ZS2_IntParams, Node execution will fail.
 */
#define ZL_NODE_ZSTD       (ZL_NodeID){ZL_PrivateStandardNodeID_zstd}

#define ZL_NODE_BITPACK_SERIAL (ZL_NodeID){ZL_PrivateStandardNodeID_bitpack_serial}
#define ZL_NODE_BITPACK_INT    (ZL_NodeID){ZL_PrivateStandardNodeID_bitpack_int}
#define ZL_NODE_FLATPACK       (ZL_NodeID){ZL_PrivateStandardNodeID_flatpack}  // What is FlatPack doing ?

// Constant
// Input : 1 stream (type determined by name) of N repetitions of a single value
// Output : 1 stream containing a single instance of the repeated value
// Result : transforms a stream consisting of a single value into a stream consisting
//          of that single value
// Note : compression will fail if the stream is empty or isn't constant
// Example 1 : 1 1 1 1 1 as 5 fields of size 1 => 1 as 1 field of size 1
// Example 2 : 300 300 300 as 3 fields of size 3 => 300 as 1 field of size 3
#define ZL_NODE_CONSTANT_SERIAL (ZL_NodeID){ZL_PrivateStandardNodeID_constant_serial}
#define ZL_NODE_CONSTANT_FIXED (ZL_NodeID){ZL_PrivateStandardNodeID_constant_fixed}

#define ZL_NODE_TOKENIZE    ZL_NODE_TOKENIZE_STRUCT
#define ZL_NODE_TOKENIZE_SORTED (ZL_NodeID){ZL_PrivateStandardNodeID_tokenize_sorted}
#define ZL_NODE_TOKENIZE_STRING_SORTED (ZL_NodeID){ZL_PrivateStandardNodeID_tokenize_string_sorted}

// dedup_num_trusted: same as dedup_num,
// but the caller is trusted that all inputs are identical,
// so it won't be checked again within the Transform.
// Only use in scenarios where the condition is guaranteed to be true.
#define ZL_NODE_DEDUP_NUM_TRUSTED (ZL_NodeID){ZL_PrivateStandardNodeID_dedup_num_trusted}

// Internal node for conversion from Serial to String
// Requires passing parameters, documented in encode_conversion_binding.h
#define ZL_NODE_SETSTRINGLENS (ZL_NodeID){ZL_PrivateStandardNodeID_set_string_lens}

// Deprecated nodes that should not be used in new code.
// We retain support for testing purposes.

#define ZL_NODE_ZSTD_FIXED_DEPRECATED (ZL_NodeID){ZL_PrivateStandardNodeID_zstd_fixed_deprecated}

// Transpose
// Input : 1 fixed-size-fields stream (all width supported)
// Output : 1 fixed-size-fields stream (new width = src.nbElts)
// Result : convert a stream of N fields of size S
//          into a stream of S fields of size N.
//          Each dst.field contains all bytes of same rank.
// Example : 1 2 3 4 5 6 7 8 as 2 fields of size 4
//       => transposed into 4 fields of size 2 : 1 5 2 6 3 7 4 8
#define ZL_NODE_TRANSPOSE_DEPRECATED   (ZL_NodeID){ZL_PrivateStandardNodeID_transpose_deprecated}

// Split transpose variants
#define ZL_NODE_TRANSPOSE_SPLIT2_DEPRECATED  (ZL_NodeID){ZL_PrivateStandardNodeID_transpose_split2_deprecated}
#define ZL_NODE_TRANSPOSE_SPLIT4_DEPRECATED  (ZL_NodeID){ZL_PrivateStandardNodeID_transpose_split4_deprecated}
#define ZL_NODE_TRANSPOSE_SPLIT8_DEPRECATED  (ZL_NodeID){ZL_PrivateStandardNodeID_transpose_split8_deprecated}

#define ZL_NODE_SPLIT_BY_STRUCT (ZL_NodeID){ZL_PrivateStandardNodeID_split_by_struct}

#define ZL_GRAPH_FIELD_LZ_LITERALS         (ZL_GraphID){ ZL_PrivateStandardGraphID_field_lz_literals }
#define ZL_GRAPH_FIELD_LZ_LITERALS_CHANNEL (ZL_GraphID){ ZL_PrivateStandardGraphID_field_lz_literals_channel }

#define ZL_GRAPH_DELTA_HUFFMAN  (ZL_GraphID){ ZL_PrivateStandardGraphID_delta_huffman }
#define ZL_GRAPH_DELTA_FLATPACK (ZL_GraphID){ ZL_PrivateStandardGraphID_delta_flatpack }
#define ZL_GRAPH_DELTA_ZSTD     (ZL_GraphID){ ZL_PrivateStandardGraphID_delta_zstd }

// clang-format on

// The below enum values must not be used directly.
// Note : none of the below values are currently stable
typedef enum {
    ZL_PrivateStandardNodeID_begin = ZL_StandardNodeID_public_end,

    ZL_PrivateStandardNodeID_set_string_lens,

    ZL_PrivateStandardNodeID_fse_v2,
    ZL_PrivateStandardNodeID_huffman_v2,
    ZL_PrivateStandardNodeID_huffman_struct_v2,

    ZL_PrivateStandardNodeID_fse_ncount,

    ZL_PrivateStandardNodeID_zstd,

    ZL_PrivateStandardNodeID_bitpack_serial,
    ZL_PrivateStandardNodeID_bitpack_int,
    ZL_PrivateStandardNodeID_flatpack,

    ZL_PrivateStandardNodeID_splitN,
    ZL_PrivateStandardNodeID_splitN_struct,
    ZL_PrivateStandardNodeID_splitN_num,

    ZL_PrivateStandardNodeID_split_by_struct,

    ZL_PrivateStandardNodeID_constant_serial,
    ZL_PrivateStandardNodeID_constant_fixed,

    ZL_PrivateStandardNodeID_tokenize_sorted,
    ZL_PrivateStandardNodeID_tokenize_string_sorted,

    ZL_PrivateStandardNodeID_dedup_num_trusted,

    // Deprecated nodes that should not be used in new code.
    // We retain support for testing purposes.

    ZL_PrivateStandardNodeID_rolz_deprecated,
    ZL_PrivateStandardNodeID_fastlz_deprecated,
    ZL_PrivateStandardNodeID_fse_deprecated,
    ZL_PrivateStandardNodeID_huffman_deprecated,
    ZL_PrivateStandardNodeID_huffman_fixed_deprecated,
    ZL_PrivateStandardNodeID_zstd_fixed_deprecated,
    ZL_PrivateStandardNodeID_transpose_deprecated,
    ZL_PrivateStandardNodeID_transpose_split2_deprecated,
    ZL_PrivateStandardNodeID_transpose_split4_deprecated,
    ZL_PrivateStandardNodeID_transpose_split8_deprecated,

    ZL_PrivateStandardNodeID_lz4,

    ZL_PrivateStandardNodeID_end // last id, used to detect out-of-bound enum
                                 // values
} ZL_PrivateStandardNodeID;

// Private Graphs

typedef enum {
    ZL_PrivateStandardGraphID_serial_store  = 1, // Fixed value
    ZL_PrivateStandardGraphID_private_begin = ZL_StandardGraphID_public_end,

    ZL_PrivateStandardGraphID_store1 = ZL_PrivateStandardGraphID_private_begin,
    ZL_PrivateStandardGraphID_string_store,

    ZL_PrivateStandardGraphID_compress1,
    ZL_PrivateStandardGraphID_serial_compress,
    ZL_PrivateStandardGraphID_struct_compress,
    ZL_PrivateStandardGraphID_numeric_compress,
    ZL_PrivateStandardGraphID_string_compress,

    ZL_PrivateStandardGraphID_string_separate_compress,

    ZL_PrivateStandardGraphID_bitpack_serial,
    ZL_PrivateStandardGraphID_bitpack_int,

    ZL_PrivateStandardGraphID_constant_serial,
    ZL_PrivateStandardGraphID_constant_fixed,

    ZL_PrivateStandardGraphID_fse_ncount,

    ZL_PrivateStandardGraphID_field_lz_literals,
    ZL_PrivateStandardGraphID_field_lz_literals_channel,

    ZL_PrivateStandardGraphID_delta_huffman,
    ZL_PrivateStandardGraphID_delta_flatpack,
    ZL_PrivateStandardGraphID_delta_zstd,
    ZL_PrivateStandardGraphID_delta_huffman_internal,
    ZL_PrivateStandardGraphID_delta_flatpack_internal,
    ZL_PrivateStandardGraphID_delta_zstd_internal,

    ZL_PrivateStandardGraphID_delta_field_lz,
    ZL_PrivateStandardGraphID_range_pack,
    ZL_PrivateStandardGraphID_range_pack_zstd,
    ZL_PrivateStandardGraphID_tokenize_delta_field_lz,

    ZL_PrivateStandardGraphID_split_serial,
    ZL_PrivateStandardGraphID_split_struct,
    ZL_PrivateStandardGraphID_split_numeric,
    ZL_PrivateStandardGraphID_split_string,

    ZL_PrivateStandardGraphID_n_to_n,

    ZL_PrivateStandardGraphID_end // last id, used to detect out-of-bound enum
                                  // values
} ZL_PrivateStandardGraphID;

// clang-format off
#define ZL_GRAPH_SERIAL_STORE  (ZL_GraphID){ZL_PrivateStandardGraphID_serial_store}
#define ZL_GRAPH_STORE1        (ZL_GraphID){ZL_PrivateStandardGraphID_store1}
#define ZL_GRAPH_STRING_STORE  (ZL_GraphID){ZL_PrivateStandardGraphID_string_store}

#define ZL_GRAPH_COMPRESS1        (ZL_GraphID){ZL_PrivateStandardGraphID_compress1}
#define ZL_GRAPH_SERIAL_COMPRESS  (ZL_GraphID){ZL_PrivateStandardGraphID_serial_compress}
#define ZL_GRAPH_NUMERIC_COMPRESS (ZL_GraphID){ZL_PrivateStandardGraphID_numeric_compress}
#define ZL_GRAPH_STRUCT_COMPRESS  (ZL_GraphID){ZL_PrivateStandardGraphID_struct_compress}
#define ZL_GRAPH_STRING_COMPRESS  (ZL_GraphID){ZL_PrivateStandardGraphID_string_compress} // Generic Selector
#define ZL_GRAPH_STRING_SEPARATE_COMPRESS (ZL_GraphID){ZL_PrivateStandardGraphID_string_separate_compress} // Separate String into components, compress each component independently

#define ZL_GRAPH_CONSTANT_SERIAL (ZL_GraphID){ZL_PrivateStandardGraphID_constant_serial}
#define ZL_GRAPH_CONSTANT_FIXED (ZL_GraphID){ZL_PrivateStandardGraphID_constant_fixed}

#define ZL_GRAPH_SELECT_GENERIC_LZ   (ZL_GraphID){ZL_StandardGraphID_select_generic_lz_backend}

#define ZL_GRAPH_SELECT_COMPRESS_SERIAL   (ZL_GraphID){ZL_PrivateStandardGraphID_serial_compress}

#define ZL_GRAPH_BITPACK_SERIAL (ZL_GraphID){ZL_PrivateStandardGraphID_bitpack_serial}
#define ZL_GRAPH_BITPACK_INT (ZL_GraphID){ZL_PrivateStandardGraphID_bitpack_int}

/**
 * Create a tokenize delta field lz graph.
 *
 * Input: A stream of width 1, 2, 4, or 8.
 */
#define ZL_GRAPH_TOKENIZE_DELTA_FIELD_LZ   \
    (ZL_GraphID)                           \
    {                                       \
        ZL_PrivateStandardGraphID_tokenize_delta_field_lz \
    }

/**
 * Create a delta field lz graph.
 *
 * Input: A stream of width 1, 2, 4, or 8.
 */
#define ZL_GRAPH_DELTA_FIELD_LZ   \
    (ZL_GraphID)                  \
    {                              \
        ZL_PrivateStandardGraphID_delta_field_lz \
    }

/**
 * Create a range pack zstd graph.
 *
 * Input: A stream of width 1, 2, 4, or 8.
 */
#define ZL_GRAPH_RANGE_PACK_ZSTD   \
    (ZL_GraphID)                   \
    {                               \
        ZL_PrivateStandardGraphID_range_pack_zstd \
    }

/**
 * Create a range pack graph.
 *
 * Input: A stream of width 1, 2, 4, or 8.
 */
#define ZL_GRAPH_RANGE_PACK   \
    (ZL_GraphID)              \
    {                          \
        ZL_PrivateStandardGraphID_range_pack \
    }

#define ZL_GRAPH_SPLIT_SERIAL (ZL_GraphID){ZL_PrivateStandardGraphID_split_serial}
#define ZL_GRAPH_SPLIT_STRUCT (ZL_GraphID){ZL_PrivateStandardGraphID_split_struct}
#define ZL_GRAPH_SPLIT_NUMERIC (ZL_GraphID){ZL_PrivateStandardGraphID_split_numeric}
#define ZL_GRAPH_SPLIT_STRING (ZL_GraphID){ZL_PrivateStandardGraphID_split_string}

#define ZL_GRAPH_N_TO_N (ZL_GraphID){ZL_PrivateStandardGraphID_n_to_n}

// clang-format on

#if defined(__cplusplus)
} // extern "C"
#endif
#endif // ZSTRONG_PRIVATE_NODES_H
