// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <folly/Range.h>
#include <folly/io/IOBuf.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "custom_transforms/thrift/kernels/decode_thrift_kernel.h"
#include "custom_transforms/thrift/kernels/encode_thrift_kernel.h"
#include "custom_transforms/thrift/kernels/thrift_kernel_utils.h"
#include "openzl/shared/mem.h"

namespace {
using namespace zstrong::thrift;

template <typename T>
std::unique_ptr<folly::IOBuf> serialize(const std::vector<T>& value)
{
    auto queue = folly::IOBufQueue{ folly::IOBufQueue::cacheChainLength() };
    apache::thrift::CompactSerializer::serialize(value, &queue);
    return queue.move();
}

template <typename K, typename V>
std::unique_ptr<folly::IOBuf> serialize(const std::unordered_map<K, V>& value)
{
    auto queue = folly::IOBufQueue{ folly::IOBufQueue::cacheChainLength() };
    apache::thrift::CompactSerializer::serialize(value, &queue);
    return queue.move();
}
} // namespace

TEST(ThriftKernelTest, ArrayI64)
{
    auto testRoundTrip = [](std::vector<int64_t> const& input) {
        auto buf  = serialize(input);
        auto data = buf->coalesce();
        std::vector<uint64_t> extracted(input.size());
        auto ret = ZS2_ThriftKernel_deserializeArrayI64(
                extracted.data(), data.data(), data.size(), input.size());
        ASSERT_FALSE(ZL_isError(ret));
        ASSERT_EQ(ZL_validResult(ret), data.size());

        if (input.size() > 0) {
            ASSERT_FALSE(
                    memcmp(extracted.data(),
                           input.data(),
                           extracted.size() * sizeof(extracted[0])));
        }

        std::vector<uint8_t> out(data.size());
        ret = ZS2_ThriftKernel_serializeArrayI64(
                out.data(), out.size(), extracted.data(), extracted.size());
        ASSERT_FALSE(ZL_isError(ret));
        ASSERT_EQ(ZL_validResult(ret), out.size());

        ASSERT_EQ(folly::crange(out), data);
    };

    testRoundTrip({});
    testRoundTrip({ -1, 0, 1, -2, 2, -10, 10 });
    testRoundTrip({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 });
    testRoundTrip({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });
    testRoundTrip(
            { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, -1 });
    testRoundTrip(
            { std::numeric_limits<int64_t>::max(),
              std::numeric_limits<int64_t>::min() });
    std::vector<int64_t> array;
    for (size_t i = 0; i < 20000; ++i) {
        array.push_back(int64_t(i));
    }
    testRoundTrip(array);
}

TEST(ThriftKernelTest, ArrayI32)
{
    auto testRoundTrip = [](std::vector<int32_t> const& input) {
        auto buf  = serialize(input);
        auto data = buf->coalesce();
        std::vector<uint32_t> extracted(input.size());
        auto ret = ZS2_ThriftKernel_deserializeArrayI32(
                extracted.data(), data.data(), data.size(), input.size());
        ASSERT_FALSE(ZL_isError(ret));
        ASSERT_EQ(ZL_validResult(ret), data.size());

        if (input.size() > 0) {
            ASSERT_FALSE(
                    memcmp(extracted.data(),
                           input.data(),
                           extracted.size() * sizeof(extracted[0])));
        }

        std::vector<uint8_t> out(data.size());
        ret = ZS2_ThriftKernel_serializeArrayI32(
                out.data(), out.size(), extracted.data(), extracted.size());
        ASSERT_FALSE(ZL_isError(ret));
        ASSERT_EQ(ZL_validResult(ret), out.size());

        ASSERT_EQ(folly::crange(out), data);
    };

    testRoundTrip({});
    testRoundTrip({ -1, 0, 1, -2, 2, -10, 10 });
    testRoundTrip({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 });
    testRoundTrip({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });
    testRoundTrip(
            { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, -1 });
    testRoundTrip(
            { std::numeric_limits<int32_t>::max(),
              std::numeric_limits<int32_t>::min() });
    std::vector<int32_t> array;
    for (size_t i = 0; i < 20000; ++i) {
        array.push_back(int32_t(i));
    }
    testRoundTrip(array);
}

