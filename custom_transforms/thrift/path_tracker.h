// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "custom_transforms/thrift/constants.h"     // @manual
#include "custom_transforms/thrift/parse_config.h"  // @manual
#include "custom_transforms/thrift/split_helpers.h" // @manual
#include "custom_transforms/thrift/thrift_types.h"  // @manual

#include <folly/Conv.h>
#include <folly/Portability.h>
#include <folly/container/F14Map.h>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace zstrong::thrift {

/**
 * The PathTracker provides the tools to walk a materialized parse config as
 * the parser walks the thrift object, so that the parser can look up the
 * stream associated with each particular field in an efficient way.
 */
template <typename StreamSetType>
class PathTracker {
   public:
    // Either WriteStreamSet or ReadStreamSet
    using StreamSet = StreamSetType;
    using Stream    = typename StreamSet::StreamType;

   private:
    // Forward declarations of internal types.

    struct Fallbacks;

    class Node;

   public:
    /**
     * The Iterator represents the current position in the current level of a
     * thrift struct tree traversal. It expects to be used by a recursive
     * descent parser, which must keep the iterator at each level alive and
     * in-place on the stack until it's done with its children--because the
     * iterator keeps a naked pointer to its parent.
     */
    class Iterator {
       public:
        explicit Iterator(
                const Iterator* parent,
                const Node& node,
                ThriftNodeId id,
                TType type,
                size_t depth)
                : parent_(parent),
                  node_(node),
                  id_(id),
                  type_(type),
                  depth_(depth)
        {
            if (depth_ > kMaxThriftDepth) {
                throw ThriftUnhandledInputError(
                        "Exceeded maximum thrift recursion depth!");
            }
        }

        ThriftNodeId id() const
        {
            return id_;
        }

        TType type() const
        {
            return type_;
        }

        Stream& stream() const
        {
            return node_.getStream();
        }

        /**
         * @pre The ID is not kLengths, kMapKey, kMapValue, kListElem, or kStop.
         *      These IDs must use their respective specialized methods below.
         * @returns The child iterator for the given id and type.
         */
        ZL_FORCE_INLINE_ATTR Iterator child(ThriftNodeId id, TType type) const;

        /// Specialized child iterator for kLengths
        ZL_FORCE_INLINE_ATTR Iterator lengths() const;
        /// Specialized child iterator for kMapKey
        ZL_FORCE_INLINE_ATTR Iterator mapKey(TType type) const;
        /// Specialized child iterator for kMapValue
        ZL_FORCE_INLINE_ATTR Iterator mapValue(TType type) const;
        /// Specialized child iterator for kListelem
        ZL_FORCE_INLINE_ATTR Iterator listElem(TType type) const;
        /// Specialized child iterator for kStop
        Iterator stop() const;

        std::vector<ThriftNodeId> path() const;

        std::string pathStr() const;

       private:
        const Iterator* parent_;
        const Node& node_;
        const ThriftNodeId id_;
        const TType type_;
        const size_t depth_;
    };

   public:
    PathTracker(
            const BaseConfig& config,
            StreamSet& ss,
            unsigned int formatVersion)
            : config_(config),
              ss_(ss),
              fallbacks_(*this, config, ss),
              root_(*this, ThriftNodeId::kRoot, config.getRootType(), nullptr),
              rootIt_(nullptr, root_, root_.id(), root_.type(), 0)
    {
        fillGraph(formatVersion);
    }

    // No moves allowed! Inner children keep raw refs to members.
    PathTracker(PathTracker&&)            = delete;
    PathTracker& operator=(PathTracker&&) = delete;

    const Iterator& root() const
    {
        return rootIt_;
    }

   private:
    // Maximum recursion depth. Changing this value requires a ZStrong format
    // version bump.
    static_assert(
            std::is_same_v<StreamSet, WriteStreamSet>
            || std::is_same_v<StreamSet, ReadStreamSet>);
    static constexpr size_t kMaxThriftDepth =
            std::is_same_v<StreamSet, WriteStreamSet> ? 128 : 256;

    static TType coerceType(TType type);

    void fillGraph(unsigned int formatVersion);

   private:
    void addStringLengthsNode(Node& stringDataNode, LogicalId id);

    const BaseConfig& config_;
    StreamSet& ss_;

    // Nodes for each thrift type, for when we don't have a node corresponding
    // to a path.
    const Fallbacks fallbacks_;

    // Owning refs to all the dynamic nodes. This vector is otherwise unused.
    std::vector<std::unique_ptr<Node>> nodes_;

    Node root_;
    Iterator rootIt_;
};

} // namespace zstrong::thrift

#include "custom_transforms/thrift/path_tracker-inl.h" // @manual
