// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "custom_transforms/thrift/parse_config.h"   // @manual
#include "custom_transforms/thrift/thrift_parsers.h" // @manual

#if ZL_FBCODE_IS_RELEASE
#    include "openzl/prod/custom_transforms/thrift/gen-cpp2/parse_config_types.h" // @manual
#else
#    include "openzl/dev/custom_transforms/thrift/gen-cpp2/parse_config_types.h" // @manual
#endif

namespace openzl::thrift::tests {

using namespace zstrong::thrift;

TEST(ConfigSerializationTest, RoundTrip)
{
    const std::vector<ThriftPath> paths = {
        { ThriftNodeId::kListElem, ThriftNodeId(1), ThriftNodeId(2) },
        { ThriftNodeId::kMapValue, ThriftNodeId(4), ThriftNodeId(5) }
    };

    const EncoderConfig config1(
            { { paths[0], { LogicalId(3), TType::T_BOOL } },
              { paths[1], { LogicalId(6), TType::T_BOOL } } },
            { { LogicalId(3), 0 }, { LogicalId(6), 1 } },
            TType::T_STRUCT,
            true,
            { LogicalCluster{ { LogicalId(3), LogicalId(6) }, 0 } },
            kMinFormatVersionEncodeClusters);
    const std::string compactStr = config1.serialize();

    // JSON must start with "{" character
    const std::string jsonStr = R"({
        "baseConfig":{
            "pathMap":{
                [
                    2147483645,
                    1,
                    2
                ]:{
                    "logicalId":3,
                    "type":2
                },
                [
                    2147483646,
                    4,
                    5
                ]:{
                    "logicalId":6,
                    "type":2
                }
            },
            "rootType":12,
            "clusters":[
                {
                    "idList": [3,6],
                    "successor": 0
                }
            ]
        },
        "successorMap":{
            "3":0,
            "6":1
        },
        "minFormatVersion": 12
    })";

    auto test = [&](std::string testStr) {
        const EncoderConfig config2(testStr);
        const DecoderConfig config3(config2, 42);

        EXPECT_EQ(config1.getLogicalIds(), config2.getLogicalIds());
        EXPECT_EQ(config2.getLogicalIds(), config3.getLogicalIds());

        EXPECT_EQ(config1.getLogicalStreamAt(paths[1]), LogicalId(6));
        for (const auto& path : paths) {
            EXPECT_EQ(
                    config1.getLogicalStreamAt(path),
                    config2.getLogicalStreamAt(path));
            EXPECT_EQ(
                    config2.getLogicalStreamAt(path),
                    config3.getLogicalStreamAt(path));
        }

        for (const auto id : config1.getLogicalIds()) {
            EXPECT_EQ(
                    config1.getSuccessorForLogicalStream(id),
                    config2.getSuccessorForLogicalStream(id));
        }

        EXPECT_EQ(config3.getOriginalSize(), 42);

        EXPECT_EQ(config1.clusters(), config2.clusters());
        EXPECT_EQ(config2.clusters(), config3.clusters());

        EXPECT_EQ(config1.getTypeSuccessorMap(), config2.getTypeSuccessorMap());
    };

    test(jsonStr);
    test(compactStr);
}

TEST(ConfigSerializationTest, TypeMismatch)
{
    const std::string valid = R"({
        "baseConfig":{
            "pathMap":{
                [1]:{ "logicalId":1, "type":1 },
                [2]:{ "logicalId":1, "type":1 }
            },
            "rootType":1
        },
        "successorMap":{
            "1":0
        }
    })";

    const std::string invalid = R"({
        "baseConfig":{
            "pathMap":{
                [1]:{ "logicalId":1, "type":1 },
                [2]:{ "logicalId":1, "type":2 }
            },
            "rootType":1
        },
        "successorMap":{
            "1":0
        }
    })";

    EXPECT_NO_THROW(EncoderConfig{ valid });
    EXPECT_ANY_THROW(EncoderConfig{ invalid });
}

