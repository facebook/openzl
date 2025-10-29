// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>
#include <filesystem>
#include "contrib/protobuf_dynamic/DescriptorLoader.h"

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

TEST(TestDescriptorLoader, LoadDescriptorFile)
{
    // Load the pre-generated descriptor file
    DescriptorLoader loader;
    auto pool = loader.loadDescriptorFile(getTestDataPath("test_simple.desc"));

    ASSERT_NE(pool, nullptr);

    // Verify we can find the SimpleMessage type
    const auto* desc = pool->FindMessageTypeByName("test.SimpleMessage");
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->name(), "SimpleMessage");
    EXPECT_EQ(desc->full_name(), "test.SimpleMessage");

    // Verify fields
    EXPECT_EQ(desc->field_count(), 2);
    EXPECT_EQ(desc->field(0)->name(), "name");
    EXPECT_EQ(desc->field(1)->name(), "value");

    // Verify we can find SimpleRepeated type
    const auto* repeated = pool->FindMessageTypeByName("test.SimpleRepeated");
    ASSERT_NE(repeated, nullptr);
    EXPECT_EQ(repeated->name(), "SimpleRepeated");
}

TEST(TestDescriptorLoader, LoadProtoFile)
{
    DescriptorLoader loader;
    // Add the directory containing the proto files (derived from __FILE__)
    std::filesystem::path test_dir =
            std::filesystem::path(__FILE__).parent_path();
    loader.addProtoPath(test_dir.string());

    // Load the .proto file
    auto pool = loader.loadProtoFile("test_simple.proto");

    ASSERT_NE(pool, nullptr);

    // Verify we can find the SimpleMessage type
    const auto* desc = pool->FindMessageTypeByName("test.SimpleMessage");
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->name(), "SimpleMessage");

    // Verify fields
    EXPECT_EQ(desc->field_count(), 2);
    EXPECT_EQ(desc->field(0)->name(), "name");
    EXPECT_EQ(desc->field(1)->name(), "value");
}

TEST(TestDescriptorLoader, ErrorOnMissingFile)
{
    DescriptorLoader loader;

    // Test missing descriptor file
    EXPECT_THROW(
            loader.loadDescriptorFile("/nonexistent/file.desc"),
            std::runtime_error);

    // Test missing proto file
    EXPECT_THROW(loader.loadProtoFile("nonexistent.proto"), std::runtime_error);
}

TEST(TestDescriptorLoader, ProtoWithImports)
{
    DescriptorLoader loader;
    // Add the directory containing the proto files (derived from __FILE__)
    std::filesystem::path test_dir =
            std::filesystem::path(__FILE__).parent_path();
    loader.addProtoPath(test_dir.string());

    // Load a proto file that has imports
    auto pool = loader.loadProtoFile("test_main.proto");

    ASSERT_NE(pool, nullptr);

    // Verify the main message type is loaded
    const auto* main_desc = pool->FindMessageTypeByName("test.MainMessage");
    ASSERT_NE(main_desc, nullptr);
    EXPECT_EQ(main_desc->field_count(), 3);

    // Verify the imported message type is also loaded
    const auto* imported_desc =
            pool->FindMessageTypeByName("test.ImportedMessage");
    ASSERT_NE(imported_desc, nullptr);
    EXPECT_EQ(imported_desc->field_count(), 2);

    // Verify the imported enum is loaded
    const auto* imported_enum = pool->FindEnumTypeByName("test.ImportedEnum");
    ASSERT_NE(imported_enum, nullptr);
    EXPECT_EQ(imported_enum->value_count(), 3);
}

TEST(TestDescriptorLoader, InvalidProtoSyntax)
{
    DescriptorLoader loader;
    std::filesystem::path test_dir =
            std::filesystem::path(__FILE__).parent_path();
    loader.addProtoPath(test_dir.string());

    // Should throw on parse error
    EXPECT_THROW(
            loader.loadProtoFile("test_invalid.proto"), std::runtime_error);
}
