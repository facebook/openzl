// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <numeric>

#include "openzl/codecs/zl_split.h"
#include "openzl/codecs/zl_store.h"
#include "openzl/zl_reflection.h"
#include "tests/datagen/structures/CompressibleStringProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"
#include "tests/utils.h"

namespace openzl::tests::components {
namespace {

std::vector<size_t> randomSegmentSizes(datagen::DataGen& gen)
{
    auto nbSegments = gen.usize_range("nb_segments", 0, 100);
    std::vector<size_t> sizes;
    size_t remaining = gen.usize_range("total_fixed", 0, 4096);
    for (size_t j = 0; j + 1 < nbSegments; ++j) {
        auto sz = gen.usize_range("seg_size", 0, remaining);
        sizes.push_back(sz);
        remaining -= sz;
    }
    if (gen.boolean("last_is_zero")) {
        // Last segment = 0 means "rest of input"
        sizes.push_back(0);
    }
    return sizes;
}

ZL_NodeID getSplitNode(const Compressor& compressor, GraphID graphID)
{
    // Static graph (nodes wrapped by test framework): head node is the
    // SplitN node
    auto nodeID = ZL_Compressor_Graph_getHeadNode(compressor.get(), graphID);
    if (nodeID.nid != ZL_NODE_ILLEGAL.nid) {
        return nodeID;
    }
    // Parameterized graph (from registerSplitGraph): SplitN node is a
    // custom node
    auto customNodes =
            ZL_Compressor_Graph_getCustomNodes(compressor.get(), graphID);
    if (customNodes.nbNodeIDs > 0) {
        return customNodes.nodeids[0];
    }
    return ZL_NODE_ILLEGAL;
}

// Returns the minimum number of elements required by the split graph.
// For serial, one element = one byte.
// For struct/numeric, one element = one struct/numeric element.
std::pair<size_t, size_t> getMinMaxInputElements(
        const Compressor& compressor,
        GraphID graphID,
        size_t maxInputSize)
{
    auto nodeID = getSplitNode(compressor, graphID);
    if (nodeID.nid == ZL_NODE_ILLEGAL.nid) {
        return { 0, 0 };
    }
    auto params = ZL_Compressor_Node_getLocalParams(compressor.get(), nodeID);
    if (params.copyParams.nbCopyParams < 1) {
        return { 0, 0 };
    }
    auto nbSegments =
            params.copyParams.copyParams[0].paramSize / sizeof(size_t);
    poly::span<const size_t> segSizes{
        (const size_t*)params.copyParams.copyParams[0].paramPtr, nbSegments
    };
    size_t sum = 0;
    for (auto sz : segSizes) {
        sum += sz;
    }
    // If the last segment is 0 ("rest"), the minimum is the sum of
    // the other segments. If all are non-zero, input must be exactly
    // the sum.
    auto lastIsZero = !segSizes.empty() && segSizes.back() == 0;
    return { sum, lastIsZero ? std::max(sum, maxInputSize) : sum };
}

// ---- SplitByParam (Serial, min format version 9) ----

class SplitByParamComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "SplitByParam";
    }

    int minFormatVersion() const override
    {
        return 9;
    }

