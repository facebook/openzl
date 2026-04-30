// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/parse/encode_parse.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/overflow.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

#include <folly/Conv.h>

#if ZL_HAS_AVX2
#    include <immintrin.h>
#endif

namespace zstrong {
namespace {

ZL_Report fillVSFStream(
        ZL_Encoder* eictx,
        size_t idx,
        std::vector<std::string_view> const& elts)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    size_t totalSize = 0;
    for (auto const& elt : elts) {
        totalSize += elt.size();
    }

    ZL_Output* const stream =
            ZL_Encoder_createTypedStream(eictx, idx, totalSize, 1);
    ZL_ERR_IF_NULL(stream, allocation);

    auto* const sizes = ZL_Output_reserveStringLens(stream, elts.size());
    ZL_ERR_IF_NULL(sizes, allocation);

    auto content = (char*)ZL_Output_ptr(stream);

    for (size_t i = 0; i < elts.size(); ++i) {
        std::memcpy(content, elts[i].data(), elts[i].size());
        content += elts[i].size();
        sizes[i] = elts[i].size();
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(stream, elts.size()));

    return ZL_returnSuccess();
}

template <typename T>
std::optional<T> parseValue(std::string_view elt)
{
    auto const maybe = folly::tryTo<T>(elt);
    if (!maybe.hasError()) {
        auto const str = folly::to<std::string>(maybe.value());
        if (str == elt) {
            return maybe.value();
        }
    }
    return std::nullopt;
}

template <typename T>
size_t parseEncodeKernel(
        T* nums,
        std::vector<std::string_view>& exceptions,
        std::vector<uint32_t>& exceptionIndices,
        char const* data,
        uint32_t const* sizes,
        size_t nbElts)
{
    size_t numIdx = 0;

    for (size_t i = 0; i < nbElts; ++i) {
        std::string_view elt{ data, sizes[i] };

        auto const maybe = parseValue<T>(elt);

        if (maybe.has_value()) {
            nums[numIdx++] = *maybe;
        } else {
            exceptions.push_back(elt);
            exceptionIndices.push_back(i);
        }

        data += elt.size();
    }

    return numIdx;
}

#if ZL_HAS_AVX2
/// LUT for masks to zero out leading bytes
constexpr std::array<std::array<uint8_t, 32>, 21> kNonZeroMask alignas(32) =
        [] {
            std::array<std::array<uint8_t, 32>, 21> result;

            for (size_t len = 0; len < result.size(); ++len) {
                size_t const leadingZeros = 32 - len;
                for (size_t i = 0; i < leadingZeros; ++i) {
                    result[len][i] = 0;
                }
                for (size_t i = leadingZeros; i < 32; ++i) {
                    result[len][i] = 0xFF;
                }
            }

            return result;
        }();

/// LUT for character addition lookups.
/// We do 4 additions at a time, so the first lookup is thousands,
/// then hundreds, then tens, then ones.
constexpr std::array<std::array<uint16_t, 10>, 4> kLookup = [] {
    std::array<std::array<uint16_t, 10>, 4> result;

    size_t mult = 1;
    for (size_t idx = result.size(); idx-- > 0; mult *= 10) {
        for (size_t val = 0; val < 10; ++val) {
            result[idx][val] = val * mult;
        }
    }

    return result;
}();

/// Multiply @p result by 10000 and add @p add.
/// @returns true if either operation overflows.
bool overflowAccumulate(uint64_t* result, uint64_t add)
{
    // Max value that won't overflow when multiplying by 10000.
    uint64_t const kMaxResult = ((uint64_t)-1) / 10000;
#    ifndef NDEBUG
    uint64_t tmp;
    ZL_ASSERT(!ZL_overflowMulU64(kMaxResult, 10000, &tmp));
    ZL_ASSERT(ZL_overflowMulU64(kMaxResult + 1, 10000, &tmp));
#    endif
    if (*result > kMaxResult) {
        return true;
    }
    *result *= 10000;

    // Add & check for wrapping, we know the add is small so this is correct.
    ZL_ASSERT_LT(add, 10000);
    uint64_t const next = *result + add;
    if (next < *result) {
        return true;
    }
    *result = next;

    return false;
}

/**
 * Strictly parses an int64 contained in [ptr, end), and is allowed to read
 * up to 32 bytes before @p end. Fails if:
 * - The string is not an integer
 * - The integer begins with +
 * - The integer has leading zeros
 * - The integer overlows an int64_t
 */
std::optional<int64_t> parseInt64Unsafe(char const* ptr, char const* end)
{
    // Determine if it is negative, remove '-' & validate it is non-empty
    if (ptr == end) {
        return std::nullopt;
    }
    bool const negative = ptr[0] == '-';
    if (negative) {
        ++ptr;
        if (ptr == end) {
            return std::nullopt;
        }
    }

    // Validate it doesn't have a leading zero
    if (ptr[0] == '0') {
        return std::nullopt;
    }

    // Validate the length isn't >20 = guaranteed overflow
    size_t const len = end - ptr;
    if (len > 20) {
        return std::nullopt;
    }

    // 1. Validate all the characters are between '0' & '9'
    // 2. Subtract '0' from each character
    // 3. Zero the bytes before `ptr`
    __m256i v = _mm256_loadu_si256((__m256i_u const*)(end - 32));
    __m256i m = _mm256_load_si256((__m256i const*)kNonZeroMask[len].data());
    __m256i invalid = _mm256_cmpgt_epi8(v, _mm256_set1_epi8('9'));
    invalid         = _mm256_or_si256(
            invalid, _mm256_cmpgt_epi8(_mm256_set1_epi8('0'), v));
    invalid = _mm256_and_si256(invalid, m);
    if (_mm256_movemask_epi8(invalid) != 0) {
        return std::nullopt;
    }
    v = _mm256_sub_epi8(v, _mm256_set1_epi8('0'));
    v = _mm256_and_si256(v, m);

    // Store the vector:
    // [0, 32 - len) = 0
    // [32 - len, 32) = [ptr, end) - '0'
    uint8_t data[32];
    _mm256_storeu_si256((__m256i_u*)data, v);

    // Always read the last 20 bytes, values
    // before the beginning of the int are 0.
    uint8_t* src = data + 32 - 20;

#    ifndef NDEBUG
    for (size_t i = 0; i < (32 - len); ++i) {
        ZL_ASSERT_EQ(data[i], 0);
    }
#    endif
    ZL_ASSERT_LE(len, 20);

    // Add to the result in 5 loops of 4
    // We only need to check the last iteration for overflow, because integers
    // of 19 digits or less cannot overflow a uint64_t. We check for signed
    // overflow below.
    uint64_t uresult = 0;
    for (size_t idx = 0; idx < 20;) {
        uint64_t sum = 0;
        for (size_t u = 0; u < 4; ++u, ++idx) {
            ZL_ASSERT_LT(src[idx], 10);
            sum += kLookup[u][src[idx]];
        }
        if (idx == 20) {
            if (overflowAccumulate(&uresult, sum)) {
                return std::nullopt;
            }
        } else {
            uresult = uresult * 10000 + sum;
        }
    }

    // Convert the uint64_t into an int64_t and check for overflows.
    if (negative) {
        auto const result = int64_t(-uresult);
        if (result > 0) {
            return std::nullopt;
        }
        return result;
    } else {
        auto const result = int64_t(uresult);
        if (result < 0) {
            return std::nullopt;
        }
        return result;
    }
}

template <>
size_t parseEncodeKernel(
        int64_t* nums,
        std::vector<std::string_view>& exceptions,
        std::vector<uint32_t>& exceptionIndices,
        char const* data,
        uint32_t const* sizes,
        size_t nbElts)
{
    size_t numIdx = 0;

    // Use safe parse until we've read 32 bytes from the input stream
    size_t idx;
    {
        size_t offset;
        for (idx = 0, offset = 0; idx < nbElts && offset < 32; ++idx) {
            std::string_view elt{ data + offset, sizes[idx] };

            auto const maybe = parseValue<int64_t>(elt);

            if (maybe.has_value()) {
                nums[numIdx++] = *maybe;
            } else {
                exceptions.push_back(elt);
                exceptionIndices.push_back(idx);
            }

            offset += elt.size();
        }
        data += offset;
    }

    // Decode the remainder using the fast parsing function
    for (; idx < nbElts; ++idx) {
        auto const maybe = parseInt64Unsafe(data, data + sizes[idx]);

        if (maybe.has_value()) {
            ZL_ASSERT(
                    folly::to<std::string>(maybe.value())
                    == std::string(data, data + sizes[idx]));
            nums[numIdx++] = *maybe;
        } else {
            exceptions.push_back({ data, sizes[idx] });
            exceptionIndices.push_back(idx);
        }

        data += sizes[idx];
    }

    return numIdx;
}
#endif

template <typename T>
ZL_Report parseEncode(ZL_Encoder* eictx, ZL_Input const* input) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    std::vector<std::string_view> exceptions;
    std::vector<uint32_t> exceptionIndices;

