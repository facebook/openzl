// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>
#include "data_compression/experimental/zstrong/tools/protobuf/tests/test_schema.pb.h"
#include "openzl/cpp/Compressor.hpp"
#include "tools/protobuf/ProtoDeserializer.h"
#include "tools/protobuf/ProtoSerializer.h"

using namespace ::testing;
using namespace openzl::protobuf;

namespace {
TestSchema genTestSchema()
{
    TestSchema obj;
    obj.set_optional_int32(1);
    obj.set_optional_int64(2);
    obj.set_optional_uint32(3);
    obj.set_optional_uint64(4);
    obj.set_optional_sint32(5);
    obj.set_optional_sint64(6);
    obj.set_optional_fixed32(7);
    obj.set_optional_fixed64(8);
    obj.set_optional_sfixed32(9);
    obj.set_optional_sfixed64(10);
    obj.set_optional_float(11);
    obj.set_optional_double(12);
    obj.set_optional_bool(true);
    obj.set_optional_string("string");
    obj.set_optional_bytes("bytes");

    // Nested Message
    obj.mutable_optional_nested()->set_optional_int32(13);

    // Enum
    obj.set_optional_enum(EnumSchema::ONE);

    // Repeated Fields
    obj.add_repeated_int32(1);
    obj.add_repeated_int32(2);
    obj.add_repeated_int32(3);

    obj.add_repeated_nested()->set_optional_int32(1);
    obj.add_repeated_nested()->set_optional_int32(2);
    obj.add_repeated_nested()->set_optional_int32(3);
    obj.add_repeated_nested()->set_optional_int32(4);

    obj.add_repeated_enum(EnumSchema::ZERO);
    obj.add_repeated_enum(EnumSchema::ONE);
    return obj;
}
} // namespace

TEST(TestProtoSerializer, BasicRoundTrip)
{
    auto obj = genTestSchema();

    // Serialize
    auto serializer = ProtoSerializer();
    auto serialized = serializer.serialize(obj);

    // Deserialize
    auto deserializer = ProtoDeserializer();
    auto deserialized = deserializer.deserialize<TestSchema>(serialized);

    // Check Round Trip
    EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equivalent(
            obj, deserialized));
}

TEST(TestProtoSerializer, CustomCompressor)
{
    ProtoSerializer serializer;

    openzl::Compressor compressor;
    compressor.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);
    serializer.setCompressor(std::move(compressor));
    ProtoDeserializer deserializer;

    auto obj          = genTestSchema();
    auto serialized   = serializer.serialize(obj);
    auto deserialized = deserializer.deserialize<TestSchema>(serialized);

    EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equivalent(
            obj, deserialized));

    openzl::Compressor store;
    store.selectStartingGraph(ZL_GRAPH_STORE);
    serializer.setCompressor(std::move(store));
    auto stored   = serializer.serialize(obj);
    auto destored = deserializer.deserialize<TestSchema>(serialized);

    EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equivalent(
            obj, destored));
    EXPECT_NE(stored.size(), serialized.size());
}
