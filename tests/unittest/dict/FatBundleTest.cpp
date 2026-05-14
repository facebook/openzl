// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <vector>

#include "openzl/dict/bundle.h"
#include "openzl/dict/dict.h"

#include "DictTestHelpers.h"

TEST(FatBundleTest, PackDstTooSmall)
{
    auto dict = buildPackedDict(10);

    const void* dicts[] = { dict.data() };
    size_t dictSizes[]  = { dict.size() };
    size_t needed = ZL_BUNDLE_HEADER_SIZE + ZL_UNIQUE_ID_SIZE + dict.size();

    std::vector<uint8_t> dst(needed - 1);
    ZL_Report r = ZL_DictBundle_packFatBundle(
            dst.data(), dst.size(), dicts, dictSizes, 1);
    ASSERT_TRUE(ZL_isError(r));
    EXPECT_EQ(ZL_errorCode(r), ZL_ErrorCode_dstCapacity_tooSmall);
}
