// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "custom_transforms/thrift/constants.h"      // @manual
#include "custom_transforms/thrift/thrift_parsers.h" // @manual
#include "custom_transforms/thrift/thrift_types.h"   // @manual
#include "openzl/zl_version.h"                       // @manual

#if ZL_FBCODE_IS_RELEASE
#    include "openzl/prod/custom_transforms/thrift/gen-cpp2/parse_config_types.h" // @manual
#    include "openzl/prod/custom_transforms/thrift/gen-cpp2/parse_config_types_custom_protocol.h" // @manual
#else
#    include "openzl/dev/custom_transforms/thrift/gen-cpp2/parse_config_types.h" // @manual
#    include "openzl/dev/custom_transforms/thrift/gen-cpp2/parse_config_types_custom_protocol.h" // @manual
#endif

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace zstrong::thrift {

// These classes wrap Thrift structs from the zstrong::thrift::cpp2 namespace.
// The wrappers add bounds-checking and some pre-calculation of useful values.
// In the future we may add more functionality, such as checking that
// each LogicalId has a successor specified in the EncoderConfig.

struct PathInfo {
    explicit PathInfo(const cpp2::PathInfo& info);
    PathInfo(LogicalId id_, TType type_) : id(id_), type(type_) {}
    const LogicalId id;
    const TType type;
};

struct LogicalCluster {
    explicit LogicalCluster(const cpp2::LogicalCluster& info);
    LogicalCluster(std::vector<LogicalId> idList_, int successor_)
            : idList(std::move(idList_)), successor(successor_)
    {
    }
    std::vector<LogicalId> idList;
    int successor;

    bool operator==(const LogicalCluster& other) const
    {
        return idList == other.idList && successor == other.successor;
    }
};

class BaseConfig {
   public:
    explicit BaseConfig(const cpp2::BaseConfig& config)
    {
        setBaseConfig(config);
    }
    explicit BaseConfig(
            std::map<ThriftPath, PathInfo> pathMap,
            TType rootType                       = TType::T_STRUCT,
            std::vector<LogicalCluster> clusters = {});

    const folly::F14FastSet<LogicalId>& getLogicalIds() const;
    TType getRootType() const;

    std::optional<LogicalId> getLogicalStreamAt(const ThriftPath& path) const;

    const std::map<ThriftPath, PathInfo>& pathMap() const
    {
        return pathMap_;
    }
    const std::vector<LogicalCluster>& clusters() const
    {
        return clusters_;
    }
    std::vector<LogicalId> getUnclusteredStreams() const;

    // Note: EncoderConfigBuilder mutations invalidate the returned reference
    const LogicalCluster& getCluster(size_t clusterIdx) const;

    // Note: getClusterPaths() and getClusterType() are not performant.
    // They should only be used for testing and offline training.
    //
    // getClusterType() returns T_VOID for empty clusters. If the cluster is not
    // type-homogenous, it will throw.
    std::vector<ThriftPath> getClusterPaths(size_t clusterIdx) const;
    TType getClusterType(size_t clusterIdx) const;

   protected:
    BaseConfig() {}
    void setBaseConfig(cpp2::BaseConfig config);
    cpp2::BaseConfig toThriftObj() const;
    void validate() const; // must call at the end of each constructor

    // Note: EncoderConfigBuilder mutations invalidate the returned reference
    LogicalCluster& getCluster(size_t clusterIdx);

    folly::F14FastSet<LogicalId> logicalStreamIds_;
    std::map<ThriftPath, PathInfo> pathMap_;
    TType rootType_ = TType::T_STRUCT;
    std::vector<LogicalCluster> clusters_;

    // TODO(T193417270) Support split-by-map-key
};

class EncoderConfig : public BaseConfig {
   public:
    explicit EncoderConfig(std::string_view str);

    // TODO(T193417296) deprecate this constructor in favor of builder pattern
    //
    // This constructor is currently only used in unit tests, after all unit
    // tests are migrated to the builder pattern we can deprecate it.
    EncoderConfig(
            std::map<ThriftPath, PathInfo> pathMap,
            std::map<LogicalId, int> successors,
            TType rootType                       = TType::T_STRUCT,
            bool parseTulipV2                    = false,
            std::vector<LogicalCluster> clusters = {},
            int minFormatVersion                 = kMinFormatVersionEncode)
            : BaseConfig(std::move(pathMap), rootType, std::move(clusters)),
              successors_(std::move(successors)),
              parseTulipV2_(parseTulipV2),
              minFormatVersion_(minFormatVersion)
    {
        initTypeSuccessorMap();
        validate();
    }

    EncoderConfig()
    {
        validate();
    }

    std::string serialize() const;
    std::optional<int> getSuccessorForLogicalStream(LogicalId id) const;
    bool getShouldParseTulipV2() const;
    int getMinFormatVersion() const;
    std::map<ZL_Type, int> getTypeSuccessorMap() const;

   protected:
    // Note: every constructor must call validate() at the end.
    void validate() const;
    void initTypeSuccessorMap();
    std::map<LogicalId, int> successors_;
    bool parseTulipV2_    = false;
    int minFormatVersion_ = kMinFormatVersionEncode;
    // A map from stream type to the successor for streams with LogicalId
    // that are not mapped to a successor and not clustered
    std::map<ZL_Type, int> typeSuccessorMap_;

   private:
    cpp2::EncoderConfig toThriftObj() const;
    static constexpr int kNonNumericDefaultSuccessor = 1;
    static constexpr int kNumericDefaultSuccessor    = 6;
};

class DecoderConfig : public BaseConfig {
   public:
    explicit DecoderConfig(folly::ByteRange bytes);
    explicit DecoderConfig(std::string_view str)
            : DecoderConfig(folly::ByteRange{ str })
    {
    }
    DecoderConfig(
            const BaseConfig& baseConfig,
            size_t originalSize,
            bool unparseMessageHeaders = false);
    std::string serialize() const;
    size_t getOriginalSize() const;
    bool getShouldUnparseMessageHeaders() const;

   private:
    cpp2::DecoderConfig toThriftObj() const;
    size_t originalSize_;
    bool unparseMessageHeaders_;
};

class EncoderConfigBuilder : public EncoderConfig {
   public:
    EncoderConfigBuilder() = default;

    EncoderConfigBuilder(std::string_view str) : EncoderConfig(str) {}

    // Clean up the config, run validation, and serialize. The builder object
    // remains in a valid, cleaned-up state for additional mutations.
    //
    // Empty clusters are deleted during clean-up, which means cluster indices
    // are invalidated by this method.
    std::string finalize();

    // Add path to successor map. A LogicalId will be assigned and used
    // internally for this path.
    void addPath(const ThriftPath& path, TType type);

    // Look up the logical id for a path and add that id to the successor map
    void setSuccessorForPath(const ThriftPath& path, int successor);
    // Modify the default successor for a specific stream type.
    void setSuccessorForType(ZL_Type type, int successor);

    // Returns index of the new cluster in the cluster list. Bumps min format
    // version to at least kMinFormatVersionEncodeClusters.
    //
    // Note: returned index is invalidated by mutations.
    size_t addEmptyCluster(int successor);

    // Add path to a cluster by index. Path will be added to the back of the
    // cluster.
    void addPathToCluster(const ThriftPath& path, size_t clusterIdx);

    // Change the successor of a cluster
    void updateClusterSuccessor(int clusterIdx, int successor);

    // Enable TulipV2 parsing. Bumps min format version to at least
    // kMinFormatVersionEncodeTulipV2.
    void setShouldParseTulipV2();

    // The default root type, T_STRUCT, should work for 99% of use-cases.
    // If necessary, call this method to override it.
    void setRootType(TType rootType);

   private:
    // Helper for mutations such as addEmptyCluster() which use newer features
    void bumpMinFormatVersionIfSmaller(int minRequiredFormatVersion);

    // Will throw if the path doesn't exist.
    LogicalId pathToId(const ThriftPath& path) const;
};

} // namespace zstrong::thrift
