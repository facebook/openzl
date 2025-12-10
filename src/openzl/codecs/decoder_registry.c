// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/decoder_registry.h"

#include "openzl/codecs/bitpack/decode_bitpack_binding.h"
#include "openzl/codecs/bitunpack/decode_bitunpack_binding.h"
#include "openzl/codecs/concat/decode_concat_binding.h"
#include "openzl/codecs/constant/decode_constant_binding.h"
#include "openzl/codecs/conversion/decode_conversion_binding.h"
#include "openzl/codecs/dedup/decode_dedup_binding.h"
#include "openzl/codecs/delta/decode_delta_binding.h"
#include "openzl/codecs/dispatchN_byTag/decode_dispatchN_byTag_binding.h"
#include "openzl/codecs/dispatch_string/decode_dispatch_string_binding.h"
#include "openzl/codecs/divide_by/decode_divide_by_binding.h"
#include "openzl/codecs/entropy/decode_entropy_binding.h"
#include "openzl/codecs/flatpack/decode_flatpack_binding.h"
#include "openzl/codecs/float_deconstruct/decode_float_deconstruct_binding.h"
#include "openzl/codecs/interleave/decode_interleave_binding.h"
#include "openzl/codecs/lz/decode_lz_binding.h"
#include "openzl/codecs/lz/graph_lz.h"
#include "openzl/codecs/lz4/decode_lz4_binding.h"
#include "openzl/codecs/merge_sorted/decode_merge_sorted_binding.h"
#include "openzl/codecs/parse_int/decode_parse_int_binding.h"
#include "openzl/codecs/parse_int/graph_parse_int.h"
#include "openzl/codecs/prefix/decode_prefix_binding.h"
#include "openzl/codecs/quantize/decode_quantize_binding.h"
#include "openzl/codecs/range_pack/decode_range_pack_binding.h"
#include "openzl/codecs/rolz/decode_rolz_binding.h"
#include "openzl/codecs/splitByStruct/decode_splitByStruct_binding.h"
#include "openzl/codecs/splitN/decode_splitN_binding.h"
#include "openzl/codecs/tokenize/decode_tokenize_binding.h"
#include "openzl/codecs/transpose/decode_transpose_binding.h"
#include "openzl/codecs/zigzag/decode_zigzag_binding.h"
#include "openzl/codecs/zstd/decode_zstd_binding.h"
#include "openzl/common/wire_format.h" // ZS2_strid_*

/// Register a standard Typed Transform,
/// provided that it follows a naming convention for its defining macros
/// as in: DI_TRANSFORM_NAME - TRANSFORM_NAME_GRAPH
#define REGISTER_TTRANSFORM(_id, _minFormatVersion, _TRANSFORM_NAME) \
    REGISTER_DEPRECATED_TTRANSFORM(                                  \
            _id, _minFormatVersion, ZL_MAX_FORMAT_VERSION, _TRANSFORM_NAME)

/// Specialized variant, for Typed Transforms only
/// _regMacro initializes a ZL_TypedDecoderDesc
/// _graph initializes a ZL_TypedGraphDesc
#define REGISTER_TTRANSFORM_G(_id, _minFormatVersion, _regMacro, _graph) \
    REGISTER_DEPRECATED_TTRANSFORM_G(                                    \
            _id, _minFormatVersion, ZL_MAX_FORMAT_VERSION, _regMacro, _graph)

#define REGISTER_DEPRECATED_TTRANSFORM(                       \
        _id, _minFormatVersion, _maxFormatVersion, MACRONAME) \
    REGISTER_DEPRECATED_TTRANSFORM_G(                         \
            _id,                                              \
            _minFormatVersion,                                \
            _maxFormatVersion,                                \
            DI_##MACRONAME,                                   \
            MACRONAME##_GRAPH)

/// Helper to register a transform that was added in _minFormatVersion & is last
/// supported in _maxFormatVersion. E.g. formats in the range
/// [_minFormatVerison, _maxFormatVersion] support the transform.
#define REGISTER_DEPRECATED_TTRANSFORM_G(                             \
        _id, _minFormatVersion, _maxFormatVersion, _regMacro, _graph) \
    [_id] = { .dtr = { \
                .miGraphDesc    = _graph(_id),              \
                .transformFn    = DT_typedTransformWrapper, \
                .implDesc.dtt   = _regMacro(_id),           \
                .type           = dtr_typed,                \
              }, \
              (_minFormatVersion),                        \
              (_maxFormatVersion) }

