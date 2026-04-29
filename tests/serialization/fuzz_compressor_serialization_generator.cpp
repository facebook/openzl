// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string_view>

#include <openssl/sha.h>

#include <folly/FileUtil.h>
#include <folly/String.h>

#include "openzl/cpp/Compressor.hpp"

#include "tests/datagen/DataGen.h"
#include "tests/datagen/random_producer/PRNGWrapper.h"
#include "tests/datagen/structures/CompressorProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/serialization/GraphBuilder.h"
#include "tests/utils.h"

namespace {

using namespace openzl::tests;
namespace fs = std::filesystem;

std::vector<std::string> generateFuzzDeserializationCorpus()
{
    std::random_device rd;
    auto gen                = std::make_shared<std::mt19937>(rd());
    auto rw                 = std::make_shared<datagen::PRNGWrapper>(gen);
    auto compressorProducer = datagen::CompressorProducer{ rw };

    std::vector<std::string> corpus;
    corpus.reserve(1000);
    for (uint32_t i = 0; i < 1000; ++i) {
        auto zlCompressor = compressorProducer.make();
        openzl::CompressorRef compressor(zlCompressor.get());
        corpus.emplace_back(compressor.serialize());
    }
    return corpus;
}

std::vector<std::string> generateFuzzDeserializeAndCompressSimpleCorpus()
{
    std::random_device rd;
    auto gen = std::make_shared<std::mt19937>(rd());
    auto rw  = std::make_shared<datagen::PRNGWrapper>(gen);
    datagen::DataGen datagen(rw);
    std::vector<std::string> corpus;
    for (uint16_t componentId = 0;
         componentId < (uint16_t)OpenZLComponentID::NumComponents;
         componentId++) {
        for (uint32_t i = 0; i < 100; ++i) {
            auto randomSeed = rw->u32("seed");
            auto openzlComponent =
                    makeOpenZLComponent((OpenZLComponentID)componentId);
            if (!openzlComponent->supportsSerialization()) {
                continue;
            }
            openzl::Compressor compressor;
            std::vector<openzl::GraphID> graphs =
                    openzlComponent->predefinedGraphs(compressor);
            for (auto graph :
                 openzlComponent->generateGraphs(compressor, datagen, 3)) {
                graphs.push_back(graph);
            }

            for (auto node : openzlComponent->predefinedNodes(compressor)) {
                graphs.push_back(buildTrivialGraph(compressor.get(), node));
            }
            for (auto node :
                 openzlComponent->generateNodes(compressor, datagen, 3)) {
                graphs.push_back(buildTrivialGraph(compressor.get(), node));
            }
            for (auto graph : graphs) {
                compressor.selectStartingGraph(graph);
                auto serializedCompressor = compressor.serialize();
                std::string info;
                info.reserve(6 + serializedCompressor.size());
                info.append(reinterpret_cast<const char*>(&randomSeed), 4);
                info.append(reinterpret_cast<const char*>(&componentId), 2);
                info.append(serializedCompressor);
                corpus.emplace_back(std::move(info));
            }
        }
    }
    return corpus;
}

std::vector<std::string> generateFuzzDeserializeAndCompressRandomCorpus()
{
    std::random_device rd;
    auto gen = std::make_shared<std::mt19937>(rd());
    auto rw  = std::make_shared<datagen::PRNGWrapper>(gen);
    datagen::DataGen dataGen{ rw };

    std::vector<std::string> corpus;
    corpus.reserve(1000);
    for (uint32_t i = 0; i < 1000; ++i) {
        openzl::Compressor compressor;
        GraphBuilder builder(dataGen, compressor);
        builder.addAllComponents();
        builder.buildCompressor();
        corpus.emplace_back(compressor.serialize());
    }
    return corpus;
}

std::optional<std::vector<std::string>> generateCorpus(std::string_view harness)
{
    if (harness == "FuzzDeserialization") {
        return generateFuzzDeserializationCorpus();
    } else if (harness == "FuzzDeserializeAndCompressSimple") {
        return generateFuzzDeserializeAndCompressSimpleCorpus();
    } else if (harness == "FuzzDeserializeAndCompressRandom") {
        return generateFuzzDeserializeAndCompressRandomCorpus();
    } else if (harness == "FuzzRandomGraphsSerializesAndCompresses") {
        return std::vector<std::string>{};
    } else if (harness == "FuzzRandomCompressorDeserializesSuccessfully") {
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

    if (testSuite != "CompressorSerializationTest") {
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
