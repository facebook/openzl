// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "InputExpander.h"

#include <cstring>
#include <stdexcept>

namespace openzl::tests::datagen {

namespace {

/**
 * Flips a nondetermininstic number of bits in the input string. Bits are chosen
 * according to a Poisson process. That is, the i_th bit flipped is chosen to be
 * X_i bits after the i-1_th bit, where X_i is a Poisson random variable.
 */
void poissonBitflipInplace(
        std::shared_ptr<std::mt19937> generator,
        std::string& str,
        int lambda = 1000)
{
    std::poisson_distribution<> dist(lambda);
    const size_t n = str.size() * 8;
    size_t bitptr  = dist(*generator);
    while (bitptr < n) {
        str[bitptr / 8] ^= (1 << (bitptr % 8));
        bitptr += dist(*generator);
    }
}

} // namespace

// @return a pair {expanded string, number expansions}
static std::pair<std::string, size_t> expandInternal(
        const std::string& src,
        size_t targetSize)
{
    const size_t sz       = src.size();
    const size_t nbCopies = (targetSize + sz - 1) / sz;

    size_t cpSize = 1;
    std::string result(nbCopies * sz, 0);
    char* dst = result.data();

    memcpy(dst, src.data(), sz);
    while (cpSize * 2 <= nbCopies) {
        memcpy(dst + cpSize * sz, dst, cpSize * sz);
        cpSize <<= 1;
    }
    memcpy(dst + cpSize * sz, dst, (nbCopies - cpSize) * sz);
    return { result, nbCopies };
}

std::string InputExpander::expandSerial(
        const std::string& src,
        size_t targetSize)
{
    if (src.size() == 0 && targetSize == 0) {
        return {};
    }
    if (src.size() == 0) {
        throw std::runtime_error("Cannot expand an input of size 0");
    }
    return expandInternal(src, targetSize).first;
}

std::string InputExpander::expandSerialWithMutation(
        const std::string& src,
        size_t targetSize,
        std::shared_ptr<std::mt19937> generator)
{
    if (src.size() == 0 && targetSize == 0) {
        return {};
    }
    if (src.size() == 0) {
        throw std::runtime_error("Cannot expand an input of size 0");
    }
    auto [result, _nbCopies] = expandInternal(src, targetSize);
    if (generator == nullptr) {
        generator = std::make_shared<std::mt19937>();
    }
    poissonBitflipInplace(generator, result);
    return result;
}

std::pair<std::string, std::vector<uint32_t>>
InputExpander::expandStringWithMutation(
        const std::string& src,
        const std::vector<uint32_t>& segmentSizes,
        size_t targetSize,
        std::shared_ptr<std::mt19937> generator)
{
    if (src.size() == 0 && targetSize == 0) {
        return { {}, segmentSizes };
    }
    if (src.size() == 0) {
        throw std::runtime_error("Cannot expand an empty input");
    }
    if (segmentSizes.empty()) {
        throw std::runtime_error(
                "Cannot expand an input with an empty segment list");
    }
    auto [result, nbCopies] = expandInternal(src, targetSize);
    if (generator == nullptr) {
        generator = std::make_shared<std::mt19937>();
    }
    poissonBitflipInplace(generator, result);

    std::vector<uint32_t> newSizes;
    result.reserve(nbCopies * segmentSizes.size());
    for (size_t i = 0; i < nbCopies; ++i) {
        std::copy(
                segmentSizes.begin(),
                segmentSizes.end(),
                std::back_inserter(newSizes));
    }
    return std::make_pair(result, newSizes);
}

} // namespace openzl::tests::datagen
