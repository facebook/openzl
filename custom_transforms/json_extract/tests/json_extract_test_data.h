// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <random>

#include <folly/dynamic.h>
#include <folly/json.h>

namespace openzl::tests {

template <typename Gen>
int64_t genInt(Gen& gen)
{
    std::uniform_int_distribution<int> sign(0, 1);
    std::uniform_int_distribution<uint64_t> value;
    std::uniform_int_distribution<int> bits(0, 62);
    auto v = value(gen);
    v &= uint64_t(1) << bits(gen);
    auto vs = int64_t(v);
    if (sign(gen)) {
        return -vs;
    } else {
        return vs;
    }
}

template <typename Gen>
double genDouble(Gen& gen)
{
    std::uniform_real_distribution<double> dist;
    return dist(gen);
}

template <typename Gen>
std::string genStr(Gen& gen)
{
    std::uniform_int_distribution<size_t> len(0, 32);
    std::uniform_int_distribution<char> chr;
    auto const length = len(gen);
    std::string s;
    s.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        s.push_back(chr(gen));
    }
    return s;
}

template <typename Gen>
folly::dynamic genJsonArrayOfInt(Gen& gen, size_t arraySize)
{
    auto data = folly::dynamic::array();

    for (size_t i = 0; i < arraySize; ++i) {
        data.push_back(genInt(gen));
    }
    return data;
}

template <typename Gen>
folly::dynamic genJsonMapIntFloat(Gen& gen, size_t mapSize)
{
    folly::dynamic data = folly::dynamic::object();
    for (size_t i = 0; i < mapSize; ++i) {
        data.emplace(std::to_string(genInt(gen)), genDouble(gen));
    }
    return data;
}

template <typename Gen>
folly::dynamic genJsonArrayOfStr(Gen& gen, size_t arraySize)
{
    folly::dynamic data = folly::dynamic::array();
    for (size_t i = 0; i < arraySize; ++i) {
        data.push_back(genStr(gen));
    }
    return data;
}

template <typename Gen>
std::string genJsonLikeData(Gen& gen, size_t bytes)
{
    std::uniform_int_distribution<size_t> len(0, 256);
    std::uniform_int_distribution<int> choice(0, 2);
    std::string out;
    out.reserve(bytes);
    while (out.size() < bytes) {
        auto const c = choice(gen);
        if (c == 0) {
            out += folly::toJson(genJsonArrayOfInt(gen, len(gen)));
        } else if (c == 1) {
            out += folly::toJson(genJsonMapIntFloat(gen, len(gen)));
        } else {
            out += folly::toJson(genJsonArrayOfStr(gen, len(gen)));
        }
    }
    out.resize(bytes);
    return out;
}

inline std::string genJsonLikeData(size_t bytes)
{
    std::mt19937 gen(0xdeadbeef ^ bytes);
    return genJsonLikeData(gen, bytes);
}

} // namespace openzl::tests