TEST(ThriftKernelTest, ArrayFloat)
{
    auto testRoundTrip = [](std::vector<float> const& input) {
        auto buf  = serialize(input);
        auto data = buf->coalesce();
        std::vector<uint32_t> extracted(input.size());
        auto ret = ZS2_ThriftKernel_deserializeArrayFloat(
                extracted.data(), data.data(), data.size(), input.size());
        ASSERT_FALSE(ZL_isError(ret));
        ASSERT_EQ(ZL_validResult(ret), data.size());

        if (input.size() > 0) {
            ASSERT_FALSE(
                    memcmp(extracted.data(),
                           input.data(),
                           extracted.size() * sizeof(extracted[0])));
        }

        std::vector<uint8_t> out(data.size());
        ret = ZS2_ThriftKernel_serializeArrayFloat(
                out.data(), out.size(), extracted.data(), extracted.size());
        ASSERT_FALSE(ZL_isError(ret));
        ASSERT_EQ(ZL_validResult(ret), out.size());

        ASSERT_EQ(folly::crange(out), data);
    };

    testRoundTrip({});
    testRoundTrip({ 0.0 });
    testRoundTrip(
            { -1.5,
              0.0,
              2.5,
              std::numeric_limits<float>::quiet_NaN(),
              std::numeric_limits<float>::signaling_NaN(),
              std::numeric_limits<float>::lowest(),
              std::numeric_limits<float>::denorm_min(),
              std::numeric_limits<float>::epsilon(),
              std::numeric_limits<float>::round_error(),
              std::numeric_limits<float>::infinity(),
              -std::numeric_limits<float>::infinity(),
              std::numeric_limits<float>::max() });
    testRoundTrip({ -0.0,  0.0, 0.1, 0.01, 0.001,     0.2,   0.3,
                    0.4,   0.5, 0.6, 0.7,  0.8,       0.9,   0.99,
                    0.999, 1.0, 1.1, 1.2,  1000000.0, 10e10, -10e10 });
    std::vector<float> array;
    for (size_t i = 0; i < 20000; ++i) {
        array.push_back(float(i));
    }
    testRoundTrip(array);
}

TEST(ThriftKernelTest, MapI32Float)
{
    auto testRoundTrip = [](std::unordered_map<int32_t, float> const& input) {
        auto buf  = serialize(input);
        auto data = buf->coalesce();
        std::vector<uint32_t> keys(input.size());
        std::vector<uint32_t> values(input.size());
        auto ret = ZS2_ThriftKernel_deserializeMapI32Float(
                keys.data(),
                values.data(),
                data.data(),
                data.size(),
                input.size());
        ASSERT_FALSE(ZL_isError(ret));
        ASSERT_EQ(ZL_validResult(ret), data.size());

        {
            std::vector<uint32_t> actualKeys;
            std::vector<uint32_t> actualValues;
            for (auto const& [k, v] : input) {
                actualKeys.push_back(static_cast<uint32_t>(k));
                actualValues.push_back(ZL_read32(&v));
            }
            ASSERT_EQ(keys, actualKeys);
            ASSERT_EQ(values, actualValues);
        }

        std::vector<uint8_t> out(data.size());
        ret = ZS2_ThriftKernel_serializeMapI32Float(
                out.data(),
                out.size(),
                keys.data(),
                values.data(),
                keys.size());
        ASSERT_FALSE(ZL_isError(ret));
        ASSERT_EQ(ZL_validResult(ret), out.size());

        ASSERT_EQ(folly::crange(out), data);
    };

    testRoundTrip({});
    testRoundTrip(
            { { 0, 0.0 },
              { 1, -0.0 },
              { -1, -50.0 },
              { std::numeric_limits<int32_t>::min(), 5.0 },
              { std::numeric_limits<int32_t>::max(), -5.0 } });
    std::unordered_map<int32_t, float> map;
    for (size_t i = 0; i < 20000; ++i) {
        map.emplace(int32_t(i), float(i));
    }
    testRoundTrip(map);
}

