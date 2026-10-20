# Copyright (c) Meta Platforms, Inc. and affiliates.

load("@fbcode_macros//build_defs:cpp_library.bzl", "cpp_library")
load("@fbcode_macros//build_defs:python_library.bzl", "python_library")
load(":defs.bzl", "private_headers", "public_headers", "zl_fbcode_is_release_pp_flag", "zs_library")

oncall("data_compression")

zs_library(
    name = "config",
    headers = public_headers(
        glob([
            "include/openzl/zl_config.h",
            "include/zstrong/zs2_config.h",
        ])
    ),
    header_namespace = "",
)

zs_library(
    name = "public_headers",
    headers = public_headers(
        glob(
            [
                "include/openzl/*.h",
                "include/openzl/codecs/**/*.h",
                "include/openzl/detail/**/*.h",
                "include/zstrong/*.h",
                "include/zstrong/codecs/**/*.h",
                "include/zstrong/detail/**/*.h",
            ],
            exclude = [
                "include/openzl/zl_config.h",
                "include/zstrong/zs2_config.h",
            ],
        )
    ),
    header_namespace = "",
    propagated_pp_flags = [
        zl_fbcode_is_release_pp_flag(),
        "-DZL_IS_FBCODE=1",
    ],
    exported_deps = [
        ":config",
    ],
)

zs_library(
    name = "common",
    srcs = glob([
        "src/openzl/common/**/*.c",
        "src/openzl/codecs/**/common_*.c",
        "src/openzl/codecs/common/**/*.c",
        "src/openzl/codecs/**/graph_*.c",
        "src/openzl/shared/**/*.c",
        "src/zstrong/common/**/*.c",
        "src/zstrong/transforms/**/common_*.c",
        "src/zstrong/transforms/common/**/*.c",
        "src/zstrong/transforms/**/graph_*.c",
        "src/zstrong/shared/**/*.c",
    ]),
    headers = private_headers(
        glob([
            "src/openzl/common/**/*.h",
            "src/openzl/codecs/common/**/*.h",
            "src/openzl/codecs/**/common_*.h",
            "src/openzl/codecs/**/graph_*.h",
            "src/openzl/shared/**/*.h",
            "src/zstrong/common/**/*.h",
            "src/zstrong/transforms/common/**/*.h",
            "src/zstrong/transforms/**/common_*.h",
            "src/zstrong/transforms/**/graph_*.h",
            "src/zstrong/shared/**/*.h",
        ])
    )
    | {
        header: header
        for header in [
            "src/openzl/codecs/bitSplit/common_bitSplit_kernel.h",
            "src/openzl/codecs/bitpack/common_bitpack_kernel.h",
            "src/openzl/codecs/common/fast_table.h",
            "src/openzl/codecs/common/fast_table16.h",
            "src/openzl/codecs/common/fast_tag_table.h",
            "src/openzl/codecs/common/window.h",
            "src/openzl/codecs/conversion/graph_conversion.h",
            "src/openzl/codecs/partition/common_partition.h",
            "src/openzl/codecs/pivco_huffman/common_pivco_kernel.h",
            "src/openzl/codecs/quantize/common_quantize.h",
            "src/openzl/codecs/zstd/common_zstd.h",
            "src/openzl/common/a1cbor_helpers.h",
            "src/openzl/common/allocation.h",
            "src/openzl/common/limits.h",
            "src/openzl/common/logging.h",
            "src/openzl/common/materializer_ctx.h",
            "src/openzl/common/opaque.h",
            "src/openzl/common/operation_context.h",
            "src/openzl/common/refcount.h",
            "src/openzl/common/sha256.h",
            "src/openzl/common/stream.h",
            "src/openzl/common/wire_format.h",
            "src/openzl/shared/a1cbor.h",
            "src/openzl/shared/base64.h",
            "src/openzl/shared/data_stats.h",
            "src/openzl/shared/estimate.h",
            "src/openzl/shared/histogram.h",
            "src/openzl/shared/numeric_operations.h",
        ]
    },
    header_namespace = "",
    exported_deps = [
        "fbsource//third-party/xxHash:xxhash",
        "fbsource//third-party/zstd:zstd",
        ":config",
        ":fse",
        ":public_headers",
    ],
)

