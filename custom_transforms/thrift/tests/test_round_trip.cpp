// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/constants.h"      // @manual
#include "custom_transforms/thrift/parse_config.h"   // @manual
#include "custom_transforms/thrift/tests/util.h"     // @manual
#include "custom_transforms/thrift/thrift_parsers.h" // @manual
#include "custom_transforms/tulip_v2/tests/tulip_v2_data_utils.h"
#include "thrift/lib/cpp2/protocol/Serializer.h"

#include <gtest/gtest.h>

using apache::thrift::BinarySerializer;
using apache::thrift::CompactSerializer;

namespace openzl::thrift::tests {

using namespace zstrong::thrift;
namespace cpp2 = zstrong::thrift::tests::cpp2;

namespace {
// Test round-trip where the root type is not T_STRUCT
template <typename Serializer>
void testNakedRoots(const ZL_VOEncoderDesc& desc)
{
    std::vector<std::pair<TType, std::string>> const testPairs = {
        { TType::T_LIST,
          Serializer::template serialize<std::string>(
                  std::vector<int>{ 1, 2, 3 }) },
        { TType::T_SET,
          Serializer::template serialize<std::string>(
                  std::set<int>{ 1, 2, 3 }) },
        { TType::T_MAP,
          Serializer::template serialize<std::string>(
                  std::map<int, int>{ { 1, 1 }, { 2, 2 }, { 3, 3 } }) },
    };

    for (const auto& pair : testPairs) {
        // Build an empty config with rootType == pair.first
        std::string const configStr =
                EncoderConfig({}, {}, pair.first).serialize();
        EXPECT_NO_THROW(
                runThriftSplitterRoundTrip(desc, pair.second, configStr));
        std::string const concatData = pair.second + pair.second;
        EXPECT_NO_THROW(
                runThriftSplitterRoundTrip(desc, pair.second, configStr));
    }
}

template <typename Serializer>
void testSimpleRoundTrip(const ZL_VOEncoderDesc& desc, int minFormatVersion)
{
    std::mt19937 gen(0xdeadbeef);
    for (size_t _ = 0; _ < 100; _++) {
        int const seed              = std::uniform_int_distribution()(gen);
        std::string const configStr = buildValidEncoderConfig(
                minFormatVersion, seed, ConfigGenMode::kMoreFreedom);
        std::string const data = generateRandomThrift<Serializer>(gen);

        EXPECT_NO_THROW(runThriftSplitterRoundTrip(
                desc, data, configStr, minFormatVersion));

        std::string const concatData = data + data;

        EXPECT_NO_THROW(runThriftSplitterRoundTrip(
                desc, concatData, configStr, minFormatVersion));
    }
}
} // namespace

TEST(RoundTripTest, ConfigurableSplitCompactLoremIpsum)
{
    std::string const data =
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Pellentesque efficitur.";
    std::string const configStr =
            buildValidEncoderConfig(kMinFormatVersionEncode);

    /* The Thrift parser currently fails on non-Thrift data */
    EXPECT_ANY_THROW(runThriftSplitterRoundTrip(
            thriftCompactConfigurableSplitter, data, configStr));
}

TEST(RoundTripTest, ConfigurableSplitBinaryLoremIpsum)
{
    std::string const data =
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Pellentesque efficitur.";
    std::string const configStr =
            buildValidEncoderConfig(kMinFormatVersionEncode);

    /* The Thrift parser currently fails on non-Thrift data */
    EXPECT_ANY_THROW(runThriftSplitterRoundTrip(
            thriftBinaryConfigurableSplitter, data, configStr));
}

TEST(RoundTripTest, ConfigurableSplitCompactSimpleThrift)
{
    testSimpleRoundTrip<apache::thrift::CompactSerializer>(
            thriftCompactConfigurableSplitter, kMinFormatVersionEncode);
    testSimpleRoundTrip<apache::thrift::CompactSerializer>(
            thriftCompactConfigurableSplitter, kMinFormatVersionEncodeClusters);
}

TEST(RoundTripTest, ConfigurableSplitBinarySimpleThrift)
{
    testSimpleRoundTrip<apache::thrift::BinarySerializer>(
            thriftBinaryConfigurableSplitter, kMinFormatVersionEncode);
    testSimpleRoundTrip<apache::thrift::BinarySerializer>(
            thriftBinaryConfigurableSplitter, kMinFormatVersionEncodeClusters);
}

TEST(RoundTripTest, NakedRootCompact)
{
    testNakedRoots<CompactSerializer>(thriftCompactConfigurableSplitter);
}

TEST(RoundTripTest, NakedRootBinary)
{
    testNakedRoots<BinarySerializer>(thriftBinaryConfigurableSplitter);
}

TEST(RoundTripTest, TestTulipV2)
{
    std::string const goodConfig = EncoderConfig(
                                           {},
                                           {},
                                           TType::T_STRUCT,
                                           true,
                                           {},
                                           kMinFormatVersionEncodeTulipV2)
                                           .serialize();
    std::string const badConfig = EncoderConfig(
                                          {},
                                          {},
                                          TType::T_STRUCT,
                                          false,
                                          {},
                                          kMinFormatVersionEncodeTulipV2)
                                          .serialize();

    std::mt19937 gen(0xdeadbeef);
    for (size_t n = 1; n < 10; ++n) {
        auto data = openzl::tulip_v2::tests::generateTulipV2(n, gen);
        EXPECT_NO_THROW(runThriftSplitterRoundTrip(
                thriftCompactConfigurableSplitter,
                data,
                goodConfig,
                kMinFormatVersionEncodeTulipV2));
        EXPECT_ANY_THROW(runThriftSplitterRoundTrip(
                thriftBinaryConfigurableSplitter,
                data,
                goodConfig,
                kMinFormatVersionEncodeTulipV2));
        EXPECT_ANY_THROW(runThriftSplitterRoundTrip(
                thriftCompactConfigurableSplitter,
                data,
                badConfig,
                kMinFormatVersionEncodeTulipV2));
    }
}

TEST(RoundTripTest, ClusterFieldsAreMissing)
{
    // Field values don't matter, we only care about the field ids
    cpp2::UnknownFieldsTestStruct testStruct;
    const auto compactData =
            apache::thrift::CompactSerializer::serialize<std::string>(
                    testStruct);
    const auto binaryData =
            apache::thrift::BinarySerializer::serialize<std::string>(
                    testStruct);
    std::string const configStr =
            buildValidEncoderConfig(kMinFormatVersionEncodeClusters);
    EXPECT_NO_THROW(runThriftSplitterRoundTrip(
            thriftCompactConfigurableSplitter,
            compactData,
            configStr,
            kMinFormatVersionEncodeClusters));
    EXPECT_NO_THROW(runThriftSplitterRoundTrip(
            thriftBinaryConfigurableSplitter,
            binaryData,
            configStr,
            kMinFormatVersionEncodeClusters));
}

TEST(RoundTripTest, SimpleStringTestIndividualSplit)
{
    EncoderConfigBuilder builder;
    builder.addPath({ ThriftNodeId(1) }, TType::T_STRING);
    builder.setSuccessorForPath({ ThriftNodeId(1) }, 0);

    std::vector<cpp2::StringTestStruct> testStructs;

    // Test when only first field is present (VO stream)
    {
        cpp2::StringTestStruct testStruct;
        testStruct.field1() = "foo";
        testStructs.push_back(testStruct);
    }

    // Test when only second field is present (Singleton stream)
    {
        cpp2::StringTestStruct testStruct;
        testStruct.field2() = "bar";
        testStructs.push_back(testStruct);
    }

    // Test when neither field is present
    testStructs.push_back(cpp2::StringTestStruct{});

    for (const auto& testStruct : testStructs) {
        std::string const data =
                apache::thrift::BinarySerializer::serialize<std::string>(
                        testStruct);
        EXPECT_NO_THROW(runThriftSplitterRoundTrip(
                thriftBinaryConfigurableSplitter,
                data,
                builder.finalize(),
                ZL_MAX_FORMAT_VERSION));
    }
}

TEST(RoundTripTest, SimpleStringTestClusterSplit)
{
    EncoderConfigBuilder builder;
    builder.addPath({ ThriftNodeId(1) }, TType::T_STRING);
    builder.addPath({ ThriftNodeId(2) }, TType::T_STRING);
    const size_t clusterIdx = builder.addEmptyCluster(0);
    builder.addPathToCluster({ ThriftNodeId(1) }, clusterIdx);
    builder.addPathToCluster({ ThriftNodeId(2) }, clusterIdx);

    std::vector<cpp2::StringTestStruct> testStructs;

    // Test when both fields are present
    {
        cpp2::StringTestStruct testStruct;
        testStruct.field1() = "foo";
        testStruct.field2() = "bar";
        testStructs.push_back(testStruct);
    }

    // Test when only the second field is present
    {
        cpp2::StringTestStruct testStruct;
        testStruct.field2() = "bar";
    }

    // Test when neither field is present
    testStructs.push_back(cpp2::StringTestStruct{});

    for (const auto& testStruct : testStructs) {
        std::string const data =
                apache::thrift::BinarySerializer::serialize<std::string>(
                        testStruct);
        EXPECT_NO_THROW(runThriftSplitterRoundTrip(
                thriftBinaryConfigurableSplitter,
                data,
                builder.finalize(),
                ZL_MAX_FORMAT_VERSION));
    }
}

TEST(RoundTripTest, OldStyleVSF)
{
    EncoderConfigBuilder builder;
    builder.addPath({ ThriftNodeId(1) }, TType::T_STRING);
    builder.addPath({ ThriftNodeId(1), ThriftNodeId::kLength }, TType::T_U32);
    builder.setSuccessorForPath({ ThriftNodeId(1) }, 0);
    builder.setSuccessorForPath({ ThriftNodeId(1), ThriftNodeId::kLength }, 0);

    std::vector<cpp2::StringTestStruct> testStructs;

    // Test when field is present
    {
        cpp2::StringTestStruct testStruct;
        testStruct.field1() = "foo";
        testStructs.push_back(testStruct);
    }

    // Test when field is not present
    testStructs.push_back(cpp2::StringTestStruct{});

    for (const auto& testStruct : testStructs) {
        std::string const data =
                apache::thrift::BinarySerializer::serialize<std::string>(
                        testStruct);

        // Round-trip should work for format version < 14
        EXPECT_NO_THROW(runThriftSplitterRoundTrip(
                thriftBinaryConfigurableSplitter,
                data,
                builder.finalize(),
                kMinFormatVersionEncode,
                kMinFormatVersionStringVSF - 1));

        // Round-trip should fail for format version >= 14
        EXPECT_ANY_THROW(runThriftSplitterRoundTrip(
                thriftBinaryConfigurableSplitter,
                data,
                builder.finalize(),
                kMinFormatVersionStringVSF,
                ZL_MAX_FORMAT_VERSION));
    }
}
} // namespace openzl::thrift::tests
