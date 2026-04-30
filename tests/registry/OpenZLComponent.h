// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <vector>

#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/zl_version.h"
#include "tests/datagen/DataGen.h"
#include "tests/registry/OpenZLInput.h"
#include "tests/utils.h" // @manual

namespace openzl::tests {

/**
 * Base class for components that can be used in OpenZL tests. A component
 * provides a set of predefined and generated node and graph configurations,
 * as well as predefined and generated inputs that MUST succeed for every
 * node and graph configuration.
 *
 * A component can be a node-only component, a graph-only component, or both.
 * There does not need to be a 1:1 mapping between components and nodes or
 * graphs. A single node or codec could have multiple components. Or a single
 * component could be used to test multiple nodes or graphs that are logically
 * similar. This is up to the authors discretion. However, if two nodes or
 * graphs require different inputs, they should be in different components, so
 * the inputs can be specialized to the component.
 *
 * These components are used in the OpenZL test suite to:
 * - Test every possible node and graph configuration against every input
 * - Test version compatbility tests against previous OpenZL versions
 * - Fuzz OpenZL components
 */
class OpenZLComponent {
   public:
    /**
     * @returns the CamelCase name for this component containing only
     alphanumeric
     * characters (not '_').
     *
     * NOTE: This name must be unique.
     */
    virtual std::string name() const = 0;

    /**
     * @returns the minimum format version that this component supports.
     */
    virtual int minFormatVersion() const = 0;

    /**
     * @returns the maximum format version that this component supports.
     * Non-deprecated components do not need to override this method.
     */
    virtual int maxFormatVersion() const
    {
        return ZL_MAX_FORMAT_VERSION;
    }

    /**
     * @returns true if the component is a standard component.
     */
    virtual bool isStandardComponent() const
    {
        return true;
    }

    /**
     * @returns true if the component supports serialization. This means that
     * any compressor using NodeIDs or GraphIDs from this component must be
     * deserializable after the `registerComponent(Compressor& compressor)`
     * call.
     */
    virtual bool supportsSerialization() const
    {
        return true;
    }

    /**
     * Registers the base component with the compressor so it can be used within
     * serialized compressors when `supportsSerialization()` returns true.
     * Standard components do not need to implement this method, since there is
     * nothing to register.
     */
    void registerComponent(Compressor& compressor) const
    {
        (void)compressor;
    }

    /**
     * Registers the component with the DCtx. Standard components do not need to
     * implement this method, since there is nothing to register.
     */
    virtual void registerComponent(DCtx& dctx) const
    {
        (void)dctx;
    }

    /**
     * @returns The maximum compressed size of @p inputs.
     * Components can override this if ZL_compressBound() is not large enough,
     * e.g. because it expands the inputs significantly.
     */
    virtual size_t compressBound(poly::span<const Input> inputs) const
    {
        size_t totalSrcSize = 0;
        for (const auto& input : inputs) {
            totalSrcSize += input.contentSize();
            if (input.type() == openzl::Type::String) {
                totalSrcSize += input.numElts() * sizeof(*input.stringLens());
            }
        }
        totalSrcSize += inputs.size() * 256;
        return ZL_COMPRESSBOUND_UNGUARDED(totalSrcSize);
    }

    /**
     * @returns A list of predefined node configurations for this component.
     *
     * Graph-only components should not implement this method.
     */
    virtual std::vector<NodeID> predefinedNodes(Compressor& compressor) const
    {
        return {};
    }

    /**
     * @returns A list of predefined graph configurations for this component.
     *
     * Node-only components should not implement this method.
     */
    virtual std::vector<GraphID> predefinedGraphs(Compressor& compressor) const
    {
        return {};
    }

    /**
     * @returns A list of at most `num` generated node configurations for this
     * component. This method should only be overridden if there are interesting
     * node configurations that are not covered by `predefinedNodes()`.
     *
     * Graph-only components should not implement this method.
     */
    virtual std::vector<NodeID> generateNodes(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const
    {
        return {};
    }

    /**
     * @param gen Random number generator (may be backed by a fuzzer or PRNG).
     * @param num Generate at most this number of inputs.
     * @param maxInputSize Each input should be at most this large. This can be
     * interpreted as a loose bound, rather than a strict limit if needed.
     * @returns A list of at most `num` generated graph configurations for this
     * component. This method should only be overridden if there are interesting
     * graph configurations that are not covered by `predefinedGraphs()`.
     *
     * Node-only components should not implement this method.
     */
    virtual std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const
    {
        return {};
    }

    /**
     * @returns A list of predefined inputs for this component that MUST succeed
     * compression for every NodeID and GraphID returned by any of the
     * predefined*() or generate*() methods.
     *
     * @note If not all inputs are valid for all nodes and graphs, you should
     * use the generateInputs() method, which is given the GraphID.
     */
    virtual std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs()
            const = 0;

    /**
     * Generates a list of inputs to test the @p graphID that MUST succeed
     * compression. These inputs will only be used to test this @p graphID,
     * so do not need to succeed in general for all possible nodes & graphs.
     *
     * @param gen Random number generator (may be backed by a fuzzer or PRNG).
     * @param num Generate at most this number of inputs.
     * @param maxInputSize Each input should be at most this large. This can be
     * interpreted as a loose bound, rather than a strict limit if needed.
     * @param compressor The compressor that owns the @p graphID.
     * @param graphID The graph that the inputs will be compressed against.
     * This graph comes either from predefinedGraphs() or generateGraphs(), or
     * is a static graph with the head node from either predefinedNodes() or
     * generatedNodes(). You can use this graphID to query the compressor for
     * its parameters, so you can generate inputs that are compatible with the
     * graph. These inputs will only be used to test this one @p graphID.
     * @returns A list of at most `num` generated inputs for this component that
     * MUST succeed compression for every NodeID and GraphID returned by any of
     * the predefined*() or generate*() methods. If there aren't interesting
     * inputs beyond the predefined inputs, this method should not be
     * overridden.
     */
    virtual std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor& compressor,
            GraphID graphID) const
    {
        return {};
    }

    struct Benchmark {
        std::string name;
        GraphID graph;
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
    };

    /**
     * @returns A list of benchmarks to run.
     *
     * These benchmarks will be run in two modes:
     *
     * - Overall mode: All the benchmarks will be combined into a single summary
     *                 benchmark for the component.
     * - Detailed Mode: Each benchmark will be run individually.
     *
     * It is recommended to make each benchmark approximately the same "weight"
     * so that in summary mode they all contribute to the overall result. E.g.
     * if one scenario processes 100 bytes, and another processes 1MB, the first
     * scenario won't practically contribute to the summary result.
     *
     * These benchmarks should aim to be the minimal set that captures the
     * interesting behavior of the component.
     */
    virtual std::vector<Benchmark> benchmarks(
            Compressor& compressor,
            datagen::DataGen& gen) const
    {
        return {};
    }

    virtual ~OpenZLComponent() = default;

   private:
};

} // namespace openzl::tests
