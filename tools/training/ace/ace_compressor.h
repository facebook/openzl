// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <chrono>

#include "openzl/cpp/poly/Optional.hpp"
#include "openzl/cpp/poly/StringView.hpp"
#include "openzl/openzl.hpp"
#include "tools/training/ace/ace_utils.h"
#include "tools/training/utils/utils.h"

namespace openzl {
namespace training {

struct ACENode {
    std::string name;
    poly::optional<NodeParameters> params;
    Type inputType;
    std::vector<Type> outputTypes;
    /// Minimum format version this node requires; 0 means unconstrained.
    unsigned minFormatVersion{ 0 };
};

struct ACEGraph {
    std::string name;
    poly::optional<GraphParameters> params;
    TypeMask inputTypeMask;
};

class ACECompressor;

/// A compressor built by ACE that is a single node followed by ACECompressor
/// successors.
struct ACENodeCompressor {
    ACENodeCompressor(
            ACENode node_,
            std::vector<std::unique_ptr<ACECompressor>> successors_)
            : node(std::move(node_)), successors(std::move(successors_))
    {
        if (node.outputTypes.size() != successors.size()) {
            throw Exception(
                    "Number of successors must match number of output types");
        }
    }

    ACENodeCompressor(const ACENodeCompressor& other) noexcept;
    ACENodeCompressor(ACENodeCompressor&& other) = default;

    ACENodeCompressor& operator=(const ACENodeCompressor& other) noexcept;
    ACENodeCompressor& operator=(ACENodeCompressor&& other) = default;

    ~ACENodeCompressor() = default;

    ACENode node;
    std::vector<std::unique_ptr<ACECompressor>> successors;

    uint64_t hash() const;
    GraphID build(Compressor& compressor) const;
};

/// A compressor built by ACE that is a single graph.
struct ACEGraphCompressor {
    explicit ACEGraphCompressor(ACEGraph graph_) : graph(std::move(graph_)) {}

    ACEGraph graph;

    uint64_t hash() const;
    GraphID build(Compressor& compressor) const;
};

struct ACECompressionResult {
    size_t originalSize{ 0 };
    size_t compressedSize{ 0 };
    std::chrono::nanoseconds compressionTime{ 0 };
    std::chrono::nanoseconds decompressionTime{ 0 };

    float compressionRatio() const
    {
        return (float)originalSize / compressedSize;
    }

    float compressionSpeedMBps() const
    {
        return ((float)originalSize * 1000.0) / compressionTime.count();
    }

    float decompressionSpeedMBps() const
    {
        return ((float)originalSize * 1000.0) / decompressionTime.count();
    }

    std::vector<float> asFloatVector() const
    {
        return {
            compressionRatio(),
            compressionSpeedMBps(),
            decompressionSpeedMBps(),
        };
    }

    bool operator<(const ACECompressionResult& other) const
    {
        return std::tie(compressedSize, compressionTime, decompressionTime)
                < std::tie(
                        other.compressedSize,
                        other.compressionTime,
                        other.decompressionTime);
    }

    ACECompressionResult& operator+=(const ACECompressionResult& other)
    {
        originalSize += other.originalSize;
        compressedSize += other.compressedSize;
        compressionTime += other.compressionTime;
        decompressionTime += other.decompressionTime;
        return *this;
    }
};

poly::optional<ACECompressionResult> benchmark(
        const Compressor& compressor,
        poly::span<const Input> inputs);

poly::optional<ACECompressionResult> benchmark(
        const Compressor& compressor,
        poly::span<const poly::span<const Input>> inputs);

/// A compressor built by ACE that can either be a ACENodeCompressor or
/// ACEGraphCompressor.
class ACECompressor {
   public:
    /* implicit */ ACECompressor(ACENodeCompressor node)
            : node_(std::move(node)), hash_(computeHash())
    {
    }
    /* implicit */ ACECompressor(ACEGraphCompressor graph)
            : graph_(std::move(graph)), hash_(computeHash())
    {
    }

    explicit ACECompressor(
            ACENode node,
            std::vector<ACECompressor>&& successors);

    explicit ACECompressor(ACEGraph graph)
            : ACECompressor(ACEGraphCompressor(std::move(graph)))
    {
    }

