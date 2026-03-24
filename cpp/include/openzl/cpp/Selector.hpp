// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <vector>

#include "openzl/cpp/CParam.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/FunctionGraph.hpp"
#include "openzl/cpp/Input.hpp"
#include "openzl/cpp/LocalParams.hpp"
#include "openzl/cpp/Type.hpp"
#include "openzl/cpp/poly/Optional.hpp"
#include "openzl/cpp/poly/Span.hpp"
#include "openzl/zl_selector.h"

namespace openzl {
class SelectorState {
   public:
    explicit SelectorState(GraphState& state) : state_(&state) {}

    ZL_Graph* get()
    {
        return state_->get();
    }

    const ZL_Graph* get() const
    {
        return state_->get();
    }

    poly::span<const GraphID> customGraphs() const
    {
        return state_->customGraphs();
    }

    int getCParam(CParam param) const
    {
        return state_->getCParam(param);
    }

    poly::optional<int> getLocalIntParam(int key) const
    {
        return state_->getLocalIntParam(key);
    }

    poly::optional<poly::span<const uint8_t>> getLocalParam(int key) const
    {
        return state_->getLocalParam(key);
    }

    void* getScratchSpace(size_t size)
    {
        return state_->getScratchSpace(size);
    }

    void parameterizeDestination(poly::optional<GraphParameters> params)
    {
        params_ = std::move(params);
    }

    poly::optional<GraphPerformance> tryGraph(
            const Input& input,
            GraphID graph,
            const poly::optional<GraphParameters>& params = poly::nullopt) const
    {
        return state_->tryGraph(input, graph, params);
    }

   private:
    friend class Selector;

    GraphState* state_;
    poly::optional<GraphParameters> params_;
};

struct SelectorDescription {
    poly::optional<std::string> name;
    TypeMask inputTypeMask;
    std::vector<GraphID> customGraphs;
    poly::optional<LocalParams> localParams;
};

class Selector : private FunctionGraph {
   public:
    virtual SelectorDescription selectorDescription() const = 0;

    virtual GraphID select(SelectorState& state, const Input& input) const = 0;

    ~Selector() override = default;

    static GraphID registerSelector(
            Compressor& compressor,
            std::shared_ptr<Selector> selector);

   private:
    void graph(GraphState& state) const override;

    FunctionGraphDescription functionGraphDescription() const override;
};
} // namespace openzl
