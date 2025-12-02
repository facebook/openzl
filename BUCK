# Copyright (c) Meta Platforms, Inc. and affiliates.

load("@fbcode_macros//build_defs:cpp_library.bzl", "cpp_library")
load("@fbcode_macros//build_defs:python_library.bzl", "python_library")
load("@fbsource//tools/target_determinator/macros:ci.bzl", "ci")
load("@fbsource//tools/target_determinator/macros:ci_skycastle.bzl", "ci_skycastle")
load(":defs.bzl", "private_headers", "public_headers", "zl_fbcode_is_release_pp_flag", "zs_library")

oncall("data_compression")

zs_library(
    name = "config",
    headers = public_headers(glob([
        "include/openzl/zl_config.h",
        "include/zstrong/zs2_config.h",
    ])),
    header_namespace = "",
)

zs_library(
    name = "public_headers",
    headers = public_headers(glob(
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
    )),
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
    headers = private_headers(glob([
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
    ])),
    header_namespace = "",
    exported_deps = [
        ":config",
        ":fse",
        ":public_headers",
    ],
    exported_external_deps = [
        ("xxHash", None, "xxhash"),
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
    headers = private_headers(glob([
        "src/openzl/compress/**/*.h",
        "src/openzl/codecs/**/encode_*.h",
        "src/openzl/codecs/encoder_registry.h",
        "src/zstrong/compress/**/*.h",
        "src/zstrong/transforms/**/encode_*.h",
        "src/zstrong/transforms/encoder_registry.h",
    ])),
    header_namespace = "",
    compiler_flags = [
        # "-mavx2",
        "-DUSE_FOLLY",
    ],
    deps = [
        "fbsource//third-party/zstd:zstd",
        ":common",
    ],
    exported_deps = [
        ":common",
        ":fse",
    ],
    exported_external_deps = [
        "zstd",
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
    headers = private_headers(glob([
        "src/openzl/decompress/**/*.h",
        "src/openzl/codecs/**/decode_*.h",
        "src/openzl/codecs/decoder_registry.h",
        "src/zstrong/decompress/**/*.h",
        "src/zstrong/transforms/**/decode_*.h",
        "src/zstrong/transforms/decoder_registry.h",
    ])),
    header_namespace = "",
    deps = ["fbsource//third-party/zstd:zstd"],
    exported_deps = [
        ":common",
        ":fse",
    ],
    exported_external_deps = [
        "zstd",
    ],
)

zs_library(
    name = "zstronglib",
    exported_deps = [
        ":common",
        ":compress",
        ":decompress",
    ],
    exported_external_deps = [
        "zstd",
    ],
)

# TODO: Fix FSE: Split into compress and decompress pieces.
zs_library(
    name = "fse",
    srcs = glob([
        "src/openzl/fse/**/*.c",
        "src/zstrong/fse/**/*.c",
    ]) + select({
        "DEFAULT": glob([
            "src/openzl/fse/**/*.S",
            "src/zstrong/fse/**/*.S",
        ]),
        "ovr_config//compiler:msvc": [],
    }),
    headers = private_headers(glob([
        "src/openzl/fse/**/*.h",
        "src/zstrong/fse/**/*.h",
    ])),
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

# CI Skycastle workflow to enforce relative Buck dependencies
ci_skycastle(
    name = "check_relative_deps",
    workflow = "//openzl/dev/skycastle/check_relative_deps.sky",
    ci_srcs = [
        "fbcode/openzl/dev/**",
    ],
    entrypoint = "main",
    oncall = "data_compression",
    schedules = [
        ci.diff,
    ],
)