TEST(ConfigSerializationTest, ClusterTypeMistmatch)
{
    const std::string valid = R"({
        "baseConfig":{
            "pathMap":{
                [1]:{ "logicalId":1, "type":1 },
                [2]:{ "logicalId":2, "type":1 }
            },
            "rootType":1,
            "clusters":[
                {
                    "idList": [1,2],
                    "successor": 0
                }
            ]
        },
        "successorMap":{
            "1":0
        },
        "minFormatVersion": 12
    })";

    const std::string invalid = R"({
        "baseConfig":{
            "pathMap":{
                [1]:{ "logicalId":1, "type":1 },
                [2]:{ "logicalId":2, "type":2 }
            },
            "rootType":1,
            "clusters":[
                {
                    "idList": [1,2],
                    "successor": 0
                }
            ]
        },
        "successorMap":{
            "1":0
        },
        "minFormatVersion": 12
    })";

    EXPECT_NO_THROW(EncoderConfig{ valid });
    EXPECT_ANY_THROW(EncoderConfig{ invalid });
}

TEST(ConfigSerializationTest, EncoderConfigSerializationDefualtSuccessors)
{
    const std::vector<ThriftPath> paths = {
        { ThriftNodeId::kListElem, ThriftNodeId(1), ThriftNodeId(2) },
        { ThriftNodeId::kMapValue, ThriftNodeId(4), ThriftNodeId(5) }
    };
    const std::string jsonStrNoMap = R"({
        "baseConfig":{
            "pathMap":{
                [
                    2147483645,
                    1,
                    2
                ]:{
                    "logicalId":3,
                    "type":2
                },
                [
                    2147483646,
                    4,
                    5
                ]:{
                    "logicalId":6,
                    "type":2
                }
            },
            "rootType":12,
            "clusters":[
                {
                    "idList": [3,6],
                    "successor": 0
                }
            ]
        },
        "successorMap":{
            "3":0,
            "6":1
        },
        "minFormatVersion": 12
    })";
    EncoderConfig config1(jsonStrNoMap);
    auto typeSuccessorMap = config1.getTypeSuccessorMap();
    EXPECT_EQ(typeSuccessorMap[ZL_Type_serial], 1);
    EXPECT_EQ(typeSuccessorMap[ZL_Type_struct], 1);
    EXPECT_EQ(typeSuccessorMap[ZL_Type_numeric], 6);
    EXPECT_EQ(typeSuccessorMap[ZL_Type_string], 1);
    const std::string jsonStrHasMap = R"({
        "baseConfig":{
            "pathMap":{
                [
                    2147483645,
                    1,
                    2
                ]:{
                    "logicalId":3,
                    "type":2
                },
                [
                    2147483646,
                    4,
                    5
                ]:{
                    "logicalId":6,
                    "type":2
                }
            },
            "rootType":12,
            "clusters":[
                {
                    "idList": [3,6],
                    "successor": 0
                }
            ]
        },
        "successorMap":{
            "3":0,
            "6":1
        },
        "minFormatVersion": 12,
        "typeSuccessorMap":{
            "2":4,
            "4":3,
            "8":2
        }
    })";
    EncoderConfig config2(jsonStrHasMap);
    typeSuccessorMap = config2.getTypeSuccessorMap();
    EXPECT_EQ(
            typeSuccessorMap[ZL_Type_serial],
            1); // Use default since field is not present in map
    EXPECT_EQ(typeSuccessorMap[ZL_Type_struct], 4);
    EXPECT_EQ(typeSuccessorMap[ZL_Type_numeric], 3);
    EXPECT_EQ(typeSuccessorMap[ZL_Type_string], 2);
}

