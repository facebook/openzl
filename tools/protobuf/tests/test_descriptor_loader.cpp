// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>
#include <filesystem>
#include "tools/protobuf/DescriptorLoader.h"
#include "tools/protobuf/tests/utils.h"

using namespace openzl::protobuf;

TEST(TestDescriptorLoader, LoadDescriptorFile)
{
    // Load the pre-generated descriptor file
    DescriptorLoader loader;
    auto pool =
            loader.loadDescriptorFile(getTestDataPath("test_simple.desc.bin"));

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
    auto data_path = getTestDataPath("test_simple.proto");

    loader.addProtoPath(data_path.parent_path());

    // Load the .proto file
    auto pool = loader.loadProtoFile(data_path.filename());

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
    auto data_path = getTestDataPath("test_main.proto");
    loader.addProtoPath(data_path.parent_path());

    // Load a proto file that has imports
    auto pool = loader.loadProtoFile(data_path.filename());

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
    auto data_path = getTestDataPath("test_invalid.proto");
    loader.addProtoPath(data_path.parent_path());

    // Should throw on parse error
    EXPECT_THROW(
            loader.loadProtoFile(data_path.filename()), std::runtime_error);
}