    bool isStandardComponent() const override
    {
        return false;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        std::vector<NodeID> nodes;
        // 0 segments: only accepts 0-element input
        nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                compressor.get(), ZL_Type_serial, nullptr, 0));
        {
            size_t sizes[] = { 0 };
            nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                    compressor.get(), ZL_Type_serial, sizes, 1));
        }
        {
            size_t sizes[] = { 4, 0 };
            nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                    compressor.get(), ZL_Type_serial, sizes, 2));
        }
        {
            size_t sizes[] = { 2, 2, 0 };
            nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                    compressor.get(), ZL_Type_serial, sizes, 3));
        }
        return nodes;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        std::vector<GraphID> graphs;
        // 0 segments, 0 successors: only accepts 0-element input
        graphs.push_back(ZL_Compressor_registerSplitGraph(
                compressor.get(), ZL_Type_serial, nullptr, nullptr, 0));
        {
            size_t sizes[]          = { 0 };
            ZL_GraphID successors[] = { ZL_GRAPH_STORE };
            graphs.push_back(ZL_Compressor_registerSplitGraph(
                    compressor.get(), ZL_Type_serial, sizes, successors, 1));
        }
        {
            size_t sizes[]          = { 4, 0 };
            ZL_GraphID successors[] = { ZL_GRAPH_STORE, ZL_GRAPH_STORE };
            graphs.push_back(ZL_Compressor_registerSplitGraph(
                    compressor.get(), ZL_Type_serial, sizes, successors, 2));
        }
        {
            size_t sizes[]          = { 2, 2, 0 };
            ZL_GraphID successors[] = { ZL_GRAPH_STORE,
                                        ZL_GRAPH_STORE,
                                        ZL_GRAPH_STORE };
            graphs.push_back(ZL_Compressor_registerSplitGraph(
                    compressor.get(), ZL_Type_serial, sizes, successors, 3));
        }
        return graphs;
    }

    std::vector<NodeID> generateNodes(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<NodeID> nodes;
        for (size_t i = 0; i < num; ++i) {
            auto sizes = randomSegmentSizes(gen);
            nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                    compressor.get(),
                    ZL_Type_serial,
                    sizes.data(),
                    sizes.size()));
        }
        return nodes;
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<GraphID> graphs;
        for (size_t i = 0; i < num; ++i) {
            auto sizes = randomSegmentSizes(gen);
            std::vector<ZL_GraphID> successors(sizes.size(), ZL_GRAPH_STORE);
            graphs.push_back(ZL_Compressor_registerSplitGraph(
                    compressor.get(),
                    ZL_Type_serial,
                    sizes.data(),
                    successors.data(),
                    sizes.size()));
        }
        return graphs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        // Only generateInputs() can work correctly because we need to know
        // how large the input needs to be.
        return {};
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor& compressor,
            GraphID graphID) const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        auto [minSize, maxSize] =
                getMinMaxInputElements(compressor, graphID, maxInputSize);
        for (size_t i = 0; i < num; ++i) {
            auto inputSize = gen.usize_range("input_size", minSize, maxSize);
            inputs.push_back(
                    SerialOpenZLInput::make(
                            gen.randStringWithLength("input", inputSize)));
        }
        return inputs;
    }
};

// ---- SplitStructByParam (Struct, min format version 15) ----

class SplitStructByParamComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "SplitStructByParam";
    }

    int minFormatVersion() const override
    {
        return 15;
    }

    bool isStandardComponent() const override
    {
        return false;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        std::vector<NodeID> nodes;
        // 0 segments: only accepts 0-element input
        nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                compressor.get(), ZL_Type_struct, nullptr, 0));
        {
            size_t sizes[] = { 0 };
            nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                    compressor.get(), ZL_Type_struct, sizes, 1));
        }
        {
            size_t sizes[] = { 2, 0 };
            nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                    compressor.get(), ZL_Type_struct, sizes, 2));
        }
        {
            size_t sizes[] = { 1, 1, 0 };
            nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                    compressor.get(), ZL_Type_struct, sizes, 3));
        }
        return nodes;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        std::vector<GraphID> graphs;
        // 0 segments, 0 successors: only accepts 0-element input
        graphs.push_back(ZL_Compressor_registerSplitGraph(
                compressor.get(), ZL_Type_struct, nullptr, nullptr, 0));
        {
            size_t sizes[]          = { 0 };
            ZL_GraphID successors[] = { ZL_GRAPH_STORE };
            graphs.push_back(ZL_Compressor_registerSplitGraph(
                    compressor.get(), ZL_Type_struct, sizes, successors, 1));
        }
        {
            size_t sizes[]          = { 2, 0 };
            ZL_GraphID successors[] = { ZL_GRAPH_STORE, ZL_GRAPH_STORE };
            graphs.push_back(ZL_Compressor_registerSplitGraph(
                    compressor.get(), ZL_Type_struct, sizes, successors, 2));
        }
        {
            size_t sizes[]          = { 1, 1, 0 };
            ZL_GraphID successors[] = { ZL_GRAPH_STORE,
                                        ZL_GRAPH_STORE,
                                        ZL_GRAPH_STORE };
            graphs.push_back(ZL_Compressor_registerSplitGraph(
                    compressor.get(), ZL_Type_struct, sizes, successors, 3));
        }
        return graphs;
    }

    std::vector<NodeID> generateNodes(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<NodeID> nodes;
        for (size_t i = 0; i < num; ++i) {
            auto sizes = randomSegmentSizes(gen);
            nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                    compressor.get(),
                    ZL_Type_struct,
                    sizes.data(),
                    sizes.size()));
        }
        return nodes;
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<GraphID> graphs;
        for (size_t i = 0; i < num; ++i) {
            auto sizes = randomSegmentSizes(gen);
            std::vector<ZL_GraphID> successors(sizes.size(), ZL_GRAPH_STORE);
            graphs.push_back(ZL_Compressor_registerSplitGraph(
                    compressor.get(),
                    ZL_Type_struct,
                    sizes.data(),
                    successors.data(),
                    sizes.size()));
        }
        return graphs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        // Only generateInputs() can work correctly because we need to know
        // how large the input needs to be.
        return {};
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor& compressor,
            GraphID graphID) const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            // constexpr size_t widths[] = { 1, 2, 4, 8 };
            // auto widthIdx             = gen.usize_range("width_idx", 0, 3);
            // auto width                = widths[widthIdx];
            auto width                      = gen.usize_range("width", 1, 10);
            auto [minElements, maxElements] = getMinMaxInputElements(
                    compressor, graphID, maxInputSize / width);
            auto numElements =
                    gen.usize_range("num_elements", minElements, maxElements);
            auto input = gen.randStringWithLength("input", numElements * width);
            inputs.push_back(StructOpenZLInput::make(std::move(input), width));
        }
        return inputs;
    }
};

