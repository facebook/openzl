// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

namespace zstrong::thrift {
namespace detail {
constexpr std::string_view kOldStyleVsfErrorMsg =
        "This is probably caused by using an old config which has separate paths for string data and string lengths. "
        "When encoding on format version 14+, such configs are illegal, "
        "as string data and lengths are combined in a single stream of type ZL_Type_string.";
} // namespace detail

/*********************
 * PathTracker::Node *
 *********************/

template <typename StreamSetType>
class PathTracker<StreamSetType>::Node {
   private:
    static constexpr size_t kVecSlots = 1024;

   public:
    explicit Node(
            const PathTracker& pt,
            ThriftNodeId id,
            TType type,
            Stream* stream)
            : id_(id),
              type_(coerceType(type)),
              fallbacks_(pt.fallbacks_),
              stream_(stream)
    {
    }

    ThriftNodeId id() const
    {
        return id_;
    }

    TType type() const
    {
        return type_;
    }

    static bool isInlinedId(ThriftNodeId id)
    {
        bool const isInlined = id == ThriftNodeId::kMapKey
                || id == ThriftNodeId::kMapValue
                || id == ThriftNodeId::kListElem || id == ThriftNodeId::kLength
                || id == ThriftNodeId::kStop;
        assert(!isInlined || isSpecialId(id));
        return isInlined;
    }

    /**
     * @pre !isInlinedId(id) - these special IDs must use their respective
     * member functions to access their child or fallback.
     * @returns The child node for the given @p id and @p type or a fallback
     * node if the child does not exist.
     */
    ZL_FORCE_INLINE_ATTR const Node& childOrFallback(
            ThriftNodeId id,
            TType type) const;

    /// @returns The ThriftNodeId::kLengths child node or fallback
    ZL_FORCE_INLINE_ATTR const Node& lengths() const
    {
        if (lengths_ != nullptr) {
            // Already validated the type by construction in addChild().
            assert((lengths_->checkType(TType::T_U32), true));
            return *lengths_;
        } else {
            return fallbacks_.lengths;
        }
    }

    Stream& getStream() const
    {
        if (stream_ == NULL) {
            throw std::runtime_error("Tried to get NULL stream from Node!");
        }
        return *stream_;
    }

    bool hasStream() const
    {
        return stream_ != nullptr;
    }

    /// @returns The ThriftNodeId::kMapKey child node or fallback
    ZL_FORCE_INLINE_ATTR const Node& mapKey(TType type) const
    {
        return checkedNodeOrFallback(mapKey_, type);
    }

    /// @returns The ThriftNodeId::kMapValue child node or fallback
    ZL_FORCE_INLINE_ATTR const Node& mapValue(TType type) const
    {
        return checkedNodeOrFallback(mapValue_, type);
    }

    /// @returns The ThriftNodeId::kListElem child node or fallback
    ZL_FORCE_INLINE_ATTR const Node& listElem(TType type) const
    {
        return checkedNodeOrFallback(listElem_, type);
    }

    /// @returns The ThriftNodeId::kStop child node
    const Node& stop() const
    {
        return fallbacks_.thriftTypes[static_cast<size_t>(TType::T_STOP)];
    }

    /// Used during graph construction only, can be called on any id
    Node* child(ThriftNodeId id);

    /// Used during graph construction only, can be called on any id
    void addChild(ThriftNodeId id, Node& child);

    void setType(TType type)
    {
        type_ = coerceType(type);
    }

    ZL_FORCE_INLINE_ATTR void checkType(TType t) const;

    void setStream(Stream* stream)
    {
        if constexpr (std::is_same_v<StreamSetType, WriteStreamSet>) {
            // TODO: if type() is added to ReadStream, remove "if constexpr"
            assert(type_ == stream->type());
        }
        stream_ = stream;
    }