TEST(ThriftKernelTest, MapI32ArrayFloat)
{
    auto testRoundTrip =
            [](std::unordered_map<int32_t, std::vector<float>> const& input) {
                auto buf  = serialize(input);
                auto data = buf->coalesce();
                std::vector<uint32_t> keys(input.size());
                std::vector<uint32_t> lengths(input.size());
                VectorDynamicOutput<uint32_t> innerValuesOut;
                auto ret = ZS2_ThriftKernel_deserializeMapI32ArrayFloat(
                        keys.data(),
                        lengths.data(),
                        innerValuesOut.asCType(),
                        data.data(),
                        data.size(),
                        input.size());
                ASSERT_FALSE(ZL_isError(ret));
                ASSERT_EQ(ZL_validResult(ret), data.size());

                auto innerValues = std::move(innerValuesOut).written();

                {
                    std::vector<uint32_t> actualKeys;
                    std::vector<uint32_t> actualLengths;
                    std::vector<uint32_t> actualInnerValues;
                    for (auto const& [k, a] : input) {
                        actualKeys.push_back(static_cast<uint32_t>(k));
                        actualLengths.push_back((uint32_t)a.size());
                        for (auto const& v : a) {
                            actualInnerValues.push_back(ZL_read32(&v));
                        }
                    }
                    ASSERT_EQ(keys, actualKeys);
                    ASSERT_EQ(lengths, actualLengths);
                    ASSERT_EQ(innerValues, actualInnerValues);
                }

                auto const* innerValuesPtr = innerValues.data();
                auto const* const innerValuesEnd =
                        innerValuesPtr + innerValues.size();

                std::vector<uint8_t> out(data.size());
                ret = ZS2_ThriftKernel_serializeMapI32ArrayFloat(
                        out.data(),
                        out.size(),
                        keys.data(),
                        lengths.data(),
                        keys.size(),
                        &innerValuesPtr,
                        innerValuesEnd);
                ASSERT_FALSE(ZL_isError(ret));
                ASSERT_EQ(ZL_validResult(ret), out.size());
                ASSERT_EQ(innerValuesPtr, innerValuesEnd);

                ASSERT_EQ(folly::crange(out), data);
            };

    testRoundTrip({});
    testRoundTrip({ { 0, { 0.0, 0.1 } } });
    testRoundTrip(
            { { 0, { 0.0, 0.1 } },
              { 1, { -0.0 } },
              { -1, {} },
              { std::numeric_limits<int32_t>::min(), { 5.0 } },
              { std::numeric_limits<int32_t>::max(), { -5.0 } },
              { 2, std::vector<float>(1000, 0.5) } });
    std::unordered_map<int32_t, std::vector<float>> map;
    for (size_t i = 0; i < 20000; ++i) {
        map.emplace(int32_t(i), std::vector<float>(1, float(i)));
    }
    testRoundTrip(map);
}

TEST(ThriftKernelTest, MapI32ArrayI64)
{
    auto testRoundTrip =
            [](std::unordered_map<int32_t, std::vector<int64_t>> const& input) {
                auto buf  = serialize(input);
                auto data = buf->coalesce();
                std::vector<uint32_t> keys(input.size());
                std::vector<uint32_t> lengths(input.size());
                VectorDynamicOutput<uint64_t> innerValuesOut;
                auto ret = ZS2_ThriftKernel_deserializeMapI32ArrayI64(
                        keys.data(),
                        lengths.data(),
                        innerValuesOut.asCType(),
                        data.data(),
                        data.size(),
                        input.size());
                ASSERT_FALSE(ZL_isError(ret));
                ASSERT_EQ(ZL_validResult(ret), data.size());

                auto innerValues = std::move(innerValuesOut).written();

                {
                    std::vector<uint32_t> actualKeys;
                    std::vector<uint32_t> actualLengths;
                    std::vector<uint64_t> actualInnerValues;
                    for (auto const& [k, a] : input) {
                        actualKeys.push_back(static_cast<uint32_t>(k));
                        actualLengths.push_back((uint32_t)a.size());
                        for (auto const& v : a) {
                            actualInnerValues.push_back(
                                    static_cast<uint64_t>(v));
                        }
                    }
                    ASSERT_EQ(keys, actualKeys);
                    ASSERT_EQ(lengths, actualLengths);
                    ASSERT_EQ(innerValues, actualInnerValues);
                }

                auto const* innerValuesPtr = innerValues.data();
                auto const* const innerValuesEnd =
                        innerValuesPtr + innerValues.size();

                std::vector<uint8_t> out(data.size());
                ret = ZS2_ThriftKernel_serializeMapI32ArrayI64(
                        out.data(),
                        out.size(),
                        keys.data(),
                        lengths.data(),
                        keys.size(),
                        &innerValuesPtr,
                        innerValuesEnd);
                ASSERT_FALSE(ZL_isError(ret));
                ASSERT_EQ(ZL_validResult(ret), out.size());
                ASSERT_EQ(innerValuesPtr, innerValuesEnd);

                ASSERT_EQ(folly::crange(out), data);
            };

    testRoundTrip({});
    testRoundTrip({ { 0, { -1, 1 } } });
    testRoundTrip(
            { { 0, { 0, 10 } },
              { 1, { -10 } },
              { -1, {} },
              { std::numeric_limits<int32_t>::min(), { 50000 } },
              { std::numeric_limits<int32_t>::max(), { -50000 } },
              { 2, std::vector<int64_t>(1000, 5) } });
    std::unordered_map<int32_t, std::vector<int64_t>> map;
    for (size_t i = 0; i < 20000; ++i) {
        map.emplace(int32_t(i), std::vector<int64_t>(1, int64_t(i)));
    }
    testRoundTrip(map);
}

