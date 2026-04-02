// (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/CustomDecoder.hpp"
#include "openzl/cpp/CustomEncoder.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/FunctionGraph.hpp"
#include "openzl/cpp/codecs/Store.hpp"
#include "openzl/zl_reflection.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"
#include "tests/utils.h"

namespace openzl::tests {

class OpenZLComponentTest : public ::testing::TestWithParam<int> {
   public:
    void SetUp() override
    {
        component_ = makeOpenZLComponent(OpenZLComponentID(GetParam()));
        component_->registerComponent(dctx_);
        component_->registerComponent(compressor_);
        // Ensure permissive mode is not on by default
        compressor_.setParameter(CParam::PermissiveCompression, 0);
        // Ensure that streams won't be replaced with store
        compressor_.setParameter(CParam::MinStreamSize, -1);
        compressor_.setParameter(
                CParam::StoreOnExpansion, ZL_TernaryParam_disable);
    }

    GraphID makeTrivialGraph(NodeID node)
    {
        auto numOutputs =
                ZL_Compressor_Node_getNumOutcomes(compressor_.get(), node);
        std::vector<GraphID> successors(numOutputs, graphs::Store{}());
        return compressor_.buildStaticGraph(node, successors);
    }

    void testComponentRoundTrip(
            GraphID graph,
            const OpenZLInput& input,
            int formatVersion)
    {
        auto inputs = input.inputs();
        std::string compressed;
        compressed.resize(component_->compressBound(inputs));
        testRoundTrip(
                compressed,
                compressor_,
                cctx_,
                dctx_,
                graph,
                formatVersion,
                inputs);
    }

    void testComponentWithGraphOnInput(GraphID graph, const OpenZLInput& input)
    {
        auto min =
                std::max(component_->minFormatVersion(), ZL_MIN_FORMAT_VERSION);
        auto max =
                std::min(component_->maxFormatVersion(), ZL_MAX_FORMAT_VERSION);
        for (auto formatVersion = min; formatVersion <= max; ++formatVersion) {
            testComponentRoundTrip(graph, input, formatVersion);
        }
    }

    void testComponentOnGraphs(const std::vector<GraphID>& graphs)
    {
        for (const auto& input : component_->predefinedInputs()) {
            for (const auto graph : graphs) {
                testComponentWithGraphOnInput(graph, *input);
            }
        }
        for (const auto graph : graphs) {
            for (const auto& input : component_->generateInputs(
                         gen_, 10, 256 * 1024, compressor_, graph)) {
                testComponentWithGraphOnInput(graph, *input);
            }
        }
    }

    void testComponent()
    {
        auto graphs = component_->predefinedGraphs(compressor_);
        for (auto graph : component_->generateGraphs(compressor_, gen_, 10)) {
            graphs.push_back(graph);
        }

        for (auto node : component_->predefinedNodes(compressor_)) {
            graphs.push_back(makeTrivialGraph(node));
        }
        for (auto node : component_->generateNodes(compressor_, gen_, 10)) {
            graphs.push_back(makeTrivialGraph(node));
        }

        testComponentOnGraphs(graphs);
    }

    std::unique_ptr<OpenZLComponent> component_;
    Compressor compressor_;
    CCtx cctx_;
    DCtx dctx_;
    datagen::DataGen gen_;

   private:
    void testComponentName(const std::string& name)
    {
        for (const auto& c : name) {
            EXPECT_TRUE(isalnum(c));
        }
    }
};

TEST_P(OpenZLComponentTest, RoundTrip)
{
    testComponent();
}

INSTANTIATE_TEST_SUITE_P(
        ,
        OpenZLComponentTest,
        testing::Range(0, int(OpenZLComponentID::NumComponents)),
        [](const testing::TestParamInfo<int>& param) {
            return makeOpenZLComponent(OpenZLComponentID(param.param))->name();
        });

} // namespace openzl::tests