    explicit ACECompressor(poly::string_view serialized);

    ACECompressor(const ACECompressor&) = default;
    ACECompressor(ACECompressor&&)      = default;

    ACECompressor& operator=(const ACECompressor&) = default;
    ACECompressor& operator=(ACECompressor&&)      = default;

    ~ACECompressor() = default;

    uint64_t hash() const
    {
        return hash_;
    }

    bool isNode() const
    {
        assert(node_.has_value() ^ graph_.has_value());
        return node_.has_value();
    }

    bool isGraph() const
    {
        return !isNode();
    }

    const ACENodeCompressor& asNode() const
    {
        assert(isNode());
        return *node_;
    }

    const ACEGraphCompressor& asGraph() const
    {
        assert(isGraph());
        return *graph_;
    }

    TypeMask inputTypeMask() const
    {
        return isNode() ? typeToMaskWithConversion(asNode().node.inputType)
                        : asGraph().graph.inputTypeMask;
    }

    bool acceptsInputType(Type type) const
    {
        return (TypeMask(int(type)) & inputTypeMask()) != TypeMask::None;
    }

    GraphID build(Compressor& compressor) const
    {
        return isNode() ? asNode().build(compressor)
                        : asGraph().build(compressor);
    }

    std::string prettyPrint() const;
    std::string serialize() const;

    /// Calls @p v on each component of the compressor.
    ///
    /// @param visit A visitor that takes a reference to the ACECompressor
    /// component and the type of data being passed to it.
    template <typename VisitorFn>
    void forEachComponent(Type inputType, VisitorFn&& visit) const
    {
        visit(*this, inputType);
        if (isNode()) {
            const auto& node = asNode();
            for (size_t i = 0; i < node.successors.size(); ++i) {
                node.successors[i]->forEachComponent(
                        node.node.outputTypes[i], visit);
            }
        }
    }

    /// Calls @p replace on every component in the compressor. If it returns a
    /// value that component is replaced and the recursion stops on that branch.
    /// @p replace takes the compressor, the type that the compressor must
    /// accept, and the depth in the tree.
    template <typename ReplaceFn>
    ACECompressor replace(Type inputType, ReplaceFn&& replace, size_t depth = 0)
            const
    {
        auto replacement = replace(*this, inputType, depth);
        if (replacement.has_value()) {
            return std::move(*replacement);
        }
        if (isNode()) {
            const auto& node = asNode();
            std::vector<std::unique_ptr<ACECompressor>> successors;
            successors.reserve(node.successors.size());
            for (size_t i = 0; i < node.successors.size(); ++i) {
                auto s = node.successors[i]->replace(
                        node.node.outputTypes[i], replace, depth + 1);
                successors.push_back(
                        std::make_unique<ACECompressor>(std::move(s)));
            }
            return ACENodeCompressor(node.node, std::move(successors));
        } else {
            return *this;
        }
    }

    size_t numComponents() const
    {
        size_t n = 0;
        // Type doesn't matter here
        forEachComponent(Type::Serial, [&n](const auto&, auto) { ++n; });
        return n;
    }

    bool operator==(const ACECompressor& other) const
    {
        // Use the hash for equality for speed & simplicity.
        // Probability of collision is low and the harm of a collision is low,
        // so this should be fine.
        return hash() == other.hash();
    }

    bool operator!=(const ACECompressor& other) const
    {
        return !(*this == other);
    }

    /// @returns The benchmark result of the compressor on the @p inputs or
    /// poly::nullopt if the compressor fails to compress (including when it
    /// requires a newer format version than @p formatVersion).
    poly::optional<ACECompressionResult> benchmark(
            poly::span<const Input> inputs,
            uint32_t formatVersion) const;

   private:
    uint64_t computeHash() const;

    poly::optional<ACENodeCompressor> node_{};
    poly::optional<ACEGraphCompressor> graph_{};
    uint64_t hash_;
};
} // namespace training
} // namespace openzl

namespace std {
template <>
struct hash<openzl::training::ACECompressor> {
    size_t operator()(const openzl::training::ACECompressor& compressor) const
    {
        return compressor.hash();
    }
};
} // namespace std
