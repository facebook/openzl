// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/json_extract/encode_json_extract.h"
#include "custom_transforms/json_extract/common_json_extract.h"
#include "openzl/codecs/common/copy.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/bits.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#ifdef __AVX2__
#    include <immintrin.h>
#endif

namespace zstrong {
namespace {

class Buffer {
   public:
    explicit Buffer(ZL_Encoder* eictx, int idx, size_t capacity)
            : stream_(ZL_Encoder_createTypedStream(eictx, idx, capacity, 1))
    {
        end_ = start();
    }

    void push_back(char c)
    {
        *end_++ = c;
    }

    /// If kFast, then both the Buffer and the @p src must be able to over
    /// read/write ZS_WILDCOPY_OVERLENGTH after the copy.
    template <bool kFast>
    void append(std::string_view src)
    {
        if constexpr (kFast) {
            ZS_wildcopy(end_, src.data(), src.size(), ZS_wo_no_overlap);
        } else if (!src.empty()) {
            std::memcpy(end_, src.data(), src.size());
        }
        end_ += src.size();
    }

    std::string_view view() const
    {
        return { start(), end_ };
    }

    /// Commit a serialized stream
    ZL_Report commit() const
    {
        return ZL_Output_commit(stream_, view().size());
    }

    /// Commit a variable size stream
    ZL_Report commit(std::vector<uint32_t> const& fieldSizes)
    {
        uint32_t* const out =
                ZL_Output_reserveStringLens(stream_, fieldSizes.size());
        ZL_RET_R_IF_NULL(allocation, out);
        if (fieldSizes.size() > 0) {
            memcpy(out,
                   fieldSizes.data(),
                   sizeof(uint32_t) * fieldSizes.size());
        }
        ZL_RET_R_IF_ERR(ZL_Output_commit(stream_, fieldSizes.size()));
        return ZL_returnSuccess();
    }

   private:
    char* start() const
    {
        return (char*)ZL_Output_ptr(stream_);
    }

    char* end_;
    ZL_Output* stream_;
};

/// The 4 extracted output streams
class Extracted {
    Buffer json_;
    Buffer ints_;
    std::vector<uint32_t> intLengths_;
    Buffer floats_;
    std::vector<uint32_t> floatLengths_;
    Buffer strs_;
    std::vector<uint32_t> strLengths_;

   public:
    explicit Extracted(ZL_Encoder* eictx, std::string_view src)
            : json_(eictx, 0, src.size()),
              ints_(eictx, 1, src.size()),
              floats_(eictx, 2, src.size()),
              strs_(eictx, 3, src.size())
    {
        intLengths_.reserve(src.size() / 16);
        floatLengths_.reserve(src.size() / 16);
        strLengths_.reserve(src.size() / 16);
    }

    template <bool kFast>
    void pushInt(std::string_view src)
    {
        ints_.append<kFast>(src);
        intLengths_.push_back(uint32_t(src.size()));
    }

    template <bool kFast>
    void pushFloat(std::string_view src)
    {
        floats_.append<kFast>(src);
        floatLengths_.push_back(uint32_t(src.size()));
    }

    template <bool kFast>
    void pushStr(std::string_view src)
    {
        strs_.append<kFast>(src);
        strLengths_.push_back(uint32_t(src.size()));
    }

    template <bool kFast>
    void pushJson(std::string_view src)
    {
        json_.append<kFast>(src);
    }

    void pushJson(Token src)
    {
        json_.push_back(char(src));
    }

    ZL_Report commit()
    {
        ZL_RET_R_IF_ERR(json_.commit());
        ZL_RET_R_IF_ERR(ints_.commit(intLengths_));
        ZL_RET_R_IF_ERR(floats_.commit(floatLengths_));
        ZL_RET_R_IF_ERR(strs_.commit(strLengths_));
        return ZL_returnSuccess();
    }

