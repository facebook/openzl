// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <array>
#include <limits>
#include <random>

#include <gtest/gtest.h>

#include "openzl/codecs/entropy/deprecated/common_entropy.h"
#include "openzl/common/cursor.h"
#include "openzl/common/speed.h"
#include "openzl/zl_errors.h"
#include "tests/datagen/random_producer/compat_uniform_distribution.h"
#include "tests/utils.h"

namespace {
template <typename Int>
void checkDecode(
        std::string const& encoded,
        std::vector<Int> const& data,
        ZS_Entropy_DecodeParameters const* optionalParams = nullptr)
{
    ZL_Report const decodedSize = ZS_Entropy_getDecodedSize(
            encoded.data(), encoded.size(), sizeof(Int));
    ASSERT_ZS_VALID(decodedSize);
    ASSERT_EQ(ZL_validResult(decodedSize), data.size());
    std::vector<Int> decoded(data.size());
    ZS_Entropy_DecodeParameters params = ZS_Entropy_DecodeParameters_default();
    ZL_RC rc = ZL_RC_wrap((uint8_t const*)encoded.data(), encoded.size());
    ZL_Report const ret = ZS_Entropy_decode(
            decoded.data(),
            decoded.size(),
            &rc,
            sizeof(Int),
            optionalParams != nullptr ? optionalParams : &params);
    if (ZL_isError(ret))
        throw std::runtime_error("Decoding failed");
    ASSERT_EQ(ZL_RC_avail(&rc), (size_t)0);
    ASSERT_EQ(ZL_validResult(ret), data.size());
    ASSERT_EQ(decoded, data);
}

template <typename Int>
std::string encodeWithParams(
        std::vector<Int> const& data,
        ZS_Entropy_EncodeParameters const* params)
{
    std::string encoded;
    encoded.resize(ZS_Entropy_encodedSizeBound(data.size(), sizeof(Int)));
    ZL_LOG(V,
           "bsize = %zu (ds = %zu | es = %zu)",
           encoded.size(),
           data.size(),
           sizeof(Int));
    ZL_WC wc = ZL_WC_wrap((uint8_t*)encoded.data(), encoded.size());
    if (ZL_isError(ZS_Entropy_encode(
                &wc, data.data(), data.size(), sizeof(Int), params)))
        throw std::runtime_error("Entropy encoding failed");
    encoded.resize(ZL_WC_size(&wc));
    ZL_LOG(V, "esize = %zu", encoded.size());
    return encoded;
}

template <typename Int>
std::string encodeWithTypes(
        std::vector<Int> const& data,
        ZS_Entropy_TypeMask_e types,
        bool allowAvx2Huffman = false,
        uint8_t fseNbStates   = 0)
{
    ZS_Entropy_EncodeParameters params = {
        .allowedTypes = types,
        .encodeSpeed  = ZL_EncodeSpeed_fromBaseline(ZL_EncodeSpeedBaseline_any),
        .decodeSpeed  = ZL_DecodeSpeed_fromBaseline(ZL_DecodeSpeedBaseline_any),
        .precomputedHistogram = nullptr,
        .cardinalityEstimate  = 0,
        .maxValueUpperBound   = 0,
        .maxTableLog          = 0,
        .allowAvx2Huffman     = allowAvx2Huffman,
        .fseNbStates          = fseNbStates,
        .blockSplits          = nullptr,
        .tableManager         = nullptr,
    };
    return encodeWithParams(data, &params);
}

template <typename Int>
void testRoundTripRaw()
{
    auto const min        = std::numeric_limits<Int>::min();
    auto const max        = std::numeric_limits<Int>::max();
    std::vector<Int> data = { min, max, min + 1, max - 1, (Int)-1, 0, 1 };
    auto const encoded    = encodeWithTypes(data, ZS_Entropy_TypeMask_raw);
    checkDecode(encoded, data);
    ASSERT_EQ(
            ZL_validResult(ZS_Entropy_getType(encoded.data(), encoded.size())),
            ZS_Entropy_Type_raw);
}

template <typename Int>
void testRoundTripConstant()
{
    for (size_t i = 1; i < 10000; i *= 2) {
        std::vector<Int> data(i, 0x42);
        auto const encoded =
                encodeWithTypes(data, ZS_Entropy_TypeMask_constant);
        checkDecode(encoded, data);
        ASSERT_EQ(
                ZL_validResult(
                        ZS_Entropy_getType(encoded.data(), encoded.size())),
                ZS_Entropy_Type_constant);
    }
}

template <typename Int>
void testRoundTripConstantOrRaw()
{
    std::vector<Int> data(10, 0x35);
    auto const allowedTypes =
            (ZS_Entropy_TypeMask_e)(ZS_Entropy_TypeMask_raw
                                    | ZS_Entropy_TypeMask_constant);
    auto encoded = encodeWithTypes(data, allowedTypes);
    checkDecode(encoded, data);
    ASSERT_EQ(
            ZL_validResult(ZS_Entropy_getType(encoded.data(), encoded.size())),
            ZS_Entropy_Type_constant);
    ASSERT_EQ(encoded.size(), 1 + sizeof(Int));

    data.push_back(0x42);
    encoded = encodeWithTypes(data, allowedTypes);
    checkDecode(encoded, data);
    ASSERT_EQ(
            ZL_validResult(ZS_Entropy_getType(encoded.data(), encoded.size())),
            ZS_Entropy_Type_raw);
    ASSERT_EQ(encoded.size(), 1 + data.size() * sizeof(Int));
}

template <typename Int>
void testRoundTripHuf(bool allowNonHuf, bool allowAvx2Huffman)
{
    size_t const maxSymbol =
            std::min<size_t>(std::numeric_limits<Int>::max(), (1u << 12) - 1);
    openzl::tests::datagen::compat_binomial_distribution<Int> dist(
            (Int)maxSymbol, 0.5);
    std::mt19937 gen(42);
    std::vector<Int> data;
    size_t const minSize = allowNonHuf ? 0 : 1000;
    int mask             = ZS_Entropy_TypeMask_huf;
    if (allowNonHuf) {
        mask |= ZS_Entropy_TypeMask_raw;
        mask |= ZS_Entropy_TypeMask_constant;
    }
    for (size_t size = minSize; size <= ZS_HUF_MAX_BLOCK_SIZE;
         size        = (size + 1) * 2) {
        data.reserve(size);
        while (data.size() < size) {
            data.push_back(dist(gen));
        }
        auto const encoded = encodeWithTypes(
                data, (ZS_Entropy_TypeMask_e)mask, allowAvx2Huffman);
        ZS_Entropy_DecodeParameters params =
                ZS_Entropy_DecodeParameters_default();
        checkDecode(encoded, data, &params);
        if (!allowNonHuf) {
            ASSERT_EQ(
                    ZL_validResult(
                            ZS_Entropy_getType(encoded.data(), encoded.size())),
                    ZS_Entropy_Type_huf);
        }
    }
}

template <typename Int>
void testRoundTripFse(bool allowNonFse, uint8_t nbstates)
{
    openzl::tests::datagen::compat_geometric_distribution<Int> dist;
    std::mt19937 gen(42);
    std::vector<Int> data;
    size_t const minSize = allowNonFse ? 0 : 1000;
    int mask             = ZS_Entropy_TypeMask_fse;
    if (allowNonFse) {
        mask |= ZS_Entropy_TypeMask_raw;
        mask |= ZS_Entropy_TypeMask_constant;
    }
    for (size_t size = minSize; size <= ZS_HUF_MAX_BLOCK_SIZE;
         size        = (size + 1) * 2) {
        data.reserve(size);
        while (data.size() < size) {
            data.push_back(dist(gen));
        }
        auto const encoded = encodeWithTypes(
                data, (ZS_Entropy_TypeMask_e)mask, false, nbstates);
        ZS_Entropy_DecodeParameters decodeParams =
                ZS_Entropy_DecodeParameters_default();
        decodeParams.fseNbStates = nbstates;
        checkDecode(encoded, data, &decodeParams);
        if (!allowNonFse) {
            ASSERT_EQ(
                    ZL_validResult(
                            ZS_Entropy_getType(encoded.data(), encoded.size())),
                    ZS_Entropy_Type_fse);
        }
    }
}

template <typename Int>
void testRoundTripBit(bool allowNonBit)
{
    std::mt19937 gen(42);
    size_t const size = 100000;
    for (size_t numBits = 1; numBits < sizeof(Int) * 8; ++numBits) {
        openzl::tests::datagen::compat_uniform_int_distribution<Int> dist(
                0, (Int)((1 << numBits) - 1));
        std::vector<Int> data;
        data.reserve(size);
        while (data.size() < size) {
            data.push_back(dist(gen));
        }
        int const mask     = allowNonBit
                    ? (ZS_Entropy_TypeMask_all & ~ZS_Entropy_TypeMask_multi)
                    : ZS_Entropy_TypeMask_bit;
        auto const encoded = encodeWithTypes(data, (ZS_Entropy_TypeMask_e)mask);
        checkDecode(encoded, data);
        // We should always select bit, we have a uniform distribution
        ASSERT_EQ(
                ZL_validResult(
                        ZS_Entropy_getType(encoded.data(), encoded.size())),
                ZS_Entropy_Type_bit);
    }
}

TEST(EntropyTest, RawRoundTrip)
{
    testRoundTripRaw<uint8_t>();
    testRoundTripRaw<int8_t>();
    testRoundTripRaw<uint16_t>();
    testRoundTripRaw<int16_t>();
    testRoundTripRaw<uint32_t>();
    testRoundTripRaw<int32_t>();
    testRoundTripRaw<uint64_t>();
    testRoundTripRaw<int64_t>();
}

TEST(EntropyTest, ConstantRoundTrip)
{
    testRoundTripConstant<uint8_t>();
    testRoundTripConstant<int8_t>();
    testRoundTripConstant<uint16_t>();
    testRoundTripConstant<int16_t>();
    testRoundTripConstant<uint32_t>();
    testRoundTripConstant<int32_t>();
    testRoundTripConstant<uint64_t>();
    testRoundTripConstant<int64_t>();
}

TEST(EntropyTest, ConstantOrRawRoundTrip)
{
    testRoundTripConstantOrRaw<uint8_t>();
    testRoundTripConstantOrRaw<int8_t>();
    testRoundTripConstantOrRaw<uint16_t>();
    testRoundTripConstantOrRaw<int16_t>();
    testRoundTripConstantOrRaw<uint32_t>();
    testRoundTripConstantOrRaw<int32_t>();
    testRoundTripConstantOrRaw<uint64_t>();
    testRoundTripConstantOrRaw<int64_t>();
}

TEST(EntropyTest, HufOnlyRoundTrip)
{
    // Normal huffman
    testRoundTripHuf<uint8_t>(false, false);
    testRoundTripHuf<uint16_t>(false, false);
    // Avx2 Huffman
    // testRoundTripHuf<uint8_t>(false, true);
    // testRoundTripHuf<uint16_t>(false, true);
}

TEST(EntropyTest, HufAvx2RoundTrip)
{
    openzl::tests::datagen::compat_binomial_distribution<uint8_t> dist;
    std::mt19937 gen(42);
    std::vector<uint8_t> data;
    size_t const size = ZS_HUF_MAX_BLOCK_SIZE;
    data.reserve(size);
    while (data.size() < size) {
        data.push_back(dist(gen));
    }

    ZS_Entropy_EncodeParameters eparams = {
        .allowedTypes = ZS_Entropy_TypeMask_huf,
        .encodeSpeed  = ZL_EncodeSpeed_fromBaseline(ZL_EncodeSpeedBaseline_any),
        .decodeSpeed  = ZL_DecodeSpeed_fromBaseline(ZL_DecodeSpeedBaseline_any),
        .precomputedHistogram = nullptr,
        .cardinalityEstimate  = 0,
        .maxValueUpperBound   = 0,
        .maxTableLog          = 0,
        .allowAvx2Huffman     = true,
        .blockSplits          = nullptr,
        .tableManager         = nullptr,
    };
    auto const encoded = encodeWithParams(data, &eparams);

    ZS_Entropy_DecodeParameters dparams = ZS_Entropy_DecodeParameters_default();
    checkDecode(encoded, data, &dparams);
    ASSERT_EQ(
            ZL_validResult(ZS_Entropy_getType(encoded.data(), encoded.size())),
            ZS_Entropy_Type_huf);
}

TEST(EntropyTest, HufOrRawOrConstantRoundTrip)
{
    // Normal Huffman
    testRoundTripHuf<uint8_t>(true, false);
    testRoundTripHuf<uint16_t>(true, false);
    // Avx2 Huffman
    // testRoundTripHuf<uint8_t>(true, true);
    // testRoundTripHuf<uint16_t>(true, true);
}

TEST(EntropyTest, FseOnlyRoundTripDefaultStates)
{
    testRoundTripFse<uint8_t>(false, 0);
}

TEST(EntropyTest, FseOnlyRoundTrip2States)
{
    testRoundTripFse<uint8_t>(false, 2);
}

TEST(EntropyTest, FseOnlyRoundTrip4States)
{
    testRoundTripFse<uint8_t>(false, 4);
}

TEST(EntropyTest, FseOrRawOrConstantRoundTrip)
{
    testRoundTripFse<uint8_t>(true, 0);
}

TEST(EntropyTest, BitOnlyRoundTrip)
{
    testRoundTripBit<uint8_t>(false);
    testRoundTripBit<uint16_t>(false);
}

TEST(EntropyTest, BitRoundTrip)
{
    testRoundTripBit<uint8_t>(true);
    testRoundTripBit<uint16_t>(true);
}

template <typename Int>
void testBlockSplit()
{
    std::vector<Int> data(700 * 8);
    for (size_t b = 0; b < 8; ++b) {
        for (size_t i = 0; i < 700; ++i) {
            data[b * 700 + i] = (Int)(b * 757);
        }
    }
    auto params = ZS_Entropy_EncodeParameters_fromAllowedTypes(
            (ZS_Entropy_TypeMask_e)(ZS_Entropy_TypeMask_huf
                                    | ZS_Entropy_TypeMask_constant
                                    | ZS_Entropy_TypeMask_multi));
    auto const encodedWithoutBlockSplit = encodeWithParams(data, &params);
    checkDecode(encodedWithoutBlockSplit, data);
    {
        size_t const lowerBound = (data.size() * 3) / 8;
        ASSERT_GE(encodedWithoutBlockSplit.size(), lowerBound);
    }

    // Test Constant block splits
    {
        std::array<size_t, 7> splits = { 700 * 1, 700 * 2, 700 * 3, 700 * 4,
                                         700 * 5, 700 * 6, 700 * 7 };
        ZS_Entropy_BlockSplits blockSplits = {
            .splits   = splits.data(),
            .nbSplits = splits.size(),
        };
        params.blockSplits = &blockSplits;

        auto const encodedWithBlockSplits = encodeWithParams(data, &params);

        ASSERT_LT(
                encodedWithBlockSplits.size(), encodedWithoutBlockSplit.size());
        size_t const upperBound = 10 + 10 * 8;
        ASSERT_LE(encodedWithBlockSplits.size(), upperBound);

        checkDecode(encodedWithBlockSplits, data);
    }

    // Test Huffman block splits
    {
        std::array<size_t, 3> splits       = { 700 * 2, 700 * 4, 700 * 6 };
        ZS_Entropy_BlockSplits blockSplits = {
            .splits   = splits.data(),
            .nbSplits = splits.size(),
        };
        params.blockSplits = &blockSplits;

        auto const encodedWithBlockSplits = encodeWithParams(data, &params);

        ASSERT_LT(
                encodedWithBlockSplits.size(), encodedWithoutBlockSplit.size());
        size_t const lowerBound = data.size() * 1 / 8;
        ASSERT_GE(encodedWithBlockSplits.size(), lowerBound);

        checkDecode(encodedWithBlockSplits, data);
    }
}

TEST(EntropyTest, BlockSplit)
{
    testBlockSplit<uint8_t>();
    testBlockSplit<uint16_t>();
}

} // namespace
