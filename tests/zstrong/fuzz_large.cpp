// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tests/datagen/DataGen.h"
#include "tests/datagen/InputExpander.h"
#include "tests/datagen/random_producer/LionheadFDPWrapper.h"
#include "tests/datagen/test_registry/CustomNodes.h"
#include "tests/fuzz_utils.h"
#include "tests/zstrong/test_fixed_fixture.h"

namespace zstrong {
namespace tests {

class LargeInputTest : public ZStrongTest {};

FUZZ_F(LargeInputTest, FuzzSerialTransform)
{
    using datagen::test_registry::TransformID;
    // for now, just choose from the list of nodes that just take a single
    // string as input
    const TransformID minTrId = TransformID::SplitByStruct;
    const TransformID maxTrId = TransformID::Bitunpack64;

    auto rw = std::make_shared<
            datagen::LionheadFDPWrapper<StructuredFDP<HarnessMode>>>(f);

    const TransformID trId =
            (TransformID)rw->range("transform_id", (int)minTrId, (int)maxTrId);
    const auto& customNode = datagen::test_registry::getCustomNodes().at(trId);
    if (trId == TransformID::SplitByStruct) {
        return;
    }

    const auto inputVec = rw->all_remaining_bytes();
    const auto input    = std::string(inputVec.begin(), inputVec.end());
    if (input.size() == 0) {
        return;
    }
    const auto expandedInput = datagen::InputExpander::expandSerialWithMutation(
            input, 32 * (1 << 20));

    reset();
    const auto node = customNode.registerEncoder(cgraph_);
    if (customNode.registerDecoder) {
        (*customNode.registerDecoder)(dctx_);
    }
    ZL_GraphID const graph = declareGraph(node);
    finalizeGraph(graph, 1);
    testRoundTripCompressionMayFail(expandedInput);
}

FUZZ_F(LargeInputTest, FuzzSerialGraph)
{
    datagen::DataGen dg = fromFDP(f);
    using datagen::test_registry::TransformID;

    // transposeSplit and fieldLZ both take serial input
    const auto coin = dg.coin("transform_id");
    TransformID trId;
    if (coin) {
        trId = TransformID::TransposeSplit;
    } else {
        trId = TransformID::FieldLz;
    }
    const auto& customGraph =
            datagen::test_registry::getCustomGraphs().at(trId);

    const auto inputVec = dg.all_remaining_bytes();
    const auto input    = std::string(inputVec.begin(), inputVec.end());
    if (input.size() == 0) {
        return;
    }
    const auto expandedInput = datagen::InputExpander::expandSerialWithMutation(
            input, 32 * (1 << 20));

    reset();
    const auto graph = customGraph.registerEncoder(cgraph_);
    if (customGraph.registerDecoder) {
        (*customGraph.registerDecoder)(dctx_);
    }
    finalizeGraph(graph, 1);
    testRoundTrip(expandedInput);
}

} // namespace tests
} // namespace zstrong
