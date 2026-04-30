// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "tests/datagen/registry_records/ZigzagRegistryRecord.h"

using namespace ::testing;

namespace openzl::tests::datagen {

TEST(RegistryTest, Test)
{
    auto record = ZigzagRegistryRecord();
    auto data   = record("e");
    EXPECT_EQ(data, "\000\001\002\003\004\005\006\007\008\009\010\011\012\013");
    data = record("i");
    EXPECT_EQ(data, "\255\254\253\252\251\250\249\248\247\246\245\244\243\242");
    EXPECT_EQ(record.size(), 2);
}

} // namespace openzl::tests::datagen