zs_library(
    name = "compress",
    srcs = glob([
        "src/openzl/compress/**/*.c",
        "src/openzl/codecs/**/encode_*.c",
        "src/openzl/codecs/encoder_registry.c",
        "src/zstrong/compress/**/*.c",
        "src/zstrong/transforms/**/encode_*.c",
        "src/zstrong/transforms/encoder_registry.c",
    ]),
    headers = private_headers(
        glob([
            "src/openzl/compress/**/*.h",
            "src/openzl/codecs/**/encode_*.h",
            "src/openzl/codecs/encoder_registry.h",
            "src/zstrong/compress/**/*.h",
            "src/zstrong/transforms/**/encode_*.h",
            "src/zstrong/transforms/encoder_registry.h",
        ])
    )
    | {
        header: header
        for header in [
            "src/openzl/codecs/bitSplit/encode_bitSplit_binding.h",
            "src/openzl/codecs/bitSplit/encode_bitSplit_kernel.h",
            "src/openzl/codecs/bitSplit/encode_bitsplit_bf16_binding.h",
            "src/openzl/codecs/bitSplit/encode_bitsplit_fp_binding.h",
            "src/openzl/codecs/bitSplit/encode_bitsplit_top8_binding.h",
            "src/openzl/codecs/bitpack/encode_bitpack_binding.h",
            "src/openzl/codecs/bitunpack/encode_bitunpack_binding.h",
            "src/openzl/codecs/concat/encode_concat_binding.h",
            "src/openzl/codecs/constant/encode_constant_binding.h",
            "src/openzl/codecs/constant/encode_constant_kernel.h",
            "src/openzl/codecs/conversion/encode_conversion_binding.h",
            "src/openzl/codecs/dedup/encode_dedup_binding.h",
            "src/openzl/codecs/delta/encode_delta_binding.h",
            "src/openzl/codecs/delta/encode_delta_kernel.h",
            "src/openzl/codecs/dispatchN_byTag/encode_dispatchN_byTag_binding.h",
            "src/openzl/codecs/dispatchN_byTag/encode_dispatchN_byTag_kernel.h",
            "src/openzl/codecs/dispatch_by_tag/encode_dispatch_by_tag_kernel.h",
            "src/openzl/codecs/dispatch_string/encode_dispatch_string_binding.h",
            "src/openzl/codecs/dispatch_string/encode_dispatch_string_kernel.h",
            "src/openzl/codecs/divide_by/encode_divide_by_binding.h",
            "src/openzl/codecs/divide_by/encode_divide_by_kernel.h",
            "src/openzl/codecs/encoder_registry.h",
            "src/openzl/codecs/entropy/deprecated/encode_fse_kernel.h",
            "src/openzl/codecs/entropy/encode_entropy_binding.h",
            "src/openzl/codecs/entropy/encode_entropy_selector.h",
            "src/openzl/codecs/entropy/encode_huffman_kernel.h",
            "src/openzl/codecs/flatpack/encode_flatpack_binding.h",
            "src/openzl/codecs/flatpack/encode_flatpack_kernel.h",
            "src/openzl/codecs/float_deconstruct/encode_float_deconstruct_binding.h",
            "src/openzl/codecs/float_deconstruct/encode_float_deconstruct_kernel.h",
            "src/openzl/codecs/interleave/encode_interleave_binding.h",
            "src/openzl/codecs/lz/encode_field_lz_literals_selector.h",
            "src/openzl/codecs/lz/encode_field_lz_sequences.h",
            "src/openzl/codecs/lz/encode_lz_binding.h",
            "src/openzl/codecs/lz/encode_lz_kernel.h",
            "src/openzl/codecs/lz4/encode_lz4_binding.h",
            "src/openzl/codecs/merge_sorted/encode_merge_sorted_binding.h",
            "src/openzl/codecs/merge_sorted/encode_merge_sorted_kernel.h",
            "src/openzl/codecs/mux_lengths/encode_mux_lengths_binding.h",
            "src/openzl/codecs/mux_lengths/encode_mux_lengths_kernel.h",
            "src/openzl/codecs/parse_int/encode_parse_int_binding.h",
            "src/openzl/codecs/parse_int/encode_parse_int_kernel.h",
            "src/openzl/codecs/partition/encode_partition_binding.h",
            "src/openzl/codecs/partition/encode_partition_bitpack.h",
            "src/openzl/codecs/partition/encode_partition_kernel.h",
            "src/openzl/codecs/pivco_huffman/arch/encode_pivco_arch.h",
            "src/openzl/codecs/pivco_huffman/encode_pivco_binding.h",
            "src/openzl/codecs/pivco_huffman/encode_pivco_kernel.h",
            "src/openzl/codecs/prefix/encode_prefix_binding.h",
            "src/openzl/codecs/prefix/encode_prefix_kernel.h",
            "src/openzl/codecs/quantize/encode_quantize_binding.h",
            "src/openzl/codecs/quantize/encode_quantize_kernel.h",
            "src/openzl/codecs/range_pack/encode_range_pack_binding.h",
            "src/openzl/codecs/range_pack/encode_range_pack_kernel.h",
            "src/openzl/codecs/rolz/encode_rolz_binding.h",
            "src/openzl/codecs/rolz/encode_rolz_kernel.h",
            "src/openzl/codecs/rolz/encode_rolz_sequences.h",
            "src/openzl/codecs/sentinel/encode_sentinel_binding.h",
            "src/openzl/codecs/sentinel/encode_sentinel_kernel.h",
            "src/openzl/codecs/sparse_num/encode_sparse_num_binding.h",
            "src/openzl/codecs/sparse_num/encode_sparse_num_kernel.h",
            "src/openzl/codecs/splitByStruct/encode_splitByStruct_binding.h",
            "src/openzl/codecs/splitByStruct/encode_splitByStruct_kernel.h",
            "src/openzl/codecs/splitN/encode_splitN_binding.h",
            "src/openzl/codecs/splitN/encode_split_byrange_binding.h",
            "src/openzl/codecs/tokenize/encode_tokenize2to1_kernel.h",
            "src/openzl/codecs/tokenize/encode_tokenize4to2_kernel.h",
            "src/openzl/codecs/tokenize/encode_tokenizeVarto4_kernel.h",
            "src/openzl/codecs/tokenize/encode_tokenize_binding.h",
            "src/openzl/codecs/tokenize/encode_tokenize_kernel.h",
            "src/openzl/codecs/tokenize/encode_tokenize_kernel_sort.h",
            "src/openzl/codecs/transpose/encode_transpose_binding.h",
            "src/openzl/codecs/transpose/encode_transpose_kernel.h",
            "src/openzl/codecs/zigzag/encode_zigzag_binding.h",
            "src/openzl/codecs/zigzag/encode_zigzag_kernel.h",
            "src/openzl/codecs/zstd/encode_zstd_binding.h",
            "src/openzl/compress/cctx.h",
            "src/openzl/compress/cdictmgr.h",
            "src/openzl/compress/cgraph.h",
            "src/openzl/compress/cnode.h",
            "src/openzl/compress/cnodes.h",
            "src/openzl/compress/codec_output_cache.h",
            "src/openzl/compress/dyngraph_interface.h",
            "src/openzl/compress/enc_interface.h",
            "src/openzl/compress/encode_frameheader.h",
            "src/openzl/compress/gcparams.h",
            "src/openzl/compress/graph_registry.h",
            "src/openzl/compress/graphmgr.h",
            "src/openzl/compress/graphs/generic_clustering_graph.h",
            "src/openzl/compress/graphs/sddl/simple_data_description_language.h",
            "src/openzl/compress/graphs/sddl/simple_data_description_language_source_code.h",
            "src/openzl/compress/graphs/sddl2/sddl2.h",
            "src/openzl/compress/graphs/sddl2/sddl2_disasm.h",
            "src/openzl/compress/graphs/sddl2/sddl2_interpreter.h",
            "src/openzl/compress/graphs/sddl2/sddl2_vm.h",
            "src/openzl/compress/graphs/small_lengths_graph.h",
            "src/openzl/compress/graphs/split_graph.h",
            "src/openzl/compress/implicit_conversion.h",
            "src/openzl/compress/localparams.h",
            "src/openzl/compress/name.h",
            "src/openzl/compress/nodemgr.h",
            "src/openzl/compress/rtgraphs.h",
            "src/openzl/compress/segmenter.h",
            "src/openzl/compress/segmenters/segmenter_numeric.h",
            "src/openzl/compress/segmenters/segmenter_serial.h",
            "src/openzl/compress/selector.h",
            "src/openzl/compress/selectors/ml/features.h",
            "src/openzl/compress/selectors/ml/gbt.h",
            "src/openzl/compress/selectors/ml/ml_selector_graph.h",
            "src/openzl/compress/selectors/ml/mlselector.h",
            "src/openzl/compress/selectors/selector_brute_force.h",
            "src/openzl/compress/selectors/selector_compress.h",
            "src/openzl/compress/selectors/selector_constant.h",
            "src/openzl/compress/selectors/selector_genericLZ.h",
            "src/openzl/compress/selectors/selector_numeric.h",
            "src/openzl/compress/selectors/selector_store.h",
            "src/openzl/compress/selectors/transformer/cardinality.h",
            "src/openzl/compress/selectors/transformer/generated/score_numeric16.h",
            "src/openzl/compress/selectors/transformer/generated/score_numeric32.h",
            "src/openzl/compress/selectors/transformer/generated/score_numeric64.h",
            "src/openzl/compress/selectors/transformer/generated/score_numeric8.h",
            "src/openzl/compress/selectors/transformer/numeric_extract.h",
            "src/openzl/compress/selectors/transformer/numeric_stats.h",
            "src/openzl/compress/selectors/transformer/wide_arith.h",
            "src/openzl/compress/trStates.h",
        ]
    },
    header_namespace = "",
    compiler_flags = [
        # "-mavx2",
        "-DUSE_FOLLY",
    ],
    deps = [
        "fbsource//third-party/lz4:lz4",
    ],
    exported_deps = [
        "fbsource//third-party/zstd:zstd",
        ":common",
        ":dict",
        ":fse",
    ],
)

