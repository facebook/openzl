// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <functional>
#include <map>

#include "tools/training/utils/benchmark.h"

namespace openzl {
namespace training {

/**
 * A generic mutation on an openzl::Compressor produced by training.
 * It is allowed to change any property of the compressor through the mutate()
 * function.
 */
class Mutation {
   public:
    virtual void mutate(Compressor& compressor) const = 0;

    Compressor create(const std::function<Compressor()>& makeCompressor) const
    {
        auto compressor = makeCompressor();
        mutate(compressor);
        return compressor;
    }

    virtual ~Mutation() = default;
};

/**
 * A scoped Mutation that is only allowed to change a single backend graph in
 * the compressor. Given that it only changes a single backend graph, multiple
 * BackendGraphMutation's can be applied to the same compressor without
 * conflicting with each other, under the assumption that one graph doesn't
 * forward to another.
 *
 * It is allowed to either override the base graph with
 * BackendGraphMutation::newGraph() and/or override the parameters with
 * BackendGraphMutation::newGraphParams().
 *
 * Additionally, provides a helper benchmarkBackendGraph to benchmark only the
 * updated backend graph on the inputs to that graph. The inputs should be
 * gathered using collectInputStreamsForGraphs().
 *
 * @see pareto_combination.h, which uses this to merge Pareto-optimal sets of
 * backend compressors for multiple backend graphs into a single Pareto-optimal
 * frontier for the entire compressor.
 */
class BackendGraphMutation : public Mutation {
   public:
    /// @param name The name of the backend graph to mutate
    explicit BackendGraphMutation(std::string name) : name_(std::move(name)) {}

    const std::string& graphName() const
    {
        return name_;
    }

    GraphID graph(const Compressor& compressor) const;

    /**
     * Overrides the base graph with newGraph() if it returns non-null, then
     * overrides the graph parameters with newGraphParams() if it returns
     * non-null.
     */
    void mutate(Compressor& compressor) const override final;

    /**
     * Implement this method to override the base graph of
     * `BackendGraphMutation::graph()` using
     * `ZL_Compressor_overrideBaseGraph()` in the mutation.
     *
     * @returns The new backend graph or null.
     */
    virtual poly::optional<GraphID> newGraph(Compressor& compressor) const
    {
        return poly::nullopt;
    }

    /**
     * Implement this method to override the graph parameters of
     * `BackendGraphMutation::graph()` using
     * `ZL_Compressor_overrideGraphParams()` in the mutation. The graph
     * parameters are overriden after overriding the base graph if both are
     * provided.
     *
     * @returns The new parameters or null.
     */
    virtual poly::optional<GraphParameters> newGraphParams(
            Compressor& compressor) const
    {
        return poly::nullopt;
    }

    /// @returns The results of compressing @p backendGraphInputs as inputs
    /// directly to the backend graph `graph(compressor)` after this mutation is
    /// applied.
    poly::optional<CompressionResult> benchmarkBackendGraph(
            const std::function<Compressor()>& makeCompressor,
            poly::span<const MultiInput> backendGraphInputs) const;

    ~BackendGraphMutation() override = default;

   private:
    std::string name_;
};

} // namespace training
} // namespace openzl
