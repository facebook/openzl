# Copyright (c) Meta Platforms, Inc. and affiliates.

load("@fbcode//fbpkg:fbpkg.bzl", "fbpkg")
load("@fbcode_macros//build_defs:cpp_binary.bzl", "cpp_binary")
load("@fbcode_macros//build_defs:cpp_library.bzl", "cpp_library")
load("@fbcode_macros//build_defs:cpp_unittest.bzl", "cpp_unittest")
load("@fbsource//tools/build_defs:selects.bzl", "selects")
load("@fbsource//tools/build_defs:type_defs.bzl", "is_string")
load("@fbsource//tools/build_defs/windows:windows_flag_map.bzl", "windows_convert_gcc_clang_flags")
load("@fbsource//xplat/security/lionhead:defs.bzl", "ALL_EMPLOYEES", "Interaction", "Metadata", "Priv", "Reachability", "Severity")
load("@fbsource//xplat/security/lionhead/build_defs:generic_harness.bzl", "generic_lionhead_harness")
load("//security/lionhead/harnesses:defs.bzl", "cpp_lionhead_harness")

_ZL_PROD_PREFIXES = [
    "openzl/prod",
    "openzl/prod",
]

_ZL_DEV_PREFIXES = [
    "openzl/dev",
    "openzl/dev",
]

_ZL_PREFIXES = _ZL_PROD_PREFIXES + _ZL_DEV_PREFIXES

def _is_release():
    for prefix in _ZL_PROD_PREFIXES:
        if native.package_name().startswith(prefix):
            return True
    return False

def zl_fbcode_is_release_pp_flag():
    if _is_release():
        return "-DZL_FBCODE_IS_RELEASE=1"
    else:
        return "-DZL_FBCODE_IS_RELEASE=0"

def _strip_prefix(headers, prefix):
    header_map = {}
    for header in headers:
        if not header.startswith(prefix):
            fail("Header {} does not start with {}".format(header, prefix))
        name = header[len(prefix):]
        header_map[name] = header
    return header_map

def public_headers(headers):
    return _strip_prefix(headers, "include/")

def private_headers(headers):
    return _strip_prefix(headers, "src/")

def _zl_repo_prefix():
    for prefix in _ZL_PREFIXES:
        if native.package_name().startswith(prefix):
            return prefix
    fail("Unknown package name: " + native.package_name())

def relative_headers(headers):
    """
    Returns a map of headers relative to the OpenZL repo root.
    This must not be used in OpenZL core, and is meant only for targets that
    don't escape the repo.
    """
    root = _zl_repo_prefix()
    package = native.package_name()
    prefix = package[len(root) + 1:]

    header_map = {}
    for header in headers:
        name = prefix + "/" + header
        header_map[name] = header

    return header_map

# Base compiler flags for all builds (GCC/Clang syntax)
_ZS_COMPILER_FLAGS_CLANG = [
    "-fno-sanitize=pointer-overflow",
]

# Dev compiler flags (GCC/Clang syntax)
_ZS_DEV_COMPILER_FLAGS_CLANG = [
    "-Wall",
    "-Wcast-qual",
    "-Wcast-align",
    "-Wshadow",
    "-Wstrict-aliasing=1",
    "-Wstrict-prototypes",
    "-Wundef",
    "-Wpointer-arith",
    "-Wvla",
    "-Wformat=2",
    "-Wfloat-equal",
    "-Wredundant-decls",  # these two flags together guarantee that each
    "-Wmissing-prototypes",  # function is declared exactly once.
    "-Wswitch-enum",  # all enum vals must have cases in switches.
    "-pedantic",
    "-Wno-unused-function",  # because editor flags static inline functions
    "-Wimplicit-fallthrough",
    "-Wno-c2x-extensions",
    # "-DZS_ERROR_ENABLE_LEAKY_ALLOCATIONS=1", # set this to always create verbose errors
]

# Dev C-specific compiler flags (GCC/Clang syntax)
_ZS_DEV_C_COMPILER_FLAGS_CLANG = [
    "-Wextra",
    "-Wconversion",
    "-Wno-missing-field-initializers",  # Allow missing fields in designated initializers
]

# File-specific compiler flags (GCC/Clang syntax)
_ZS_SRC_FILE_COMPILER_FLAGS_CLANG = {
    "src/openzl/common/errors.c": [
        "-Wno-format-nonliteral",
    ],
    "src/openzl/common/logging.c": [
        "-Wno-format-nonliteral",
    ],
    "src/zstrong/common/errors.c": [
        "-Wno-format-nonliteral",
    ],
    "src/zstrong/common/logging.c": [
        "-Wno-format-nonliteral",
    ],
}