zs_library(
    name = "decompress",
    srcs = glob([
        "src/openzl/decompress/**/*.c",
        "src/openzl/codecs/**/decode_*.c",
        "src/openzl/codecs/decoder_registry.c",
        "src/zstrong/decompress/**/*.c",
        "src/zstrong/transforms/**/decode_*.c",
        "src/zstrong/transforms/decoder_registry.c",
    ]),
    headers = private_headers(
        glob([
            "src/openzl/decompress/**/*.h",
            "src/openzl/codecs/**/decode_*.h",
            "src/openzl/codecs/decoder_registry.h",
            "src/zstrong/decompress/**/*.h",
            "src/zstrong/transforms/**/decode_*.h",
            "src/zstrong/transforms/decoder_registry.h",
        ])
    )
    | {
        header: header
        for header in [
            "src/openzl/codecs/bitSplit/decode_bitSplit_binding.h",
            "src/openzl/codecs/bitSplit/decode_bitSplit_kernel.h",
            "src/openzl/codecs/bitpack/decode_bitpack_binding.h",
            "src/openzl/codecs/bitunpack/decode_bitunpack_binding.h",
            "src/openzl/codecs/concat/decode_concat_binding.h",
            "src/openzl/codecs/constant/decode_constant_binding.h",
            "src/openzl/codecs/constant/decode_constant_kernel.h",
            "src/openzl/codecs/conversion/decode_conversion_binding.h",
            "src/openzl/codecs/decoder_registry.h",
            "src/openzl/codecs/dedup/decode_dedup_binding.h",
            "src/openzl/codecs/delta/decode_delta_binding.h",
            "src/openzl/codecs/delta/decode_delta_kernel.h",
            "src/openzl/codecs/dispatchN_byTag/decode_dispatchN_byTag_binding.h",
            "src/openzl/codecs/dispatchN_byTag/decode_dispatchN_byTag_kernel.h",
            "src/openzl/codecs/dispatch_by_tag/decode_dispatch_by_tag_kernel.h",
            "src/openzl/codecs/dispatch_string/decode_dispatch_string_binding.h",
            "src/openzl/codecs/dispatch_string/decode_dispatch_string_kernel.h",
            "src/openzl/codecs/divide_by/decode_divide_by_binding.h",
            "src/openzl/codecs/divide_by/decode_divide_by_kernel.h",
            "src/openzl/codecs/entropy/decode_entropy_binding.h",
            "src/openzl/codecs/entropy/decode_huffman_kernel.h",
            "src/openzl/codecs/entropy/deprecated/decode_fse_kernel.h",
            "src/openzl/codecs/flatpack/decode_flatpack_binding.h",
            "src/openzl/codecs/flatpack/decode_flatpack_kernel.h",
            "src/openzl/codecs/float_deconstruct/decode_float_deconstruct_binding.h",
            "src/openzl/codecs/float_deconstruct/decode_float_deconstruct_kernel.h",
            "src/openzl/codecs/interleave/decode_interleave_binding.h",
            "src/openzl/codecs/lz/decode_lz_binding.h",
            "src/openzl/codecs/lz/decode_lz_kernel.h",
            "src/openzl/codecs/lz4/decode_lz4_binding.h",
            "src/openzl/codecs/merge_sorted/decode_merge_sorted_binding.h",
            "src/openzl/codecs/merge_sorted/decode_merge_sorted_kernel.h",
            "src/openzl/codecs/mux_lengths/decode_mux_lengths_binding.h",
            "src/openzl/codecs/mux_lengths/decode_mux_lengths_kernel.h",
            "src/openzl/codecs/parse_int/decode_parse_int_binding.h",
            "src/openzl/codecs/parse_int/decode_parse_int_kernel.h",
            "src/openzl/codecs/partition/decode_partition_binding.h",
            "src/openzl/codecs/partition/decode_partition_bitpack_fusion.h",
            "src/openzl/codecs/partition/decode_partition_kernel.h",
            "src/openzl/codecs/pivco_huffman/arch/decode_pivco_arch.h",
            "src/openzl/codecs/pivco_huffman/decode_pivco_binding.h",
            "src/openzl/codecs/pivco_huffman/decode_pivco_kernel.h",
            "src/openzl/codecs/prefix/decode_prefix_binding.h",
            "src/openzl/codecs/prefix/decode_prefix_kernel.h",
            "src/openzl/codecs/quantize/decode_quantize_binding.h",
            "src/openzl/codecs/quantize/decode_quantize_kernel.h",
            "src/openzl/codecs/range_pack/decode_range_pack_binding.h",
            "src/openzl/codecs/range_pack/decode_range_pack_kernel.h",
            "src/openzl/codecs/rolz/decode_rolz_binding.h",
            "src/openzl/codecs/rolz/decode_rolz_kernel.h",
            "src/openzl/codecs/sentinel/decode_sentinel_binding.h",
            "src/openzl/codecs/sentinel/decode_sentinel_kernel.h",
            "src/openzl/codecs/sparse_num/decode_sparse_num_binding.h",
            "src/openzl/codecs/sparse_num/decode_sparse_num_kernel.h",
            "src/openzl/codecs/splitByStruct/decode_splitByStruct_binding.h",
            "src/openzl/codecs/splitByStruct/decode_splitByStruct_kernel.h",
            "src/openzl/codecs/splitN/decode_splitN_binding.h",
            "src/openzl/codecs/splitN/decode_splitN_kernel.h",
            "src/openzl/codecs/tokenize/decode_tokenize2to1_kernel.h",
            "src/openzl/codecs/tokenize/decode_tokenize4to2_kernel.h",
            "src/openzl/codecs/tokenize/decode_tokenizeVarto4_kernel.h",
            "src/openzl/codecs/tokenize/decode_tokenize_binding.h",
            "src/openzl/codecs/tokenize/decode_tokenize_kernel.h",
            "src/openzl/codecs/transpose/decode_transpose_binding.h",
            "src/openzl/codecs/transpose/decode_transpose_kernel.h",
            "src/openzl/codecs/zigzag/decode_zigzag_binding.h",
            "src/openzl/codecs/zigzag/decode_zigzag_kernel.h",
            "src/openzl/codecs/zstd/decode_zstd_binding.h",
            "src/openzl/decompress/decode_frameheader.h",
            "src/openzl/decompress/decoder_fusion.h",
            "src/openzl/decompress/dictx.h",
            "src/openzl/decompress/dtransforms.h",
            "src/openzl/decompress/gdparams.h",
        ]
    },
    header_namespace = "",
    deps = [
        "fbsource//third-party/lz4:lz4",
    ],
    exported_deps = [
        "fbsource//third-party/zstd:zstd",
        ":common",
        ":dict",
        ":fse",
    ],
)

