// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdio>
#include <filesystem>
#include <optional>
#include <random>
#include <string_view>

#include <openssl/sha.h>

#include <folly/FileUtil.h>
#include <folly/String.h>

#include "openzl/cpp/CCtx.hpp"
#include "tests/datagen/DataGen.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/utils.h"

namespace openzl::tests {
namespace {
namespace fs = std::filesystem;

// Randomly subset the samples for each component to this number
constexpr size_t kSamplesPerComponent = 10;

void addToCorpora(
        std::vector<std::string>& corpora,
        datagen::DataGen& gen,
        Compressor& compressor,
        const OpenZLComponent& component,
        const OpenZLInput& input,
        GraphID graph)
{
    auto min = std::max(component.minFormatVersion(), ZL_MIN_FORMAT_VERSION);
    auto max = std::min(component.maxFormatVersion(), ZL_MAX_FORMAT_VERSION);
    for (auto formatVersion = min; formatVersion <= max; ++formatVersion) {
        auto inputs = input.inputs();
        std::string compressed;
        compressed.resize(component.compressBound(inputs));

        CCtx cctx;
        DCtx dctx;
        component.registerComponent(dctx);
        auto csize = testRoundTrip(
                compressed,
                compressor,
                cctx,
                dctx,
                graph,
                formatVersion,
                inputs);
        compressed.resize(csize);
        corpora.push_back(std::move(compressed));
    }
}

void addToCorpora(
        std::vector<std::string>& corpora,
        datagen::DataGen& gen,
        const OpenZLComponent& component)
{
    Compressor compressor;
    component.registerComponent(compressor);
    compressor.setParameter(CParam::MinStreamSize, -1);
    compressor.setParameter(CParam::StoreOnExpansion, ZL_TernaryParam_disable);
    compressor.setParameter(CParam::PermissiveCompression, 0);
    std::vector<GraphID> graphs;
    for (auto node : component.predefinedNodes(compressor)) {
        graphs.push_back(buildTrivialGraph(compressor.get(), node));
    }
    for (auto node : component.generateNodes(compressor, gen, 1)) {
        graphs.push_back(buildTrivialGraph(compressor.get(), node));
    }
    for (auto graph : component.predefinedGraphs(compressor)) {
        graphs.push_back(graph);
    }
    for (auto graph : component.generateGraphs(compressor, gen, 1)) {
        graphs.push_back(graph);
    }
    for (const auto& input : component.predefinedInputs()) {
        for (auto graph : graphs) {
            addToCorpora(corpora, gen, compressor, component, *input, graph);
        }
    }
    for (auto graph : graphs) {
        for (const auto& input :
             component.generateInputs(gen, 10, 4096, compressor, graph)) {
            addToCorpora(corpora, gen, compressor, component, *input, graph);
        }
    }
}

std::vector<std::string> generateFuzzDecompressCorpus()
{
    datagen::DataGen gen(std::random_device{}());
    std::vector<std::string> corpora;
    for (int component = 0; component < int(OpenZLComponentID::NumComponents);
         ++component) {
        const size_t startSize = corpora.size();
        addToCorpora(
                corpora,
                gen,
                *makeOpenZLComponent(OpenZLComponentID(component)));
        std::random_shuffle(corpora.begin() + startSize, corpora.end());
        corpora.resize(
                std::min(corpora.size(), startSize + kSamplesPerComponent));
    }
    return corpora;
}

std::optional<std::vector<std::string>> generateCorpus(std::string_view harness)
{
    if (harness == "FuzzCompress") {
        // Generate an empty corpus, as this doesn't need a seed.
        return std::vector<std::string>{};
    } else if (harness == "FuzzDecompress") {
        return generateFuzzDecompressCorpus();
    } else if (harness == "FuzzRoundTrip") {
        // Generate an empty corpus, as this doesn't need a seed.
        return std::vector<std::string>{};
    } else {
        return std::nullopt;
    }
}

std::string sha256(std::string_view data)
{
    std::vector<uint8_t> digest;
    digest.resize(SHA256_DIGEST_LENGTH, 0);
    SHA256(reinterpret_cast<uint8_t const*>(data.data()),
           data.size(),
           digest.data());
    return folly::hexlify(digest);
}

} // namespace
} // namespace openzl::tests

int main(int argc, char** argv)
{
    using namespace openzl::tests;

    if (argc != 4) {
        std::fprintf(
                stderr,
                "USAGE: %s TEST_SUITE TEST_CASE OUTPUT_DIRECTORY\n",
                argv[0]);
        return 1;
    }

    std::string const testSuite = argv[1];
    std::string const testCase  = argv[2];
    fs::path const outDir       = argv[3];

    if (testSuite != "OpenZLComponentFuzzer") {
        fprintf(stderr, "Unknown test suite: %s\n", testSuite.c_str());
        return 2;
    }

    auto const corpus = generateCorpus(testCase);
    if (!corpus.has_value()) {
        std::fprintf(stderr, "Unknown test case: %s\n", testCase.c_str());
        return 3;
    }

    fs::create_directories(outDir);

    for (auto const& blob : *corpus) {
        auto const path = outDir / sha256(blob);
        if (!folly::writeFile(blob, path.c_str())) {
            std::fprintf(stderr, "Failed to write path: %s\n", path.c_str());
            return 4;
        }
    }

    return 0;
}