# Convert flags for MSVC when needed
_ZS_COMPILER_FLAGS = select({
    "DEFAULT": _ZS_COMPILER_FLAGS_CLANG,
    "ovr_config//compiler:msvc": windows_convert_gcc_clang_flags(_ZS_COMPILER_FLAGS_CLANG),
})

_ZS_DEV_COMPILER_FLAGS = select({
    "DEFAULT": _ZS_DEV_COMPILER_FLAGS_CLANG,
    "ovr_config//compiler:msvc": windows_convert_gcc_clang_flags(_ZS_DEV_COMPILER_FLAGS_CLANG),
})

_ZS_DEV_C_COMPILER_FLAGS = select({
    "DEFAULT": _ZS_DEV_C_COMPILER_FLAGS_CLANG,
    "ovr_config//compiler:msvc": windows_convert_gcc_clang_flags(_ZS_DEV_C_COMPILER_FLAGS_CLANG),
})

_ZS_SRC_FILE_COMPILER_FLAGS = {
    src: select({
        "DEFAULT": flags,
        "ovr_config//compiler:msvc": windows_convert_gcc_clang_flags(flags),
    })
    for src, flags in _ZS_SRC_FILE_COMPILER_FLAGS_CLANG.items()
}

_ZS_C_COMPILER_FLAGS_CLANG = [
    "-std=c11",
]

_ZS_C_COMPILER_FLAGS = select({
    "DEFAULT": _ZS_C_COMPILER_FLAGS_CLANG,
    "ovr_config//compiler:msvc": windows_convert_gcc_clang_flags(_ZS_C_COMPILER_FLAGS_CLANG),
})

_ZS_CXX_COMPILER_FLAGS_CLANG = [
    "-Wno-c99-extensions",  # permit use of C99 features from C++ (i.e., tests)
    "-Wno-language-extension-token",
]

_ZS_CXX_COMPILER_FLAGS = select({
    "DEFAULT": _ZS_CXX_COMPILER_FLAGS_CLANG,
    "ovr_config//compiler:msvc": windows_convert_gcc_clang_flags(_ZS_CXX_COMPILER_FLAGS_CLANG),
})

ZS_HARNESS_MODES = [
    "fbcode//security/lionhead/mode/dbgo-asan-libfuzzer",
    "fbcode//security/lionhead/mode/opt-asan-afl",
    "fbcode//security/lionhead/mode/opt-asan-libfuzzer",
    "fbcode//security/lionhead/mode/opt-ubsan-security-libfuzzer",
]

ZS_FUZZ_METADATA = Metadata(
    # Class of users that can access the target.
    exposure = ALL_EMPLOYEES,
    # Which project does this harness belong to
    project = "data_compression_experimental",
    # Security impact of target crashing or being killed.
    severity_denial_of_service = Severity.FILE_LOW_PRI_TASK,
    # Security impact of attacker breaking/bypassing target functionality.
    severity_service_takeover = Severity.FILE_LOW_PRI_TASK,
    # No auth is needed
    privilege_required = Priv.PRE_AUTH,
    # Not reachable through network
    reachability = Reachability.LOCAL,
    # no clicks needed by user
    user_interaction_required = Interaction.ZERO_CLICK,
)

def _zs_src_file_compiler_flags(src):
    flags = []
    if not is_string(src):
        flags = src[1]
        src = src[0]

    if src.endswith(".c"):
        flags += _ZS_C_COMPILER_FLAGS
        if not _is_release():
            flags += _ZS_DEV_C_COMPILER_FLAGS
    elif src.endswith(".cpp"):
        flags += _ZS_CXX_COMPILER_FLAGS

    flags += _ZS_SRC_FILE_COMPILER_FLAGS.get(src, [])

    return (src, flags)

def _add_zs_compiler_flags(kwargs, strict_conversions = True, float_equal = True):
    # Start with base compiler flags (already converted for MSVC via select)
    compiler_flags = _ZS_COMPILER_FLAGS

    # Add dev or release compiler flags (already converted for MSVC via select)
    if not _is_release():
        compiler_flags += _ZS_DEV_COMPILER_FLAGS

    # Add the original compiler flags from kwargs
    compiler_flags += kwargs.get("compiler_flags", [])

    # Handle strict_conversions and float_equal filtering
    # Note: These filters work on GCC/Clang flags. For MSVC, the flags are already
    # converted and these specific flags won't be present, so filtering is safe.
    if not strict_conversions:
        compiler_flags = selects.apply(compiler_flags, lambda flags: [x for x in flags if x != "-Wconversion"])

    if not float_equal:
        compiler_flags = selects.apply(compiler_flags, lambda flags: [x for x in flags if x != "-Wfloat-equal"])

    kwargs["compiler_flags"] = compiler_flags

    # add file-specific compiler flags
    kwargs["srcs"] = [
        _zs_src_file_compiler_flags(src)
        for src in kwargs.get("srcs", [])
    ]

    # Set empty header namespace
    kwargs["header_namespace"] = ""
    headers = kwargs.get("headers", None)

    if isinstance(headers, list):
        # Unless we already have a header map, default to headers
        # being relative to the OpenZL repo root.
        kwargs["headers"] = relative_headers(headers)

    private_headers = kwargs.get("private_headers", None)
    if isinstance(private_headers, list):
        # Unless we already have a header map, default to headers
        # being relative to the OpenZL repo root.
        kwargs["private_headers"] = relative_headers(private_headers)

