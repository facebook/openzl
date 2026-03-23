// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdio>
#include <filesystem>
#include <optional>
#include <random>
#include <string_view>

#include <openssl/sha.h>

#include <folly/FileUtil.h>
#include <folly/String.h>

#include "custom_transforms/tulip_v2/tests/tulip_v2_data_utils.h"

namespace {
using namespace openzl::tulip_v2::tests;
namespace fs = std::filesystem;

std::vector<std::string> generateFuzzCompressCorpus()
{
    std::random_device rd;
    std::vector<std::string> examples;
    std::mt19937 gen(rd());
    examples.reserve(100);
    for (size_t n = 0; n < 100; ++n) {
        examples.push_back(generateTulipV2(n % 5, gen));
    }
    return examples;
}

std::vector<std::string> generateFuzzDecompressCorpus()
{
    std::vector<std::string> examples;
    for (auto const& input : generateFuzzCompressCorpus()) {
        auto compressed = compressTulipV2(input, {});
        examples.push_back(std::move(compressed));
    }
    return examples;
}

std::optional<std::vector<std::string>> generateCorpus(std::string_view harness)
{
    if (harness == "FuzzCompress") {
        return generateFuzzCompressCorpus();
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

int main(int argc, char** argv)
{
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

    if (testSuite != "TulipV2Test") {
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