    std::string_view jsonContent() const
    {
        return json_.view();
    }
};

/// @returns true iff @p c is a character that should be extracted from the
/// JSON. All characters in this set are extracted from the JSON, without
/// exception.
bool isInSet(char c)
{
    auto const u = static_cast<uint8_t>(c);
    if (u < 32 || u > 126)
        return false;
    if (u == 34)
        return false;
    if (u == 44)
        return false;
    if (u == 58)
        return false;
    if (u >= 91 && u <= 93)
        return false;
    if (u == 123)
        return false;
    if (u == 125)
        return false;
    return true;
}

#ifdef __AVX2__
/**
 * Builds a 32-bit bitmask for whether each byte in @p srcV is in @p bitmapV.
 * This is used in several contexts, so the bitmap is variable.
 * This algorithm is based on the universal algorithm describe in:
 * http://0x80.pl/articles/simd-byte-lookup.html
 *
 * @param checkMSB If true, then we check that the MSB of each value is 0.
 * Otherwise, we do not check this bit, and assume that it is zero.
 *
 * NOTE: This implementation only works for sets containing characters [0, 128).
 *
 * Python function to generate a bitmap given a predicate is_in_set():
 *
 * def bitmap(is_in_set):
 *     b = 0
 *     for i in range(128):
 *         lo = (i // 8) % 16
 *         hi = i % 8
 *         c = lo | (hi << 4)
 *         if is_in_set(c):
 *             b |= 1 << i
 *     b_lo = b & ((1 << 64) - 1)
 *     b_hi = b >> 64
 *     return hex(b_lo).upper(), hex(b_hi).upper()
 */
ZL_FORCE_INLINE uint32_t
isInSetV(const __m256i bitmapV, const __m256i srcV, bool checkMSB)
{
    __m256i const loV     = _mm256_and_si256(srcV, _mm256_set1_epi8(0x0F));
    __m256i const bitsetV = _mm256_shuffle_epi8(bitmapV, loV);

    __m256i const hiV = _mm256_and_si256(
            _mm256_srli_epi16(srcV, 4), _mm256_set1_epi8(0x0F));
    __m256i const bitmaskV =
            _mm256_shuffle_epi8(_mm256_set1_epi64x(0x8040201008040201), hiV);

    __m256i maskV =
            _mm256_cmpeq_epi8(_mm256_and_si256(bitsetV, bitmaskV), bitmaskV);
    if (checkMSB) {
        maskV = _mm256_and_si256(
                maskV, _mm256_cmpgt_epi8(_mm256_set1_epi8(0x08), hiV));
    }

    return (uint32_t)_mm256_movemask_epi8(maskV);
}

/**
 * Determines if every character in @p token is in the set defined by @p
 * bitmapV.
 *
 * WARNING: Assumes it is safe to access up to 32-bytes beyond token.end().
 */
ZL_FORCE_INLINE bool isTokenInSet(__m256i const bitmapV, std::string_view token)
{
    size_t const size = alignUp(token.size(), 32);
    ZL_ASSERT_GE(size, 32);

    size_t i;
    for (i = 0; ZL_UNLIKELY(i + 32 < size); i += 32) {
        uint32_t const mask = isInSetV(
                bitmapV,
                _mm256_loadu_si256((__m256i_u*)(token.data() + i)),
                /* checkMSB */ false);
        if (~mask) {
            return false;
        }
    }
    uint32_t const mask = isInSetV(
            bitmapV,
            _mm256_loadu_si256((__m256i_u*)(token.data() + i)),
            /* checkMSB */ false);
    if (~mask << (size - token.size())) {
        return false;
    }
    return true;
}

#endif

/// Determines of @p token is likely an integer.
/// WARNING: If kFast we assume we can access up to 32 bytes beyond token.end()
template <bool kFast>
ZL_FORCE_INLINE bool isInt(std::string_view token)
{
#ifdef __AVX2__
    if constexpr (kFast) {
        uint64_t const kBitsetLo = 0x808080808080808;
        uint64_t const kBitsetHi = 0x000040000000808;
        __m256i const kBitmapV =
                _mm256_setr_epi64x(kBitsetLo, kBitsetHi, kBitsetLo, kBitsetHi);
        return isTokenInSet(kBitmapV, token);
    }
#endif
    return std::all_of(token.begin(), token.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c == '-');
    });
}