   private:
    ZL_FORCE_INLINE_ATTR const Node& checkedNodeOrFallback(
            const Node* node,
            TType type) const
    {
        if (node != nullptr) {
            node->checkType(type);
            return *node;
        } else {
            auto& fallback = fallbacks_.thriftTypes[static_cast<size_t>(type)];
            assert((fallback.checkType(type), true));
            return fallback;
        }
    }

    const ThriftNodeId id_;
    TType type_;
    folly::F14FastMap<ThriftNodeId, Node*> childrenMap_;
    std::vector<Node*> children_;
    const PathTracker::Fallbacks& fallbacks_;
    Stream* stream_;
    Node* lengths_{ nullptr };
    Node* mapKey_{ nullptr };
    Node* mapValue_{ nullptr };
    Node* listElem_{ nullptr };
};

template <typename StreamSetType>
const typename PathTracker<StreamSetType>::Node& PathTracker<
        StreamSetType>::Node::childOrFallback(ThriftNodeId id, TType type) const
{
    assert(!isInlinedId(id));
    auto idx          = static_cast<size_t>(id);
    const Node* child = nullptr;
    if (idx < kVecSlots) {
        if (children_.size() > idx) {
            child = children_[idx];
        }
        if (child == nullptr) {
            child = &fallbacks_.thriftTypes[static_cast<size_t>(type)];
        }
    } else {
        auto it = childrenMap_.find(id);
        if (it == childrenMap_.end()) {
            child = &fallbacks_.thriftTypes[static_cast<size_t>(type)];
        } else {
            child = it->second;
        }
    }
    child->checkType(type);
    return *child;
}

template <typename StreamSetType>
void PathTracker<StreamSetType>::Node::checkType(TType t) const
{
    t = coerceType(t);
    if (t != type()) {
        throw std::runtime_error(
                fmt::format(
                        "Node (id {}) has type {} ({}) but is being accessed with type {} ({})!",
                        id(),
                        thriftTypeToString(type()),
                        type(),
                        thriftTypeToString(t),
                        t));
    }
}

template <typename StreamSetType>
typename PathTracker<StreamSetType>::Node*
PathTracker<StreamSetType>::Node::child(ThriftNodeId id)
{
    if (id == ThriftNodeId::kMapKey) {
        return mapKey_;
    }
    if (id == ThriftNodeId::kMapValue) {
        return mapValue_;
    }
    if (id == ThriftNodeId::kListElem) {
        return listElem_;
    }
    if (id == ThriftNodeId::kLength) {
        return lengths_;
    } else if (id == ThriftNodeId::kStop) {
        throw std::runtime_error{
            "kStop should never be used in a Thrift config path"
        };
    }
    assert(!isInlinedId(id));
    auto idx = static_cast<size_t>(id);
    if (idx < kVecSlots) {
        if (children_.size() <= idx) {
            return nullptr;
        }
        return children_[idx];
    } else {
        auto it = childrenMap_.find(id);
        if (it == childrenMap_.end()) {
            return nullptr;
        }
        return it->second;
    }
}

template <typename StreamSetType>
void PathTracker<StreamSetType>::Node::addChild(ThriftNodeId id, Node& child)
{
    if (id == ThriftNodeId::kMapKey) {
        mapKey_ = &child;
    } else if (id == ThriftNodeId::kMapValue) {
        mapValue_ = &child;
    } else if (id == ThriftNodeId::kListElem) {
        listElem_ = &child;
    } else if (id == ThriftNodeId::kLength) {
        lengths_ = &child;
        // Enforce that the type of the lengths field is always T_U32.
        // Otherwise an invalid config could set the type of kLengths
        // to something else. Setting this here lets us assume that the
        // type of lengths_ is always T_U32, so we don't need to check
        // it during (un)parsing.
        if (lengths_->type_ != TType::T_U32) {
            assert(lengths_->type() == TType::T_VOID);
            lengths_->setType(TType::T_U32);
        }
    } else if (id == ThriftNodeId::kStop) {
        throw std::runtime_error{
            "kStop should never be used in a Thrift config path"
        };
    } else {
        assert(!isInlinedId(id));
        auto idx = static_cast<size_t>(id);
        if (idx < kVecSlots) {
            if (children_.size() <= idx) {
                children_.resize(idx + 1);
            }
            children_[idx] = &child;
        } else {
            childrenMap_.emplace(id, &child);
        }
    }
}

/**************************
 * PathTracker::Iterator  *
 **************************/

template <typename StreamSetType>
typename PathTracker<StreamSetType>::Iterator
PathTracker<StreamSetType>::Iterator::child(ThriftNodeId id, TType type) const
{
    assert(!node_.isInlinedId(id));
    const auto& c = node_.childOrFallback(id, type);
    return Iterator(this, c, id, type, depth_ + 1);
}

template <typename StreamSetType>
typename PathTracker<StreamSetType>::Iterator
PathTracker<StreamSetType>::Iterator::lengths() const
{
    return Iterator(
            this,
            node_.lengths(),
            ThriftNodeId::kLength,
            TType::T_U32,
            depth_ + 1);
}

template <typename StreamSetType>
typename PathTracker<StreamSetType>::Iterator
PathTracker<StreamSetType>::Iterator::mapKey(TType type) const
{
    return Iterator(
            this, node_.mapKey(type), ThriftNodeId::kMapKey, type, depth_ + 1);
}

template <typename StreamSetType>
typename PathTracker<StreamSetType>::Iterator
PathTracker<StreamSetType>::Iterator::mapValue(TType type) const
{
    return Iterator(
            this,
            node_.mapValue(type),
            ThriftNodeId::kMapValue,
            type,
            depth_ + 1);
}

template <typename StreamSetType>
typename PathTracker<StreamSetType>::Iterator
PathTracker<StreamSetType>::Iterator::listElem(TType type) const
{
    return Iterator(
            this,
            node_.listElem(type),
            ThriftNodeId::kListElem,
            type,
            depth_ + 1);
}

template <typename StreamSetType>
typename PathTracker<StreamSetType>::Iterator
PathTracker<StreamSetType>::Iterator::stop() const
{
    return Iterator(
            this, node_.stop(), ThriftNodeId::kStop, TType::T_STOP, depth_ + 1);
}

template <typename StreamSetType>
std::vector<ThriftNodeId> PathTracker<StreamSetType>::Iterator::path() const
{
    if (parent_ == nullptr) {
        return {};
    }
    auto path = parent_->path();
    path.push_back(id());
    return path;
}

template <typename StreamSetType>
std::string PathTracker<StreamSetType>::Iterator::pathStr() const
{
    return pathToStr(path());
}

/**************************
 * PathTracker::Fallbacks *
 **************************/

template <typename StreamSetType>
struct PathTracker<StreamSetType>::Fallbacks {
    using StreamSet = PathTracker<StreamSetType>::StreamSet;

