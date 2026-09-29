// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/tests/test_prob_selector_fixture.h" // @manual
#include "custom_transforms/thrift/tests/util.h"     // @manual
#include "custom_transforms/thrift/thrift_parsers.h" // @manual

#include "security/lionhead/utils/lib_ftest/enable_sfdp_thrift.h" // IWYU pragma: keep

// Must be included after enable_sfdp_thrift.h

#include "openzl/zl_public_nodes.h"
#include "security/lionhead/utils/lib_ftest/ftest.h"
#include "tests/fuzz_utils.h"

using apache::thrift::BinarySerializer;
using apache::thrift::CompactSerializer;
using namespace ::testing;

namespace openzl::thrift::tests {

using namespace zstrong::thrift;
namespace cpp2 = zstrong::thrift::tests::cpp2;
using namespace ::openzl::tests;
namespace lionhead = ::facebook::security::lionhead;

namespace {
std::vector<std::string> const& compressExamplesCompact()
{
    static auto* examplesPtr = new std::vector<std::string>([] {
        std::vector<std::string> examples;
        std::mt19937 gen(0xdeadbeef);
        for (size_t n = 0; n < 5; ++n) {
            auto data = generateRandomThrift<CompactSerializer>(gen);
            examples.push_back(std::move(data));
        }
        return examples;
    }());
    return *examplesPtr;
}

std::vector<std::string> const& decompressExamplesCompact()
{
    static auto* examplesPtr = new std::vector<std::string>([] {
        std::vector<std::string> examples;
        std::mt19937 gen(0xdeadbeef);
        for (size_t n = 0; n < 5; ++n) {
            int const seed           = std::uniform_int_distribution()(gen);
            std::string const config = buildValidEncoderConfig(
                    ZL_MAX_FORMAT_VERSION, seed, ConfigGenMode::kMoreFreedom);
            auto data       = generateRandomThrift<CompactSerializer>(gen);
            auto compressed = thriftSplitCompress(
                    thriftCompactConfigurableSplitter,
                    std::move(data),
                    config,
                    ZL_MAX_FORMAT_VERSION);
            examples.push_back(std::move(compressed));
        }
        return examples;
    }());
    return *examplesPtr;
}

std::vector<std::string> const& compressExamplesBinary()
{
    static auto* examplesPtr = new std::vector<std::string>([] {
        std::vector<std::string> examples;
        std::mt19937 gen(0xdeadbeef);
        for (size_t n = 0; n < 5; ++n) {
            auto data = generateRandomThrift<BinarySerializer>(gen);
            examples.push_back(std::move(data));
        }
        return examples;
    }());
    return *examplesPtr;
}

std::vector<std::string> const& decompressExamplesBinary()
{
    static auto* examplesPtr = new std::vector<std::string>([] {
        std::vector<std::string> examples;
        std::mt19937 gen(0xdeadbeef);
        for (size_t n = 0; n < 5; ++n) {
            int const seed           = std::uniform_int_distribution()(gen);
            std::string const config = buildValidEncoderConfig(
                    ZL_MAX_FORMAT_VERSION, seed, ConfigGenMode::kMoreFreedom);
            auto data       = generateRandomThrift<BinarySerializer>(gen);
            auto compressed = thriftSplitCompress(
                    thriftBinaryConfigurableSplitter,
                    std::move(data),
                    config,
                    ZL_MAX_FORMAT_VERSION);
            examples.push_back(std::move(compressed));
        }
        return examples;
    }());
    return *examplesPtr;
}

// Generate two format versions, one for the config and one for the encoder
enum class GenFormatVersionsMode { kAllowIncompatible, kForceCompatible };
template <class Mode>
std::pair<int, int> genFormatVersions(
        lionhead::fdp::StructuredFDP<Mode>& f,
        GenFormatVersionsMode mode)
{
    assert(mode == GenFormatVersionsMode::kAllowIncompatible
           || mode == GenFormatVersionsMode::kForceCompatible);

    auto const helper = [&](int minFormatVersion, int maxFormatVersion) {
        // We want to spend most of our fuzzing time on the max format version
        const bool useMaxFormatVersion =
                lionhead::fdp::Coin(0.9).gen("use_max_format_version", f);
        const int generatedFormatVersion =
                lionhead::fdp::Range(minFormatVersion, maxFormatVersion)
                        .gen("format_version", f);
        return useMaxFormatVersion ? maxFormatVersion : generatedFormatVersion;
    };

    const bool useCompatibleVersions =
            lionhead::fdp::Coin(0.9).gen(
                    "should_format_versions_be_compatible", f)
            || mode == GenFormatVersionsMode::kForceCompatible;
    int const encoderFormatVersion =
            helper(kMinFormatVersionEncode, ZL_MAX_FORMAT_VERSION);
    if (useCompatibleVersions) {
        int const configFormatVersion =
                helper(kMinFormatVersionEncode, encoderFormatVersion);
        return { configFormatVersion, encoderFormatVersion };
    } else {
        int const configFormatVersion =
                helper(kMinFormatVersionEncode, ZL_MAX_FORMAT_VERSION);
        return { configFormatVersion, encoderFormatVersion };
    }
}

} // namespace

FUZZ(ThriftCompactTest, FuzzRoundTrip)
{
    auto originalThrift =
            f.template thrift<cpp2::TestStruct>("thrift_test_data");
    auto original = apache::thrift::CompactSerializer::serialize<std::string>(
            originalThrift);
    auto const [configFormatVersion, encoderFormatVersion] =
            genFormatVersions(f, GenFormatVersionsMode::kForceCompatible);
    auto compressed = thriftSplitCompress(
            thriftCompactConfigurableSplitter,
            original,
            buildValidEncoderConfig(
                    configFormatVersion,
                    f.u32("config_seed"),
                    ConfigGenMode::kMoreFreedom),
            encoderFormatVersion);
    thriftSplitDecompress(compressed, original);
}

FUZZ(ThriftCompactTest, FuzzCompress)
{
    std::string input = gen_str(
            f, "input_data", InputLengthInBytes(1), compressExamplesCompact());
    std::string compressed;
    try {
        auto const [configFormatVersion, encoderFormatVersion] =
                genFormatVersions(f, GenFormatVersionsMode::kAllowIncompatible);
        compressed = thriftSplitCompress(
                thriftCompactConfigurableSplitter,
                input,
                buildValidEncoderConfig(
                        configFormatVersion,
                        f.u32("config_seed"),
                        ConfigGenMode::kMoreFreedom),
                encoderFormatVersion);
    } catch (...) {
        // Compression is allowed to fail
        return;
    }
    // If compression succeeds we must round trip
    thriftSplitDecompress(compressed, input);
}

FUZZ(ThriftCompactTest, FuzzDecompress)
{
    std::string input =
            gen_str(f,
                    "input_data",
                    InputLengthInBytes(1),
                    decompressExamplesCompact());
    try {
        thriftSplitDecompress(input, std::nullopt);
    } catch (...) {
        // Failure is okay, just cannot crash
    }
}

FUZZ(ThriftBinaryTest, FuzzRoundTrip)
{
    auto originalThrift =
            f.template thrift<cpp2::TestStruct>("thrift_test_data");
    auto original = apache::thrift::BinarySerializer::serialize<std::string>(
            originalThrift);
    auto const [configFormatVersion, encoderFormatVersion] =
            genFormatVersions(f, GenFormatVersionsMode::kForceCompatible);
    auto compressed = thriftSplitCompress(
            thriftBinaryConfigurableSplitter,
            original,
            buildValidEncoderConfig(
                    configFormatVersion,
                    f.u32("config_seed"),
                    ConfigGenMode::kMoreFreedom),
            encoderFormatVersion);
    thriftSplitDecompress(compressed, original);
}

FUZZ(ThriftBinaryTest, FuzzCompress)
{
    std::string input = gen_str(
            f, "input_data", InputLengthInBytes(1), compressExamplesBinary());
    std::string compressed;
    try {
        auto const [configFormatVersion, encoderFormatVersion] =
                genFormatVersions(f, GenFormatVersionsMode::kAllowIncompatible);
        compressed = thriftSplitCompress(
                thriftBinaryConfigurableSplitter,
                input,
                buildValidEncoderConfig(
                        configFormatVersion,
                        f.u32("config_seed"),
                        ConfigGenMode::kMoreFreedom),
                encoderFormatVersion);
    } catch (...) {
        // Compression is allowed to fail
        return;
    }
    // If compression succeeds we must round trip
    thriftSplitDecompress(compressed, input);
}

FUZZ(ThriftBinaryTest, FuzzDecompress)
{
    std::string input = gen_str(
            f, "input_data", InputLengthInBytes(1), decompressExamplesBinary());
    try {
        thriftSplitDecompress(input, std::nullopt);
    } catch (...) {
        // Failure is okay, just cannot crash
    }
}

FUZZ_F(ProbSelectorTest, FuzzRoundTrip)
{
    int numSuccessors     = f.u32_range("num_successors", 1, 10);
    ZL_GraphID succList[] = { ZL_GRAPH_ZSTD, ZL_GRAPH_STORE, ZL_GRAPH_HUFFMAN };
    std::vector<ZL_GraphID> successors;
    std::vector<size_t> probWeights;
    successors.reserve(numSuccessors);
    probWeights.reserve(numSuccessors);
    for (int i = 0; i < numSuccessors; ++i) {
        successors.push_back(succList[f.u32_range("succ_idx", 0, 2)]);
        probWeights.push_back(f.u32_range("prob_weight", 1, 20));
    }
    std::string input = gen_str(
            f, "input_data", InputLengthInBytes(1), compressExamplesBinary());
    std::vector<uint8_t> convertedInput;
    convertedInput.assign(input.data(), input.data() + input.size());
    testRoundTrip(
            successors.data(),
            probWeights.data(),
            numSuccessors,
            convertedInput);
}

} // namespace openzl::thrift::tests
