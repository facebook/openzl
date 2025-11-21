// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_set>

#include "openzl/shared/mem.h"
#include "openzl/zl_config.h"
#include "openzl/zl_version.h"

#if ZL_IS_FBCODE
#    include "tools/cxx/Resources.h"
#endif

namespace zstrong::bench::utils {

namespace {

/**
 * getStringView:
 * Returns a string_view into a vector of T.
 * It's important to not mutate the vector as long as there's a string_view
 * referencing it.
 */
template <typename T>
inline const std::string_view getStringView(const std::vector<T>& data)
{
    static_assert(std::is_integral_v<T>, "");
    return std::string_view(
            reinterpret_cast<const char*>(data.data()),
            data.size() * sizeof(data[0]));
}

inline void writeLE(void* memPtr, uint8_t val)
{
    *(uint8_t*)memPtr = val;
}

inline void writeLE(void* memPtr, uint16_t val)
{
    ZL_writeLE16(memPtr, val);
}

inline void writeLE(void* memPtr, uint32_t val)
{
    ZL_writeLE32(memPtr, val);
}

inline void writeLE(void* memPtr, uint64_t val)
{
    ZL_writeLE64(memPtr, val);
}
} // namespace

/**
 * toUint8Vector:
 * Converts a vector of integers from type T into a vector of uint8_t with a LE
 * representation of the same elements.
 */
template <typename T>
std::vector<uint8_t> toUint8Vector(const std::vector<T>& src)
{
    std::vector<uint8_t> dst;
    dst.resize(sizeof(T) * src.size());
    uint8_t* curr = dst.data();
    for (T elem : src) {
        writeLE(curr, elem);
        curr += sizeof(T);
    }
    return dst;
}

/**
 * generateUniformRandomVector:
 * Generates a uniformly distributed vector of integers of type T.
 * Range in inclusive.
 */
template <typename T>
std::vector<T>
generateUniformRandomVector(size_t size, size_t seed, T range_from, T range_to)
{
    std::vector<T> vec;
    vec.resize(size);
    std::mt19937 mersenne_engine(seed);
    std::uniform_int_distribution<T> dist(range_from, range_to);
    auto gen = [&dist, &mersenne_engine]() { return dist(mersenne_engine); };

    std::generate(vec.begin(), vec.end(), gen);
    return vec;
}

template <typename T>
std::vector<T> generateUniformRandomVector(
        size_t size,
        size_t seed,
        std::vector<T> const& alphabet)
{
    std::vector<T> vec;
    vec.resize(size);
    std::mt19937 mersenne_engine(seed);

    std::uniform_int_distribution<size_t> dist(0, alphabet.size() - 1);
    auto gen = [&]() { return alphabet[dist(mersenne_engine)]; };
    std::generate(vec.begin(), vec.end(), gen);
    return vec;
}

template <typename T>
std::vector<T> generateRandomAlphabet(
        size_t cardinality,
        size_t seed,
        T rangeFrom = std::numeric_limits<T>::min(),
        T rangeTo   = std::numeric_limits<T>::max())
{
    std::unordered_set<T> alphabet;
    std::mt19937 gen(seed);
    size_t const rangeSize1 = rangeTo - rangeFrom;
    if (rangeSize1 < cardinality * 2) {
        std::vector<T> vec(rangeSize1 + 1);
        std::iota(vec.begin(), vec.end(), rangeFrom);
        std::shuffle(vec.begin(), vec.end(), gen);
        vec.resize(cardinality);
        return vec;
    }

    std::uniform_int_distribution<T> dist(rangeFrom, rangeTo);
    while (alphabet.size() < cardinality) {
        alphabet.insert(dist(gen));
    }
    return std::vector<T>{ alphabet.begin(), alphabet.end() };
}

/**
 * generateNormalRandomVector:
 * Generates a vector of integers of type T sampled from a normal distribution.
 * Rerolls values that are outside of T's limits.
 * TODO: add support for floating type Ts.
 */
template <typename T>
std::vector<T>
generateNormalRandomVector(size_t size, size_t seed, double mean, double stddev)
{
    std::vector<T> vec;
    vec.resize(size);
    std::mt19937 mersenne_engine(seed);
    std::normal_distribution<> dist(mean, stddev);
    auto gen = [&dist, &mersenne_engine]() {
        while (true) {
            double value = dist(mersenne_engine);
            if (value >= (double)std::numeric_limits<T>::min() - 0.01
                || value <= (double)std::numeric_limits<T>::max() + 0.01) {
                return (T)(value + 0.5); // Add 0.5 for better rounding, only
                                         // works for integers
            }
        }
    };

    std::generate(vec.begin(), vec.end(), gen);
    return vec;
}

/**
 * Generates a vector containing @p numRuns sorted runs in increasing order.
 * The values are taken from @p alphabet, which must contain at least
 * @pavgRunLength unique values. The run lengths are normally distributed around
 * @p avgRunLength.
 */
template <typename T>
std::vector<T> genSortedRuns(
        std::vector<T> alphabet,
        size_t numRuns,
        size_t avgRunLength,
        size_t seed)
{
    assert(alphabet.size() >= avgRunLength);
    auto runLengths = generateNormalRandomVector<size_t>(
            numRuns, seed, (double)avgRunLength, (double)avgRunLength / 20);
    std::mt19937 gen(seed);
    std::vector<T> out;
    out.reserve(avgRunLength * numRuns);
    for (auto runLength : runLengths) {
        // Get a random slice of the alphabet
        runLength = std::min(alphabet.size(), runLength);
        std::shuffle(alphabet.begin(), alphabet.end(), gen);
        // Insert it at the end of our vector
        auto const prevSize = out.size();
        out.insert(
                out.end(),
                alphabet.begin(),
                alphabet.begin() + (int64_t)runLength);
        // Sort the range
        std::sort(out.begin() + (int64_t)prevSize, out.end());
    }
    return out;
}

// Generates a random vector of field sizes for variable-sized fields
// NOTE: in order to maintain usability, this function can drop up to
// `minSegLen` bytes
inline std::vector<uint32_t> genStringLens(
        size_t const nbBytes,
        uint32_t const minSegLen,
        uint32_t const maxSegLen,
        size_t const seed)
{
    ZL_ASSERT_GE(nbBytes, maxSegLen);
    ZL_ASSERT_LE(minSegLen, maxSegLen);
    const std::vector<uint32_t> possFieldSizes =
            utils::generateUniformRandomVector<uint32_t>(
                    nbBytes, seed, minSegLen, maxSegLen);
    std::vector<uint32_t> fieldSizes;
    fieldSizes.reserve(nbBytes);

    size_t remSpace = nbBytes;
    size_t nbFields = 0;
    while (remSpace > 0) {
        uint32_t fieldSize = std::min<uint32_t>(
                possFieldSizes[nbFields], (uint32_t)remSpace);
        if (minSegLen > remSpace) {
            break;
        }

        fieldSizes.push_back(fieldSize);
        remSpace -= fieldSize;
        nbFields++;
    }

    // Allocate any remaining space to any non-full fields
    for (size_t i = 0; i < nbFields && remSpace > 0; ++i) {
        if (fieldSizes[i] < maxSegLen) {
            size_t diff = std::min<size_t>(maxSegLen - fieldSizes[i], remSpace);
            fieldSizes[i] += diff;
            remSpace -= diff;
        }
    }

    return fieldSizes;
}

/**
 * Generates a vector containing @p size integers which divide by @p divisor by
 * generating integers randomly in the range of [0, max / divisor] and then
 * multiplying by divisor.
 */
template <typename T>
inline std::vector<T> generateDivisableData(size_t size, size_t seed, T divisor)
{
    std::vector<T> data;
    data.resize(size);
    std::mt19937 mersenne_engine(seed);
    std::uniform_int_distribution<T> dist(
            std::numeric_limits<T>::min(),
            std::numeric_limits<T>::max() / divisor);
    auto gen = [&dist, &mersenne_engine, &divisor]() {
        return dist(mersenne_engine) * divisor;
    };

    std::generate(data.begin(), data.end(), gen);
    return data;
}

/**
 * Reads a file from path into a string.
 */
inline std::string readCorpus(const std::filesystem::path& name)
{
    const auto corpusRootPath = []() -> std::filesystem::path {
        {
            // Use enviornment variable if given
            const char* envPath = std::getenv("BENCH_CORPUS_PATH");
            if (envPath) {
                return envPath;
            }
        }
#if ZL_IS_FBCODE
        if (build::doesResourceExist("corpus")) {
            return build::getResourcePath("corpus").string();
        }
#endif
        // We failed finding a path
        return "";
    }();

    const auto corpusPath = corpusRootPath / name;

    std::ifstream file(corpusPath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + corpusPath.string());
    }

    std::string buffer(std::istreambuf_iterator<char>(file), {});
    file.close();
    return buffer;
}

} // namespace zstrong::bench::utils
