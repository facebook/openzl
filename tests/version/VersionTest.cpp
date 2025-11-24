// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "VersionTestInterface.h"

#include <algorithm>
#include <map>
#include <random>
#include <vector>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "tools/cxx/Resources.h"

#include "custom_transforms/thrift/kernels/tests/thrift_kernel_test_utils.h"

namespace zstrong {
namespace {
uint32_t hash4(uint32_t x)
{
    return x * 2654435761U;
}

std::vector<std::string> const& generatedEntropy()
{
    static std::vector<std::string>* entropy = [] {
        auto seed = 0xdeadbeef;
        seed ^= hash4(0xcafebabe);
        seed ^= hash4(0xfaceb00c);
        std::mt19937 gen(seed);
        std::uniform_int_distribution<char> dist;
        auto x = std::make_unique<std::vector<std::string>>();
        x->emplace_back();
        for (auto len : { 5, 10, 20, 50, 100 }) {
            for (size_t i = 0; i < 5; ++i) {
                std::string out;
                out.reserve(len);
                for (size_t j = 0; j < len; ++j) {
                    out.push_back(dist(gen));
                }
                x->emplace_back(out);
            }
        }
        std::shuffle(x->begin(), x->end(), gen);
        return x.release();
    }();
    return *entropy;
}

template <typename Gen>
std::string generateDatum(
        Gen& gen,
        size_t nbElts,
        size_t eltWidth,
        size_t cardinality,
        bool zeroAllowed)
{
    assert(cardinality > 0);
    if (eltWidth > 8 || cardinality >= nbElts) {
        assert(cardinality >= nbElts);
        size_t const bytes = nbElts * eltWidth;
        std::uniform_int_distribution<uint64_t> dist(1, uint64_t(-1));
        std::string out;
        out.resize(bytes + 8);
        for (size_t op = 0; op < bytes; op += 8) {
            uint64_t data = dist(gen);
            memcpy(&out[op], &data, 8);
        }
        out.resize(bytes);
        return out;
    } else {
        std::vector<uint64_t> alphabet;
        alphabet.reserve(cardinality * eltWidth);
        if (zeroAllowed) {
            alphabet.push_back({});
        }
        {
            std::uniform_int_distribution<uint64_t> dist(0, uint64_t(-1));
            for (size_t i = zeroAllowed ? 1 : 0; i < cardinality; ++i) {
                alphabet.push_back(dist(gen) | 4);
            }
        }
        std::string out(nbElts * eltWidth + 8, '\0');
        std::uniform_int_distribution<size_t> dist(0, alphabet.size() - 1);
        for (size_t i = 0; i < nbElts; ++i) {
            memcpy(&out[i * eltWidth], &alphabet[dist(gen)], 8);
        }
        out.resize(nbElts * eltWidth);
        return out;
    }
}

std::vector<std::string> generateData(Config config)
{
    auto seed = 0xdeadbeef;
    seed ^= hash4(config.formatVersion);
    seed ^= hash4(config.eltWidth);
    seed ^= hash4((unsigned)config.zeroAllowed);
    std::mt19937 gen(seed);
    std::vector<std::string> data;
    data.push_back({});
    if (config.eltWidth > 8) {
        for (size_t l = 1; l < 10; ++l) {
            data.push_back(generateDatum(
                    gen, l, config.eltWidth, l, config.zeroAllowed));
        }
    } else {
        data.push_back(generateDatum(
                gen, 10000, config.eltWidth, 5, config.zeroAllowed));
        data.push_back(generateDatum(
                gen, 10000, config.eltWidth, 50, config.zeroAllowed));
        data.push_back(generateDatum(
                gen, 10000, config.eltWidth, 500, config.zeroAllowed));
        for (size_t l = 1; l < 1000; l += 111) {
            for (size_t a = std::max<size_t>(1, l / 4); a <= l; a *= 2) {
                data.push_back(generateDatum(
                        gen, l, config.eltWidth, a, config.zeroAllowed));
            }
        }
    }
    return data;
}

std::string_view constexpr kDevResourceName =
        "openzl/dev/tests/version/dev_version_test_interface.so";
std::string_view constexpr kReleaseResourceName =
        "openzl/dev/tests/version/release_version_test_interface.so";

VersionTestInterface getVersionTestInterface(std::string_view resourceName)
{
    auto const path = build::getResourcePath(resourceName);
    return VersionTestInterface(path.c_str());
}

VersionTestInterface& dev()
{
    static VersionTestInterface* vti =
            new VersionTestInterface(getVersionTestInterface(kDevResourceName));
    return *vti;
}

VersionTestInterface& release()
{
    static VersionTestInterface* vti = new VersionTestInterface(
            getVersionTestInterface(kReleaseResourceName));
    return *vti;
}

unsigned minFormatVersion()
{
    return std::max(dev().minFormatVersion(), release().minFormatVersion());
}

unsigned maxFormatVersion()
{
    return std::min(dev().maxFormatVersion(), release().maxFormatVersion());
}

bool formatVersionIsSupported(unsigned formatVersion)
{
    return formatVersion >= minFormatVersion()
            && formatVersion <= maxFormatVersion();
}

std::vector<std::string> const& getTestData(
        Config config,
        std::optional<NodeID> node = std::nullopt)
{
    static auto* data = new std::map<
            std::pair<Config, std::optional<NodeID>>,
            std::vector<std::string>>();
    std::pair<Config, std::optional<NodeID>> key{ config, node };
    auto it = data->find(key);
    if (it == data->end()) {
        std::vector<std::string> datum;
        if (config.customData == UseCustomData::Enable && node.has_value()) {
            auto customData = dev().customData(node.value());
            for (auto&& [customDatum, eltWidth] : customData) {
                if (eltWidth == config.eltWidth) {
                    datum.push_back(std::move(customDatum));
                }
            }
            assert(!datum.empty());
        } else {
            datum = generateData(config);
        }
        it = data->emplace(key, std::move(datum)).first;
    }
    return it->second;
}

TEST(VersionTest, LibraryVersionIsBumped)
{
    using VersionNumber = std::tuple<unsigned, unsigned, unsigned>;
    VersionNumber const devVersion{ dev().majorVersion(),
                                    dev().minorVersion(),
                                    dev().patchVersion() };
    VersionNumber const releaseVersion{ release().majorVersion(),
                                        release().minorVersion(),
                                        release().patchVersion() };
    ASSERT_LT(releaseVersion, devVersion)
            << "You must bump the version number of dev when making a release!";
}

class VersionTest : public testing::TestWithParam<unsigned> {};

INSTANTIATE_TEST_SUITE_P(
        FormatVersions,
        VersionTest,
        testing::Range(minFormatVersion(), maxFormatVersion() + 1));

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(VersionTest);

/// Test that the list of standard transform IDs available in a given format
/// version matches between dev and release. We check the transform ID instead
/// of the node ID because:
///   1. Node IDs aren't stable between versions
///   2. New nodes can be added without breaking comptability, as long as they
///      are compatible with an existing transform's decoder.
TEST_P(VersionTest, TransformListCompatibility)
{
    auto const formatVersion             = GetParam();
    auto const getTransformIDsForVersion = [formatVersion](auto nodes) {
        auto const end = std::remove_if(
                nodes.begin(), nodes.end(), [formatVersion](auto const& node) {
                    return node.config.formatVersion != formatVersion
                            || node.config.compressionMayFail;
                });
        nodes.resize(end - nodes.begin());
        std::set<int> transformIDs;
        for (auto const& node : nodes) {
            transformIDs.insert(node.transformID.id);
        }
        if (formatVersion >= 6 && formatVersion < 9) {
            // Add ZL_StandardTransformID_bitunpack to older format versions.
            // TODO(T149600916): This can be removed after a release.
            transformIDs.insert(34);
        }
        return std::vector<int>{ transformIDs.begin(), transformIDs.end() };
    };
    auto devTransforms     = getTransformIDsForVersion(dev().nodes());
    auto releaseTransforms = getTransformIDsForVersion(release().nodes());
    EXPECT_EQ(devTransforms, releaseTransforms);
}

class RandomGraphTest
        : public testing::TestWithParam<std::pair<unsigned, unsigned>> {};

static std::vector<std::pair<unsigned, unsigned>> versionEltWidth()
{
    std::vector<std::pair<unsigned, unsigned>> out;
    for (unsigned v = minFormatVersion(); v <= maxFormatVersion(); ++v) {
        for (unsigned eltWidth : { 1, 2, 4, 8 }) {
            out.emplace_back(v, eltWidth);
        }
    }
    return out;
}

INSTANTIATE_TEST_SUITE_P(
        Params,
        RandomGraphTest,
        testing::ValuesIn(versionEltWidth()),
        [](auto const& info) {
            auto const version  = info.param.first;
            auto const eltWidth = info.param.second;
            return fmt::format(
                    "FormatVersion_{}_EltWidth_{}", version, eltWidth);
        });

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(RandomGraphTest);

TEST_P(RandomGraphTest, ForwardCompatibility)
{
    auto const [formatVersion, eltWidth] = GetParam();
    Config config{
        formatVersion, eltWidth, true, UseCustomData::Disable, false
    };
    auto const& dataVec    = getTestData(config);
    auto const& entropyVec = generatedEntropy();
    for (size_t i = 0; i < dataVec.size(); ++i) {
        auto const& data      = dataVec[i];
        auto const& entropy   = entropyVec[i % entropyVec.size()];
        auto const compressed = dev().compress(data, 1, formatVersion, entropy);
        ASSERT_EQ(data, dev().decompress(compressed));
        ASSERT_EQ(data, release().decompress(compressed));
    }
}

TEST_P(RandomGraphTest, BackwardCompatibility)
{
    auto const [formatVersion, eltWidth] = GetParam();
    Config config{
        formatVersion, eltWidth, true, UseCustomData::Disable, false
    };
    auto const& dataVec    = getTestData(config);
    auto const& entropyVec = generatedEntropy();
    for (size_t i = 0; i < dataVec.size(); ++i) {
        auto const& data    = dataVec[i];
        auto const& entropy = entropyVec[i % entropyVec.size()];
        auto const compressed =
                release().compress(data, 1, formatVersion, entropy);
        ASSERT_EQ(data, release().decompress(compressed));
        ASSERT_EQ(data, dev().decompress(compressed));
    }
}

/// Group all nodes with the same NodeID & TransformID into the same test in
/// order to reduce the number of tests
using NodeAndTransformID = std::pair<int, int>;

std::vector<NodeAndTransformID> supportedNodes(VersionTestInterface& vti)
{
    std::unordered_set<NodeAndTransformID> nodes;
    for (auto const& node : vti.nodes()) {
        nodes.insert({ node.id.id, node.transformID.id });
    }
    return std::vector<NodeAndTransformID>{ nodes.begin(), nodes.end() };
}

std::string printNode(testing::TestParamInfo<NodeAndTransformID> const& info)
{
    auto const& [nodeID, transformID] = info.param;
    return fmt::format(
            "{}NodeID_{}_{}TransformID_{}",
            nodeID < 0 ? "Custom" : "",
            std::abs(nodeID),
            transformID < 0 ? "Custom" : "",
            std::abs(transformID));
}

std::string nodeInfoString(Node const& node)
{
    auto const customDataStr = node.config.customData == UseCustomData::Enable
            ? ", CustomData"
            : "";
    return fmt::format(
            "{}NodeID: {}, {}TransformID: {}, FormatVersion: {}, EltWidth: {}{}",
            node.id.id < 0 ? "Custom" : "",
            std::abs(node.id.id),
            node.transformID.id < 0 ? "Custom" : "",
            std::abs(node.transformID.id),
            node.config.formatVersion,
            node.config.eltWidth,
            customDataStr);
}

class DevNodeTest : public testing::TestWithParam<NodeAndTransformID> {};

INSTANTIATE_TEST_SUITE_P(
        SupportedDevNodes,
        DevNodeTest,
        testing::ValuesIn(supportedNodes(dev())),
        printNode);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(DevNodeTest);

TEST_P(DevNodeTest, ForwardCompatibility)
{
    auto const& [nodeID, transformID] = GetParam();
    LOG(INFO) << "Begin test with " << dev().nodes().size() << " nodes";
    auto start = std::chrono::steady_clock::now();
    for (auto const& node : dev().nodes()) {
        LOG(INFO) << "Attempting to run node test: " << node;
        if (!formatVersionIsSupported(node.config.formatVersion)) {
            continue;
        }
        if (node.id.id != nodeID || node.transformID.id != transformID) {
            continue;
        }
        LOG(INFO) << "Running node test: " << nodeInfoString(node);
        for (auto const& data : getTestData(node.config, node.id)) {
            std::string compressed;
            try {
                compressed = dev().compress(
                        data,
                        node.config.eltWidth,
                        node.config.formatVersion,
                        node.id);
            } catch (std::exception const&) {
                if (!node.config.compressionMayFail) {
                    throw;
                }
                continue;
            }
            ASSERT_EQ(data, dev().decompress(compressed));
            ASSERT_EQ(data, release().decompress(compressed));
        }
        LOG(INFO) << "Test succeeded: " << node << " "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    }
}

class ReleaseNodeTest : public testing::TestWithParam<NodeAndTransformID> {};

INSTANTIATE_TEST_SUITE_P(
        SupportedReleaseNodes,
        ReleaseNodeTest,
        testing::ValuesIn(supportedNodes(release())),
        printNode);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ReleaseNodeTest);

TEST_P(ReleaseNodeTest, BackwardCompatibility)
{
    auto const& [nodeID, transformID] = GetParam();
    for (auto const& node : release().nodes()) {
        if (!formatVersionIsSupported(node.config.formatVersion)) {
            continue;
        }
        if (node.id.id != nodeID || node.transformID.id != transformID) {
            continue;
        }
        LOG(INFO) << "Running node test: " << nodeInfoString(node);
        for (auto const& data : getTestData(node.config, node.id)) {
            std::string compressed;
            try {
                compressed = release().compress(
                        data,
                        node.config.eltWidth,
                        node.config.formatVersion,
                        node.id);
            } catch (std::exception const&) {
                if (!node.config.compressionMayFail) {
                    throw;
                }
                continue;
            }
            ASSERT_EQ(data, release().decompress(compressed));
            ASSERT_EQ(data, dev().decompress(compressed));
        }
        LOG(INFO) << "Test succeeded: " << nodeInfoString(node);
    }
}

std::vector<int> supportedGraphs(VersionTestInterface& vti)
{
    std::unordered_set<int> graphIDs;
    for (auto const& graph : vti.graphs()) {
        graphIDs.insert(graph.id.id);
    }
    return std::vector<int>(graphIDs.begin(), graphIDs.end());
}

std::string printGraph(testing::TestParamInfo<int> const& info)
{
    auto const graphID = info.param;
    return fmt::format(
            "{}GraphID_{}", graphID < 0 ? "Custom" : "", std::abs(graphID));
}

std::string graphInfoString(Graph const& graph)
{
    auto const customDataStr = graph.config.customData == UseCustomData::Enable
            ? ", CustomData"
            : "";
    return fmt::format(
            "{}GraphID: {}, FormatVersion: {}, EltWidth: {}{}",
            graph.id.id < 0 ? "Custom" : "",
            std::abs(graph.id.id),
            graph.config.formatVersion,
            graph.config.eltWidth,
            customDataStr);
}

class DevGraphTest : public testing::TestWithParam<int> {};

INSTANTIATE_TEST_SUITE_P(
        SupportedDevGraphs,
        DevGraphTest,
        testing::ValuesIn(supportedGraphs(dev())),
        printGraph);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(DevGraphTest);

TEST_P(DevGraphTest, ForwardCompatibility)
{
    auto const graphID = GetParam();
    LOG(INFO) << "Begin test with " << dev().graphs().size() << " graphs";
    auto start = std::chrono::steady_clock::now();
    for (auto const& graph : dev().graphs()) {
        LOG(INFO) << "Attempting to run graph test: " << graphInfoString(graph);
        if (!formatVersionIsSupported(graph.config.formatVersion)) {
            continue;
        }
        if (graph.id.id != graphID) {
            continue;
        }
        LOG(INFO) << "Running graph test: " << graphInfoString(graph);
        for (auto const& data : getTestData(graph.config)) {
            std::string compressed;
            try {
                compressed = dev().compress(
                        data,
                        graph.config.eltWidth,
                        graph.config.formatVersion,
                        graph.id);
            } catch (std::exception const&) {
                if (!graph.config.compressionMayFail) {
                    throw;
                }
                continue;
            }
            ASSERT_EQ(data, dev().decompress(compressed));
            ASSERT_EQ(data, release().decompress(compressed));
        }
        LOG(INFO) << "Test succeeded: " << graphInfoString(graph) << " "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    }
}

class ReleaseGraphTest : public testing::TestWithParam<int> {};

INSTANTIATE_TEST_SUITE_P(
        SupportedReleaseGraphs,
        ReleaseGraphTest,
        testing::ValuesIn(supportedGraphs(release())),
        printGraph);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ReleaseGraphTest);

TEST_P(ReleaseGraphTest, BackwardCompatibility)
{
    auto const graphID = GetParam();
    for (auto const& graph : release().graphs()) {
        if (!formatVersionIsSupported(graph.config.formatVersion)) {
            continue;
        }
        if (graph.id.id != graphID) {
            continue;
        }
        LOG(INFO) << "Running graph test: " << graphInfoString(graph);
        for (auto const& data : getTestData(graph.config)) {
            std::string compressed;
            try {
                compressed = release().compress(
                        data,
                        graph.config.eltWidth,
                        graph.config.formatVersion,
                        graph.id);
            } catch (std::exception const&) {
                if (!graph.config.compressionMayFail) {
                    throw;
                }
                continue;
            }
            ASSERT_EQ(data, release().decompress(compressed));
            ASSERT_EQ(data, dev().decompress(compressed));
        }
        LOG(INFO) << "Test succeeded: " << graphInfoString(graph);
    }
}

} // namespace
} // namespace zstrong
