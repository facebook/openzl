// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/encoder_registry.h"

#include "openzl/codecs/bitpack/encode_bitpack_binding.h"
#include "openzl/codecs/bitunpack/encode_bitunpack_binding.h"
#include "openzl/codecs/concat/encode_concat_binding.h"
#include "openzl/codecs/constant/encode_constant_binding.h"
#include "openzl/codecs/conversion/encode_conversion_binding.h"
#include "openzl/codecs/dedup/encode_dedup_binding.h"
#include "openzl/codecs/delta/encode_delta_binding.h"
#include "openzl/codecs/dispatchN_byTag/encode_dispatchN_byTag_binding.h"
#include "openzl/codecs/dispatch_string/encode_dispatch_string_binding.h"
#include "openzl/codecs/divide_by/encode_divide_by_binding.h"
#include "openzl/codecs/entropy/encode_entropy_binding.h"
#include "openzl/codecs/flatpack/encode_flatpack_binding.h"
#include "openzl/codecs/float_deconstruct/encode_float_deconstruct_binding.h"
#include "openzl/codecs/interleave/encode_interleave_binding.h"
#include "openzl/codecs/lz/encode_lz_binding.h"
#include "openzl/codecs/lz4/encode_lz4_binding.h"
#include "openzl/codecs/merge_sorted/encode_merge_sorted_binding.h"
#include "openzl/codecs/parse_int/encode_parse_int_binding.h"
#include "openzl/codecs/prefix/encode_prefix_binding.h"
#include "openzl/codecs/quantize/encode_quantize_binding.h"
#include "openzl/codecs/range_pack/encode_range_pack_binding.h"
#include "openzl/codecs/rolz/encode_rolz_binding.h"
#include "openzl/codecs/splitByStruct/encode_splitByStruct_binding.h"
#include "openzl/codecs/splitN/encode_splitN_binding.h"
#include "openzl/codecs/tokenize/encode_tokenize_binding.h"
#include "openzl/codecs/transpose/encode_transpose_binding.h"
#include "openzl/codecs/zigzag/encode_zigzag_binding.h"
#include "openzl/codecs/zstd/encode_zstd_binding.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_version.h"

// Set _minFormatVersion to the value of ZL_MAX_FORMAT_VERSION
// at the time you add the transform.
#define REGISTER_TRANSFORM(_nid, _strid, _minFormatVersion, _macro) \
    REGISTER_DEPRECATED_TRANSFORM(                                  \
            _nid, _strid, _minFormatVersion, ZL_MAX_FORMAT_VERSION, _macro)

/// Registers deprecated transforms that are no longer allowed to be used.
/// Formats in the range [_minFormatVerison, _maxFormatVersion] support the
/// transform.
#define REGISTER_DEPRECATED_TRANSFORM(                              \
        _nid, _strid, _minFormatVersion, _maxFormatVersion, _macro) \
    [_nid] = {                                                      \
    .nodetype = node_internalTransform,                             \
    .publicIDtype = trt_standard,                                   \
    .minFormatVersion = (_minFormatVersion),                        \
    .maxFormatVersion = (_maxFormatVersion),                        \
    .transformDesc = {                                              \
        .publicDesc = _macro(_strid),                               \
    },                                                              \
}

