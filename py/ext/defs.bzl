# Copyright (c) Meta Platforms, Inc. and affiliates.

load("@fbcode_macros//build_defs:cpp_python_extension.bzl", "cpp_python_extension")
load("@fbcode_macros//build_defs:native_rules.bzl", "buck_genrule")
load("@fbcode_macros//build_defs:python_binary.bzl", "python_binary")

def _gen_module_type_stubs(name_prefix, stubgen_name, base_module, module_name):
    rule_name = name_prefix + "__type_stub__" + module_name
    file_name = module_name.replace(".", "/") + "/__init__.pyi"
    if base_module:
        module_name = base_module + "." + module_name
    buck_genrule(
        name = rule_name,
        out = rule_name,
        cmd = "$(location :{}) -m {} -o $OUT".format(stubgen_name, module_name),
    )
    return (":" + rule_name), file_name

def cpp_python_extension_with_type_stubs(name, base_module = None, module_name = None, submodules = (), **kwargs):
    if not module_name:
        module_name = name

    bootstrap_name = name + "__without_type_stubs"
    cpp_python_extension(name = bootstrap_name, base_module = base_module, module_name = module_name, **kwargs)

    stubgen_name = name + "__stubgen"
    python_binary(
        name = stubgen_name,
        main_module = "nanobind.stubgen",
        deps = [
            "fbsource//third-party/nanobind:stubgen",
            ":" + bootstrap_name,
        ],
    )

    types = {}
    rule_name, file_name = _gen_module_type_stubs(name, stubgen_name, base_module, module_name)
    types[rule_name] = file_name

    for submodule in submodules:
        rule_name, file_name = _gen_module_type_stubs(name, stubgen_name, base_module, module_name + "." + submodule)
        types[rule_name] = file_name

    cpp_python_extension(name = name, base_module = base_module, module_name = module_name, types = types, **kwargs)