TEST(ConfigSerializationTest, EmptyClusterShouldFailValidation)
{
    const std::vector<ThriftPath> paths = {
        { ThriftNodeId::kListElem, ThriftNodeId(1), ThriftNodeId(2) },
        { ThriftNodeId::kMapValue, ThriftNodeId(4), ThriftNodeId(5) }
    };

    const std::map<ThriftPath, PathInfo> pathMap = {
        { paths[0], { LogicalId(3), TType::T_BOOL } },
        { paths[1], { LogicalId(6), TType::T_BOOL } }
    };
    const std::map<LogicalId, int> successorMap = { { LogicalId(3), 0 },
                                                    { LogicalId(6), 1 } };

    const auto buildEncoderConfig = [&](std::vector<LogicalCluster> clusters) {
        EncoderConfig(
                pathMap,
                successorMap,
                TType::T_STRUCT,
                false,
                clusters,
                kMinFormatVersionEncodeClusters);
    };

    EXPECT_NO_THROW(buildEncoderConfig({}));
    EXPECT_NO_THROW(
            buildEncoderConfig({ LogicalCluster{ { LogicalId(3) }, 0 } }));
    EXPECT_ANY_THROW(buildEncoderConfig(
            { LogicalCluster{ {}, 0 } })); // empty cluster should throw
}

TEST(ConfigSerializationTest, VersionCompatibility)
{
    // Configs that include clusters should fail on older format versions
    for (int formatVersion = kMinFormatVersionEncode;
         formatVersion < kMinFormatVersionEncodeClusters;
         formatVersion++) {
        const std::vector<ThriftPath> paths = {
            { ThriftNodeId::kListElem, ThriftNodeId(1), ThriftNodeId(2) }
        };
        const std::map<ThriftPath, PathInfo> pathMap = {
            { paths[0], { LogicalId(0), TType::T_BOOL } },
        };
        std::vector<LogicalCluster> clusters = { LogicalCluster{
                { LogicalId(0) }, 0 } };
        EXPECT_ANY_THROW(EncoderConfig(
                pathMap, {}, TType::T_STRUCT, false, clusters, formatVersion));
        EXPECT_NO_THROW(EncoderConfig(
                pathMap,
                {},
                TType::T_STRUCT,
                false,
                clusters,
                kMinFormatVersionEncodeClusters));
    };

    // Configs that specify TulipV2 mode should fail on older format versions
    for (int formatVersion = kMinFormatVersionEncode;
         formatVersion < kMinFormatVersionEncodeTulipV2;
         formatVersion++) {
        const bool tulipV2Mode = true;
        EXPECT_ANY_THROW(EncoderConfig(
                {}, {}, TType::T_STRUCT, tulipV2Mode, {}, formatVersion));
        EXPECT_NO_THROW(EncoderConfig(
                {},
                {},
                TType::T_STRUCT,
                tulipV2Mode,
                {},
                kMinFormatVersionEncodeTulipV2));
    }
}

TEST(ConfigSerializationTest, RejectIllegalStringLengthsSplit)
{
    // See D51728701 for context
    EncoderConfigBuilder builder;
    builder.addPath({ ThriftNodeId(1), ThriftNodeId::kLength }, TType::T_U32);
    EXPECT_ANY_THROW(builder.finalize());
    builder.addPath({ ThriftNodeId(1) }, TType::T_STRING);
    assert(builder.pathMap().size() == 2);
    EXPECT_NO_THROW(builder.finalize());
}

TEST(ConfigSerializationTest, RejectIllegalListLengthsSplit)
{
    // See D51728701 for context
    EncoderConfigBuilder builder;
    builder.addPath({ ThriftNodeId(1), ThriftNodeId::kLength }, TType::T_U32);
    EXPECT_ANY_THROW(builder.finalize());
    builder.addPath(
            { ThriftNodeId(1), ThriftNodeId::kListElem }, TType::T_FLOAT);
    assert(builder.pathMap().size() == 2);
    EXPECT_NO_THROW(builder.finalize());
}

} // namespace openzl::thrift::tests
