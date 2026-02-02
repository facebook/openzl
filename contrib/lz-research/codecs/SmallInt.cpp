// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "SmallInt.hpp"
#include "CodecIDs.hpp"

#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/simd_wrapper.h"

#if ZL_ARCH_X86_64
#    include <immintrin.h>
#endif

namespace openzl::lz {
namespace {
SimpleCodecDescription smallIntCodecDescription()
{
    return SimpleCodecDescription{
        .id          = unsigned(CustomCodecIDs::SmallInt),
        .name        = "!lz_research.small_int",
        .inputType   = Type::Numeric,
        .outputTypes = { Type::Serial, Type::Numeric },
    };
}
} // namespace

SimpleCodecDescription SmallIntEncoder::simpleCodecDescription() const
{
    return smallIntCodecDescription();
}

template <typename Int>
size_t encodeImpl(
        uint8_t* __restrict smallOut,
        Int* __restrict largeOut,
        Int const* __restrict in,
        size_t nbElts)
{
    size_t nbLarge = 0;

    const size_t kUnroll = 16 / sizeof(*in);

    const size_t prefix = nbElts % kUnroll;
    for (size_t i = 0; i < prefix; ++i) {
        if (ZL_LIKELY(in[i] < 255)) {
            smallOut[i] = in[i];
        } else {
            smallOut[i]         = 255;
            largeOut[nbLarge++] = in[i] - 255;
        }
    }

    // TODO: This could be fully vectorized
    for (size_t i = prefix; i < nbElts; i += kUnroll) {
#if ZL_ARCH_X86_64
        if (sizeof(*in) == 2) {
            __m128i const v       = _mm_loadu_si128((__m128i const*)(in + i));
            __m128i const clamped = _mm_min_epu16(v, _mm_set1_epi16(254));
            __m128i const c       = _mm_cmpeq_epi16(clamped, v);
            if (_mm_movemask_epi8(c) == 0xffff) {
                __m128i const p = _mm_packus_epi16(v, v);
                _mm_storel_epi64((__m128i*)(smallOut + i), p);
                continue;
            }
        }
#endif

        for (size_t u = 0; u < kUnroll; ++u) {
            if (ZL_LIKELY(in[i + u] < 255)) {
                smallOut[i + u] = in[i + u];
            } else {
                smallOut[i + u]     = 255;
                largeOut[nbLarge++] = in[i + u] - 255;
            }
        }
    }
    return nbLarge;
}

void SmallIntEncoder::encode(EncoderState& encoder) const
{
    auto& input         = encoder.inputs()[0];
    auto const nbElts   = input.numElts();
    auto const eltWidth = input.eltWidth();
    auto smallStream    = encoder.createOutput(0, nbElts, 1);
    auto largeStream    = encoder.createOutput(1, nbElts, eltWidth);

    uint8_t* const smallOut = (uint8_t*)smallStream.ptr();

    size_t nbLarge;
    void* largePtr    = largeStream.ptr();
    void const* inPtr = input.ptr();
    switch (eltWidth) {
        case 1:
            memcpy(smallOut, inPtr, nbElts);
            nbLarge = 0;
            break;
        case 2:
            nbLarge = encodeImpl(
                    smallOut,
                    (uint16_t*)largePtr,
                    (uint16_t const*)inPtr,
                    nbElts);
            break;
        case 4:
            nbLarge = encodeImpl(
                    smallOut,
                    (uint32_t*)largePtr,
                    (uint32_t const*)inPtr,
                    nbElts);
            break;
        case 8:
            nbLarge = encodeImpl(
                    smallOut,
                    (uint64_t*)largePtr,
                    (uint64_t const*)inPtr,
                    nbElts);
            break;
    };

    smallStream.commit(nbElts);
    largeStream.commit(nbLarge);
}

SimpleCodecDescription SmallIntDecoder::simpleCodecDescription() const
{
    return smallIntCodecDescription();
}

template <typename Int>
void decodeImpl(
        Int* __restrict out,
        uint8_t const* __restrict smallIn,
        size_t nbElts,
        Int const* __restrict largeIn,
        size_t nbLarge)
{
    size_t const nbLargeNeeded = std::count_if(
            smallIn, smallIn + nbElts, [](uint8_t val) { return val == 255; });
    if (nbLarge != nbLargeNeeded) {
        throw std::runtime_error("nbLarge != nbLargeNeeded");
    }

    size_t const prefix = nbElts & 15;

    size_t largeIdx = 0;
    for (size_t i = 0; i < prefix; ++i) {
        if (ZL_LIKELY(smallIn[i] != 255)) {
            out[i] = smallIn[i];
        } else {
            out[i] = 255 + largeIn[largeIdx++];
        }
    }

    const ZL_Vec128 v255 = ZL_Vec128_set8(255);

    // TODO: This could be fully vectorized
    ZL_ASSERT_EQ((nbElts - prefix) % 16, 0);
    for (size_t i = prefix; i < nbElts; i += 16) {
        const int anyLarge = ZL_Vec128_mask8(
                ZL_Vec128_cmp8(ZL_Vec128_read(smallIn + i), v255));
        if (ZL_LIKELY(anyLarge == 0)) {
            for (size_t j = 0; j < 16; ++j) {
                out[i + j] = smallIn[i + j];
            }
        } else {
            for (size_t j = 0; j < 16; ++j) {
                if (smallIn[i + j] != 255) {
                    out[i + j] = smallIn[i + j];
                } else {
                    out[i + j] = 255 + largeIn[largeIdx++];
                }
            }
        }
    }
}

void SmallIntDecoder::decode(DecoderState& decoder) const
{
    auto& smallStream = decoder.singletonInputs()[0];
    auto& largeStream = decoder.singletonInputs()[1];

    size_t const nbElts   = smallStream.numElts();
    size_t const eltWidth = largeStream.eltWidth();
    size_t const nbLarge  = largeStream.numElts();

    auto outStream = decoder.createOutput(0, nbElts, eltWidth);

    uint8_t const* const smallIn = (uint8_t const*)smallStream.ptr();

    void* const outPtr         = outStream.ptr();
    void const* const largePtr = largeStream.ptr();
    switch (eltWidth) {
        case 1:
            if (nbLarge != 0) {
                throw std::runtime_error("nbLarge != 0");
            }
            memcpy(outPtr, smallIn, nbElts);
            break;
        case 2:
            decodeImpl(
                    (uint16_t*)outPtr,
                    smallIn,
                    nbElts,
                    (uint16_t const*)largePtr,
                    nbLarge);
            break;
        case 4:
            decodeImpl(
                    (uint32_t*)outPtr,
                    smallIn,
                    nbElts,
                    (uint32_t const*)largePtr,
                    nbLarge);
            break;
        case 8:
            decodeImpl(
                    (uint64_t*)outPtr,
                    smallIn,
                    nbElts,
                    (uint64_t const*)largePtr,
                    nbLarge);
            break;
    }

    outStream.commit(nbElts);
}

size_t
SmallInt::encode(uint8_t* small, uint16_t* large, const uint16_t* src, size_t n)
{
    return encodeImpl(small, large, src, n);
}

void SmallInt::decode(
        uint16_t* dst,
        const uint8_t* small,
        size_t numSmall,
        const uint16_t* large,
        size_t numLarge)
{
    decodeImpl(dst, small, numSmall, large, numLarge);
}

} // namespace openzl::lz
