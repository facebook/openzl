// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/parse_config.h" // @manual
#include "custom_transforms/thrift/constants.h"    // @manual

#include <folly/Conv.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/dynamic.h>
#include <folly/json.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include "thrift/lib/cpp2/protocol/Serializer.h"

namespace zstrong::thrift {

template <typename U, typename T>
inline std::vector<U> convertVec(const std::vector<T>& vec)
{
    std::vector<U> res;
    res.reserve(vec.size());
    std::transform(vec.begin(), vec.end(), std::back_inserter(res), [](T t) {
        return folly::to<U>(t);
    });
    return res;
}

PathInfo::PathInfo(const cpp2::PathInfo& info)
        : id(folly::to<LogicalId>(folly::copy(info.logicalId().value()))),
          type(folly::to<TType>(folly::copy(info.type().value())))
{
}

LogicalCluster::LogicalCluster(const cpp2::LogicalCluster& rawCluster)
        : idList(convertVec<LogicalId>(rawCluster.idList().value())),
          successor(rawCluster.successor().value_or(-1))
{
}

void BaseConfig::setBaseConfig(cpp2::BaseConfig config)
{
    const auto rawPathMap = std::move(*config.pathMap());
    for (const auto& [rawPath, rawInfo] : rawPathMap) {
        logicalStreamIds_.insert(
                LogicalId(folly::copy(rawInfo.logicalId().value())));
        pathMap_.emplace(convertVec<ThriftNodeId>(rawPath), PathInfo(rawInfo));
    }
    rootType_ = folly::to<TType>(folly::copy(config.rootType().value()));
    const auto rawClusters = config.clusters().value_or({});
    clusters_.reserve(rawClusters.size());
    for (const cpp2::LogicalCluster& rawCluster : rawClusters) {
        clusters_.emplace_back(rawCluster);
    }
}

BaseConfig::BaseConfig(
        std::map<ThriftPath, PathInfo> pathMap,
        TType rootType,
        std::vector<LogicalCluster> clusters)
        : pathMap_(std::move(pathMap)),
          rootType_(rootType),
          clusters_(std::move(clusters))
{
    for (const auto& [_, info] : pathMap_) {
        logicalStreamIds_.insert(info.id);
    }
    validate();
}

cpp2::BaseConfig BaseConfig::toThriftObj() const
{
    cpp2::BaseConfig baseConfig;
    std::map<std::vector<int>, cpp2::PathInfo> rawPathMap;
    for (const auto& [path, info] : pathMap_) {
        cpp2::PathInfo rawInfo;
        rawInfo.logicalId() = folly::to<int16_t>(info.id);
        rawInfo.type()      = folly::to<int8_t>(info.type);
        rawPathMap.emplace(convertVec<int>(path), rawInfo);
    }
    baseConfig.pathMap()  = std::move(rawPathMap);
    baseConfig.rootType() = folly::to<int8_t>(rootType_);

    std::vector<cpp2::LogicalCluster> rawClusters;
    rawClusters.reserve(clusters_.size());
    for (const auto& cluster : clusters_) {
        cpp2::LogicalCluster rawCluster;
        rawCluster.idList()    = convertVec<int16_t>(cluster.idList);
        rawCluster.successor() = cluster.successor;
        rawClusters.push_back(rawCluster);
    }
    baseConfig.clusters() = std::move(rawClusters);

    return baseConfig;
}

void BaseConfig::validate() const
{
    // Check that all paths for a single LogicalStream share the same type
    folly::F14FastMap<LogicalId, TType> types;
    for (const auto& [path, info] : pathMap_) {
        auto const it = types.find(info.id);
        if (it == types.end()) {
            types.emplace(info.id, info.type);
        } else {
            // At this time we don't support *any* type mixing. We might want to
            // relax that in the future, e.g. for float32 and int32.
            if (info.type != it->second) {
                throw std::runtime_error{ fmt::format(
                        "Types for logical stream {} don't match! Expected {}, got {}",
                        info.id,
                        it->second,
                        info.type) };
            }
        }
    }

    // Check that clusters satisfy the following properties:
    // (1) Non-empty
    // (2) Consist of valid (existing) LogicalIds
    // (3) Type-homogenous
    // Note: properties (1) and (2) are implicitly checked by .at() calls.
    for (const LogicalCluster& cluster : clusters_) {
        if (cluster.idList.empty()) {
            throw std::runtime_error{ "Empty cluster" };
        }
        const TType type = types.at(cluster.idList.at(0));
        for (auto const& streamId : cluster.idList) {
            if (types.at(streamId) != type) {
                throw std::runtime_error{ fmt::format(
                        "Cluster is not type-homogenous!") };
            }
        }
    }
}

std::vector<LogicalId> BaseConfig::getUnclusteredStreams() const
{
    folly::F14FastSet<LogicalId> clusteredStreams;
    clusteredStreams.reserve(logicalStreamIds_.size());
    for (const auto& cluster : clusters_) {
        clusteredStreams.insert(cluster.idList.begin(), cluster.idList.end());
    }

    std::vector<LogicalId> unclusteredStreams;
    unclusteredStreams.reserve(logicalStreamIds_.size());
    std::copy_if(
            logicalStreamIds_.begin(),
            logicalStreamIds_.end(),
            std::back_inserter(unclusteredStreams),
            [&](LogicalId id) { return !clusteredStreams.contains(id); });

    std::sort(unclusteredStreams.begin(), unclusteredStreams.end());

    return unclusteredStreams;
}

const LogicalCluster& BaseConfig::getCluster(size_t clusterIdx) const
{
    try {
        return clusters_.at(clusterIdx);
    } catch (const std::out_of_range&) {
        throw std::out_of_range{ fmt::format(
                "Invalid cluster index: {}. There are {} clusters.",
                clusterIdx,
                clusters_.size()) };
    }
}

LogicalCluster& BaseConfig::getCluster(size_t clusterIdx)
{
    using ClusterType = decltype(clusters_)::value_type;
    static_assert(!std::is_const_v<ClusterType>);
    return const_cast<LogicalCluster&>(
            const_cast<const BaseConfig*>(this)->getCluster(clusterIdx));
}

std::vector<ThriftPath> BaseConfig::getClusterPaths(size_t clusterIdx) const
{
    // TODO(T193417431) This should really be a member variable. It's wasteful
    // to recompute every time, and there are places outside this function where
    // it would be useful to have the inverse map.
    folly::F14FastMap<LogicalId, const ThriftPath&> inversePathMap;
    for (const auto& [path, info] : pathMap_) {
        inversePathMap.emplace(info.id, path);
    }

    const LogicalCluster& cluster = getCluster(clusterIdx);
    std::vector<ThriftPath> paths;
    paths.reserve(cluster.idList.size());
    for (LogicalId id : cluster.idList) {
        try {
            paths.push_back(inversePathMap.at(id));
        } catch (std::out_of_range&) {
            throw std::out_of_range{ fmt::format(
                    "Couldn't find path for logical id {}", id) };
        }
    }
    return paths;
}

TType BaseConfig::getClusterType(size_t clusterIdx) const
{
    const std::vector<ThriftPath> paths = getClusterPaths(clusterIdx);
    if (paths.empty()) {
        return TType::T_VOID;
    }
    const TType first = pathMap_.at(paths.front()).type;
    const auto illegal =
            std::find_if(paths.begin() + 1, paths.end(), [&](auto path) {
                return pathMap_.at(path).type != first;
            });
    if (illegal != paths.end()) {
        throw std::runtime_error{ fmt::format(
                "Cluster contains streams of multiple TTypes: {} and {}",
                first,
                pathMap_.at(*illegal).type) };
    }
    return first;
}

std::map<ZL_Type, int> EncoderConfig::getTypeSuccessorMap() const
{
    return typeSuccessorMap_;
}

void EncoderConfig::initTypeSuccessorMap()
{
    // Send to zstd
    typeSuccessorMap_[ZL_Type_serial] = kNonNumericDefaultSuccessor;
    typeSuccessorMap_[ZL_Type_string] = kNonNumericDefaultSuccessor;
    typeSuccessorMap_[ZL_Type_struct] = kNonNumericDefaultSuccessor;
    // Send to numeric ml
    typeSuccessorMap_[ZL_Type_numeric] = kNumericDefaultSuccessor;
}

EncoderConfig::EncoderConfig(std::string_view str)
{
    // SimpleJSON support is for experimental use only. Do not use it in prod.
    // See tests/test_serialization.cpp for an example JSON blob.
    bool const isJSON = str.size() > 0 && str[0] == '{';

    auto const config = isJSON
            ? apache::thrift::SimpleJSONSerializer::deserialize<
                      cpp2::EncoderConfig>(str)
            : apache::thrift::CompactSerializer::deserialize<
                      cpp2::EncoderConfig>(str);

    setBaseConfig(config.baseConfig().value());
    for (const auto& [rawId, rawSuccessor] : config.successorMap().value()) {
        successors_.emplace(folly::to<LogicalId>(rawId), rawSuccessor);
    }

    // Initialize default map for older configs which do not contain this
    // information, or configs which are using type defaults.
    initTypeSuccessorMap();
    if (!config.typeSuccessorMap().value().empty()) {
        for (const auto& [rawType, rawSuccessor] :
             config.typeSuccessorMap().value()) {
            typeSuccessorMap_[folly::to<ZL_Type>(rawType)] = rawSuccessor;
        }
    }
    parseTulipV2_ = config.parseTulipV2().value_or(false);
    minFormatVersion_ =
            config.minFormatVersion().value_or(kMinFormatVersionEncode);

    validate();
}

// Note: every constructor must call validate() at the end.
void EncoderConfig::validate() const
{
    BaseConfig::validate();

    // Validate special node ids
    for (const auto& [path, _] : pathMap_) {
        for (const auto id : path) {
            if (isSpecialId(id)
                && !validateThriftNodeId(id, minFormatVersion_)) {
                throw std::runtime_error{ fmt::format(
                        "Special ThriftNodeId {} is not supported by format version {}",
                        id,
                        minFormatVersion_) };
            }
        }
    }

    // Disable TulipV2 for older format versions
    if (parseTulipV2_ == true
        && minFormatVersion_ < kMinFormatVersionEncodeTulipV2) {
        throw std::runtime_error{ fmt::format(
                "Cannot encode in TulipV2 mode for format version {}. You may have forgotten to set the correct format version when building this config.",
                minFormatVersion_) };
    }

    // Disable clusters for older format versions
    if (!clusters_.empty()
        && minFormatVersion_ < kMinFormatVersionEncodeClusters) {
        throw std::runtime_error{ fmt::format(
                "Cannot encode with clusters for format version {}. You may have forgotten to set the correct format version when building this config.",
                minFormatVersion_) };
    }

    // There are three ways to split out the lengths of a string:
    // (1) Data-only
    // (2) Lengths-only
    // (3) Data-and-lengths (VSF style)
    // Only (1) and (3) are supported. Due to a bug in the original
    // implementation, configs which use (2) may cause encoding to fail.
    //
    // It turns out that the cost to fix this without breaking
    // backwards-compatablity is high. Since we don't have a compelling
    // use-case, it's easier to just ban it. It's impossible to ban this for
    // strings only, so (2) is banned for lists and maps as well.
    //
    // If we find a use-case for (2), we always have the option to roll out a
    // change on the decoder side to add support for that usage.
    std::set<ThriftPathView> dataPrefixes{};
    for (const auto& [path, _] : pathMap_) {
        if (path.empty()) {
            throw std::runtime_error{ "Config has an empty path!" };
        }
        if (path.back() != ThriftNodeId::kLength) {
            // For T_STRING, the full data path is a prefix of the lengths path
            dataPrefixes.insert(ThriftPathView(path));

            // For T_MAP and T_LIST, the data path ends with an extra kListElem
            // or similar, so we need to chop that off to get a common prefix
            // with the lengths path
            dataPrefixes.insert(ThriftPathView(path.data(), path.size() - 1));
        }
    }
    for (const auto& [path, _] : pathMap_) {
        assert(!path.empty());
        ThriftPathView prefix(path.data(), path.size() - 1);
        if (path.back() == ThriftNodeId::kLength
            && !dataPrefixes.contains(prefix)) {
            throw std::runtime_error{ fmt::format(
                    "Config splits lengths but not data at path {}. This usage is not supported.",
                    pathToStr(path)) };
        }
    }
}

cpp2::EncoderConfig EncoderConfig::toThriftObj() const
{
    cpp2::EncoderConfig config;
    config.baseConfig() = BaseConfig::toThriftObj();
    std::map<int, int> rawSuccessorMap;
    for (const auto& [id, successor] : successors_) {
        rawSuccessorMap.emplace(folly::to<int>(id), successor);
    }
    std::map<int, int> rawTypeSuccessorMap;
    for (const auto& [type, successor] : typeSuccessorMap_) {
        if ((type == ZL_Type_numeric && successor == kNumericDefaultSuccessor)
            || (type != ZL_Type_numeric
                && successor == kNonNumericDefaultSuccessor)) {
            // Don't serialize if types use the default successor
            continue;
        }
        rawTypeSuccessorMap.emplace(folly::to<int>(type), successor);
    }
    config.typeSuccessorMap() = std::move(rawTypeSuccessorMap);
    config.successorMap()     = std::move(rawSuccessorMap);
    config.parseTulipV2()     = parseTulipV2_;
    config.minFormatVersion() = minFormatVersion_;
    return config;
}

std::string EncoderConfig::serialize() const
{
    return apache::thrift::CompactSerializer::serialize<std::string>(
            toThriftObj());
}

DecoderConfig::DecoderConfig(folly::ByteRange bytes)
{
    auto const config =
            apache::thrift::CompactSerializer::deserialize<cpp2::DecoderConfig>(
                    bytes);
    setBaseConfig(config.baseConfig().value());
    originalSize_ =
            folly::to<size_t>(folly::copy(config.originalSize().value()));
    unparseMessageHeaders_ = config.unparseMessageHeaders().value_or(false);
    validate();
}

DecoderConfig::DecoderConfig(
        const BaseConfig& baseConfig,
        size_t originalSize,
        bool unparseMessageHeaders)
        : BaseConfig(baseConfig),
          originalSize_(originalSize),
          unparseMessageHeaders_(unparseMessageHeaders)
{
    validate();
}

cpp2::DecoderConfig DecoderConfig::toThriftObj() const
{
    cpp2::DecoderConfig config;
    config.baseConfig()            = BaseConfig::toThriftObj();
    config.originalSize()          = originalSize_;
    config.unparseMessageHeaders() = unparseMessageHeaders_;
    return config;
}

std::string DecoderConfig::serialize() const
{
    return apache::thrift::CompactSerializer::serialize<std::string>(
            toThriftObj());
}

std::optional<LogicalId> BaseConfig::getLogicalStreamAt(
        const ThriftPath& path) const
{
    const auto res = pathMap_.find(path);
    if (res != pathMap_.end()) {
        return std::optional<LogicalId>{ res->second.id };
    } else {
        return std::nullopt;
    }
}

const folly::F14FastSet<LogicalId>& BaseConfig::getLogicalIds() const
{
    return logicalStreamIds_;
}

TType BaseConfig::getRootType() const
{
    return rootType_;
}

bool EncoderConfig::getShouldParseTulipV2() const
{
    return parseTulipV2_;
}

int EncoderConfig::getMinFormatVersion() const
{
    return minFormatVersion_;
}

std::optional<int> EncoderConfig::getSuccessorForLogicalStream(
        LogicalId id) const
{
    auto it = successors_.find(id);
    if (it != successors_.end()) {
        return it->second;
    }
    return std::nullopt;
}

size_t DecoderConfig::getOriginalSize() const
{
    return originalSize_;
}

bool DecoderConfig::getShouldUnparseMessageHeaders() const
{
    return unparseMessageHeaders_;
}

void EncoderConfigBuilder::addPath(const ThriftPath& path, TType type)
{
    const auto id = LogicalId(getLogicalIds().size());
    const PathInfo info(id, type);
    {
        const auto [_, inserted] = pathMap_.emplace(path, info);
        if (!inserted) {
            throw std::runtime_error{ fmt::format(
                    "Path {} already exists in this config!",
                    pathToStr(path)) };
        }
    }
    {
        const auto [_, inserted] = logicalStreamIds_.insert(id);
        if (!inserted) {
            throw std::runtime_error{ fmt::format(
                    "Logical id {} already exists in this config!",
                    folly::to<size_t>(id)) };
        }
    }
}

void EncoderConfigBuilder::setSuccessorForType(ZL_Type type, int successor)
{
    typeSuccessorMap_[type] = successor;
}

void EncoderConfigBuilder::setSuccessorForPath(
        const ThriftPath& path,
        int successor)
{
    successors_[pathToId(path)] = successor;
}

size_t EncoderConfigBuilder::addEmptyCluster(int successor)
{
    const LogicalCluster cluster{ {}, successor };
    clusters_.push_back(cluster);
    bumpMinFormatVersionIfSmaller(kMinFormatVersionEncodeClusters);
    return clusters_.size() - 1;
}

void EncoderConfigBuilder::addPathToCluster(
        const ThriftPath& path,
        size_t clusterIdx)
{
    LogicalCluster& cluster = getCluster(clusterIdx);
    const LogicalId id      = pathToId(path);
    const TType pathType    = pathMap_.at(path).type;
    const TType clusterType = getClusterType(clusterIdx);
    if (!cluster.idList.empty() && pathType != clusterType) {
        throw std::runtime_error{ fmt::format(
                "Cannot add path of TType {} to a cluster of TType {}",
                pathType,
                clusterType) };
    }
    cluster.idList.push_back(id);
}

void EncoderConfigBuilder::updateClusterSuccessor(int clusterIdx, int successor)
{
    if (clusterIdx >= clusters_.size()) {
        throw std::out_of_range{ fmt::format(
                "Cluster index {} is out of bounds!", clusterIdx) };
    }
    clusters_.at(clusterIdx).successor = successor;
}

void EncoderConfigBuilder::setShouldParseTulipV2()
{
    parseTulipV2_ = true;
    bumpMinFormatVersionIfSmaller(kMinFormatVersionEncodeTulipV2);
}

void EncoderConfigBuilder::setRootType(TType type)
{
    // The actual Thrift parser supports a larger set of root types,
    // but these are the only ones we expect to see in practice.
    constexpr std::array<TType, 4> collectionTypes{
        TType::T_LIST, TType::T_SET, TType::T_MAP, TType::T_STRUCT
    };
    if (std::find(collectionTypes.begin(), collectionTypes.end(), type)
        == collectionTypes.end()) {
        throw std::runtime_error{ fmt::format(
                "Unexpected root TType {}", type) };
    }
    rootType_ = type;
}

void EncoderConfigBuilder::bumpMinFormatVersionIfSmaller(
        int minRequiredFormatVersion)
{
    if (minFormatVersion_ < minRequiredFormatVersion) {
        minFormatVersion_ = minRequiredFormatVersion;
    }
}

LogicalId EncoderConfigBuilder::pathToId(const ThriftPath& path) const
{
    const auto info = pathMap_.find(path);
    if (info == pathMap_.end()) {
        throw std::runtime_error{ fmt::format(
                "Path {} does not exist in this config!", pathToStr(path)) };
    }
    return info->second.id;
}

std::string EncoderConfigBuilder::finalize()
{
    // Delete empty clusters
    auto lambda = [](const auto& cluster) { return cluster.idList.empty(); };
    clusters_.erase(
            std::remove_if(clusters_.begin(), clusters_.end(), lambda),
            clusters_.end());

    // Validate and serialize
    validate();
    return serialize();
}

} // namespace zstrong::thrift