zs_library(
    name = "dict",
    srcs = glob([
        "src/openzl/dict/**/*.c",
    ]),
    headers = private_headers(
        glob([
            "src/openzl/dict/**/*.h",
        ])
    )
    | {
        header: header
        for header in [
            "src/openzl/dict/bundle.h",
            "src/openzl/dict/dict.h",
            "src/openzl/dict/dictloader.h",
        ]
    },
    header_namespace = "",
    deps = [
        ":common",
    ],
    exported_deps = [
        ":public_headers",
    ],
)

zs_library(
    name = "zstronglib",
    exported_deps = [
        "fbsource//third-party/zstd:zstd",
        ":common",
        ":compress",
        ":decompress",
        ":dict",
    ],
)

# C-only OpenZL core (no C++ wrapper). For consumers that only need the C API
# and need to build on platforms where the C++ wrapper is not available (e.g. Android).
cpp_library(
    # @autodeps-skip
    name = "openzl_c_core",
    visibility = ["//openzl:openzl_c_core"],
    exported_deps = [
        ":zstronglib",  # @manual
    ],
)

# TODO: Fix FSE: Split into compress and decompress pieces.
zs_library(
    name = "fse",
    srcs = glob([
        "src/openzl/fse/**/*.c",
        "src/zstrong/fse/**/*.c",
    ]) + select({
        "DEFAULT": [],
        "ovr_config//cpu:x86_64": select({
            "DEFAULT": glob([
                "src/openzl/fse/**/*.S",
                "src/zstrong/fse/**/*.S",
            ]),
            "ovr_config//compiler:msvc": [],
        }),
    }),
    headers = private_headers(
        glob([
            "src/openzl/fse/**/*.h",
            "src/zstrong/fse/**/*.h",
        ])
    ),
    header_namespace = "",
    exported_deps = [
        "fbsource//xplat/secure_lib:secure_string",
        ":config",
    ],
)

