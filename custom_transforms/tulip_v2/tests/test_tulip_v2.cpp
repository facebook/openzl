// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <random>

#include <gtest/gtest.h>

#include "custom_transforms/tulip_v2/encode_tulip_v2.h"
#include "custom_transforms/tulip_v2/tests/tulip_v2_data_utils.h"

namespace openzl::tulip_v2::tests {
using namespace ::testing;

TEST(TulipV2Test, RoundTripDefaultSuccessors)
{
    std::mt19937 gen(0xdeadbeef);
    for (size_t n = 0; n < 10; ++n) {
        auto data         = generateTulipV2(n, gen);
        auto compressed   = compressTulipV2(data, {});
        auto decompressed = decompressTulipV2(compressed);
        ASSERT_TRUE(data == decompressed);
    }
}
} // namespace openzl::tulip_v2::tests
