// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "security/lionhead/utils/lib_ftest/ftest.h"
#include "tools/cxx/Resources.h"

#include "tests/fuzz_utils.h"

#include "tests/version/VersionTestInterface.h"

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size);

extern "C" void FtestFuzzerSetup()
{
    // Run once during global setup so we are fully initialized before fuzzing
    uint8_t c = 0;
    LLVMFuzzerTestOneInput(&c, 1);
}

namespace zstrong {
namespace tests {
namespace {

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

size_t clampEltWidth(std::string_view data, size_t eltWidth)
{
    size_t const maxEltWidth = std::max<size_t>(1000, data.size());
    return std::min(eltWidth, maxEltWidth);
}

template <typename FDP>
size_t fuzzEltWidth(FDP& f, std::string_view data, size_t eltWidth)
{
    f.template fuzzed<size_t>("elt_width", eltWidth);
    return clampEltWidth(data, eltWidth);
}

std::vector<Node> supportedNodes(VersionTestInterface& vti)
{
    auto const minVersion = minFormatVersion();
    auto const maxVersion = maxFormatVersion();
    std::vector<Node> nodes;
    for (auto const& node : vti.nodes()) {
        auto const v = node.config.formatVersion;
        if (v >= minVersion && v <= maxVersion)
            nodes.push_back(node);
    }
    return nodes;
}

FUZZ(VersionTest, FuzzNodeForwardCompatible)
{
    if (minFormatVersion() > maxFormatVersion())
        return;

    static auto const nodes = supportedNodes(dev());
    auto const node         = f.choices("node", nodes);
    auto const data         = gen_str(f, "input_str", InputLengthInBytes(1));
    auto const eltWidth     = fuzzEltWidth(f, data, node.config.eltWidth);
    std::string compressed;
    try {
        compressed = dev().compress(
                data, eltWidth, node.config.formatVersion, node.id);
    } catch (...) {
        // Compression failed - give up
        return;
    }
    ASSERT_EQ(data, dev().decompress(compressed));
    ASSERT_EQ(data, release().decompress(compressed));
}

FUZZ(VersionTest, FuzzNodeBackwardCompatible)
{
    if (minFormatVersion() > maxFormatVersion())
        return;

    static auto const nodes = supportedNodes(release());
    auto const node         = f.choices("node", nodes);
    auto const data         = gen_str(f, "input_str", InputLengthInBytes(1));
    auto const eltWidth     = fuzzEltWidth(f, data, node.config.eltWidth);
    std::string compressed;
    try {
        compressed = release().compress(
                data, eltWidth, node.config.formatVersion, node.id);
    } catch (...) {
        // Compression failed - give up
        return;
    }
    ASSERT_EQ(data, release().decompress(compressed));
    ASSERT_EQ(data, dev().decompress(compressed));
}

std::vector<Graph> supportedGraphs(VersionTestInterface& vti)
{
    auto const minVersion = minFormatVersion();
    auto const maxVersion = maxFormatVersion();
    std::vector<Graph> graphs;
    for (auto const& graph : vti.graphs()) {
        auto const v = graph.config.formatVersion;
        if (v >= minVersion && v <= maxVersion)
            graphs.push_back(graph);
    }
    return graphs;
}

FUZZ(VersionTest, FuzzGraphForwardCompatible)
{
    if (minFormatVersion() > maxFormatVersion())
        return;

    static auto const graphs = supportedGraphs(dev());
    auto const graph         = f.choices("graph", graphs);
    auto const data          = gen_str(f, "input_str", InputLengthInBytes(1));
    auto const eltWidth      = fuzzEltWidth(f, data, graph.config.eltWidth);
    std::string compressed;
    try {
        compressed = dev().compress(
                data, eltWidth, graph.config.formatVersion, graph.id);
    } catch (...) {
        // Compression failed - give up
        return;
    }
    ASSERT_EQ(data, dev().decompress(compressed));
    ASSERT_EQ(data, release().decompress(compressed));
}

FUZZ(VersionTest, FuzzGraphBackwardCompatible)
{
    if (minFormatVersion() > maxFormatVersion())
        return;

    static auto const graphs = supportedGraphs(release());
    auto const graph         = f.choices("graph", graphs);
    auto const data          = gen_str(f, "input_str", InputLengthInBytes(1));
    auto const eltWidth      = fuzzEltWidth(f, data, graph.config.eltWidth);
    std::string compressed;
    try {
        compressed = release().compress(
                data, eltWidth, graph.config.formatVersion, graph.id);
    } catch (...) {
        // Compression failed - give up
        return;
    }
    ASSERT_EQ(data, release().decompress(compressed));
    ASSERT_EQ(data, dev().decompress(compressed));
}

FUZZ(VersionTest, FuzzRandomGraphFowardCompatible)
{
    if (minFormatVersion() > maxFormatVersion())
        return;

    auto const data     = gen_str(f, "input_str", InputLengthInBytes(1));
    auto const entropy  = f.str("graph_entropy");
    auto const eltWidth = clampEltWidth(
            data,
            f.u32("elt_width", d_u32().with_examples({ 1, 2, 3, 4, 8, 9 })));
    auto const formatVersion = f.u32_range(
            "format_version", minFormatVersion(), maxFormatVersion());
    std::string compressed;
    try {
        compressed = dev().compress(data, eltWidth, formatVersion, entropy);
    } catch (...) {
        // compression failed - give up
        return;
    }
    ASSERT_EQ(data, dev().decompress(compressed));
    ASSERT_EQ(data, release().decompress(compressed));
}

FUZZ(VersionTest, FuzzRandomGraphBackwardCompatible)
{
    if (minFormatVersion() > maxFormatVersion())
        return;

    auto const data     = gen_str(f, "input_str", InputLengthInBytes(1));
    auto const entropy  = f.str("graph_entropy");
    auto const eltWidth = clampEltWidth(
            data,
            f.u32("elt_width", d_u32().with_examples({ 1, 2, 3, 4, 8, 9 })));
    auto const formatVersion = f.u32_range(
            "format_version", minFormatVersion(), maxFormatVersion());
    // TODO(terrelln): Don't allow the release to fail once we update.
    std::string compressed;
    try {
        compressed = release().compress(data, eltWidth, formatVersion, entropy);
    } catch (...) {
        return;
    }
    ASSERT_EQ(data, release().decompress(compressed));
    ASSERT_EQ(data, dev().decompress(compressed));
}

} // namespace
} // namespace tests
} // namespace zstrong
