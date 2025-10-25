// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <google/protobuf/message.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>
#include <filesystem>
#include "../DescriptorLoader.h"
#include "../DynamicMessageHelper.h"
#include "../ProtoDeserializer.h"
#include "../ProtoSerializer.h"
#include "../serialization_utils.h"

using namespace openzl::protobuf;
using MessageDifferencer = google::protobuf::util::MessageDifferencer;

TEST(TestPolymorphicSerialize, SerializeDynamicMessage)
{
    // Load test schema
    DescriptorLoader loader;
    std::filesystem::path test_dir =
            std::filesystem::path(__FILE__).parent_path();
    loader.addProtoPath(test_dir.string());
    auto pool = loader.loadProtoFile("test_simple.proto");
    ASSERT_NE(pool, nullptr);

    DynamicMessageHelper helper(pool.get());

    // Create a dynamic message
    auto message = helper.newMessage("test.SimpleMessage");
    ASSERT_NE(message, nullptr);

    // Set field values
    const auto* reflection  = message->GetReflection();
    const auto* descriptor  = message->GetDescriptor();
    const auto* name_field  = descriptor->FindFieldByName("name");
    const auto* value_field = descriptor->FindFieldByName("value");

    reflection->SetString(message.get(), name_field, "dynamic_test");
    reflection->SetInt32(message.get(), value_field, 123);

    ProtoSerializer serializer;

    // Test all protocols with polymorphic serialize
    for (auto protocol : { Protocol::Proto, Protocol::ZL, Protocol::JSON }) {
        auto serialized = serialize(*message, protocol, serializer);
        EXPECT_GT(serialized.size(), 0u);

        // Deserialize back into a new dynamic message
        auto deserialized = helper.newMessage("test.SimpleMessage");
        ASSERT_NE(deserialized, nullptr);

        ProtoDeserializer deserializer;
        deserialize(serialized, protocol, deserializer, *deserialized);

        // Verify values
        const auto* deser_reflection = deserialized->GetReflection();
        EXPECT_EQ(
                deser_reflection->GetString(*deserialized, name_field),
                "dynamic_test");
        EXPECT_EQ(deser_reflection->GetInt32(*deserialized, value_field), 123);
    }
}

TEST(TestPolymorphicSerialize, RepeatedFieldsDynamic)
{
    // Load schema with repeated fields
    DescriptorLoader loader;
    std::filesystem::path test_dir =
            std::filesystem::path(__FILE__).parent_path();
    loader.addProtoPath(test_dir.string());
    auto pool = loader.loadProtoFile("test_simple.proto");

    DynamicMessageHelper helper(pool.get());
    auto message = helper.newMessage("test.SimpleRepeated");
    ASSERT_NE(message, nullptr);

    // Add repeated values
    const auto* reflection   = message->GetReflection();
    const auto* descriptor   = message->GetDescriptor();
    const auto* values_field = descriptor->FindFieldByName("values");
    const auto* names_field  = descriptor->FindFieldByName("names");

    reflection->AddInt32(message.get(), values_field, 100);
    reflection->AddInt32(message.get(), values_field, 200);
    reflection->AddInt32(message.get(), values_field, 300);

    reflection->AddString(message.get(), names_field, "first");
    reflection->AddString(message.get(), names_field, "second");

    ProtoSerializer serializer;
    ProtoDeserializer deserializer;

    // Test round-trip
    auto serialized = serialize(*message, Protocol::ZL, serializer);
    EXPECT_GT(serialized.size(), 0u);

    auto deserialized = helper.newMessage("test.SimpleRepeated");
    deserialize(serialized, Protocol::ZL, deserializer, *deserialized);

    // Verify repeated fields
    const auto* deser_reflection = deserialized->GetReflection();
    EXPECT_EQ(deser_reflection->FieldSize(*deserialized, values_field), 3);
    EXPECT_EQ(deser_reflection->FieldSize(*deserialized, names_field), 2);

    EXPECT_EQ(
            deser_reflection->GetRepeatedInt32(*deserialized, values_field, 0),
            100);
    EXPECT_EQ(
            deser_reflection->GetRepeatedInt32(*deserialized, values_field, 2),
            300);
    EXPECT_EQ(
            deser_reflection->GetRepeatedString(*deserialized, names_field, 1),
            "second");
}
