// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
#include <iterator>
#include <set>
#include <string>

#include "cli/utils/compress_profiles.h"
#include "tools/wasm/src/openzl_wasm.h"

TEST(WasmBindingTest, WarnsWhenProfileCatalogsDiffer)
{
    std::set<std::string> cliNames;
    for (const auto& [name, profile] : openzl::cli::compressProfiles()) {
        cliNames.insert(name);
    }

    std::set<std::string> wasmNames;
    for (int i = 0; i < OPENZL_WASM_PROFILE_COUNT; ++i) {
        const char* name =
                openzl_wasm_profileName(static_cast<openzl_wasm_Profile>(i));
        ASSERT_NE(name, nullptr) << "profile " << i;
        wasmNames.insert(name);
    }

    std::set<std::string> cliOnlyNames;
    std::set_difference(
            cliNames.begin(),
            cliNames.end(),
            wasmNames.begin(),
            wasmNames.end(),
            std::inserter(cliOnlyNames, cliOnlyNames.end()));
    std::set<std::string> wasmOnlyNames;
    std::set_difference(
            wasmNames.begin(),
            wasmNames.end(),
            cliNames.begin(),
            cliNames.end(),
            std::inserter(wasmOnlyNames, wasmOnlyNames.end()));

    if (!cliOnlyNames.empty()) {
        std::cerr << "Warning: these CLI profiles are not listed in the WASM "
                     "profile catalog: "
                  << testing::PrintToString(cliOnlyNames) << "\n";
    }
    if (!wasmOnlyNames.empty()) {
        std::cerr << "Warning: these WASM profiles are not listed in the CLI "
                     "profile catalog: "
                  << testing::PrintToString(wasmOnlyNames) << "\n";
    }
}
