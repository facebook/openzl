// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdint>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

extern "C" {
#include "openzl/codecs/bitSplit/decode_bitSplit_kernel.h"
#include "openzl/codecs/bitSplit/encode_bitSplit_kernel.h"
}

namespace openzl {
namespace tests {

// =============================================================================
// Kernel Unit Tests
// =============================================================================

class BitSplitKernelTest : public ::testing::Test {};

TEST_F(BitSplitKernelTest, OutputEltWidth)
{
    // 1-8 bits -> u8
    EXPECT_EQ(ZS_bitSplit_outputEltWidth(1), 1);
    EXPECT_EQ(ZS_bitSplit_outputEltWidth(7), 1);
    EXPECT_EQ(ZS_bitSplit_outputEltWidth(8), 1);

    // 9-16 bits -> u16
    EXPECT_EQ(ZS_bitSplit_outputEltWidth(9), 2);
    EXPECT_EQ(ZS_bitSplit_outputEltWidth(15), 2);
    EXPECT_EQ(ZS_bitSplit_outputEltWidth(16), 2);

    // 17-32 bits -> u32
    EXPECT_EQ(ZS_bitSplit_outputEltWidth(17), 4);
    EXPECT_EQ(ZS_bitSplit_outputEltWidth(31), 4);
    EXPECT_EQ(ZS_bitSplit_outputEltWidth(32), 4);

    // 33-64 bits -> u64
    EXPECT_EQ(ZS_bitSplit_outputEltWidth(33), 8);
    EXPECT_EQ(ZS_bitSplit_outputEltWidth(63), 8);
    EXPECT_EQ(ZS_bitSplit_outputEltWidth(64), 8);
}

TEST_F(BitSplitKernelTest, ValidateParams_Valid)
{
    uint8_t widths1[] = { 8 };
    size_t sum        = 0;
    EXPECT_EQ(ZS_bitSplit_validateParams(widths1, 1, 8, &sum), 0);
    EXPECT_EQ(sum, 8);

    uint8_t widths2[] = { 4, 8, 12, 8 };
    EXPECT_EQ(ZS_bitSplit_validateParams(widths2, 4, 32, &sum), 0);
    EXPECT_EQ(sum, 32);

    // Partial coverage is valid
    uint8_t widths3[] = { 3, 4, 5 };
    EXPECT_EQ(ZS_bitSplit_validateParams(widths3, 3, 32, &sum), 0);
    EXPECT_EQ(sum, 12);
}

TEST_F(BitSplitKernelTest, ValidateParams_Invalid)
{
    size_t sum = 0;

    // Zero width is invalid
    uint8_t widths1[] = { 0, 8 };
    EXPECT_NE(ZS_bitSplit_validateParams(widths1, 2, 16, &sum), 0);

    // Sum exceeds element width
    uint8_t widths2[] = { 16, 16, 16 };
    EXPECT_NE(ZS_bitSplit_validateParams(widths2, 3, 32, &sum), 0);
}

TEST_F(BitSplitKernelTest, TopBitsAreZero)
{
    // Full coverage - always true
    EXPECT_TRUE(ZS_bitSplit_topBitsAreZero(0xFF, 8));
    EXPECT_TRUE(ZS_bitSplit_topBitsAreZero(0xFFFFFFFF, 32));

    // Partial coverage with top bits zero
    EXPECT_TRUE(
            ZS_bitSplit_topBitsAreZero(0x0FFF, 12)); // 12 bits used, top 4 zero
    EXPECT_TRUE(ZS_bitSplit_topBitsAreZero(0x00000FFF, 12));

    // Partial coverage with top bits non-zero
    EXPECT_FALSE(ZS_bitSplit_topBitsAreZero(0x1FFF, 12)); // bit 12 is set
    EXPECT_FALSE(ZS_bitSplit_topBitsAreZero(0xFFFF, 12));
}

TEST_F(BitSplitKernelTest, EncodeDecodeSingleValue_8bit)
{
    // Split 8-bit value 0xA5 into [4, 4]
    uint8_t bitWidths[]   = { 4, 4 };
    size_t outputWidths[] = { 1, 1 };

    uint8_t out0, out1;
    void* outputs[] = { &out0, &out1 };

    ZS_bitSplitEncode64(0xA5, bitWidths, 2, outputs, outputWidths);

    EXPECT_EQ(out0, 0x5); // bits 0-3
    EXPECT_EQ(out1, 0xA); // bits 4-7

    // Decode back
    const void* inputs[] = { &out0, &out1 };
    size_t inputWidths[] = { 1, 1 };
    uint64_t result = ZS_bitSplitDecode64(bitWidths, 2, inputs, inputWidths, 0);
    EXPECT_EQ(result, 0xA5);
}

TEST_F(BitSplitKernelTest, EncodeDecodeSingleValue_32bit)
{
    // Split 32-bit value 0xDEADBEEF into [4, 8, 12, 8]
    uint8_t bitWidths[]   = { 4, 8, 12, 8 };
    size_t outputWidths[] = { 1, 1, 2, 1 }; // u8, u8, u16, u8

    uint8_t out0, out1, out3;
    uint16_t out2;
    void* outputs[] = { &out0, &out1, &out2, &out3 };

    ZS_bitSplitEncode64(0xDEADBEEF, bitWidths, 4, outputs, outputWidths);

    EXPECT_EQ(out0, 0xF);   // bits 0-3
    EXPECT_EQ(out1, 0xEE);  // bits 4-11
    EXPECT_EQ(out2, 0xADB); // bits 12-23
    EXPECT_EQ(out3, 0xDE);  // bits 24-31

    // Decode back
    const void* inputs[] = { &out0, &out1, &out2, &out3 };
    size_t inputWidths[] = { 1, 1, 2, 1 };
    uint64_t result = ZS_bitSplitDecode64(bitWidths, 4, inputs, inputWidths, 0);
    EXPECT_EQ(result, 0xDEADBEEF);
}

TEST_F(BitSplitKernelTest, EncodeDecodePartialCoverage)
{
    // Split 32-bit value with only 12 bits used (top 20 bits are zero)
    // Value: 0x00000ABC (bits 0-11 used)
    uint8_t bitWidths[]   = { 3, 4, 5 };
    size_t outputWidths[] = { 1, 1, 1 };

    uint8_t out0, out1, out2;
    void* outputs[] = { &out0, &out1, &out2 };

    uint32_t value = 0x00000ABC; // = 0b101010111100
    ZS_bitSplitEncode64(value, bitWidths, 3, outputs, outputWidths);

    // bits 0-2: 100 = 4
    // bits 3-6: 0111 = 7
    // bits 7-11: 10101 = 21
    EXPECT_EQ(out0, 0x4);
    EXPECT_EQ(out1, 0x7);
    EXPECT_EQ(out2, 0x15);

    // Decode back
    const void* inputs[] = { &out0, &out1, &out2 };
    size_t inputWidths[] = { 1, 1, 1 };
    uint64_t result = ZS_bitSplitDecode64(bitWidths, 3, inputs, inputWidths, 0);
    EXPECT_EQ(result, value);
}

TEST_F(BitSplitKernelTest, EncodeDecodeSingleStream)
{
    // Single stream: bitSplit(32) on 32-bit value (identity-like)
    uint8_t bitWidths[]   = { 32 };
    size_t outputWidths[] = { 4 }; // u32

    uint32_t out0;
    void* outputs[] = { &out0 };

    ZS_bitSplitEncode64(0x12345678, bitWidths, 1, outputs, outputWidths);
    EXPECT_EQ(out0, 0x12345678);

    const void* inputs[] = { &out0 };
    size_t inputWidths[] = { 4 };
    uint64_t result = ZS_bitSplitDecode64(bitWidths, 1, inputs, inputWidths, 0);
    EXPECT_EQ(result, 0x12345678);
}

TEST_F(BitSplitKernelTest, EncodeDecodeMultipleElements)
{
    // Test encoding/decoding array of values
    uint8_t bitWidths[]   = { 4, 4 };
    size_t outputWidths[] = { 1, 1 };

    std::vector<uint8_t> input = { 0x12, 0x34, 0x56, 0x78, 0x9A };
    std::vector<uint8_t> stream0(input.size());
    std::vector<uint8_t> stream1(input.size());

    // Encode all elements
    for (size_t i = 0; i < input.size(); i++) {
        void* outputs[] = { &stream0[i], &stream1[i] };
        ZS_bitSplitEncode64(input[i], bitWidths, 2, outputs, outputWidths);
    }

    // Verify and decode
    const void* inputs[] = { stream0.data(), stream1.data() };
    size_t inputWidths[] = { 1, 1 };
    for (size_t i = 0; i < input.size(); i++) {
        uint64_t result =
                ZS_bitSplitDecode64(bitWidths, 2, inputs, inputWidths, i);
        EXPECT_EQ(result, input[i]) << "Mismatch at index " << i;
    }
}

TEST_F(BitSplitKernelTest, EncodeDecodeWideValues)
{
    // 64-bit value split into [16, 16, 16, 16]
    uint8_t bitWidths[]   = { 16, 16, 16, 16 };
    size_t outputWidths[] = { 2, 2, 2, 2 }; // all u16

    uint16_t out0, out1, out2, out3;
    void* outputs[] = { &out0, &out1, &out2, &out3 };

    uint64_t value = 0x123456789ABCDEF0ULL;
    ZS_bitSplitEncode64(value, bitWidths, 4, outputs, outputWidths);

    EXPECT_EQ(out0, 0xDEF0);
    EXPECT_EQ(out1, 0x9ABC);
    EXPECT_EQ(out2, 0x5678);
    EXPECT_EQ(out3, 0x1234);

    const void* inputs[] = { &out0, &out1, &out2, &out3 };
    size_t inputWidths[] = { 2, 2, 2, 2 };
    uint64_t result = ZS_bitSplitDecode64(bitWidths, 4, inputs, inputWidths, 0);
    EXPECT_EQ(result, value);
}

} // namespace tests
} // namespace openzl
