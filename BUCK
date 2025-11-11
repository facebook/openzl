# Copyright (c) Meta Platforms, Inc. and affiliates.

load(":defs.bzl", "private_headers", "public_headers", "zs_library")

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
        "src/openzl/fse/**/*.S",
        "src/zstrong/fse/**/*.c",
        "src/zstrong/fse/**/*.S",
    ]),
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