    static constexpr size_t kArraySize =
            static_cast<size_t>(TType::T_FLOAT) + 1;

    static std::array<Node, kArraySize> makeThriftTypes(
            const PathTracker& pt,
            StreamSet& ss)
    {
        static const std::map<TType, SingletonId> stream_map = {
            { TType::T_BOOL, SingletonId::kBool },
            { TType::T_BYTE, SingletonId::kInt8 },
            { TType::T_I16, SingletonId::kInt16 },
            { TType::T_I32, SingletonId::kInt32 },
            { TType::T_I64, SingletonId::kInt64 },
            { TType::T_FLOAT, SingletonId::kFloat32 },
            { TType::T_DOUBLE, SingletonId::kFloat64 },
            { TType::T_STRING, SingletonId::kBinary },
        };
        const auto id = static_cast<ThriftNodeId>(0);

        auto makeNode = [&](size_t i) {
            const auto type  = static_cast<TType>(i);
            auto streamIdIt  = stream_map.find(type);
            auto streamIdOpt = streamIdIt != stream_map.end()
                    ? std::optional<SingletonId>(streamIdIt->second)
                    : std::nullopt;
            auto stream = streamIdOpt ? &ss.getStream(*streamIdOpt) : nullptr;
            return Node(pt, id, type, stream);
        };

        auto makeNodes = [&]<size_t... I>(auto& make, std::index_sequence<I...>)
                -> std::array<Node, kArraySize> { return { { make(I)... } }; };

        return makeNodes(makeNode, std::make_index_sequence<kArraySize>{});
    }

