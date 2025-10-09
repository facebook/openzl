// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/assertion.h"
#include "openzl/shared/bits.h"

#include "openzl/common/errors_internal.h"
#include "openzl/zl_data.h"
#include "openzl/zl_dtransform.h"

#include <array>
#include <cstring>

namespace zstrong {
namespace {
size_t constexpr maxStrLen(int64_t)
{
    return 21;
}

} // namespace
} // namespace zstrong

namespace zstrong {
namespace {

// Calculates the number of digits in the base 10 representation of `x`.
// Assumes `x` is non-negative.
uint8_t u64BaseTenDigits(uint64_t x)
{
    // Pre-calculated table of `pow(10, (int)log10((1<<(64-i))-1))`
    constexpr std::array<uint64_t, 64> tenPower = {
        10000000000000000000u,
        1000000000000000000,
        1000000000000000000,
        1000000000000000000,
        1000000000000000000,
        100000000000000000,
        100000000000000000,
        100000000000000000,
        10000000000000000,
        10000000000000000,
        10000000000000000,
        1000000000000000,
        1000000000000000,
        1000000000000000,
        1000000000000000,
        100000000000000,
        100000000000000,
        100000000000000,
        10000000000000,
        10000000000000,
        10000000000000,
        1000000000000,
        1000000000000,
        1000000000000,
        1000000000000,
        100000000000,
        100000000000,
        100000000000,
        10000000000,
        10000000000,
        10000000000,
        1000000000,
        1000000000,
        1000000000,
        1000000000,
        100000000,
        100000000,
        100000000,
        10000000,
        10000000,
        10000000,
        1000000,
        1000000,
        1000000,
        1000000,
        100000,
        100000,
        100000,
        10000,
        10000,
        10000,
        1000,
        1000,
        1000,
        1000,
        100,
        100,
        100,
        10,
        10,
        10,
        1,
        1,
        0,
    };
    // We calculate the rounded-up base-10 logarithm of `x` based on its base-2
    // logarithm (`64-clz`).
    // However, for some values of the base-2 logarithm we might have two
    // possible values we distinguish between the two by comparing to the
    // 10-power of the approximation we get and checking if the value is greater
    // than our approximation.
    const unsigned clz   = ZL_clz64((uint64_t)x | 1);
    const uint8_t approx = ((64 - clz) * 1233) >> 12;
    return approx + (x >= tenPower[clz]);
}

// Returns the length of the string representation of `value`.
// Includes the sign if `value` is a negative number.
template <typename T>
uint8_t numberStringLength(T value)
{
    const uint64_t absValue =
            value < 0 ? ((~(uint64_t)(int64_t)value) + 1) : value;
    return u64BaseTenDigits(absValue) + (value < 0 ? 1 : 0);
}

// Copies a 4 byte string representation of the number `x` into `dst`.
// `x` must be a positive number and at most 9999.
ZL_INLINE void copy4FromTable(char* dst, uint64_t x)
{
    ZL_ASSERT_LT(x, 10000);
    constexpr auto table = []() {
        std::array<char, 4 * 10000> t{};
        for (int i = 0; i < 10000; i++) {
            int x = i;
            for (int j = 3; j >= 0; j--) {
                t[i * 4 + j] = (char)(x % 10 + '0');
                x /= 10;
            }
        }
        return t;
    }();
    memcpy(dst, &table[4 * x], 4);
}

// Converts the 64-bit unsigned number `x` to a string of digits in decimal.
// The number is written *backwards* into `out`.
// `out` must have at least 20 digits available backwards (i.e `out-20...out-1`
// should be writable and may be overwritten). `len` is the maximum number of
// digits in the decimal representation for the number. It's used for
// optimization and the function may write more then `len` characters into
// `out`.
// The functionality is based on
// `https://lemire.me/blog/2021/11/18/converting-integers-to-fix-digit-representations-quickly/`
// but we have made multiple modification:
// 1. Branch based on `len` so we skip unneeded calculations and memory
//   accesses. This works as long as most of our numbers are consistent in their
//   lengths, which is to be expected in most use-cases we handle.s
// 2. We write backwards.
// 3. We support 20 digits insteaf o 16.
void u64NumberToStringBackwards(uint64_t x, char* out, uint8_t len)
{
    if (len <= 4) {
        ZL_ASSERT_LT(x, 10000);
        copy4FromTable(out - 4, x);
        return;
    }

    uint64_t bottom       = x % 100000000;
    uint64_t bottomtop    = bottom / 10000;
    uint64_t bottombottom = bottom % 10000;
    copy4FromTable(out - 8, bottomtop);
    copy4FromTable(out - 4, bottombottom);

    if (len >= 8) {
        uint64_t top         = x / 100000000;
        uint64_t toptop      = top / 10000;
        uint64_t toptoptop   = toptop / 10000;
        uint64_t toptopottom = toptop % 10000;
        uint64_t topbottom   = top % 10000;
        copy4FromTable(out - 20, toptoptop);
        copy4FromTable(out - 16, toptopottom);
        copy4FromTable(out - 12, topbottom);
    }
}

// Converts the integer `value` to a string of digits in decimal.
// If the number type is signed, the character '-' is prepended.
// The number is written *backwards* into `out`.
// `out` must have at least `maxStrLen` bytes available backwards (i.e
// `out-maxStrLen...out-1` should be writable and may be overwritten). `len` is
// the maximum number of digits in the decimal representation for the number.
// It's used for optimization and the function may write more then `len`
// characters into `out`.
template <typename T>
ZL_INLINE void numberToStringBackwards(T value, char* out, uint8_t len)
{
    const uint8_t neg       = value < 0 ? 1 : 0;
    const uint64_t absValue = neg ? ((~(uint64_t)(int64_t)value) + 1) : value;
    u64NumberToStringBackwards(absValue, out, len - neg);
    if constexpr (std::is_signed<T>::value)
        *(out - len + neg - 1) = '-';
}

// Calculates and fills the field sizes matchings parsed integer numbers
// and parse exceptiongs.
// `fieldSizes` must contain at least `nbElts` elements.
// Returns the sum of the filled `filedSizes`.
template <typename T>
ZL_Report parseDecodeIntFillFieldSizes(
        size_t nbElts,
        T const* nums,
        size_t nbNums,
        uint32_t const* exIdxs,
        uint32_t const* exSizes,
        uint32_t* fieldSizes)
{
    ZL_ASSERT_LE(nbNums, nbElts);
    auto const numsEnd = nums + nbNums;
    auto exIdxsEnd     = exIdxs + (nbElts - nbNums);
    size_t outSize     = 0;
    for (size_t i = 0; i < nbElts; ++i) {
        size_t fieldSize = 0;
        if (exIdxs < exIdxsEnd && i == *exIdxs) {
            ++exIdxs;
            fieldSize = *exSizes++;
        } else {
            ZL_RET_R_IF_EQ(srcSize_tooSmall, nums, numsEnd);
            fieldSize = numberStringLength(*(nums++));
        }
        fieldSizes[i] = fieldSize;
        outSize += fieldSize;
    }
    ZL_RET_R_IF_NE(corruption, nums, numsEnd);
    ZL_RET_R_IF_NE(corruption, exIdxs, exIdxsEnd);
    return ZL_returnValue(outSize);
}

// Fills the content of the buffer `dst` with the decoded parsed integers
// from `nums` and the exceptions.
// `dst` must be large enough to contain the decoded integers and exceptions
// (must be at least the length from `parseDecodeIntFillFieldSizes`).
// `dstSize` must be the exact size returned by `parseDecodeIntFillFieldSizes`.
// `fieldSizes` must also be correct and match the actual length of each of the
// fields.
template <typename T>
void parseDecodeIntFillContent(
        size_t const nbElts,
        T const* const nums,
        size_t const nbNums,
        uint32_t const* const exIdxs,
        char const* const exData,
        size_t const exDataSize,
        uint32_t const* const fieldSizes,
        size_t const dstSize,
        uint8_t* dst)
{
    auto const numsEnd   = nums + nbNums;
    auto const exIdxsEnd = exIdxs + (nbElts - nbNums);
    auto const dstStart  = dst;

    // We start by writing backwards, we can do so safely as long as we have at
    // least maxStrLen bytes of backwards space available.
    int64_t backwardsIndex = nbElts;
    {
        auto currNums   = numsEnd;
        auto currExIds  = exIdxsEnd - 1;
        auto currDst    = dst + dstSize;
        auto currExData = exData + exDataSize;
        while (currDst >= dst + maxStrLen(T{})) {
            backwardsIndex--;
            const auto fieldSize = fieldSizes[backwardsIndex];
            currDst -= fieldSize;
            if (currExIds >= exIdxs && backwardsIndex == *currExIds) {
                currExData -= fieldSize;
                memcpy(currDst, currExData, fieldSize);
                --currExIds;
            } else {
                ZL_ASSERT_GT(currDst + fieldSize, dstStart);
                ZL_ASSERT_GE(currDst + fieldSize - maxStrLen(T{}), dstStart);
                ZL_ASSERT_LE(fieldSize, maxStrLen(T{}));
                numberToStringBackwards(
                        *(--currNums), (char*)currDst + fieldSize, fieldSize);
            }
        }
        ZL_ASSERT_GE(currDst, dstStart);
    }

    // Write the rest of the fields forwards, requires using scratch
    // space with enough "backwards" space to allow overwirte.
    {
        uint8_t* currDst = dst;
        auto currNums    = nums;
        auto currExIds   = exIdxs;
        auto currExData  = exData;
        char scratchSpace[maxStrLen(T{})];
        for (int64_t i = 0; i < backwardsIndex; ++i) {
            auto fieldSize = fieldSizes[i];
            if (currExIds < exIdxsEnd && i == *currExIds) {
                ++currExIds;
                memcpy(currDst, currExData, fieldSize);
                currExData += fieldSize;
            } else {
                ZL_ASSERT_LE(fieldSize, maxStrLen(T{}));
                numberToStringBackwards(
                        *(currNums++),
                        (char*)scratchSpace + maxStrLen(T{}),
                        fieldSize);
                memcpy(currDst,
                       scratchSpace + maxStrLen(T{}) - fieldSize,
                       fieldSize);
            }
            currDst += fieldSize;
        }
    }
}

} // namespace
} // namespace zstrong