    char const* data      = (char const*)ZL_Input_ptr(input);
    uint32_t const* sizes = ZL_Input_stringLens(input);
    size_t const nbElts   = ZL_Input_numElts(input);

    ZL_Output* numbers =
            ZL_Encoder_createTypedStream(eictx, 0, nbElts, sizeof(T));
    ZL_ERR_IF_NULL(numbers, allocation);

    T* const nums = (T*)ZL_Output_ptr(numbers);

    size_t const numNums = parseEncodeKernel<T>(
            nums, exceptions, exceptionIndices, data, sizes, nbElts);

    ZL_ERR_IF_ERR(ZL_Output_commit(numbers, numNums));

    ZL_Output* exceptionIndicesStream =
            ZL_Encoder_createTypedStream(eictx, 1, exceptionIndices.size(), 4);
    ZL_ERR_IF_NULL(exceptionIndicesStream, allocation);

    if (exceptionIndices.size() > 0)
        memcpy(ZL_Output_ptr(exceptionIndicesStream),
               exceptionIndices.data(),
               exceptionIndices.size() * 4);

    ZL_ERR_IF_ERR(
            ZL_Output_commit(exceptionIndicesStream, exceptionIndices.size()));

    ZL_ERR_IF_ERR(fillVSFStream(eictx, 2, exceptions));

