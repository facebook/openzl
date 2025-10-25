// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "../DynamicMessageHelper.h"
#include "../DescriptorLoader.h"
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>
#include <filesystem>

using namespace openzl::protobuf;

namespace {

// Helper to get the path to test data files
std::string getTestDataPath(const std::string& filename)
{
    // Use __FILE__ to derive the test data directory
    std::string this_file = __FILE__;
    std::filesystem::path this_path(this_file);
    std::filesystem::path test_dir = this_path.parent_path();
    return (test_dir / filename).string();
}

} // namespace

TEST(TestDynamicMessages, CreateDynamicMessage)
{
    // Load a proto file to get descriptors
    DescriptorLoader loader;
    std::filesystem::path test_dir = std::filesystem::path(__FILE__).parent_path();
    loader.addProtoPath(test_dir.string());
    
    auto pool = loader.loadProtoFile("test_simple.proto");
    ASSERT_NE(pool, nullptr);
    
    // Create helper
    DynamicMessageHelper helper(pool.get());
    
    // Create a dynamic message
    auto message = helper.newMessage("test.SimpleMessage");
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(message->GetTypeName(), "test.SimpleMessage");
    
    // Verify we can access fields
    const auto* descriptor = message->GetDescriptor();
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->field_count(), 2);
    EXPECT_EQ(descriptor->field(0)->name(), "name");
    EXPECT_EQ(descriptor->field(1)->name(), "value");
}

TEST(TestDynamicMessages, RoundTripSerialization)
{
    // Load descriptors
    DescriptorLoader loader;
    std::filesystem::path test_dir = std::filesystem::path(__FILE__).parent_path();
    loader.addProtoPath(test_dir.string());
    auto pool = loader.loadProtoFile("test_simple.proto");
    
    DynamicMessageHelper helper(pool.get());
    
    // Create and populate a message
    auto message = helper.newMessage("test.SimpleMessage");
    ASSERT_NE(message, nullptr);
    
    const auto* reflection = message->GetReflection();
    const auto* descriptor = message->GetDescriptor();
    
    // Set field values
    const auto* name_field = descriptor->FindFieldByName("name");
    const auto* value_field = descriptor->FindFieldByName("value");
    ASSERT_NE(name_field, nullptr);
    ASSERT_NE(value_field, nullptr);
    
    reflection->SetString(message.get(), name_field, "test_name");
    reflection->SetInt32(message.get(), value_field, 42);
    
    // Serialize
    std::string serialized;
    ASSERT_TRUE(message->SerializeToString(&serialized));
    EXPECT_GT(serialized.size(), 0u);
    
    // Parse back
    auto parsed = helper.parseMessage("test.SimpleMessage", 
                                      serialized.data(), 
                                      serialized.size());
    ASSERT_NE(parsed, nullptr);
    
    // Verify values
    const auto* parsed_reflection = parsed->GetReflection();
    EXPECT_EQ(parsed_reflection->GetString(*parsed, name_field), "test_name");
    EXPECT_EQ(parsed_reflection->GetInt32(*parsed, value_field), 42);
}

TEST(TestDynamicMessages, FieldAccessAndModification)
{
    // Load descriptors
    DescriptorLoader loader;
    std::filesystem::path test_dir = std::filesystem::path(__FILE__).parent_path();
    loader.addProtoPath(test_dir.string());
    auto pool = loader.loadProtoFile("test_simple.proto");
    
    DynamicMessageHelper helper(pool.get());
    
    // Create a repeated field message
    auto message = helper.newMessage("test.SimpleRepeated");
    ASSERT_NE(message, nullptr);
    
    const auto* reflection = message->GetReflection();
    const auto* descriptor = message->GetDescriptor();
    
    // Find repeated fields
    const auto* values_field = descriptor->FindFieldByName("values");
    const auto* names_field = descriptor->FindFieldByName("names");
    ASSERT_NE(values_field, nullptr);
    ASSERT_NE(names_field, nullptr);
    
    // Add values to repeated fields
    reflection->AddInt32(message.get(), values_field, 10);
    reflection->AddInt32(message.get(), values_field, 20);
    reflection->AddInt32(message.get(), values_field, 30);
    
    reflection->AddString(message.get(), names_field, "first");
    reflection->AddString(message.get(), names_field, "second");
    
    // Verify field counts
    EXPECT_EQ(reflection->FieldSize(*message, values_field), 3);
    EXPECT_EQ(reflection->FieldSize(*message, names_field), 2);
    
    // Verify values
    EXPECT_EQ(reflection->GetRepeatedInt32(*message, values_field, 0), 10);
    EXPECT_EQ(reflection->GetRepeatedInt32(*message, values_field, 1), 20);
    EXPECT_EQ(reflection->GetRepeatedInt32(*message, values_field, 2), 30);
    
    EXPECT_EQ(reflection->GetRepeatedString(*message, names_field, 0), "first");
    EXPECT_EQ(reflection->GetRepeatedString(*message, names_field, 1), "second");
}

TEST(TestDynamicMessages, GetDescriptor)
{
    // Load descriptors
    DescriptorLoader loader;
    std::filesystem::path test_dir = std::filesystem::path(__FILE__).parent_path();
    loader.addProtoPath(test_dir.string());
    auto pool = loader.loadProtoFile("test_simple.proto");
    
    DynamicMessageHelper helper(pool.get());
    
    // Test getting descriptors
    const auto* simple_desc = helper.getDescriptor("test.SimpleMessage");
    ASSERT_NE(simple_desc, nullptr);
    EXPECT_EQ(simple_desc->name(), "SimpleMessage");
    EXPECT_EQ(simple_desc->full_name(), "test.SimpleMessage");
    
    const auto* repeated_desc = helper.getDescriptor("test.SimpleRepeated");
    ASSERT_NE(repeated_desc, nullptr);
    EXPECT_EQ(repeated_desc->name(), "SimpleRepeated");
    
    // Test non-existent type
    const auto* missing_desc = helper.getDescriptor("test.NonExistent");
    EXPECT_EQ(missing_desc, nullptr);
}

TEST(TestDynamicMessages, ParseInvalidData)
{
    // Load descriptors
    DescriptorLoader loader;
    std::filesystem::path test_dir = std::filesystem::path(__FILE__).parent_path();
    loader.addProtoPath(test_dir.string());
    auto pool = loader.loadProtoFile("test_simple.proto");
    
    DynamicMessageHelper helper(pool.get());
    
    // Try to parse garbage data
    const char garbage[] = "This is not valid protobuf data!";
    auto parsed = helper.parseMessage("test.SimpleMessage", garbage, sizeof(garbage));
    // parseMessage returns nullptr on parse failure
    EXPECT_EQ(parsed, nullptr);
}

TEST(TestDynamicMessages, NullPoolThrows)
{
    // Creating helper with null pool should throw
    EXPECT_THROW(
        DynamicMessageHelper helper(nullptr),
        std::invalid_argument);
}
