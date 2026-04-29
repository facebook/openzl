// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <string>

#include "openzl/zl_unique_id.h"

#include "sha_vec.h"

using namespace ::testing;

namespace openzl::tests {

TEST(Sha256Test, Validity)
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

TEST(Sha256Test, SanityCheck)
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

TEST(Sha256Test, NISTVectors)
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

} // namespace openzl::tests