namespace zstrong {
namespace {
template <typename T>
ZL_Report parseDecodeInt(ZL_Decoder* dictx, ZL_Input const* inputs[]) noexcept
{
    ZL_Input const* numbers          = inputs[0];
    ZL_Input const* exceptionIndices = inputs[1];
    ZL_Input const* exceptions       = inputs[2];

    ZL_RET_R_IF_NE(
            corruption,
            ZL_Input_numElts(exceptionIndices),
            ZL_Input_numElts(exceptions));
    ZL_RET_R_IF_NE(corruption, ZL_Input_eltWidth(exceptionIndices), 4);
    ZL_RET_R_IF_NE(corruption, ZL_Input_eltWidth(numbers), sizeof(T));

    // Note: we calculate the maximal outbound and allocate a matching outstream
    // this is inefficient as before we need the actual out stream we calculate
    // its exact size when populating the filesizes.
    // However, allocating the filedsizes stream requires first allocating the
    // underlying data strea, which is we can't reverse the order and need
    // a wasteful upper bound allocation.
    size_t const outBound = ZL_Input_contentSize(exceptions)
            + ZL_Input_numElts(numbers) * maxStrLen(T{});
    ZL_Output* outStream = ZL_Decoder_create1OutStream(dictx, outBound, 1);
    ZL_RET_R_IF_NULL(allocation, outStream);

    size_t const nbElts =
            ZL_Input_numElts(numbers) + ZL_Input_numElts(exceptions);

    uint32_t* fieldSizes = ZL_Output_reserveStringLens(outStream, nbElts);
    ZL_RET_R_IF_NULL(allocation, fieldSizes);

    auto nums   = (T const*)ZL_Input_ptr(numbers);
    auto nbNums = ZL_Input_numElts(numbers);

    auto exIdxs     = (uint32_t const*)ZL_Input_ptr(exceptionIndices);
    auto exData     = (char const*)ZL_Input_ptr(exceptions);
    auto exSizes    = ZL_Input_stringLens(exceptions);
    auto exDataSize = ZL_Input_contentSize(exceptions);

    ZL_TRY_LET_R(
            outSize,
            parseDecodeIntFillFieldSizes(
                    nbElts, nums, nbNums, exIdxs, exSizes, fieldSizes));
    ZL_ASSERT_LE(outSize, outBound);

    parseDecodeIntFillContent(
            nbElts,
            nums,
            nbNums,
            exIdxs,
            exData,
            exDataSize,
            fieldSizes,
            outSize,
            (uint8_t*)ZL_Output_ptr(outStream));
    ZL_RET_R_IF_ERR(ZL_Output_commit(outStream, nbElts));

    return ZL_returnSuccess();
}

} // namespace
} // namespace zstrong

extern "C" ZL_Report ZL_DCtx_registerParseInt64(
        ZL_DCtx* dctx,
        ZL_IDType transformID)
{
    std::array<ZL_Type, 3> const kOutStreams = {
        ZL_Type_numeric,
        ZL_Type_numeric,
        ZL_Type_string,
    };
    ZL_TypedGraphDesc graph = {
        .CTid           = transformID,
        .inStreamType   = ZL_Type_string,
        .outStreamTypes = kOutStreams.data(),
        .nbOutStreams   = kOutStreams.size(),
    };
    ZL_TypedDecoderDesc desc = {
        .gd          = graph,
        .transform_f = zstrong::parseDecodeInt<int64_t>,
        .name        = "parse int64",
    };
    return ZL_DCtx_registerTypedDecoder(dctx, &desc);
}