/// Determines of @p token is likely a float.
/// WARNING: If kFast we assume we can access up to 32 bytes beyond token.end()
template <bool kFast>
ZL_FORCE_INLINE bool isFloat(std::string_view token)
{
#ifdef __AVX2__
    if constexpr (kFast) {
        uint64_t const kBitsetLo = 0x808580808080808;
        uint64_t const kBitsetHi = 0x004040004000808;
        __m256i const kBitmapV =
                _mm256_setr_epi64x(kBitsetLo, kBitsetHi, kBitsetLo, kBitsetHi);
        return isTokenInSet(kBitmapV, token);
    }
#endif
    return std::all_of(token.begin(), token.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c == '+') || (c == '-') || (c == '.')
                || (c == 'e') || (c == 'E');
    });
}

/// Equivalent to buildBitmaskFallback(bitmask, src, isInSet, 0, true).
ZL_FORCE_NOINLINE std::string_view buildBitmask(
        uint64_t* bitmask,
        std::string_view& src)
{
    std::fill(bitmask, bitmask + kBitmaskSize, 0);
    size_t blockSize = 0;
#ifdef __AVX2__
    blockSize = alignDown(std::min(src.size(), kBlockSize), 32);

    uint32_t* bitmask32 = (uint32_t*)bitmask;

    uint64_t const kBitsetLo = 0xFCFCFCFCFCF8FCFC;
    uint64_t const kBitsetHi = 0x7CFC5CD85CF4FCFC;
    __m256i const kBitmapV =
            _mm256_setr_epi64x(kBitsetLo, kBitsetHi, kBitsetLo, kBitsetHi);

    for (size_t i = 0, o = 0; i < blockSize; i += 32, ++o) {
        __m256i const srcV =
                _mm256_loadu_si256((const __m256i_u*)(src.data() + i));
        uint32_t const mask = isInSetV(kBitmapV, srcV, /* checkMSB */ true);
        bitmask32[o]        = mask;
    }
#endif

    return buildBitmaskFallback(
            bitmask, src, isInSet, blockSize, /* extendBlock */ true);
}

/// Given a token, in which every character isInSet, determine which stream to
/// dispatch it to.
/// WARNING: If kFast we assume we can access up to 32 bytes beyond token.end()
template <bool kFast>
ZL_FORCE_INLINE Token
dispatchToken(Extracted& extracted, std::string_view token)
{
    ZL_ASSERT(std::all_of(token.begin(), token.end(), isInSet));
    Token out;
    if (isFloat<kFast>(token)) {
        if (isInt<kFast>(token)) {
            extracted.pushInt<kFast>(token);
            out = Token::INT;
        } else {
            extracted.pushFloat<kFast>(token);
            out = Token::FLOAT;
        }
    } else if (token == "true") {
        out = Token::TRUE;
    } else if (token == "false") {
        out = Token::FALSE;
    } else if (token == "null") {
        out = Token::NUL;
    } else {
        extracted.pushStr<kFast>(token);
        out = Token::STR;
    }
    extracted.pushJson(out);
    return out;
}

/**
 * Given the @p bitmask describing whether each char in @p src isInSet, locate
 * each token and extract it using @p dispatchToken.
 */