    Fallbacks(const PathTracker& pt, const BaseConfig& config, StreamSet& ss)
            : thriftTypes(makeThriftTypes(pt, ss)),
              lengths(pt,
                      ThriftNodeId::kLength,
                      TType::T_U32,
                      &ss.getStream(SingletonId::kLengths))
    {
        (void)config;
    }

    std::array<Node, kArraySize> thriftTypes;
    Node lengths;
};

/***********************
 * PathTracker Methods *
 ***********************/

template <typename StreamSetType>
TType PathTracker<StreamSetType>::coerceType(TType type)
{
    if (type == TType::T_SET) {
        // lists and sets are equivalent, reduce one into the other.
        type = TType::T_LIST;
    }
    return type;
}

template <typename StreamSetType>
void PathTracker<StreamSetType>::addStringLengthsNode(
        Node& stringDataNode,
        const LogicalId id)
{
    if (stringDataNode.child(ThriftNodeId::kLength) != nullptr) {
        throw std::runtime_error{ fmt::format(
                "Attempting to add two length nodes to the same string node! {}",
                detail::kOldStyleVsfErrorMsg) };
    }
    auto stream = &ss_.getStringLengthStream(id);
    auto node   = std::make_unique<Node>(
            *this, ThriftNodeId::kLength, TType::T_U32, stream);
    stringDataNode.addChild(ThriftNodeId::kLength, *node);
    nodes_.push_back(std::move(node));
}

template <typename StreamSetType>
void PathTracker<StreamSetType>::fillGraph(const unsigned int formatVersion)
{
    for (const auto& [path, info] : config_.pathMap()) {
        Node* cur = &root_;
        for (const auto id : path) {
            Node* next = cur->child(id);
            // TODO: do better job guessing / inferring type.
            TType type = TType::T_VOID;
            if (id == ThriftNodeId::kMapKey || id == ThriftNodeId::kMapValue) {
                type = TType::T_MAP;
            } else if (id == ThriftNodeId::kListElem) {
                // could also be a set but we treat them as equivalent
                type = TType::T_LIST;
            } else if (!isSpecialId(id)) {
                type = TType::T_STRUCT;
            }
            if (type != TType::T_VOID) {
                if (cur->type() == TType::T_VOID) {
                    cur->setType(type);
                } else {
                    cur->checkType(type);
                }
            }
            if (next == nullptr) {
                auto node = std::make_unique<Node>(
                        *this, id, TType::T_VOID, nullptr);
                next = node.get();
                cur->addChild(id, *next);
                nodes_.push_back(std::move(node));
            }
            cur = next;
        }

        if (formatVersion >= kMinFormatVersionStringVSF) {
            if (cur->hasStream()) {
                throw std::runtime_error{ fmt::format(
                        "Attempting to set two different streams on the same node! {}",
                        detail::kOldStyleVsfErrorMsg) };
            }
            if (info.type == TType::T_STRING) {
                addStringLengthsNode(*cur, info.id);
            }
        }

        auto stream = &ss_.getStream(info.id);
        if (cur->type() != TType::T_VOID) {
            cur->checkType(info.type);
        }
        cur->setType(info.type);
        cur->setStream(stream);
    }
}

} // namespace zstrong::thrift
