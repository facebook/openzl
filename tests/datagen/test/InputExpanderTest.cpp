// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "tests/datagen/InputExpander.h"

using namespace ::testing;

namespace openzl::tests::datagen {

TEST(InputExpanderTest, expandSerialWithMutation)
{
    std::string str =
            "lsakfjdslfkdsjisjfisjgeriogjds;glkvdjfgiasjer;iosaljg;saldkfjdslg;kjdsl;kdsjfkljd";
    std::string expanded =
            InputExpander::expandSerialWithMutation(str, 32 * 1000000);

    EXPECT_GE(expanded.size(), 32 * 1000000);
    for (size_t i = 0; i < expanded.size(); ++i) {
        ASSERT_NE(expanded[i], '\0');
    }
}

} // namespace openzl::tests::datagen