TEST(ThriftKernelTest, MapI32ArrayArrayI64)
{
    auto testRoundTrip =
            [](std::unordered_map<
                    int32_t,
                    std::vector<std::vector<int64_t>>> const& input) {
                auto buf  = serialize(input);
                auto data = buf->coalesce();
                std::vector<uint32_t> keys(input.size());
                std::vector<uint32_t> lengths(input.size());
                VectorDynamicOutput<uint32_t> innerLengthsOut;
                VectorDynamicOutput<uint64_t> innerInnerValuesOut;
                auto ret = ZS2_ThriftKernel_deserializeMapI32ArrayArrayI64(
                        keys.data(),
                        lengths.data(),
                        innerLengthsOut.asCType(),
                        innerInnerValuesOut.asCType(),
                        data.data(),
                        data.size(),
                        input.size());
                ASSERT_FALSE(ZL_isError(ret));
                ASSERT_EQ(ZL_validResult(ret), data.size());

                auto innerLengths = std::move(innerLengthsOut).written();
                auto innerInnerValues =
                        std::move(innerInnerValuesOut).written();

                {
                    std::vector<uint32_t> actualKeys;
                    std::vector<uint32_t> actualLengths;
                    std::vector<uint32_t> actualInnerLengths;
                    std::vector<uint64_t> actualInnerInnerValues;
                    for (auto const& [k, array] : input) {
                        actualKeys.push_back(static_cast<uint32_t>(k));
                        actualLengths.push_back((uint32_t)array.size());
                        for (auto const& innerArray : array) {
                            actualInnerLengths.push_back(
                                    (uint32_t)innerArray.size());
                            for (auto const& v : innerArray) {
                                actualInnerInnerValues.push_back(
                                        static_cast<uint64_t>(v));
                            }
                        }
                    }
                    ASSERT_EQ(keys, actualKeys);
                    ASSERT_EQ(lengths, actualLengths);
                    ASSERT_EQ(innerLengths, actualInnerLengths);
                    ASSERT_EQ(innerInnerValues, actualInnerInnerValues);
                }

                auto const* innerLengthsPtr = innerLengths.data();
                auto const* const innerLengthsEnd =
                        innerLengthsPtr + innerLengths.size();
                auto const* innerInnerValuesPtr = innerInnerValues.data();
                auto const* const innerInnerValuesEnd =
                        innerInnerValuesPtr + innerInnerValues.size();

                std::vector<uint8_t> out(data.size());
                ret = ZS2_ThriftKernel_serializeMapI32ArrayArrayI64(
                        out.data(),
                        out.size(),
                        keys.data(),
                        lengths.data(),
                        keys.size(),
                        &innerLengthsPtr,
                        innerLengthsEnd,
                        &innerInnerValuesPtr,
                        innerInnerValuesEnd);
                ASSERT_FALSE(ZL_isError(ret));
                ASSERT_EQ(ZL_validResult(ret), out.size());
                ASSERT_EQ(innerLengthsPtr, innerLengthsEnd);
                ASSERT_EQ(innerInnerValuesPtr, innerInnerValuesEnd);

                ASSERT_EQ(folly::crange(out), data);
            };

    testRoundTrip({});
    testRoundTrip({ { 0, {} } });
    testRoundTrip({ { 0, { {} } } });
    testRoundTrip({ { 0, { {}, {} } } });
    testRoundTrip({ { 0, std::vector<std::vector<int64_t>>(10000) } });
    testRoundTrip({ { 0, { std::vector<int64_t>(10000) } } });
    testRoundTrip(
            { { 0, { { 0, 1 }, {}, { 2 }, { 3, 4, 5 } } },
              { -1, { { 0, 1 }, { 2, 3, 4 } } },
              { 1, {} },
              { 2, { {}, { 3, 4, 5 } } } });
    std::unordered_map<int32_t, std::vector<std::vector<int64_t>>> map;
    for (size_t i = 0; i < 20000; ++i) {
        map.emplace(
                int32_t(i),
                std::vector<std::vector<int64_t>>(
                        2, std::vector<int64_t>(1, int64_t(i))));
    }
    testRoundTrip(map);
}