    return ZL_returnSuccess();
}

} // namespace
} // namespace zstrong

ZL_NodeID ZS2_Compressor_registerParseInt64(
        ZL_Compressor* cgraph,
        ZL_IDType transformID)
{
    std::array<ZL_Type, 3> const kOutStreams = { ZL_Type_numeric,
                                                 ZL_Type_numeric,
                                                 ZL_Type_string };
    ZL_TypedGraphDesc graph                  = {
                         .CTid           = transformID,
                         .inStreamType   = ZL_Type_string,
                         .outStreamTypes = kOutStreams.data(),
                         .nbOutStreams   = kOutStreams.size(),
    };
    ZL_TypedEncoderDesc desc = {
        .gd          = graph,
        .transform_f = zstrong::parseEncode<int64_t>,
        .name        = "parse int64",
    };
    return ZL_Compressor_registerTypedEncoder(cgraph, &desc);
}

ZL_NodeID ZS2_Compressor_registerParseFloat64(
        ZL_Compressor* cgraph,
        ZL_IDType transformID)
{
    std::array<ZL_Type, 3> const kOutStreams = { ZL_Type_numeric,
                                                 ZL_Type_numeric,
                                                 ZL_Type_string };
    ZL_TypedGraphDesc graph                  = {
                         .CTid           = transformID,
                         .inStreamType   = ZL_Type_string,
                         .outStreamTypes = kOutStreams.data(),
                         .nbOutStreams   = kOutStreams.size(),
    };
    ZL_TypedEncoderDesc desc = {
        .gd          = graph,
        .transform_f = zstrong::parseEncode<double>,
        .name        = "parse float64",
    };
    return ZL_Compressor_registerTypedEncoder(cgraph, &desc);
}
