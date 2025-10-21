// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "tools/protobuf/StringReader.h"
#include "tools/protobuf/StringWriter.h"

using namespace ::testing;
using namespace openzl::protobuf;

TEST(TestUtils, BasicStringWriter)
{
    StringWriter writer;

    writer.write("hello");
    writer.write(" ");
    writer.write("world");
    auto str = writer.move();
    EXPECT_EQ(str, "hello world");

    writer.writeLE<int32_t>(4);
    writer.writeLE<int16_t>(2);
    writer.writeLE<bool>(true);
    str             = writer.move();
    char expected[] = {
        0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01,
    };
    EXPECT_EQ(str.size(), sizeof(expected));
    EXPECT_EQ(memcmp(str.data(), expected, sizeof(expected)), 0);
}

TEST(TestUtils, StringWriterLargeString)
{
    StringWriter w1;
    std::string s(1 << 16, 'a');
    w1.write(s);
    auto str = w1.move();
    EXPECT_EQ(str, s);
    EXPECT_EQ(str.size(), 1 << 16);
}

TEST(TestUtils, BasicStringReader)
{
    std::string s;

    auto s1 = "hello world";
    StringReader r1(s1);
    r1.read(s, 5);
    EXPECT_EQ(s, "hello");
    EXPECT_FALSE(r1.atEnd());
    r1.read(s, 6);
    EXPECT_EQ(s, " world");
    EXPECT_TRUE(r1.atEnd());

    char expected[] = {
        0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01,
    };
    auto s2 = std::string(expected, sizeof(expected));
    StringReader r2(s2);
    int32_t i32;
    int16_t i16;
    bool b;
    r2.readLE(i32);
    EXPECT_EQ(i32, 4);
    EXPECT_FALSE(r2.atEnd());
    r2.readLE(i16);
    EXPECT_EQ(i16, 2);
    EXPECT_FALSE(r2.atEnd());
    r2.readLE(b);
    EXPECT_TRUE(r2.atEnd());
    EXPECT_THROW(r2.readLE(b), std::out_of_range);
}