TEST(ThriftKernelTest, MapI32MapI64Float)
{
    auto testRoundTrip =
            [](std::unordered_map<
                    int32_t,
                    std::unordered_map<int64_t, float>> const& input) {
                auto buf  = serialize(input);
                auto data = buf->coalesce();
                std::vector<uint32_t> keys(input.size());
                std::vector<uint32_t> lengths(input.size());
                VectorDynamicOutput<uint64_t> innerKeysOut;
                VectorDynamicOutput<uint32_t> innerValuesOut;
                auto ret = ZS2_ThriftKernel_deserializeMapI32MapI64Float(
                        keys.data(),
                        lengths.data(),
                        innerKeysOut.asCType(),
                        innerValuesOut.asCType(),
                        data.data(),
                        data.size(),
                        input.size());
                ASSERT_FALSE(ZL_isError(ret));
                ASSERT_EQ(ZL_validResult(ret), data.size());

                auto innerKeys   = std::move(innerKeysOut).written();
                auto innerValues = std::move(innerValuesOut).written();
                ASSERT_EQ(innerKeys.size(), innerValues.size());

                {
                    std::vector<uint32_t> actualKeys;
                    std::vector<uint32_t> actualLengths;
                    std::vector<uint64_t> actualInnerKeys;
                    std::vector<uint32_t> actualInnerValues;
                    for (auto const& [k, m] : input) {
                        actualKeys.push_back(static_cast<uint32_t>(k));
                        actualLengths.push_back((uint32_t)m.size());
                        for (auto const& [ik, iv] : m) {
                            actualInnerKeys.push_back(
                                    static_cast<uint64_t>(ik));
                            actualInnerValues.push_back(ZL_read32(&iv));
                        }
                    }
                    ASSERT_EQ(keys, actualKeys);
                    ASSERT_EQ(lengths, actualLengths);
                    ASSERT_EQ(innerKeys, actualInnerKeys);
                    ASSERT_EQ(innerValues, actualInnerValues);
                }

                auto const* innerKeysPtr = innerKeys.data();
                auto const* const innerKeysEnd =
                        innerKeysPtr + innerKeys.size();
                auto const* innerValuesPtr = innerValues.data();
                auto const* const innerValuesEnd =
                        innerValuesPtr + innerValues.size();

                std::vector<uint8_t> out(data.size());
                ret = ZS2_ThriftKernel_serializeMapI32MapI64Float(
                        out.data(),
                        out.size(),
                        keys.data(),
                        lengths.data(),
                        keys.size(),
                        &innerKeysPtr,
                        innerKeysEnd,
                        &innerValuesPtr,
                        innerValuesEnd);
                ASSERT_FALSE(ZL_isError(ret));
                ASSERT_EQ(ZL_validResult(ret), out.size());
                ASSERT_EQ(innerKeysPtr, innerKeysEnd);
                ASSERT_EQ(innerValuesPtr, innerValuesEnd);

                ASSERT_EQ(folly::crange(out), data);
            };

    testRoundTrip({});
    testRoundTrip(
            { { 0, { { -1, 0.0 }, { 1, 0.1 } } },
              { 1, {} },
              { -1, { { 0, 5.0 } } },
              { 2,
                { { std::numeric_limits<int64_t>::min(), -0.5 },
                  { std::numeric_limits<int64_t>::max(), -0.0 } } } });
    std::unordered_map<int32_t, std::unordered_map<int64_t, float>> map;
    for (size_t i = 0; i < 20000; ++i) {
        std::unordered_map<int64_t, float> innerMap;
        innerMap[i] = float(i);
        map.emplace(int32_t(i), std::move(innerMap));
    }
    {
        std::unordered_map<int64_t, float> innerMap;
        for (size_t i = 0; i < 20000; ++i) {
            innerMap.emplace(int64_t(i), float(i));
        }
        map.emplace(-1, std::move(innerMap));
    }
    testRoundTrip(map);
}
