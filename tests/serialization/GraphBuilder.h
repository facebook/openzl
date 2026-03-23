// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <vector>

#include "openzl/cpp/Compressor.hpp"
#include "tests/datagen/DataGen.h"
#include "tests/registry/OpenZLComponent.h"

namespace openzl::tests {

/**
 * Configuration for graph building behavior.
 */
struct GraphBuilderConfig {
    /// Maximum depth of the graph
    size_t maxDepth = 10;

    /// Probability of terminating graph with store (0.0 to 1.0)
    float stopProbability = 0.1f;

    /// Probability of picking a node vs graph (0.0 to 1.0)
    float nodeProbability = 0.5f;

    /// The maximum number of nodes to include in a graph
    size_t maxNodesInGraph = 20;

    size_t numGeneratedComponents = 3;

    size_t numComponentsPerId = 3;
};

/**
 * Utility class for building random graphs using OpenZLComponents.
 *
 * This class provides functionality similar to the buildGraph function
 * in fuzz_graph.cpp but uses the OpenZLComponent abstraction for generating
 * nodes and graphs. It can combine nodes/graphs from multiple components
 * to create complex random graph structures.
 *
 * Example usage:
 * @code
 *     datagen::DataGen gen;
 *     Compressor compressor;
 *     GraphBuilder builder(gen, compressor);
 *
 *     // Add components to use for graph building
 *     builder.addComponent(components::makeZstdComponent());
 *     builder.addComponent(components::makeDeltaComponent());
 *
 *     // Build a random graph
 *     auto graph = builder.buildGraph(ZL_Type_serial);
 * @endcode
 */
class GraphBuilder {
   public:
    /**
     * Constructs a GraphBuilder with the given random generator and compressor.
     *
     * @param gen Random data generator for making random choices
     * @param compressor Compressor to register graphs/nodes with
     * @param config Optional configuration for graph building behavior
     */
    GraphBuilder(
            datagen::DataGen& gen,
            Compressor& compressor,
            GraphBuilderConfig config = {});

    /**
     * Adds a component whose nodes and graphs can be used during building.
     * Components are registered with the compressor upon addition.
     *
     * @param component The component to add
     */
    void addComponent(std::unique_ptr<OpenZLComponent> component);

    /**
     * Adds all standard OpenZL components to the builder.
     */
    void addAllComponents();

    /**
     * Builds a random graph using buildGraph and sets it as the compressor's
     * starting graph.
     */
    void buildCompressor();

    /**
     * @returns The configuration used by this builder
     */
    const GraphBuilderConfig& config() const
    {
        return config_;
    }

    /**
     * @returns Mutable reference to the configuration
     */
    GraphBuilderConfig& config()
    {
        return config_;
    }

   private:
    /**
     * Builds a store graph appropriate for the input type.
     * For strings, this uses SEPARATE_STRING_COMPONENTS followed by store.
     *
     * @param inType The input type
     * @return A store graph ID
     */
    GraphID buildStoreGraph(ZL_Type inType);

    /**
     * Recursively builds a random graph structure.
     *
     * @param nodesInGraph Counter for nodes in the graph (incremented)
     * @param inType The expected input type for this graph
     * @param maxDepth Maximum remaining depth for recursion
     * @return The constructed graph ID
     */
    GraphID buildGraph(size_t* nodesInGraph, ZL_Type inType, size_t maxDepth);

    /**
     * Collects all available nodes from components that are compatible
     * with the given input type.
     *
     * @param inType The input type to filter by
     * @return Vector of compatible NodeIDs
     */
    std::vector<NodeID> getCompatibleNodes(ZL_Type inType);

    /**
     * Collects all available predefined graphs from components that are
     * compatible with the given input type.
     *
     * @param inType The input type to filter by
     * @return Vector of compatible GraphIDs
     */
    std::vector<GraphID> getCompatibleGraphs(ZL_Type inType);

    datagen::DataGen& gen_;
    Compressor& compressor_;
    GraphBuilderConfig config_;
    std::vector<std::unique_ptr<OpenZLComponent>> components_;
    std::vector<GraphID> graphIDs_;
    std::vector<NodeID> nodeIDs_;
};

} // namespace openzl::tests
