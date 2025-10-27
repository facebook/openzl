// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/constants.h"      // @manual
#include "custom_transforms/thrift/parse_config.h"   // @manual
#include "custom_transforms/thrift/tests/util.h"     // @manual
#include "custom_transforms/thrift/thrift_parsers.h" // @manual
#include "thrift/lib/cpp2/protocol/Serializer.h"

#include <folly/Conv.h>
#include <gtest/gtest.h>
#include <type_traits>

using apache::thrift::BinarySerializer;
using apache::thrift::CompactSerializer;

namespace zstrong::thrift::tests {
namespace {

template <typename Parser, typename Serializer>
void testEmptyConfig()
{
    std::string const configStr = EncoderConfig({}, {}).serialize();
    std::mt19937 gen(0xdeadbeef);

    for (size_t _ = 0; _ < 100; _++) {
        std::string const data = generateRandomThrift<Serializer>(gen);

        WriteStreamSet const wss =
                thriftSplitIntoWriteStreams<Parser>(data, configStr);

        // Empty config should result in zero logical streams
        EXPECT_EQ(wss.getVariableStreams().size(), 0);
        EXPECT_GT(wss.getSingletonStreams().size(), 0);
    }
}

template <typename Parser, typename Serializer>
void testPrimitiveTypes()
{
    cpp2::PrimitiveTestStruct testStruct;
    testStruct.field_bool()    = false;
    testStruct.field_byte()    = 0xbe;
    testStruct.field_i16()     = 0xbeef;
    testStruct.field_i32()     = 0xdeadbeef;
    testStruct.field_i64()     = 0xfaceb00cdeadbeef;
    testStruct.field_float32() = (float)0.42;
    testStruct.field_float64() = (double)0.42;
    std::string const testString(42, (char)0x42);
    testStruct.field_string()   = std::string(42, (char)0x42);
    size_t constexpr kNumFields = 8;

    std::string const data =
            Serializer::template serialize<std::string>(testStruct);

    std::vector<TType> const types = { TType::T_BOOL,   TType::T_BYTE,
                                       TType::T_I16,    TType::T_I32,
                                       TType::T_I64,    TType::T_FLOAT,
                                       TType::T_DOUBLE, TType::T_STRING };
    assert(types.size() == kNumFields);

    std::map<ThriftPath, PathInfo> pathMap;
    std::map<LogicalId, int> successors;
    for (int i = 0; i < kNumFields; i++) {
        ThriftPath path{ ThriftNodeId(i + 1) };
        pathMap.emplace(std::move(path), PathInfo(LogicalId(i), types.at(i)));
        successors.emplace(LogicalId(i), 0);
    }

    std::string const encoderConfigStr =
            EncoderConfig(pathMap, successors).serialize();

    folly::F14FastMap<SingletonId, WriteStream> expectedSingletonStreams;
    for (int i = 0; SingletonId(i) < SingletonId::kNumSingletonIds; i++) {
        expectedSingletonStreams.emplace(
                SingletonId(i), WriteStream{ SingletonIdToTType[i] });
    }

    expectedSingletonStreams.at(SingletonId::kTypes) =
            WriteStream{ TType::T_BYTE,
                         std::vector<TType>{ types.begin(), types.end() } };
    expectedSingletonStreams.at(SingletonId::kTypes)
            .writeValue((uint8_t)TType::T_STOP);

    expectedSingletonStreams.at(SingletonId::kFieldDeltas) =
            WriteStream{ TType::T_I16, std::vector<int16_t>(kNumFields, 1) };

    expectedSingletonStreams.at(SingletonId::kLengths) =
            WriteStream{ TType::T_U32, std::vector<uint32_t>{} };

    folly::F14FastMap<LogicalId, WriteStream> expectedVariableStreams;
    for (int i = 0; i < kNumFields; i++) {
        expectedVariableStreams.emplace(
                LogicalId(i), WriteStream{ types.at(i) });
    }
    expectedVariableStreams.at(LogicalId(0)).writeValue<uint8_t>(false);
    expectedVariableStreams.at(LogicalId(1)).writeValue<int8_t>(0xbe);
    expectedVariableStreams.at(LogicalId(2)).writeValue<int16_t>(0xbeef);
    expectedVariableStreams.at(LogicalId(3)).writeValue<int32_t>(0xdeadbeef);
    expectedVariableStreams.at(LogicalId(4))
            .writeValue<int64_t>(0xfaceb00cdeadbeef);
    expectedVariableStreams.at(LogicalId(5)).writeValue<float>(0.42);
    expectedVariableStreams.at(LogicalId(6)).writeValue<double>(0.42);
    expectedVariableStreams.at(LogicalId(7))
            .writeBytes(
                    folly::ByteRange(
                            (const uint8_t*)testString.data(),
                            testString.size()));

    // These streams are used for string VSF splits
    folly::F14FastMap<LogicalId, WriteStream>
            expectedVariableStringLengthStreams;
    expectedVariableStringLengthStreams.emplace(
            LogicalId(7),
            WriteStream{ TType::T_U32, std::vector<uint32_t>{ 42 } });

    WriteStreamSet const wss =
            thriftSplitIntoWriteStreams<Parser>(data, encoderConfigStr);

    EXPECT_EQ(wss.getSingletonStreams(), expectedSingletonStreams);
    EXPECT_EQ(wss.getVariableStreams(), expectedVariableStreams);
    EXPECT_EQ(
            wss.getVariableStringLengthStreams(),
            expectedVariableStringLengthStreams);
}

template <typename Parser, typename Serializer>
void testCollectionTypes()
{
    for (size_t size = 0; size < 64; size++) {
        std::vector<bool> testListBool;
        std::set<int32_t> testSetInt32;
        std::map<int32_t, bool> testMapDiffTypes;
        std::map<float, float> testMapSameTypes;
        for (size_t i = 0; i < size; i++) {
            bool const boolean = i % 2 == 0;
            testListBool.push_back(boolean);
            testSetInt32.emplace(0xdeadbeef + i);
            testMapDiffTypes.emplace(0xdeadbeef + i, boolean);
            testMapSameTypes.emplace(0.42 + i, -999);
        }

        cpp2::CollectionTestStruct testStruct;
        testStruct.field_list_bool()      = testListBool;
        testStruct.field_set_int32()      = testSetInt32;
        testStruct.field_map_diff_types() = testMapDiffTypes;
        testStruct.field_map_same_types() = testMapSameTypes;
        size_t constexpr kNumFields       = 4;

        std::string const data =
                Serializer::template serialize<std::string>(testStruct);

        std::vector<ThriftPath> const paths = {
            ThriftPath{ ThriftNodeId(1), ThriftNodeId::kListElem },
            ThriftPath{ ThriftNodeId(2), ThriftNodeId::kListElem },
            ThriftPath{ ThriftNodeId(3), ThriftNodeId::kMapKey },
            ThriftPath{ ThriftNodeId(3), ThriftNodeId::kMapValue },
            ThriftPath{ ThriftNodeId(4), ThriftNodeId::kMapKey },
            ThriftPath{ ThriftNodeId(4), ThriftNodeId::kMapValue },
        };

        std::vector<std::pair<ThriftPath, PathInfo>> const pathInfoPairs = {
            { paths[0], PathInfo{ LogicalId(0), TType::T_BOOL } },
            { paths[1], PathInfo{ LogicalId(1), TType::T_I32 } },
            { paths[2], PathInfo{ LogicalId(2), TType::T_I32 } },
            { paths[3], PathInfo{ LogicalId(3), TType::T_BOOL } },
            { paths[4], PathInfo{ LogicalId(4), TType::T_FLOAT } },
            { paths[5], PathInfo{ LogicalId(5), TType::T_FLOAT } },
        };
        std::map<ThriftPath, PathInfo> pathMap(
                pathInfoPairs.begin(), pathInfoPairs.end());
        size_t constexpr kNumLogicalIds = 6;

        std::map<LogicalId, int> successors;
        for (int i = 0; i < kNumFields; i++) {
            successors.emplace(LogicalId(i), 0);
        }

        std::string const encoderConfigStr =
                EncoderConfig(pathMap, successors).serialize();

        folly::F14FastMap<SingletonId, WriteStream> expectedSingletonStreams;
        for (int i = 0; SingletonId(i) < SingletonId::kNumSingletonIds; i++) {
            expectedSingletonStreams.emplace(
                    SingletonId(i), WriteStream{ SingletonIdToTType[i] });
        }

        if (std::is_same_v<Parser, CompactParser> && size == 0) {
            // TCompact doesn't transmit types for empty maps
            std::vector<TType> const expectedTypes{
                TType::T_LIST, TType::T_BOOL, TType::T_SET,  TType::T_I32,
                TType::T_MAP,  TType::T_MAP,  TType::T_STOP,
            };
            expectedSingletonStreams.at(SingletonId::kTypes) =
                    WriteStream{ TType::T_BYTE, expectedTypes };
        } else {
            std::vector<TType> const expectedTypes{
                TType::T_LIST,  TType::T_BOOL,  TType::T_SET,  TType::T_I32,
                TType::T_MAP,   TType::T_I32,   TType::T_BOOL, TType::T_MAP,
                TType::T_FLOAT, TType::T_FLOAT, TType::T_STOP,
            };
            expectedSingletonStreams.at(SingletonId::kTypes) =
                    WriteStream{ TType::T_BYTE, expectedTypes };
        }

        expectedSingletonStreams.at(SingletonId::kFieldDeltas) =
                WriteStream{ TType::T_I16,
                             std::vector<int16_t>(kNumFields, 1) };

        expectedSingletonStreams.at(SingletonId::kLengths) =
                WriteStream{ TType::T_U32,
                             std::vector<uint32_t>(kNumFields, size) };

        folly::F14FastMap<LogicalId, WriteStream> expectedVariableStreams;
        for (int i = 0; i < kNumLogicalIds; i++) {
            const std::pair<ThriftPath, PathInfo>& pathInfoPair =
                    pathInfoPairs.at(i);
            expectedVariableStreams.emplace(
                    pathInfoPair.second.id,
                    WriteStream{ pathInfoPair.second.type });
        }
        for (uint8_t const elem : testListBool) {
            expectedVariableStreams.at(LogicalId(0)).writeValue(elem);
        }
        for (auto const elem : testSetInt32) {
            expectedVariableStreams.at(LogicalId(1)).writeValue(elem);
        }
        for (auto const pair : testMapDiffTypes) {
            expectedVariableStreams.at(LogicalId(2)).writeValue(pair.first);
            expectedVariableStreams.at(LogicalId(3))
                    .writeValue((uint8_t)pair.second);
        }
        for (auto const pair : testMapSameTypes) {
            expectedVariableStreams.at(LogicalId(4)).writeValue(pair.first);
            expectedVariableStreams.at(LogicalId(5)).writeValue(pair.second);
        }

        // Empty -- this test does not use string data
        folly::F14FastMap<LogicalId, WriteStream>
                expectedVariableStringLengthStreams;

        WriteStreamSet const wss =
                thriftSplitIntoWriteStreams<Parser>(data, encoderConfigStr);

        EXPECT_EQ(wss.getSingletonStreams(), expectedSingletonStreams);
        EXPECT_EQ(wss.getVariableStreams(), expectedVariableStreams);
        EXPECT_EQ(
                wss.getVariableStringLengthStreams(),
                expectedVariableStringLengthStreams);
    }
}

} // namespace

TEST(SplitTest, EmptyConfigCompact)
{
    testEmptyConfig<CompactParser, CompactSerializer>();
}

TEST(SplitTest, EmptyConfigBinary)
{
    testEmptyConfig<BinaryParser, BinarySerializer>();
}

TEST(SplitTest, PrimitiveTypesCompact)
{
    testPrimitiveTypes<CompactParser, CompactSerializer>();
}

TEST(SplitTest, PrimitiveTypesBinary)
{
    testPrimitiveTypes<BinaryParser, BinarySerializer>();
}

TEST(SplitTest, CollectionTypesCompact)
{
    testCollectionTypes<CompactParser, CompactSerializer>();
}

TEST(SplitTest, CollectionTypesBinary)
{
    testCollectionTypes<BinaryParser, BinarySerializer>();
}

TEST(SplitTest, CompactAgainstBinary)
{
    std::string const configStr =
            buildValidEncoderConfig(kMinFormatVersionEncode);
    std::mt19937 genCompact(0xdeadbeef);
    std::mt19937 genBinary(0xdeadbeef);

    for (size_t _ = 0; _ < 100; _++) {
        std::string const compactData =
                generateRandomThrift<CompactSerializer>(genCompact);
        std::string const binaryData =
                generateRandomThrift<BinarySerializer>(genBinary);
        WriteStreamSet const wssCompact =
                thriftSplitIntoWriteStreams<CompactParser>(
                        compactData, configStr);
        WriteStreamSet const wssBinary =
                thriftSplitIntoWriteStreams<BinaryParser>(
                        binaryData, configStr);

        // Variable streams should be identical.
        EXPECT_EQ(
                wssCompact.getVariableStreams(),
                wssBinary.getVariableStreams());
        EXPECT_EQ(
                wssCompact.getVariableStringLengthStreams(),
                wssBinary.getVariableStringLengthStreams());

        // All singleton streams except kTypes should be identical.
        // kTypes may differ because of how TCompact encodes empty maps.
        //
        // Thankfully, this difference does not preclude sharing successor
        // graphs for the singleton streams. For both protocols, kTypes consists
        // of a list of TType enum values and will compress the same way. The
        // TCompact version simply omits types for empty maps.
        for (size_t i = 0; i < (size_t)SingletonId::kNumSingletonIds; i++) {
            if (SingletonId(i) != SingletonId::kTypes) {
                EXPECT_EQ(
                        wssCompact.getSingletonStreams().at(SingletonId(i)),
                        wssBinary.getSingletonStreams().at(SingletonId(i)));
            }
        }
    }
}

} // namespace zstrong::thrift::tests