def zs_library(**kwargs):
    _add_zs_compiler_flags(kwargs)
    _zs_library(**kwargs)

def zs_cxxlibrary(strict_conversions = True, float_equal = True, **kwargs):
    _add_zs_compiler_flags(kwargs, strict_conversions = strict_conversions, float_equal = float_equal)
    _zs_library(**kwargs)

def _zs_library(**kwargs):
    cpp_library(
        **kwargs
    )

def zs_binary(**kwargs):
    _add_zs_compiler_flags(kwargs)
    _zs_binary(**kwargs)

def zs_release_binary(**kwargs):
    if _is_release():
        _add_zs_compiler_flags(kwargs)
        _zs_binary(**kwargs)

def zs_cxxbinary(strict_conversions = True, float_equal = True, **kwargs):
    _add_zs_compiler_flags(kwargs, strict_conversions = strict_conversions, float_equal = float_equal)
    _zs_binary(**kwargs)

def _zs_binary(**kwargs):
    cpp_binary(**kwargs)

def zs_unittest(**kwargs):
    _add_zs_compiler_flags(kwargs)
    cpp_unittest(**kwargs)

def dev_fbpkg_builder(**kwargs):
    if not _is_release():
        fbpkg.builder(**kwargs)

def release_fbpkg_builder(**kwargs):
    if _is_release():
        fbpkg.builder(**kwargs)

def zs_fuzzers(ftest_names, generator = None, **kwargs):
    """
    Generates fuzzers for each test case in :ftest_names:.

    Args:
        srcs: The source files with the fuzzers.

        ftest_names: A list of tuples of (test_suite, test_case) to fuzz.
        Each should have a matching FUZZ(test_suite, test_case) in the source file.

        generator: Optionally a binary target that accepts three parameters:
        test_suite, test_case, and output_directory. It should generate an appropriate
        seed corpus for the given ftest in the output_directory. This is used to seed
        the fuzzer during corpus expansion. This allows us to e.g. dynamically generate
        relevant Zstrong compressed frame with interesting transforms for decompression
        fuzzers.

        deps: The fuzzer's dependencies.
    """
    if _is_release():
        # Give release fuzzers a different name
        prefix = "Release_Zstrong_"
    else:
        prefix = "Zstrong_"

    for ftest_name in ftest_names:
        name = prefix + "_".join(ftest_name)
        cpp_lionhead_harness(
            name = name,
            metadata = ZS_FUZZ_METADATA,
            ftest_name = ftest_name,
            harness_configs = {mode: {} for mode in ZS_HARNESS_MODES},
            **kwargs
        )

        if generator:
            generator_name = name + "_Generator"
            generator_config = {
                "seed_generator_command": [
                    "./generator",
                    ftest_name[0],
                    ftest_name[1],
                    "@out_seed_folder@",
                ],
            }
            generic_lionhead_harness(
                name = generator_name,
                bundle_spec_version = 1,
                environment_constraints = {
                    "remote_execution.linux": {},
                    "tw.lionhead": {},
                },
                harness_configs = {mode: generator_config for mode in ZS_HARNESS_MODES},
                harness_default_modes = {
                    "coverage": "fbcode//security/lionhead/mode/opt-cov.v2",
                    "expand": "fbcode//security/lionhead/mode/opt-asan-libfuzzer",
                    "reproduce": "fbcode//security/lionhead/mode/opt-asan-libfuzzer",
                },
                mapped_srcs = {
                    "fuzz": "fbsource//xplat/security/lionhead/utils/runners/libfuzzer:fuzz",
                    "fuzz_utils.py": "fbsource//xplat/security/lionhead/utils/runners:fuzz_utils",
                    "generator": generator,
                    generator_name: ":" + name + "_bin",
                },
                metadata = ZS_FUZZ_METADATA,
            )

def zs_raw_fuzzer(name, **kwargs):
    if _is_release():
        # Give release fuzzers a different name
        name = "Release_" + name
    cpp_lionhead_harness(
        name = name,
        metadata = ZS_FUZZ_METADATA,
        harness_configs = {mode: {} for mode in ZS_HARNESS_MODES},
        **kwargs
    )
