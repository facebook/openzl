// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <random>

#include "security/lionhead/utils/lib_ftest/enable_sfdp_thrift.h"
#include "security/lionhead/utils/lib_ftest/ftest.h"

#include "custom_transforms/tulip_v2/tests/tulip_v2_data_utils.h"
#include "openzl/common/assertion.h"
#include "tests/fuzz_utils.h"

using namespace ::testing;

namespace openzl::tulip_v2::tests {
using namespace openzl::tests;
using zstrong::tulip_v2::TulipV2Successors;

namespace {
template <typename FDP>
TulipV2Successors successors(FDP& f)
{
    TulipV2Successors successors;
    if (f.coin("store_float_features", 0.95)) {
        successors.floatFeatures = ZL_GRAPH_STORE;
    }
    if (f.coin("store_id_list_features", 0.95)) {
        successors.idListFeatures = ZL_GRAPH_STORE;
    }
    if (f.coin("store_id_list_list_features", 0.95)) {
        successors.idListListFeatures = ZL_GRAPH_STORE;
    }
    if (f.coin("store_float_list_features", 0.95)) {
        successors.floatListFeatures = ZL_GRAPH_STORE;
    }
    if (f.coin("store_id_score_list_features", 0.95)) {
        successors.idScoreListFeatures = ZL_GRAPH_STORE;
    }
    if (f.coin("store_everything_else", 0.95)) {
        successors.everythingElse = ZL_GRAPH_STORE;
    }

    return successors;
}

std::vector<std::string> const& compressExamples()
{
    static auto* examplesPtr = new std::vector<std::string>([] {
        std::vector<std::string> examples;
        std::mt19937 gen(0xdeadbeef);
        for (size_t n = 0; n < 5; ++n) {
            examples.push_back(generateTulipV2(n, gen));
        }
        return examples;
    }());
    return *examplesPtr;
}

} // namespace

FUZZ(TulipV2Test, FuzzRoundTrip)
{
    auto tulipV2Data = f.template thrift<TulipV2Data>("tulip_v2_data");
    auto serialized  = encodeTulipV2(tulipV2Data);
    auto compressed  = compressTulipV2(
            serialized,
            successors(f),
            std::max<size_t>(10000, 20 * serialized.size()));
    auto decompressed = decompressTulipV2(compressed);
    ZL_REQUIRE(serialized == decompressed);
}

FUZZ(TulipV2Test, FuzzCompress)
{
    std::string input =
            gen_str(f, "input_data", InputLengthInBytes(1), compressExamples());
    std::string compressed;
    try {
        compressed = compressTulipV2(input, successors(f), 10 * input.size());
    } catch (...) {
        // Compression is allowed to fail
        return;
    }
    // If compression succeeds we must round trip
    auto decompressed = decompressTulipV2(compressed);
    ZL_REQUIRE(input == decompressed);
}

FUZZ(TulipV2Test, FuzzDecompress)
{
    try {
        if (Data != nullptr) {
            size_t const maxDstSize = std::min<size_t>(10 << 20, Size * 100);
            decompressTulipV2(
                    std::string_view((const char*)(Data), Size), maxDstSize);
        }
    } catch (...) {
        // Failure is okay, just cannot crash
    }
}

} // namespace openzl::tulip_v2::tests