cpp_library(
    # @autodeps-skip
    name = "openzl_fbcode",
    visibility = ["//openzl:openzl"],
    exported_deps = [
        "custom_parsers:pytorch_model_parser",  # @manual
        "custom_parsers:zip_lexer",  # @manual
        "custom_parsers/shared_components:clustering",  # @manual
        "custom_transforms/json_extract:json_extract",  # @manual
        "custom_transforms/parse:parse",  # @manual
        "custom_transforms/thrift:thrift_lib",  # @manual
        "custom_transforms/thrift:thrift_parse_config_schema-cpp2-types",  # @manual
        "custom_transforms/thrift/kernels:decode_thrift_binding",  # @manual
        "custom_transforms/thrift/kernels:encode_thrift_binding",  # @manual
        "custom_transforms/tulip_v2:tulip_v2",  # @manual
        "tools:zstrong_cpp",  # @manual
        "tools:zstrong_json",  # @manual
        "tools:zstrong_ml",  # @manual
        ":openzl_core",  # @manual
    ],
)

# This target exposes the standalone OpenZL core library that only has zstd as an external dependency.
cpp_library(
    # @autodeps-skip
    name = "openzl_core",
    visibility = ["//openzl:openzl_core"],
    exported_deps = [
        "cpp:openzl_cpp",  # @manual
    ],
)

