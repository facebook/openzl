// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <string>

#include "openzl/zl_unique_id.h"
#include "tests/datagen/DataGen.h"

#include "sha_vec.h"

using namespace ::testing;

namespace openzl::tests {

TEST(UniqueIDTest, Validity)
{
    ZL_UniqueID zero1;
    for (size_t i = 0; i < 32; ++i) {
        zero1.bytes[i] = 0;
    }
    ZL_UniqueID zero2 = ZL_UniqueID_zero();
    ASSERT_TRUE(ZL_UniqueID_eq(&zero1, &zero2));
    ASSERT_FALSE(ZL_UniqueID_eq(&zero1, nullptr));
    ASSERT_FALSE(ZL_UniqueID_eq(nullptr, &zero2));
    ASSERT_FALSE(ZL_UniqueID_eq(nullptr, nullptr));

    ZL_UniqueID nonzero;
    for (size_t i = 0; i < 32; ++i) {
        nonzero.bytes[i] = i;
    }
    ASSERT_NE(ZL_UniqueID_hash(&nonzero), 0);
    ASSERT_EQ(ZL_UniqueID_hash(nullptr), 0);

    ASSERT_TRUE(ZL_UniqueID_isValid(&nonzero));
    ASSERT_FALSE(ZL_UniqueID_isValid(&zero2));
    ASSERT_FALSE(ZL_UniqueID_isValid(nullptr));
}

TEST(UniqueIDTest, SanityCheck)
{
    std::vector<std::string_view> inputs = {
        "",
        "a",
        "\x12\x33\x01",
    };
    std::vector<std::string> expected = {
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb",
        "6ad5d0c73940b3ae59c55c2a975eec0a0b3f34316f305293341e3e143602d2fb"
    };

    std::vector<ZL_UniqueID> expectedSha(inputs.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        const auto& s = expected[i];
        for (size_t j = 0; j < 32; ++j) {
            expectedSha[i].bytes[j] =
                    (uint8_t)std::stoi(s.substr(j * 2, 2), nullptr, 16);
        }
    }

    for (size_t i = 0; i < inputs.size(); ++i) {
        ZL_UniqueID actualSha256 =
                ZL_UniqueID_computeSHA256(inputs[i].data(), inputs[i].size());
        auto expectedSha256 = expectedSha[i];
        ASSERT_TRUE(ZL_UniqueID_eq(&actualSha256, &expectedSha256));
    }
}

TEST(UniqueIDTest, NISTVectors)
{
    for (size_t i = 0; i < NIST_testVectorSet.nbVectors; ++i) {
        ZL_UniqueID expected;
        ZL_UniqueID_read(&expected, NIST_testVectorSet.vectors[i].expected);
        ZL_UniqueID actual = ZL_UniqueID_computeSHA256(
                NIST_testVectorSet.vectors[i].input,
                NIST_testVectorSet.vectors[i].inputLen);
        ASSERT_TRUE(ZL_UniqueID_eq(&actual, &expected));
    }
}

TEST(UniqueIDTest, SignificantBytes)
{
    ZL_UniqueID zero = ZL_UniqueID_zero();
    EXPECT_EQ(ZL_UniqueID_significantBytes(&zero), 0u);

    // Single byte at position 0
    ZL_UniqueID one = ZL_UniqueID_fromU32(0x00000042);
    EXPECT_EQ(ZL_UniqueID_significantBytes(&one), 1u);

    // Non-zero bytes 0-1
    ZL_UniqueID two = ZL_UniqueID_fromU16(UINT16_MAX);
    EXPECT_EQ(ZL_UniqueID_significantBytes(&two), 2u);

    // Non-zero at position 4 (bytes 0-4 significant, 5-31 zero)
    ZL_UniqueID five = ZL_UniqueID_fromU64(0x0100000000);
    EXPECT_EQ(ZL_UniqueID_significantBytes(&five), 5u);

    // Non-zero byte at the very end
    ZL_UniqueID full = ZL_UniqueID_zero();
    full.bytes[31]   = 0xFF;
    EXPECT_EQ(ZL_UniqueID_significantBytes(&full), 32u);

    // Sparse: bytes 0 and 3 non-zero, rest zero
    ZL_UniqueID sparse = ZL_UniqueID_fromU32(0x02000001);
    EXPECT_EQ(ZL_UniqueID_significantBytes(&sparse), 4u);
}

TEST(UniqueIDTest, IntRoundtrip)
{
    datagen::DataGen dg;
    std::vector<uint16_t> input16 =
            dg.randVector<uint16_t>("uint16s", 0, UINT16_MAX, 1000);
    for (auto i : input16) {
        ZL_UniqueID id = ZL_UniqueID_fromU16(i);
        auto r16       = ZL_UniqueID_toU16(&id);
        ASSERT_FALSE(ZL_RES_isError(r16));
        ASSERT_EQ(ZL_RES_value(r16), i);
    }

    std::vector<uint32_t> input32 =
            dg.randVector<uint32_t>("uint32s", 0, UINT32_MAX, 1000);
    for (auto i : input32) {
        ZL_UniqueID id = ZL_UniqueID_fromU32(i);
        auto r32       = ZL_UniqueID_toU32(&id);
        ASSERT_FALSE(ZL_RES_isError(r32));
        ASSERT_EQ(ZL_RES_value(r32), i);
    }

    std::vector<uint64_t> input64 =
            dg.randVector<uint64_t>("uint64s", 0, UINT64_MAX, 1000);
    for (auto i : input64) {
        ZL_UniqueID id = ZL_UniqueID_fromU64(i);
        auto r64       = ZL_UniqueID_toU64(&id);
        ASSERT_FALSE(ZL_RES_isError(r64));
        ASSERT_EQ(ZL_RES_value(r64), i);
    }
}

} // namespace openzl::tests
