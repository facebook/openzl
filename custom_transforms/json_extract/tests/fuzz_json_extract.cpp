// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <random>

#include "security/lionhead/utils/lib_ftest/ftest.h"

#include "custom_transforms/json_extract/decode_json_extract.h"
#include "custom_transforms/json_extract/encode_json_extract.h"
#include "custom_transforms/json_extract/tests/json_extract_test_data.h"
#include "openzl/common/assertion.h"
#include "tests/fuzz_utils.h"
#include "tools/zstrong_cpp.h"

using namespace ::testing;

namespace openzl::tests {
using namespace zstrong;

namespace {
std::string compressJson(std::string_view data)
{
    zstrong::CGraph cgraph;
    auto node = ZS2_Compressor_registerJsonExtract(cgraph.get(), 0);
    std::vector<ZL_GraphID> store(4, ZL_GRAPH_STORE);
    ZL_GraphID graph = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph.get(), node, store.data(), store.size());
    cgraph.unwrap(ZL_Compressor_selectStartingGraphID(cgraph.get(), graph));
    zstrong::CCtx cctx;
    std::string compressed;
    compressed.resize(data.size() * 6 + 1024);
    zstrong::compress(cctx, &compressed, data, cgraph);
    return compressed;
}

std::string decompressJson(
        std::string_view compressed,
        std::optional<size_t> maxDstSize = std::nullopt)
{
    zstrong::DCtx dctx;
    dctx.unwrap(ZS2_DCtx_registerJsonExtract(dctx.get(), 0));
    return zstrong::decompress(dctx, compressed, maxDstSize);
}

std::vector<std::string> const& compressExamples()
{
    static auto* examplesPtr = new std::vector<std::string>([] {
        std::vector<std::string> examples;
        std::mt19937 gen(0xdeadbeef);
        for (size_t n = 0; n < 40; ++n) {
            examples.push_back(genJsonLikeData(gen, n * 100));
        }
        return examples;
    }());
    return *examplesPtr;
}

std::vector<std::string> const& decompressExamples()
{
    static auto* examplesPtr = new std::vector<std::string>([] {
        std::vector<std::string> examples;
        for (auto const& example : compressExamples()) {
            examples.push_back(compressJson(example));
        }
        return examples;
    }());
    return *examplesPtr;
}
} // namespace

FUZZ(JsonExtractTest, FuzzRoundTrip)
{
    auto const data =
            gen_str(f, "input_data", InputLengthInBytes(1), compressExamples());
    auto const compressed   = compressJson(data);
    auto const decompressed = decompressJson(compressed);
    ZL_REQUIRE(decompressed == data);
}

FUZZ(JsonExtractTest, FuzzDecompress)
{
    std::string input = gen_str(
            f, "input_data", InputLengthInBytes(1), decompressExamples());
    try {
        size_t const maxDstSize =
                std::min<size_t>(10 << 20, input.size() * 100);
        decompressJson(input, maxDstSize);
    } catch (...) {
        // Failure is okay, just cannot crash
    }
}

} // namespace openzl::tests
