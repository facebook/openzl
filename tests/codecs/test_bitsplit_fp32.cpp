// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/openzl.hpp"
#include "tests/codecs/test_codec.h"

namespace openzl {
namespace tests {

class BitsplitFP32Test : public CodecTest {
   public:
    /**
     * Tests round-trip compression/decompression using bitsplit_fp32.
     *
     * @param input Float input data (4-byte elements required)
     */
    void testBitsplitFP32RoundTrip(const std::vector<float>& input)
    {
        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);

        // bitsplit_FP32 is parameter-free, use ZL_NODE_BITSPLIT_FP32 directly
        auto graph = compressor_.buildStaticGraph(
                ZL_NODE_BITSPLIT_FP32, { graphs::Compress{}() });
        compressor_.selectStartingGraph(graph);

        Input numericInput =
                Input::refNumeric(poly::span<const float>{ input });
        testRoundTrip(numericInput);
    }
};

TEST_F(BitsplitFP32Test, RoundTrip_Floats)
{
    std::vector<float> input(1000);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = (float)i * 0.1f;
    }
    testBitsplitFP32RoundTrip(input);
}

TEST_F(BitsplitFP32Test, RoundTrip_AllZero_32bit)
{
    std::vector<float> input(1000, 0);
    testBitsplitFP32RoundTrip(input);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(BitsplitFP32Test, RoundTrip_EmptyInput)
{
    std::vector<float> input;
    testBitsplitFP32RoundTrip(input);
}

TEST_F(BitsplitFP32Test, RoundTrip_SingleElement)
{
    std::vector<float> input{ 999.999f };
    testBitsplitFP32RoundTrip(input);
}

TEST_F(BitsplitFP32Test, RoundTrip_NaN)
{
    std::vector<float> input{ std::numeric_limits<float>::quiet_NaN() };
    testBitsplitFP32RoundTrip(input);
}

TEST_F(BitsplitFP32Test, RoundTrip_Infinity)
{
    std::vector<float> input{
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    testBitsplitFP32RoundTrip(input);
}

TEST_F(BitsplitFP32Test, RoundTrip_NegativeZero)
{
    std::vector<float> input{ -0.0f };
    testBitsplitFP32RoundTrip(input);
}

TEST_F(BitsplitFP32Test, RoundTrip_FloatMin)
{
    // Smallest normal float (exponent = 1, mantissa = 0)
    std::vector<float> input{ std::numeric_limits<float>::min() };
    testBitsplitFP32RoundTrip(input);
}

TEST_F(BitsplitFP32Test, RoundTrip_FloatMax)
{
    // Largest finite float (exponent = 254, mantissa = all ones)
    std::vector<float> input{ std::numeric_limits<float>::max() };
    testBitsplitFP32RoundTrip(input);
}

TEST_F(BitsplitFP32Test, RoundTrip_Subnormals)
{
    // Subnormal floats have exponent = 0 and nonzero mantissa
    std::vector<float> input(1000);
    float subnormal = std::numeric_limits<float>::denorm_min();
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = subnormal * (float)(i + 1);
    }
    testBitsplitFP32RoundTrip(input);
}
} // namespace tests
} // namespace openzl
