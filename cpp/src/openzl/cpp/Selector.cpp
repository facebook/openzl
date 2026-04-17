// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/Selector.hpp"

#include <type_traits>

#include "openzl/cpp/Opaque.hpp"

namespace openzl {

/* static */ GraphID Selector::registerSelector(
        Compressor& compressor,
        std::shared_ptr<const Selector> selector)
{
    return FunctionGraph::registerFunctionGraph(
            compressor,
            std::shared_ptr<const FunctionGraph>(
                    selector,
                    static_cast<const FunctionGraph*>(selector.get())));
}

void Selector::graph(GraphState& state) const
{
    auto& edge = state.edges().front();
    SelectorState selectorState(state);
    auto graph = select(selectorState, edge.getInput());
    edge.setDestination(graph, selectorState.params_);
}

FunctionGraphDescription Selector::functionGraphDescription() const
{
    auto selectorDesc = selectorDescription();
    return FunctionGraphDescription{
        .name           = std::move(selectorDesc.name),
        .inputTypeMasks = { selectorDesc.inputTypeMask },
        .customGraphs   = std::move(selectorDesc.customGraphs),
        .localParams    = std::move(selectorDesc.localParams),
    };
}

} // namespace openzl