// clang-format off
const CNode ER_standardNodes[STANDARD_ENCODERS_NB] = {
    [ZL_StandardNodeID_illegal] = { .nodetype = node_illegal, .minFormatVersion=ZL_MIN_FORMAT_VERSION, .maxFormatVersion=ZL_MAX_FORMAT_VERSION },
    REGISTER_TRANSFORM(ZL_StandardNodeID_delta_int, ZL_StandardTransformID_delta_int, 3, EI_DELTA_INT),
    REGISTER_TRANSFORM(ZL_StandardNodeID_transpose_split, ZL_StandardTransformID_transpose_split, 11, EI_TRANSPOSE_SPLIT),
    REGISTER_TRANSFORM(ZL_StandardNodeID_zigzag, ZL_StandardTransformID_zigzag, 3, EI_ZIGZAG_NUM),
    REGISTER_TRANSFORM(ZL_StandardNodeID_dispatchN_byTag, ZL_StandardTransformID_dispatchN_byTag, 9, EI_DISPATCHNBYTAG),
    REGISTER_TRANSFORM(ZL_StandardNodeID_float32_deconstruct, ZL_StandardTransformID_float_deconstruct, 4, EI_FLOAT32_DECONSTRUCT),
    REGISTER_TRANSFORM(ZL_StandardNodeID_bfloat16_deconstruct, ZL_StandardTransformID_float_deconstruct, 5, EI_BFLOAT16_DECONSTRUCT),
    REGISTER_TRANSFORM(ZL_StandardNodeID_float16_deconstruct, ZL_StandardTransformID_float_deconstruct, 5, EI_FLOAT16_DECONSTRUCT),
    REGISTER_TRANSFORM(ZL_StandardNodeID_field_lz, ZL_StandardTransformID_field_lz, 3, EI_FIELD_LZ),

    // Conversion operations
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_serial_to_struct, ZL_StandardTransformID_convert_serial_to_struct, 3, EI_CONVERT_SERIAL_TO_STRUCT),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_serial_to_struct2, ZL_StandardTransformID_convert_serial_to_struct, 3, EI_CONVERT_SERIAL_TO_STRUCT2),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_serial_to_struct4, ZL_StandardTransformID_convert_serial_to_struct, 3, EI_CONVERT_SERIAL_TO_STRUCT4),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_serial_to_struct8, ZL_StandardTransformID_convert_serial_to_struct, 3, EI_CONVERT_SERIAL_TO_STRUCT8),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_struct_to_serial,  ZL_StandardTransformID_convert_struct_to_serial, 3, EI_CONVERT_STRUCT_TO_SERIAL),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_struct_to_num_le, ZL_StandardTransformID_convert_struct_to_num_le, 3, EI_CONVERT_STRUCT_TO_NUM_LE),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_struct_to_num_be, ZL_StandardTransformID_convert_struct_to_num_be, 21, EI_CONVERT_STRUCT_TO_NUM_BE),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_num_to_struct_le, ZL_StandardTransformID_convert_num_to_struct_le, 3, EI_CONVERT_NUM_TO_STRUCT_LE),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_serial_to_num8,  ZL_StandardTransformID_convert_serial_to_num_le, 3, EI_CONVERT_SERIAL_TO_NUM8),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_serial_to_num_le16, ZL_StandardTransformID_convert_serial_to_num_le, 3, EI_CONVERT_SERIAL_TO_NUM_LE16),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_serial_to_num_le32, ZL_StandardTransformID_convert_serial_to_num_le, 3, EI_CONVERT_SERIAL_TO_NUM_LE32),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_serial_to_num_le64, ZL_StandardTransformID_convert_serial_to_num_le, 3, EI_CONVERT_SERIAL_TO_NUM_LE64),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_serial_to_num_be16, ZL_StandardTransformID_convert_serial_to_num_be, 21, EI_CONVERT_SERIAL_TO_NUM_BE16),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_serial_to_num_be32, ZL_StandardTransformID_convert_serial_to_num_be, 21, EI_CONVERT_SERIAL_TO_NUM_BE32),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_serial_to_num_be64, ZL_StandardTransformID_convert_serial_to_num_be, 21, EI_CONVERT_SERIAL_TO_NUM_BE64),
    REGISTER_TRANSFORM(ZL_StandardNodeID_convert_num_to_serial_le,   ZL_StandardTransformID_convert_num_to_serial_le, 3, EI_CONVERT_NUM_TO_SERIAL_LE),
    REGISTER_TRANSFORM(ZL_StandardNodeID_separate_string_components, ZL_StandardTransformID_separate_string_components, 10, EI_SEPARATE_VSF_COMPONENTS),
    REGISTER_TRANSFORM(ZL_StandardNodeID_parse_int, ZL_StandardTransformID_parse_int, 19, EI_PARSE_INT),

    REGISTER_TRANSFORM(ZL_StandardNodeID_bitunpack, ZL_StandardTransformID_bitunpack, 6, EI_BITUNPACK),
    REGISTER_TRANSFORM(ZL_StandardNodeID_range_pack, ZL_StandardTransformID_range_pack, 8, EI_RANGE_PACK),
    REGISTER_TRANSFORM(ZL_StandardNodeID_merge_sorted, ZL_StandardTransformID_merge_sorted, 9, EI_MERGE_SORTED),
    REGISTER_TRANSFORM(ZL_StandardNodeID_prefix, ZL_StandardTransformID_prefix, 11, EI_PREFIX),
    REGISTER_TRANSFORM(ZL_StandardNodeID_divide_by, ZL_StandardTransformID_divide_by, 16, EI_DIVIDE_BY_INT),
    REGISTER_TRANSFORM(ZL_StandardNodeID_dispatch_string, ZL_StandardTransformID_dispatch_string, 16, EI_DISPATCH_STRING),
    REGISTER_TRANSFORM(ZL_StandardNodeID_concat_serial, ZL_StandardTransformID_concat_serial, 16, EI_CONCAT_SERIAL),
    REGISTER_TRANSFORM(ZL_StandardNodeID_concat_num, ZL_StandardTransformID_concat_num, 17, EI_CONCAT_NUM),
    REGISTER_TRANSFORM(ZL_StandardNodeID_concat_struct, ZL_StandardTransformID_concat_struct, 17, EI_CONCAT_STRUCT),
    REGISTER_TRANSFORM(ZL_StandardNodeID_concat_string, ZL_StandardTransformID_concat_string, 18, EI_CONCAT_STRING),
    REGISTER_TRANSFORM(ZL_StandardNodeID_dedup_num, ZL_StandardTransformID_dedup_num, 16, EI_DEDUP_NUM),
    REGISTER_TRANSFORM(ZL_StandardNodeID_interleave_string, ZL_StandardTransformID_interleave_string, 20, EI_INTERLEAVE_STRING),
    REGISTER_TRANSFORM(ZL_StandardNodeID_tokenize_struct, ZL_StandardTransformID_tokenize_fixed, 8, EI_TOKENIZE_STRUCT),
    REGISTER_TRANSFORM(ZL_StandardNodeID_tokenize_numeric, ZL_StandardTransformID_tokenize_numeric, 8, EI_TOKENIZE_NUMERIC),
    REGISTER_TRANSFORM(ZL_StandardNodeID_tokenize_string, ZL_StandardTransformID_tokenize_string, 11, EI_TOKENIZE_STRING),
    REGISTER_TRANSFORM(ZL_StandardNodeID_quantize_offsets, ZL_StandardTransformID_quantize_offsets, 3, EI_QUANTIZE_OFFSETS),
    REGISTER_TRANSFORM(ZL_StandardNodeID_quantize_lengths, ZL_StandardTransformID_quantize_lengths, 3, EI_QUANTIZE_LENGTHS),

    // Private Nodes
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_set_string_lens,           ZL_StandardTransformID_convert_serial_string, 10, EI_SETSTRINGLENS),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_fse_v2, ZL_StandardTransformID_fse_v2, 15, EI_FSE_V2),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_huffman_v2, ZL_StandardTransformID_huffman_v2, 15, EI_HUFFMAN_V2),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_huffman_struct_v2, ZL_StandardTransformID_huffman_struct_v2, 15, EI_HUFFMAN_STRUCT_V2),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_fse_ncount, ZL_StandardTransformID_fse_ncount, 15, EI_FSE_NCOUNT),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_zstd, ZL_StandardTransformID_zstd, 3, EI_ZSTD),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_bitpack_serial, ZL_StandardTransformID_bitpack_serial, 3, EI_BITPACK_SERIALIZED),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_bitpack_int, ZL_StandardTransformID_bitpack_int, 3, EI_BITPACK_INTEGER),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_flatpack, ZL_StandardTransformID_flatpack, 3, EI_FLATPACK),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_splitN, ZL_StandardTransformID_splitn, 9, EI_SPLITN),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_splitN_struct, ZL_StandardTransformID_splitn_struct, 15, EI_SPLITN_STRUCT),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_splitN_num, ZL_StandardTransformID_splitn_num, 15, EI_SPLITN_NUM),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_split_by_struct, ZL_StandardTransformID_splitByStruct, ZL_StandardTransformMinVersion_splitByStruct, EI_SPLITBYSTRUCT),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_constant_serial, ZL_StandardTransformID_constant_serial, 11, EI_CONSTANT_SERIALIZED),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_constant_fixed, ZL_StandardTransformID_constant_fixed, 11, EI_CONSTANT_FIXED),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_tokenize_sorted, ZL_StandardTransformID_tokenize_numeric, 8, EI_TOKENIZE_SORTED),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_tokenize_string_sorted, ZL_StandardTransformID_tokenize_string, 11, EI_TOKENIZE_VSF_SORTED),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_dedup_num_trusted, ZL_StandardTransformID_dedup_num, 16, EI_DEDUP_NUM_TRUSTED),
    REGISTER_TRANSFORM(ZL_PrivateStandardNodeID_lz4, ZL_StandardTransformID_lz4, 23, EI_LZ4),

    // Deprecated Nodes
    REGISTER_DEPRECATED_TRANSFORM(ZL_PrivateStandardNodeID_rolz_deprecated, ZL_StandardTransformID_rolz, 3, 12, EI_ROLZ),
    REGISTER_DEPRECATED_TRANSFORM(ZL_PrivateStandardNodeID_fastlz_deprecated, ZL_StandardTransformID_fastlz, 3, 12, EI_FASTLZ),
    REGISTER_DEPRECATED_TRANSFORM(ZL_PrivateStandardNodeID_fse_deprecated, ZL_StandardTransformID_fse_deprecated, 3, 14, EI_FSE),
    REGISTER_DEPRECATED_TRANSFORM(ZL_PrivateStandardNodeID_huffman_deprecated, ZL_StandardTransformID_huffman_deprecated, 3, 14, EI_HUFFMAN),
    REGISTER_DEPRECATED_TRANSFORM(ZL_PrivateStandardNodeID_huffman_fixed_deprecated, ZL_StandardTransformID_huffman_fixed_deprecated, 3, 14, EI_HUFFMAN_FIXED),
    REGISTER_DEPRECATED_TRANSFORM(ZL_PrivateStandardNodeID_zstd_fixed_deprecated, ZL_StandardTransformID_zstd_fixed, 3, 10, EI_ZSTD_FIXED),
    REGISTER_DEPRECATED_TRANSFORM(ZL_PrivateStandardNodeID_transpose_deprecated, ZL_StandardTransformID_transpose, 3, 10, EI_TRANSPOSE),
    REGISTER_DEPRECATED_TRANSFORM(ZL_PrivateStandardNodeID_transpose_split2_deprecated, ZL_StandardTransformID_transpose_split2, 3, 10, EI_TRANSPOSE_SPLIT2),
    REGISTER_DEPRECATED_TRANSFORM(ZL_PrivateStandardNodeID_transpose_split4_deprecated, ZL_StandardTransformID_transpose_split4, 3, 10, EI_TRANSPOSE_SPLIT4),
    REGISTER_DEPRECATED_TRANSFORM(ZL_PrivateStandardNodeID_transpose_split8_deprecated, ZL_StandardTransformID_transpose_split8, 3, 10, EI_TRANSPOSE_SPLIT8),
};
// clang-format on