// ---- SplitNumericByParam (Numeric, min format version 15) ----

class SplitNumericByParamComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "SplitNumericByParam";
    }

    int minFormatVersion() const override
    {
        return 15;
    }

    bool isStandardComponent() const override
    {
        return false;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        std::vector<NodeID> nodes;
        // 0 segments: only accepts 0-element input
        nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                compressor.get(), ZL_Type_numeric, nullptr, 0));
        {
            size_t sizes[] = { 0 };
            nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                    compressor.get(), ZL_Type_numeric, sizes, 1));
        }
        {
            size_t sizes[] = { 2, 0 };
            nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                    compressor.get(), ZL_Type_numeric, sizes, 2));
        }
        {
            size_t sizes[] = { 1, 1, 0 };
            nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                    compressor.get(), ZL_Type_numeric, sizes, 3));
        }
        return nodes;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        std::vector<GraphID> graphs;
        // 0 segments, 0 successors: only accepts 0-element input
        graphs.push_back(ZL_Compressor_registerSplitGraph(
                compressor.get(), ZL_Type_numeric, nullptr, nullptr, 0));
        {
            size_t sizes[]          = { 0 };
            ZL_GraphID successors[] = { ZL_GRAPH_STORE };
            graphs.push_back(ZL_Compressor_registerSplitGraph(
                    compressor.get(), ZL_Type_numeric, sizes, successors, 1));
        }
        {
            size_t sizes[]          = { 2, 0 };
            ZL_GraphID successors[] = { ZL_GRAPH_STORE, ZL_GRAPH_STORE };
            graphs.push_back(ZL_Compressor_registerSplitGraph(
                    compressor.get(), ZL_Type_numeric, sizes, successors, 2));
        }
        {
            size_t sizes[]          = { 1, 1, 0 };
            ZL_GraphID successors[] = { ZL_GRAPH_STORE,
                                        ZL_GRAPH_STORE,
                                        ZL_GRAPH_STORE };
            graphs.push_back(ZL_Compressor_registerSplitGraph(
                    compressor.get(), ZL_Type_numeric, sizes, successors, 3));
        }
        return graphs;
    }

    std::vector<NodeID> generateNodes(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<NodeID> nodes;
        for (size_t i = 0; i < num; ++i) {
            auto sizes = randomSegmentSizes(gen);
            nodes.push_back(ZL_Compressor_registerSplitNode_withParams(
                    compressor.get(),
                    ZL_Type_numeric,
                    sizes.data(),
                    sizes.size()));
        }
        return nodes;
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<GraphID> graphs;
        for (size_t i = 0; i < num; ++i) {
            auto sizes = randomSegmentSizes(gen);
            std::vector<ZL_GraphID> successors(sizes.size(), ZL_GRAPH_STORE);
            graphs.push_back(ZL_Compressor_registerSplitGraph(
                    compressor.get(),
                    ZL_Type_numeric,
                    sizes.data(),
                    successors.data(),
                    sizes.size()));
        }
        return graphs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        // Only generateInputs() can work correctly because we need to know
        // how large the input needs to be.
        return {};
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor& compressor,
            GraphID graphID) const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            size_t width = gen.choices<size_t>("width", { 1, 2, 4, 8 });
            auto [minElements, maxElements] = getMinMaxInputElements(
                    compressor, graphID, maxInputSize / width);
            auto numElements =
                    gen.usize_range("num_elements", minElements, maxElements);
            auto input = gen.randStringWithLength("input", numElements * width);
            inputs.push_back(makeNumericInput(std::move(input), width));
        }
        return inputs;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeSplitByParamComponent()
{
    return std::make_unique<SplitByParamComponent>();
}
std::unique_ptr<OpenZLComponent> makeSplitStructByParamComponent()
{
    return std::make_unique<SplitStructByParamComponent>();
}
std::unique_ptr<OpenZLComponent> makeSplitNumericByParamComponent()
{
    return std::make_unique<SplitNumericByParamComponent>();
}
} // namespace openzl::tests::components
