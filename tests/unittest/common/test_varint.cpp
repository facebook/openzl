// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/shared/varint.h"

namespace openzl {
namespace tests {
namespace {

TEST(VarintTest, testVarintSize)
{
    EXPECT_EQ(ZL_varintSize(0x7F), 1u);
    EXPECT_EQ(ZL_varintSize(0xFF), 2u);
    EXPECT_EQ(ZL_varintSize(0xFFFFFFFFFFFFFFFFull), 10u);
}

TEST(VarintTest, testVarintRoundTrip)
{
    uint8_t buf[ZL_VARINT_LENGTH_64];
    for (int shift = 0; shift < 64; ++shift) {
        uint64_t const pow2 = (uint64_t)1 << shift;
        for (uint64_t value : { pow2, pow2 - 1 }) {
            size_t const varintSize = ZL_varintSize(value);
            ASSERT_EQ(varintSize, ZL_varintEncode(value, buf));
            uint8_t const* ptr = buf;
            ZL_RESULT_OF(uint64_t)
            r = ZL_varintDecode(&ptr, buf + sizeof(buf));
            ASSERT_FALSE(ZL_RES_isError(r));
            uint64_t decoded = ZL_RES_value(r);
            ASSERT_EQ((size_t)(ptr - buf), varintSize);
            ASSERT_EQ(decoded, value);

            // Copy to exactly sized buffer to ensure no over-run
            std::unique_ptr<uint8_t[]> mem(new uint8_t[varintSize]);
            memcpy(mem.get(), buf, varintSize);
            ptr = mem.get();
            r   = ZL_varintDecode(&ptr, ptr + varintSize);
            ASSERT_FALSE(ZL_RES_isError(r));
            decoded = ZL_RES_value(r);
            ASSERT_EQ((size_t)(ptr - mem.get()), varintSize);
            ASSERT_EQ(decoded, value);
        }
    }
}

TEST(VarintTest, testVarintDecode)
{
    uint8_t buf[2 * ZL_VARINT_LENGTH_64];
    uint8_t const* ptr = nullptr;
    // Ensure NULL works
    {
        ZL_RESULT_OF(uint64_t) r = ZL_varintDecode(&ptr, nullptr);
        ASSERT_TRUE(ZL_RES_isError(r));
    }
    // Ensure ptr isn't changed upon failure
    ASSERT_EQ(ptr, nullptr);

    memset(buf, 0xFF, sizeof(buf));
    ptr = buf;
    // Test bad varints of every length
    for (size_t len = 0; len <= sizeof(buf); ++len) {
        ZL_RESULT_OF(uint64_t) r = ZL_varintDecode(&ptr, ptr + len);
        ASSERT_TRUE(ZL_RES_isError(r));
        ASSERT_EQ(ptr, buf);
    }
}

TEST(VarintTest, testVarintFast)
{
    static_assert(ZL_VARINT_FAST_OVERWRITE_64 >= ZL_VARINT_FAST_OVERWRITE_32);
    uint8_t buf[ZL_VARINT_FAST_OVERWRITE_64];
    for (int shift = 0; shift < 64; ++shift) {
        uint64_t const pow2 = (uint64_t)1 << shift;
        for (uint64_t value : { pow2, pow2 - 1 }) {
            {
                size_t const varintSize = ZL_varintSize((uint32_t)value);
                ASSERT_EQ(
                        varintSize,
                        ZL_varintEncode32Fast((uint32_t)value, buf));
                uint8_t const* ptr = buf;
                ZL_RESULT_OF(uint64_t)
                r = ZL_varintDecode(&ptr, buf + sizeof(buf));
                ASSERT_FALSE(ZL_RES_isError(r));
                uint64_t decoded = ZL_RES_value(r);
                ASSERT_EQ((size_t)(ptr - buf), varintSize);
                ASSERT_EQ(decoded, (uint32_t)value);
            }
            {
                size_t const varintSize = ZL_varintSize(value);
                ASSERT_EQ(varintSize, ZL_varintEncode64Fast(value, buf));
                uint8_t const* ptr = buf;
                ZL_RESULT_OF(uint64_t)
                r = ZL_varintDecode(&ptr, buf + sizeof(buf));
                ASSERT_FALSE(ZL_RES_isError(r));
                uint64_t decoded = ZL_RES_value(r);
                ASSERT_EQ((size_t)(ptr - buf), varintSize);
                ASSERT_EQ(decoded, value);
            }
        }
    }
}

TEST(VarintTest, VarintStrictEncodeDecode)
{
    // Only the high bytes are really interesting, try all combinations of the
    // high 2 bytes.
    for (uint64_t v = 0; v < 65536; ++v) {
        for (int shift = 0; shift <= (64 - 16); shift += 8) {
            uint64_t const value = v << shift;
            uint8_t dst[ZL_VARINT_FAST_OVERWRITE_64];
            size_t const size = ZL_varintEncode64Fast(value, dst);
            for (auto capacity : { size, (size_t)ZL_VARINT_LENGTH_64 }) {
                uint8_t const* ptr = dst;
                auto result = ZL_varintDecode64Strict(&ptr, ptr + capacity);
                ASSERT_EQ((size_t)(ptr - dst), size);
                ASSERT_FALSE(ZL_RES_isError(result));
                ASSERT_EQ(ZL_RES_value(result), value);
            }
            if (value == (uint32_t)value) {
                for (auto capacity : { size, (size_t)ZL_VARINT_LENGTH_32 }) {
                    uint8_t const* ptr = dst;
                    auto result = ZL_varintDecode64Strict(&ptr, ptr + capacity);
                    ASSERT_EQ((size_t)(ptr - dst), size);
                    ASSERT_FALSE(ZL_RES_isError(result));
                    ASSERT_EQ(ZL_RES_value(result), value);
                }
            }
        }
    }
}

TEST(VarintTest, VarintStrictDecodeEncode)
{
    // Only the high bytes are really interesting, try all combinations of the
    // high 2 bytes.
    for (uint64_t v = 0; v < 65536; ++v) {
        for (size_t offset = 0; offset <= 8; ++offset) {
            uint8_t src[10];
            memset(src, 0x80, sizeof(src));
            src[offset + 0] = (uint8_t)(v >> 0);
            src[offset + 1] = (uint8_t)(v >> 8);
            for (auto capacity : { offset + 2, (size_t)ZL_VARINT_LENGTH_64 }) {
                uint8_t const* ptr = src;
                auto value = ZL_varintDecode64Strict(&ptr, ptr + capacity);
                if (!ZL_RES_isError(value)) {
                    ASSERT_LE(ptr, src + capacity);
                    uint8_t dst[ZL_VARINT_FAST_OVERWRITE_64];
                    size_t const size =
                            ZL_varintEncode64Fast(ZL_RES_value(value), dst);
                    ASSERT_EQ(size, (size_t)(ptr - src));
                    ASSERT_EQ(memcmp(src, dst, size), 0);
                }
            }
            for (auto capacity : { offset + 2, (size_t)ZL_VARINT_LENGTH_32 }) {
                uint8_t const* ptr = src;
                auto result = ZL_varintDecode32Strict(&ptr, ptr + capacity);
                if (!ZL_RES_isError(result)) {
                    uint32_t const value = (uint32_t)ZL_RES_value(result);
                    ASSERT_EQ(value, ZL_RES_value(result));
                    ASSERT_LE(ptr, src + capacity);
                    uint8_t dst[ZL_VARINT_FAST_OVERWRITE_32];
                    size_t const size = ZL_varintEncode32Fast(value, dst);
                    ASSERT_EQ(size, (size_t)(ptr - src));
                    ASSERT_EQ(memcmp(src, dst, size), 0);
                }
            }
        }
    }
}

} // namespace
} // namespace tests
} // namespace openzl