cpp_library(
    # @autodeps-skip
    name = "openzl_training",
    visibility = ["//openzl:openzl_training"],
    exported_deps = [
        "tools/training:train",  # @manual
    ],
)

# Not intended to be widely used. A supported Python binding is coming.
python_library(
    # @autodeps-skip
    name = "openzl_py_deprecated",
    visibility = ["//openzl:openzl_py_deprecated"],
    deps = [
        "custom_transforms/thrift:thrift_parse_config_schema-py3-types",  # @manual
        "custom_transforms/thrift:thrift_parse_config_schema-python-types",  # @manual
        "tools/py:zstrong_json",  # @manual
        "tools/py:zstrong_ml",  # @manual
    ],
)

# Do not use in production builds.
cpp_library(
    # @autodeps-skip
    name = "openzl_test_utils",
    visibility = ["//openzl:openzl_test_utils"],
    exported_deps = [
        "custom_transforms/thrift/tests:thrift_test_utils",  # @manual
        "custom_transforms/tulip_v2/tests:tulip_v2_data_utils",  # @manual
        "tests:fuzz_utils",  # @manual
        "tests:selector_optimization",  # @manual
        "tests:test_zstrong_fixtures",  # @manual
        "tests/datagen:datagen",
        "tools:fileio",  # @manual
        "tools/streamdump:stream_dump2_headers",  # @manual
    ],
)

python_library(
    # @autodeps-skip
    name = "openzl_py",
    visibility = ["//openzl:openzl_py"],
    deps = [
        "py:openzl",
    ],
)