/// Specialized variant, for VO_Transforms only
/// The last argument is a macro that provides a ZL_VODecoderDesc init
#define REGISTER_VOTRANSFORM_G(_id, _minFormatVersion, _regMacro, _graph) \
    [_id] = { .dtr = { \
                .miGraphDesc  = _graph(_id),           \
                .transformFn  = DT_voTransformWrapper, \
                .implDesc.dvo = _regMacro(_id),        \
                .type         = dtr_vo,                \
              }, \
              (_minFormatVersion),                     \
              ZL_MAX_FORMAT_VERSION }

/// Specialized variant, for MI_Transforms only
/// The last argument is a macro that provides a ZL_MIDecoderDesc init
#define REGISTER_MITRANSFORM_G(_id, _minFormatVersion, _regMacro, _graph) \
    [_id] = { .dtr = { \
                .miGraphDesc  = _graph(_id),           \
                .transformFn  = DT_miTransformWrapper, \
                .implDesc.dmi = _regMacro(_id),        \
                .type         = dtr_mi,                \
              }, \
              (_minFormatVersion),                     \
              ZL_MAX_FORMAT_VERSION }

// clang-format off
const StandardDTransform SDecoders_array[ZL_StandardTransformID_end] = {
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_delta_int, 3, DI_DELTA_INT, NUMPIPE_GRAPH),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_transpose, 3, TRANSPOSE),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_zigzag, 3, DI_ZIGZAG_NUM, NUMPIPE_GRAPH),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_fse_v2, 15, FSE_V2),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_fse_ncount, 15, FSE_NCOUNT),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_huffman_v2, 15, HUFFMAN_V2),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_huffman_struct_v2, 15, HUFFMAN_STRUCT_V2),
    REGISTER_DEPRECATED_TTRANSFORM_G(ZL_StandardTransformID_rolz, 3, 12, DI_ROLZ, PIPE_GRAPH),
    REGISTER_DEPRECATED_TTRANSFORM_G(ZL_StandardTransformID_fastlz, 3, 12, DI_FASTLZ, PIPE_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_zstd, 3, DI_ZSTD, PIPE_GRAPH),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_field_lz, 3, FIELD_LZ),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_quantize_offsets, 3, DI_QUANTIZE_OFFSETS, QUANTIZE_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_quantize_lengths, 3, DI_QUANTIZE_LENGTHS, QUANTIZE_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_bitpack_serial, 3, DI_BITPACK_SERIALIZED, SERIALIZED_BITPACK_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_bitpack_int, 3, DI_BITPACK_INTEGER, INTEGER_BITPACK_GRAPH),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_flatpack, 3, FLATPACK),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_float_deconstruct, 4, FLOAT_DECONSTRUCT),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_bitunpack, 6, BITUNPACK),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_range_pack, 8, RANGE_PACK),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_tokenize_fixed, 8, TOKENIZE_FIXED),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_tokenize_numeric, 8, TOKENIZE_NUMERIC),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_tokenize_string, 11, TOKENIZE_VSF),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_merge_sorted, 9, MERGE_SORTED),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_constant_serial, 11, DI_CONSTANT_SERIALIZED, SERIALIZED_CONSTANT_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_constant_fixed, 11, DI_CONSTANT_FIXED, FIXED_SIZE_CONSTANT_GRAPH),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_prefix, 11, PREFIX),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_divide_by, 16, DI_DIVIDE_BY_INT, NUMPIPE_GRAPH),
    REGISTER_TTRANSFORM(ZL_StandardTransformID_parse_int, 19, PARSE_INT),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_lz4, 23, DI_LZ4, PIPE_GRAPH),

    REGISTER_VOTRANSFORM_G(ZL_StandardTransformID_splitn, 9, DI_SPLITN, GRAPH_VO_SERIAL),
    REGISTER_VOTRANSFORM_G(ZL_StandardTransformID_splitn_struct, 14, DI_SPLITN_STRUCT, GRAPH_VO_STRUCT),
    REGISTER_VOTRANSFORM_G(ZL_StandardTransformID_splitn_num, 14, DI_SPLITN_NUM, GRAPH_VO_NUM),
    REGISTER_VOTRANSFORM_G(ZL_StandardTransformID_splitByStruct, 9, DI_SPLITBYSTRUCT, GRAPH_SPLITBYSTRUCT_VO),
    REGISTER_VOTRANSFORM_G(ZL_StandardTransformID_dispatchN_byTag, 9, DI_DIPATCHNBYTAG, GRAPH_DIPATCHNBYTAG),
    REGISTER_VOTRANSFORM_G(ZL_StandardTransformID_transpose_split, 11, DI_TRANSPOSE_SPLIT, TRANSPOSE_GRAPH_SPLIT),
    REGISTER_VOTRANSFORM_G(ZL_StandardTransformID_dispatch_string, 16, DI_DISPATCH_STRING, GRAPH_DISPATCH_STRING),
    REGISTER_MITRANSFORM_G(ZL_StandardTransformID_concat_serial, 16, DI_CONCAT_SERIAL, CONCAT_SERIAL_GRAPH),
    REGISTER_MITRANSFORM_G(ZL_StandardTransformID_concat_num, 17, DI_CONCAT_NUM, CONCAT_NUM_GRAPH),
    REGISTER_MITRANSFORM_G(ZL_StandardTransformID_concat_struct, 17, DI_CONCAT_STRUCT, CONCAT_STRUCT_GRAPH),
    REGISTER_MITRANSFORM_G(ZL_StandardTransformID_concat_string, 18, DI_CONCAT_STRING, CONCAT_STRING_GRAPH),
    REGISTER_MITRANSFORM_G(ZL_StandardTransformID_dedup_num, 16, DI_DEDUP_NUM, DEDUP_NUM_GRAPH),
    REGISTER_MITRANSFORM_G(ZL_StandardTransformID_interleave_string, 20, DI_INTERLEAVE, INTERLEAVE_STRING_GRAPH),

    // Conversion operations
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_convert_serial_to_struct,    3, DI_REVERT_SERIAL_TO_STRUCT, CONVERT_SERIAL_TOKEN_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_convert_struct_to_serial,    3, DI_REVERT_STRUCT_TO_SERIAL, CONVERT_TOKEN_SERIAL_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_convert_struct_to_num_le,   3, DI_REVERT_STRUCT_TO_NUM_LE, CONVERT_TOKEN_NUM_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_convert_num_to_struct_le,   3, DI_REVERT_NUM_TO_STRUCT_LE, CONVERT_NUM_TOKEN_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_convert_serial_to_num_le,  3, DI_REVERT_SERIAL_TO_NUM_LE, CONVERT_SERIAL_NUM_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_convert_num_to_serial_le,  3, DI_REVERT_NUM_TO_SERIAL_LE, CONVERT_NUM_SERIAL_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_convert_serial_string,     10, DI_REVERT_SETFIELDSIZES, CONVERT_SERIAL_STRING_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_separate_string_components,10, DI_REVERT_VSF_SEPARATION, SEPARATE_VSF_COMPONENTS_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_convert_struct_to_num_be,   21, DI_REVERT_STRUCT_TO_NUM_BE, CONVERT_TOKEN_NUM_GRAPH),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_convert_serial_to_num_be,  21, DI_REVERT_SERIAL_TO_NUM_BE, CONVERT_SERIAL_NUM_GRAPH),

    // Legacy transforms, for backward compatibility.
    // will be removed in some future
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_transpose_split2, 3, DI_TRANSPOSE_SPLIT2, TRANSPOSE_GRAPH_SPLIT2),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_transpose_split4, 3, DI_TRANSPOSE_SPLIT4, TRANSPOSE_GRAPH_SPLIT4),
    REGISTER_TTRANSFORM_G(ZL_StandardTransformID_transpose_split8, 3, DI_TRANSPOSE_SPLIT8, TRANSPOSE_GRAPH_SPLIT8),

    // Deprecated transforms
    REGISTER_DEPRECATED_TTRANSFORM_G(ZL_StandardTransformID_zstd_fixed, 3, 10, DI_ZSTD_FIXED, FIXED_ENTROPY_GRAPH),
    REGISTER_DEPRECATED_TTRANSFORM_G(ZL_StandardTransformID_fse_deprecated, 3, 14, DI_FSE, PIPE_GRAPH),
    REGISTER_DEPRECATED_TTRANSFORM_G(ZL_StandardTransformID_huffman_deprecated, 3, 14, DI_HUFFMAN, PIPE_GRAPH),
    REGISTER_DEPRECATED_TTRANSFORM_G(ZL_StandardTransformID_huffman_fixed_deprecated, 3, 14, DI_HUFFMAN_FIXED, FIXED_ENTROPY_GRAPH),
};
// clang-format on