size_t ER_getNbStandardNodes(void)
{
    size_t nbNodes = 0;
    for (size_t i = 0; i < ZL_ARRAY_SIZE(ER_standardNodes); ++i) {
        if (ER_standardNodes[i].nodetype == node_internalTransform) {
            ++nbNodes;
        } else {
            ZL_ASSERT(ER_standardNodes[i].nodetype == node_illegal);
        }
    }
    return nbNodes;
}

void ER_getAllStandardNodeIDs(ZL_NodeID* nodes, size_t nodesSize)
{
    size_t nbNodes = 0;
    ZL_ASSERT_GE(nodesSize, ER_getNbStandardNodes());
    for (ZL_IDType i = 0;
         i < ZL_ARRAY_SIZE(ER_standardNodes) && nbNodes < nodesSize;
         ++i) {
        if (ER_standardNodes[i].nodetype == node_internalTransform) {
            nodes[nbNodes++].nid = i;
        }
    }
}

ZL_Report ER_forEachStandardNode(ER_StandardNodesCallback cb, void* opaque)
{
    for (ZL_IDType nid = 0; nid < ZL_ARRAY_SIZE(ER_standardNodes); ++nid) {
        if (ER_standardNodes[nid].nodetype == node_internalTransform) {
            ZL_RET_R_IF_ERR(
                    cb(opaque, (ZL_NodeID){ nid }, &ER_standardNodes[nid]));
        }
    }
    return ZL_returnSuccess();
}
