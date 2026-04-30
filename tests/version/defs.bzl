# Copyright (c) Meta Platforms, Inc. and affiliates.

load("@fbcode_macros//build_defs:cpp_unittest.bzl", "cpp_unittest")
load("@fbsource//xplat/security/lionhead:defs.bzl", "ALL_EMPLOYEES", "Metadata", "Severity")
load("//security/lionhead/harnesses:defs.bzl", "cpp_lionhead_harness")

def dev_only_unittest(**kwargs):
    if not native.package_name().startswith("openzl/prod"):
        cpp_unittest(**kwargs)

def version_fuzzer(name):
    if not native.package_name().startswith("openzl/prod"):
        # Derive the ftest_name from the name of the rule
        prefix = "ZStrong_VersionTest_"
        if not name.startswith(prefix):
            fail("Bad name")

        cpp_lionhead_harness(
            name = name,
            default_harness_config = {
                # Set 10 minute initialization timeout because the version fuzzers
                # do heavy initialization work in the first iteration.
                "environment": {
                    "AFL_FORKSRV_INIT_TMOUT": "600000",
                },
                # Set 1 minute timeout on inputs, rather than the 10 second default
                "perInputTimeout": 60,
            },
            # Additional versions to run beyond the opt-asan default
            extra_variants = [
                "dbgo-asan",
                "opt-ubsan-security",
            ],
            # The derived test name
            ftest_name = ("VersionTest", name[len(prefix):]),
            # All the fuzzers are defined in this file
            srcs = ["VersionFuzzer.cpp"],
            metadata = Metadata(
                # Class of users that can access the target.
                exposure = ALL_EMPLOYEES,
                # Which project does this harness belong to
                project = "data_compression_experimental",
                # Security impact of target crashing or being killed.
                severity_denial_of_service = Severity.FILE_LOW_PRI_TASK,
                # Security impact of attacker breaking/bypassing target functionality.
                severity_service_takeover = Severity.FILE_LOW_PRI_TASK,
            ),
            # Fuzzer dependencies
            # NOTE: This cannot depend on ZStrong!
            deps = [
                "fbsource//xplat/security/lionhead/utils/lib_ftest:lib",
                "..:fuzz_utils",
                "fbsource//xplat/tools/cxx:resourcesFbcode",
                ":version_test_interface",
            ],
            # We need to --export-dynamic so that we run with libfuzzer
            # Otherwise we get undefined symbol errors.
            linker_flags = ["--export-dynamic"],
            # overridden_link_style = "static_pic",
            # Include both the release and dev version test interface libraries.
            resources = {
                "dev_version_test_interface.so": ":version_test_interface.so",
                "release_version_test_interface.so": "//openzl:release_version_test_interface.so",
            },
            # Disable fuzzers in dev mode because the version
            # test interface shared libraries also have dependencies
            # that are dynamically linked, and can't be resolved.
            # However, trying to link with:
            #   overridden_link_style = "static_pic"
            # causes strange assertions to fail in the zstrong
            # library, which point towards miscompilation, or bad
            # linking.
            #
            # Since these are just fuzzers, and I don't want to
            # spend ages trying to figure out whats going on, I
            # am simply disabling dev mode for now. If you figure
            # out whats going wrong, please re-enable them.
            target_compatible_with = select({
                "DEFAULT": [],
                "ovr_config//build_mode/constraints:dev": ["ovr_config//:none"],
            }),
        )