ZL_FORCE_NOINLINE void extractTokens(
        Extracted& extracted,
        std::string_view src,
        uint64_t const* bitmask)
{
    size_t idx           = 0;
    int64_t mask         = (int64_t)bitmask[idx];
    uint32_t skipped     = 0;
    char const* tokenEnd = src.data();
    Token prev           = Token(0);
    char const* fastEnd;
    if (src.size() > 32) {
        fastEnd = src.end() - 31;
    } else {
        fastEnd = src.data();
    }
    for (;;) {
        while (mask == 0) {
            // Mask has no remaining tokens => go to the next mask
            ++idx;
            if (idx == kBitmaskSize) {
                // Push the final JSON
                extracted.pushJson<false>(
                        std::string_view{ tokenEnd, src.end() });
                goto end;
            }
            mask    = (int64_t)bitmask[idx];
            skipped = 0;
        }
        ZL_ASSERT_NE(mask, 0);

        // Skip to the start of the token
        {
            auto const toSkip = countFirstZeros(mask);
            mask              = skipN(mask, toSkip);
            skipped += toSkip;
            ZL_ASSERT_LT(skipped, 64);
        }

        char const* const start = src.data() + idx * 64 + skipped;

        while (~mask == 0) {
            // The token extends byeond the current mask
            ++idx;
            if (idx == kBitmaskSize) {
                extracted.pushJson<false>(std::string_view{ tokenEnd, start });
                // The token extends to the end of the input
                dispatchToken<false>(
                        extracted, std::string_view{ start, src.end() });
                goto end;
            }
            mask    = (int64_t)bitmask[idx];
            skipped = 0;
        }
        ZL_ASSERT_NE(~mask, 0);

        // Skip to the end of the token
        {
            auto const toSkip = countFirstZeros(~mask);
            mask              = skipN(mask, toSkip);
            skipped += toSkip;
            ZL_ASSERT_LE(skipped, 64);
        }

        char const* end = src.data() + idx * 64 + skipped;
        Token next;
        if (end < fastEnd) {
            extracted.pushJson<true>(std::string_view{ tokenEnd, start });
            next = dispatchToken<true>(
                    extracted, std::string_view{ start, end });
        } else {
            extracted.pushJson<false>(std::string_view{ tokenEnd, start });
            next = dispatchToken<false>(
                    extracted, std::string_view{ start, end });
        }

        ZL_ASSERT(!(prev == next && tokenEnd == start));
        prev = next;

        tokenEnd = end;
    }
end:
    ZL_ASSERT_EQ(idx, kBitmaskSize);
}

void validateExtraction(Extracted const& extracted)
{
    (void)extracted;
#ifndef NDEBUG
    char prev = char(Token::END) + 1;
    for (auto const c : extracted.jsonContent()) {
        if (c >= char(Token::START) && c <= char(Token::END)) {
            // No double tokens without gaps
            ZL_ASSERT_NE(prev, c);
            // Tokens are in our set
            ZL_ASSERT(isInSet(c));
        } else {
            // Not in set
            ZL_ASSERT(!isInSet(c));
        }
        prev = c;
    }
#endif
}

ZL_Report jsonExtractEncode(ZL_Encoder* eictx, const ZL_Input* input) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    std::string_view src{ (char const*)ZL_Input_ptr(input),
                          ZL_Input_numElts(input) };

    std::array<uint64_t, kBitmaskSize> bitmask;
    Extracted extracted(eictx, src);

    while (!src.empty()) {
        auto const block = buildBitmask(bitmask.data(), src);
        extractTokens(extracted, block, bitmask.data());
    }
    validateExtraction(extracted);

    ZL_ERR_IF_ERR(extracted.commit());

    return ZL_returnSuccess();
}
} // namespace
} // namespace zstrong

ZL_NodeID ZS2_Compressor_registerJsonExtract(
        ZL_Compressor* cgraph,
        ZL_IDType transformID)
{
    std::array<ZL_Type, 4> const kOutStreams = {
        ZL_Type_serial, ZL_Type_string, ZL_Type_string, ZL_Type_string
    };
    ZL_TypedGraphDesc graph = {
        .CTid           = transformID,
        .inStreamType   = ZL_Type_serial,
        .outStreamTypes = kOutStreams.data(),
        .nbOutStreams   = kOutStreams.size(),
    };
    ZL_TypedEncoderDesc desc = {
        .gd          = graph,
        .transform_f = zstrong::jsonExtractEncode,
        .name        = "json extract",
    };
    return ZL_Compressor_registerTypedEncoder(cgraph, &desc);
}
