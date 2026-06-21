// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "VarByte.hpp"
#include "CodecIDs.hpp"
#include "utils/Portability.hpp"

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"

#if ZL_ARCH_X86_64
#    include <immintrin.h>
#endif
#ifdef __ARM_FEATURE_SVE
#    include <arm_sve.h>
#endif /* __ARM_FEATURE_SVE */
#ifdef __aarch64__
#    include <arm_neon.h>
#endif /* __aarch64__ */

namespace openzl::lz {
namespace {
SimpleCodecDescription varByteCodecDescription()
{
    return SimpleCodecDescription{
        .id          = unsigned(CustomCodecIDs::VarByte),
        .name        = "!lz_research.var_byte",
        .inputType   = Type::Numeric,
        .outputTypes = { Type::Serial, Type::Serial },
    };
}

// using LUT = std::array<std::array<uint32_t, 4>, 256>;
// using LUTx2 = std::pair<LUT, LUT>;

// using DualLUT = std::array<uint32_t, 4 * 256 + 2>;

// using LUT = std::span<uint32_t const, 4 * 256>;
// using MutLUT = std::span<uint32_t, 4 * 256>;

// std::pair<LUT, LUT> getLUTs(DualLUT const& lut) {
//   return std::pair{LUT{lut.data() + 2, 4 * 256}, LUT{lut.data(), 4 * 256}};
// }

// std::pair<MutLUT, MutLUT> getLUTs(DualLUT& lut) {
//   return std::pair{
//       MutLUT{lut.data() + 2, 4 * 256}, MutLUT{lut.data(), 4 * 256}};
// }

#if ZL_ARCH_X86_64

class alignas(16) DualLUT {
   public:
    constexpr explicit DualLUT(
            std::array<std::array<uint32_t, 4>, 256> const& lo)
    {
        for (size_t i = 0; i < lo.size(); ++i) {
            for (size_t j = 0; j < lo[i].size(); ++j) {
                for (size_t k = 0; k < 4; ++k) {
                    data_[16 * i + 4 * j + k] = lo[i][j] >> (8 * k);
                }
            }
        }
    }

    constexpr explicit DualLUT(
            std::array<std::array<uint8_t, 16>, 256> const& lo)
    {
        for (size_t i = 0; i < lo.size(); ++i) {
            for (size_t j = 0; j < lo[i].size(); ++j) {
                data_[16 * i + j] = lo[i][j];
            }
        }
    }

    __m128i at(size_t loIdx, size_t hiIdx) const
    {
        auto lo = loAt(loIdx);
        auto hi = hiAt(hiIdx);
        return _mm_add_epi8(lo, hi);
    }

    __m128i loAt(size_t idx) const
    {
        assert(idx < 256);
        return load(16 * idx);
    }

    __m128i hiAt(size_t idx) const
    {
        assert(idx < 256);
        // auto const mask = _mm_set_epi64x(-1, 0);
        // return _mm_and_si128(load(16 * idx), mask);
        return _mm_bslli_si128(loAt(idx), 8);
    }

   private:
    __m128i load(size_t idx) const
    {
        return _mm_load_si128((__m128i const*)&data_[idx]);
    }

    std::array<uint8_t, 16 * 256> data_;
};
#endif

template <
        uint32_t B0,
        uint32_t B1,
        uint32_t B2,
        uint32_t B3,
        uint32_t B4,
        uint32_t B5,
        uint32_t B6,
        uint32_t B7,
        uint32_t B8,
        uint32_t B9,
        uint32_t B10,
        uint32_t B11,
        uint32_t B12,
        uint32_t B13,
        uint32_t B14,
        uint32_t B15>
class VarByteCoder {
   public:
    constexpr VarByteCoder() {}

#if ZL_ARCH_X86_64
    static void print32(char const* name, __m128i vec)
    {
        std::array<uint32_t, 4> array;
        _mm_storeu_si128((__m128i_u*)array.data(), vec);
        fprintf(stderr, "%s: ", name);
        for (auto const& val : array) {
            fprintf(stderr, "0x%x\t", val);
        }
        fprintf(stderr, "\n");
    }

    static void print16(char const* name, __m128i vec)
    {
        std::array<uint16_t, 8> array;
        _mm_storeu_si128((__m128i_u*)array.data(), vec);
        fprintf(stderr, "%s: ", name);
        for (auto const& val : array) {
            fprintf(stderr, "0x%x\t", val);
        }
        fprintf(stderr, "\n");
    }

    static void print8(char const* name, __m128i vec)
    {
        std::array<uint8_t, 16> array;
        _mm_storeu_si128((__m128i_u*)array.data(), vec);
        fprintf(stderr, "%s: ", name);
        for (auto const& val : array) {
            fprintf(stderr, "0x%x\t", val);
        }
        fprintf(stderr, "\n");
    }

    __m128i decode4(
            uint8_t control0,
            uint8_t control1,
            __m128i data,
            size_t& bitsConsumed) const
    {
        auto const [shuffle, shift] =
                getShuffleAndShift(control0, control1, bitsConsumed);
        auto const mask     = getMask(control0, control1);
        auto const base     = getBase(control0, control1);
        auto const shuffled = _mm_shuffle_epi8(data, shuffle);
        auto const shifted  = _mm_srlv_epi32(shuffled, shift);
        auto const masked   = _mm_and_si128(shifted, mask);
        auto const based    = _mm_add_epi32(masked, base);
        // static size_t idx = 0;
        // if (idx++ < 2) {
        //   fprintf(
        //       stderr,
        //       "consumed = %zu | control0 = 0x%x | control1 = 0x%x\n",
        //       bitsConsumed,
        //       control0,
        //       control1);
        //   print8("shuffleLO", shuffleShiftLUT_.loAt(control0));
        //   print8("shuffleHI", shuffleShiftLUT_.hiAt(control1));
        //   print8("shuffle", shuffle);
        //   print32("shift", shift);
        //   print32("mask", mask);
        //   print32("shuffled", shuffled);
        //   print32("shifted", shifted);
        //   print32("masked", masked);
        //   fprintf(stderr, "bitsConsumed0 = %u\n",
        //   bitsConsumedLUT_[control0]); fprintf(stderr, "bitsConsumed1 =
        //   %u\n", bitsConsumedLUT_[control1]);
        // }
        bitsConsumed += bitsConsumedLUT_[control0];
        bitsConsumed += bitsConsumedLUT_[control1];
        return based;
    }
#endif

    uint32_t decode1(uint8_t control, uint32_t data, size_t& bitsConsumed) const
    {
        assert(control < 16);
        auto const bits  = bits_[control];
        auto const value = (data >> (bitsConsumed % 8)) & ((1u << bits) - 1);
        bitsConsumed += bits;
        return baseLUT_[control] + value;
    }

    void decode4(
            uint16_t* __restrict out,
            std::pair<uint32_t, uint32_t> e0,
            std::pair<uint32_t, uint32_t> e1,
            const uint8_t* __restrict bytes,
            size_t& bitsConsumed) const
    {
        const size_t bytesOffset = bitsConsumed / 8;
        const size_t bitsOffset  = bitsConsumed % 8;

        uint64_t data       = ZL_readLE64(bytes + bytesOffset) >> bitsOffset;
        const uint64_t mask = uint64_t(e0.second) | (uint64_t(e1.second) << 32);
        const uint64_t base = uint64_t(e0.first) | (uint64_t(e1.first) << 32);

        const int bitsNeeded = __builtin_popcountll(mask);
        if (ZL_LIKELY(bitsNeeded <= 56) || bitsOffset == 0) {
            ZL_writeLE64(out, base + utils::bitDeposit(data, mask));
        } else {
            data |= uint64_t(bytes[bytesOffset + 8]) << (64 - bitsOffset);
            ZL_writeLE64(out, base + utils::bitDeposit(data, mask));
        }
        bitsConsumed += bitsNeeded;
    }

    void decode8xU16(
            uint16_t* out,
            const uint8_t* control,
            const uint8_t* bytes,
            size_t& bitsConsumed) const
    {
        const auto e0 = combined2xLUTU16_[control[0]];
        const auto e1 = combined2xLUTU16_[control[1]];
        const auto e2 = combined2xLUTU16_[control[2]];
        const auto e3 = combined2xLUTU16_[control[3]];
        decode4(out, e0, e1, bytes, bitsConsumed);
        decode4(out + 4, e2, e3, bytes, bitsConsumed);
    }

    template <typename UInt>
    void decode8(
            UInt* __restrict out,
            size_t outIdx,
            const uint8_t* __restrict control,
            const uint8_t* __restrict bytes,
            size_t& bitsConsumed) const

    {
        size_t const controlIdx = outIdx / 2;
        if constexpr (1 && sizeof(UInt) == 2) {
            decode8xU16(
                    &out[outIdx], control + controlIdx, bytes, bitsConsumed);
        } else {
#if ZL_ARCH_X86_64
            auto outVec0 = decode4(
                    control[controlIdx + 0],
                    control[controlIdx + 1],
                    _mm_loadu_si128((__m128i_u const*)&bytes[bitsConsumed / 8]),
                    bitsConsumed);
            auto outVec1 = decode4(
                    control[controlIdx + 2],
                    control[controlIdx + 3],
                    _mm_loadu_si128((__m128i_u const*)&bytes[bitsConsumed / 8]),
                    bitsConsumed);
            if constexpr (sizeof(UInt) == 4) {
                _mm_storeu_si128((__m128i_u*)&out[outIdx + 0], outVec0);
                _mm_storeu_si128((__m128i_u*)&out[outIdx + 4], outVec1);
            } else {
                auto const outVec = _mm_packus_epi32(outVec0, outVec1);
                _mm_storeu_si128((__m128i_u*)&out[outIdx], outVec);
            }
#else
            for (size_t i = 0; i < 8; ++i) {
                uint8_t const ctrl =
                        (control[outIdx / 2] >> (i % 2 == 0 ? 0 : 4)) % 16;
                out[outIdx + i] =
                        decode1(ctrl,
                                safeRead(bytes.subspan(bitsConsumed / 8)),
                                bitsConsumed);
            }
#endif
        }
    }

    template <typename UInt>
    void decode(
            std::span<UInt> out,
            std::span<uint8_t const> control,
            std::span<uint8_t const> bytes) const
    {
        // fprintf(
        //     stderr,
        //     "nbElts = %zu, control = %zu, bytes = %zu\n",
        //     out.size(),
        //     control.size(),
        //     bytes.size());
        static_assert(sizeof(UInt) > 1);
        static_assert(sizeof(UInt) <= 4);
        assert((out.size() + 1) / 2 == control.size());
        size_t bitsConsumed     = 0;
        size_t outIdx           = 0;
        size_t constexpr kLimit = 16 + (sizeof(UInt) == 2 ? 8 : 16);
        if (bytes.size() >= kLimit) {
            // Unrolling this loop to 16x makes it ~1% faster.
            // Consider doing that when productionizing.
            size_t const bitsLimit = 8 * (bytes.size() - kLimit);
            size_t const outLimit  = out.size() - 8;
            while (bitsConsumed < bitsLimit && outIdx < outLimit) {
                // fprintf(
                //     stderr,
                //     "bitsConsumed = %zu | limit = %zu | outIdx = %zu |
                //     ctrlIdx = %zu\n", bitsConsumed, bitsLimit, outIdx,
                //     controlIdx);
                decode8(out.data(),
                        outIdx,
                        control.data(),
                        bytes.data(),
                        bitsConsumed);
                outIdx += 8;
            }
        }
        for (; outIdx < out.size(); ++outIdx) {
            uint8_t const ctrl =
                    (control[outIdx / 2] >> (outIdx % 2 == 0 ? 0 : 4)) % 16;
            out[outIdx] =
                    decode1(ctrl,
                            safeRead(bytes.subspan(bitsConsumed / 8)),
                            bitsConsumed);
        }
        ZL_REQUIRE_EQ((bitsConsumed + 7) / 8, bytes.size());
    }

    template <typename UInt>
    size_t encode(
            std::span<uint8_t> control,
            std::span<uint8_t> bytes,
            std::span<UInt const> data) const
    {
        assert(control.size() == (data.size() + 1) / 2);
        assert(bytes.size() >= data.size() * sizeof(UInt));
        static_assert(sizeof(UInt) <= 4);
        auto const clzLUT        = makeClzLUT();
        auto const clzShiftedLut = shift4(makeClzLUT());
        auto const baseLUT       = toClzLUT(clzLUT, baseLUT_);
        auto const bitsLUT       = toClzLUT(clzLUT, bits_);

        // static bool printed = false;
        // if (!printed) {
        //   for (size_t i = 0; i < 16; ++i) {
        //     fprintf(stderr, "base[%zu] = %u\n", i, baseLUT_[i]);
        //   }
        //   printed = true;
        // }

        ZS_BitCStreamFF bytesStream =
                ZS_BitCStreamFF_init(bytes.data(), bytes.size());

        // size_t idx = 0;
        const size_t kUnroll = 6;
        const size_t limit   = data.size() - data.size() % kUnroll;

        size_t i          = 0;
        size_t controlIdx = 0;

        for (; i < limit; i += kUnroll) {
            const auto values = data.subspan(i, kUnroll);
            std::array<int, kUnroll> clz;
            for (size_t j = 0; j < kUnroll; ++j) {
                ZL_ASSERT_LT(values[j], (1u << 24));
                clz[j] = __builtin_clz(uint32_t(values[j]));
            }
            for (size_t j = 0; j < kUnroll; j += 2) {
                control[controlIdx++] =
                        clzLUT[clz[j]] | clzShiftedLut[clz[j + 1]];
            }
            for (size_t j = 0; j < kUnroll; j += 3) {
                for (size_t k = 0; k < 3; ++k) {
                    const auto val  = values[j + k];
                    const auto base = baseLUT[clz[j + k]];
                    const auto bits = bitsLUT[clz[j + k]];
                    ZS_BitCStreamFF_write(&bytesStream, val - base, bits);
                }
                ZS_BitCStreamFF_flush(&bytesStream);
            }
        }

        ZS_BitCStreamFF controlStream = ZS_BitCStreamFF_init(
                control.data() + controlIdx, control.size() - controlIdx);

        for (; i < data.size(); ++i) {
            const auto val = data[i];
            ZL_ASSERT_LT(val, (1u << 24));
            auto const ctrl = clzLUT[__builtin_clz(uint32_t(val))];
            assert(ctrl < 16);
            auto const bits = bits_[ctrl];
            assert(bits <= 32);
            ZS_BitCStreamFF_write(&controlStream, ctrl, 4);
            ZS_BitCStreamFF_write(&bytesStream, val - baseLUT_[ctrl], bits);
            ZS_BitCStreamFF_flush(&controlStream);
            ZS_BitCStreamFF_flush(&bytesStream);
        }

        // shuffle needs bits for adding
        // shuffle + shift
        // mask

        auto const controlResult = ZS_BitCStreamFF_finish(&controlStream);
        auto const bytesResult   = ZS_BitCStreamFF_finish(&bytesStream);
        ZL_REQUIRE_SUCCESS(controlResult);
        ZL_REQUIRE_SUCCESS(bytesResult);
        assert(controlIdx + ZL_validResult(controlResult) == control.size());
        assert(ZL_validResult(bytesResult) <= bytes.size());

        // std::vector<uint32_t> out;
        // out.resize(data.size());
        // decode(std::span{ out },
        //        control,
        //        bytes.subspan(0, ZL_validResult(bytesResult)));
        // for (size_t i = 0; i < data.size(); ++i) {
        //     ZL_REQUIRE_EQ(data[i], out[i], "error at idx %zu", i);
        // }

        return ZL_validResult(bytesResult);
    }

   private:
#if ZL_ARCH_X86_64
    std::pair<__m128i, __m128i> getShuffleAndShift(
            uint8_t control0,
            uint8_t control1,
            size_t bitsConsumed) const
    {
        auto const entry = shuffleShiftLUT_.at(control0, control1);
        auto const shuffleShift =
                _mm_add_epi8(entry, _mm_set1_epi8(bitsConsumed % 8));
        auto const shuffle = _mm_and_si128(
                _mm_srli_epi16(shuffleShift, 3), _mm_set1_epi8(0xF));
        auto const shift = _mm_and_si128(shuffleShift, _mm_set1_epi32(0x7));
        return { shuffle, shift };
    }

    __m128i getMask(uint8_t control0, uint8_t control1) const
    {
        return _mm_set_epi64x(maskLUT_[control1], maskLUT_[control0]);
    }

    __m128i getBase(uint8_t control0, uint8_t control1) const
    {
        return _mm_set_epi64x(base2xLUT_[control1], base2xLUT_[control0]);
    }
#endif

    constexpr std::array<uint8_t, 32> makeClzLUT() const
    {
        std::array<uint8_t, 32> clzLUT{};
        for (size_t clz = 1; clz < clzLUT.size(); ++clz) {
            size_t const numBits = 32 - clz;
            size_t const maxVal  = (1u << numBits) - 1;
            for (size_t i = 0; i < bits_.size(); ++i) {
#ifndef NDEBUG
                auto const exclusiveEnd = baseLUT_[i] + (1u << bits_[i]);
                assert((exclusiveEnd & (exclusiveEnd - 1)) == 0);
#endif
                if (maxVal < baseLUT_[i] + (1u << bits_[i])) {
                    clzLUT[clz] = i;
                    break;
                }
            }
        }
        return clzLUT;
    }

    template <typename T>
    constexpr std::array<T, 32> toClzLUT(
            const std::array<uint8_t, 32>& clzLUT,
            const std::array<T, 16>& lut) const
    {
        std::array<T, 32> outLUT;
        for (size_t i = 0; i < 32; ++i) {
            outLUT[i] = lut[clzLUT[i]];
        }
        return outLUT;
    }

    constexpr std::array<uint8_t, 32> shift4(std::array<uint8_t, 32> lut) const
    {
        for (size_t i = 0; i < lut.size(); ++i) {
            lut[i] <<= 4;
        }
        return lut;
    }

    static uint32_t safeRead(std::span<uint8_t const> bytes)
    {
        uint32_t value      = 0;
        size_t const toRead = std::min<size_t>(bytes.size(), 4);
        for (size_t i = 0; i < toRead; ++i) {
            value |= bytes[i] << (8 * i);
        }
        return value;
    }

    static constexpr std::array<uint8_t, 256> makeBitsConsumedLUT()
    {
        std::array<uint8_t, 16> constexpr kBits = { B0,  B1,  B2,  B3, B4,  B5,
                                                    B6,  B7,  B8,  B9, B10, B11,
                                                    B12, B13, B14, B15 };
        std::array<uint8_t, 256> lut;
        for (int byte = 0; byte < 256; ++byte) {
            uint32_t const bits0 = kBits[byte & 0xF];
            uint32_t const bits1 = kBits[byte >> 4];
            lut[byte]            = (bits0 + bits1);
        }

        return lut;
    }

#if ZL_ARCH_X86_64
    static constexpr DualLUT makeShuffleShiftLUT()
    {
        std::array<uint8_t, 16> constexpr kBits = { B0,  B1,  B2,  B3, B4,  B5,
                                                    B6,  B7,  B8,  B9, B10, B11,
                                                    B12, B13, B14, B15 };
        std::array<std::array<uint8_t, 16>, 256> lo;
        for (int byte = 0; byte < 256; ++byte) {
            uint32_t const bits0     = kBits[byte & 0xF];
            uint32_t const bits1     = kBits[byte >> 4];
            uint32_t const bitsTotal = bits0 + bits1;

            auto fillU32 = [&lo, byte](size_t idx, size_t bitOffset, bool add) {
                for (size_t i = 0; i < 4; ++i) {
                    lo[byte][4 * idx + i] = bitOffset + (add ? 8 * i : 0);
                }
            };

            fillU32(0, 0, true);
            fillU32(1, bits0, true);
            fillU32(2, bitsTotal, false);
            fillU32(3, bitsTotal, false);
        }
        return DualLUT{ lo };
    }

    constexpr static DualLUT makeShiftMaskLUT()
    {
        std::array<uint32_t, 16> constexpr kBits = {
            B0, B1, B2, B3, B4, B5, B6, B7, B8, B9, B10, B11, B12, B13, B14, B15
        };
        std::array<std::array<uint32_t, 4>, 256> lo;
        for (int byte = 0; byte < 256; ++byte) {
            uint32_t const bits0     = kBits[byte & 0xF];
            uint32_t const bits1     = kBits[byte >> 4];
            uint32_t const bitsTotal = bits0 + bits1;

            auto const toEntry = [](uint32_t bitOffset, uint32_t mask) {
                assert((mask >> 24) == 0);
                assert(bitOffset < 128);
                return bitOffset | (mask << 8);
            };

            lo[byte][0] = toEntry(0, (1u << bits0) - 1);
            lo[byte][1] = toEntry(bits0, (1u << bits1) - 1);
            lo[byte][2] = toEntry(bitsTotal, 0);
            lo[byte][3] = toEntry(bitsTotal, 0);
        }
        return DualLUT{ lo };
    }
#endif

    constexpr static std::array<uint64_t, 256> expandLUT(
            std::array<uint32_t, 16> const& lut)
    {
        std::array<uint64_t, 256> expanded;
        for (int byte = 0; byte < 256; ++byte) {
            uint64_t const val0 = lut[byte & 0xF];
            uint64_t const val1 = lut[byte >> 4];

            expanded[byte] = val0 | (val1 << 32);
        }
        return expanded;
    }

    constexpr static std::array<uint32_t, 256> expandLUTU16(
            std::array<uint32_t, 16> const& lut)
    {
        std::array<uint32_t, 256> expanded;
        for (int byte = 0; byte < 256; ++byte) {
            uint64_t const val0 = lut[byte & 0xF];
            uint64_t const val1 = lut[byte >> 4];

            expanded[byte] = (val0 & 0xFFFF) | (val1 << 16);
        }
        return expanded;
    }

    constexpr static std::array<uint32_t, 16> makeMaskLUT()
    {
        std::array<uint32_t, 16> constexpr kBits = {
            B0, B1, B2, B3, B4, B5, B6, B7, B8, B9, B10, B11, B12, B13, B14, B15
        };
        std::array<uint32_t, 16> lut;
        for (int nibble = 0; nibble < 16; ++nibble) {
            uint32_t const bits = kBits[nibble];
            lut[nibble]         = (1u << bits) - 1;
        }
        return lut;
    }

    constexpr static std::array<uint32_t, 16> makeBaseLUT()
    {
        std::array<uint32_t, 16> lut;
        lut[0] = 0;
        for (int nibble = 1; nibble < 16; ++nibble) {
            lut[nibble] = lut[nibble - 1] + (1u << bits_[nibble - 1]);
            if (lut[nibble] != (1u << bits_[nibble])) {
                lut[nibble] = 0;
            }
        }
        return lut;
    }

#if ZL_ARCH_X86_64
    static constexpr DualLUT shuffleShiftLUT_{ makeShuffleShiftLUT() };
#endif
    static constexpr std::array<uint64_t, 256> base2xLUT_{ expandLUT(
            makeBaseLUT()) };
    static constexpr std::array<uint64_t, 256> maskLUT_{ expandLUT(
            makeMaskLUT()) };
    static constexpr std::array<uint8_t, 256> const bitsConsumedLUT_{
        makeBitsConsumedLUT()
    };
    static constexpr std::array<uint32_t, 16> baseLUT_{ makeBaseLUT() };
    static constexpr std::array<uint8_t, 16> bits_{ B0,  B1,  B2,  B3, B4,  B5,
                                                    B6,  B7,  B8,  B9, B10, B11,
                                                    B12, B13, B14, B15 };

    static constexpr std::array<uint32_t, 256> base2xLUTU16_{ expandLUTU16(
            makeBaseLUT()) };
    static constexpr std::array<uint32_t, 256> mask2xLUTU16_{ expandLUTU16(
            makeMaskLUT()) };

    template <size_t N>
    static constexpr std::array<std::pair<uint32_t, uint32_t>, N> combineLUT(
            const std::array<uint32_t, N>& a,
            const std::array<uint32_t, N>& b)
    {
        std::array<std::pair<uint32_t, uint32_t>, N> out;
        for (size_t i = 0; i < N; ++i) {
            out[i] = { a[i], b[i] };
        }
        return out;
    }

    static constexpr auto combined2xLUTU16_{
        combineLUT(base2xLUTU16_, mask2xLUTU16_)
    };
};

using VarByteCoder16 =
        VarByteCoder<1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15>;
using VarByteCoder32 =
        VarByteCoder<5, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 22, 24>;

} // namespace

SimpleCodecDescription VarByteEncoder::simpleCodecDescription() const
{
    return varByteCodecDescription();
}

void VarByteEncoder::encode(EncoderState& encoder) const
{
    auto& inputStream           = encoder.inputs()[0];
    auto const nbElts           = inputStream.numElts();
    auto const eltWidth         = inputStream.eltWidth();
    size_t const controlSize    = (nbElts + 1) / 2;
    size_t const packedCapacity = nbElts * eltWidth;
    auto controlStream          = encoder.createOutput(0, controlSize, 1);
    auto packedStream           = encoder.createOutput(1, packedCapacity, 1);
    if (eltWidth <= 1 || eltWidth > 4) {
        throw std::runtime_error("eltWidth must be in [2, 4]");
    }

    uint8_t* const control  = (uint8_t*)controlStream.ptr();
    uint8_t* const packed   = (uint8_t*)packedStream.ptr();
    void const* const input = inputStream.ptr();

    size_t packedSize;
    if (eltWidth == 2) {
        packedSize = VarByteCoder16().encode(
                std::span{ control, controlSize },
                std::span{ packed, packedCapacity },
                std::span{ (uint16_t const*)input, nbElts });
    } else {
        assert(eltWidth == 4);
        packedSize = VarByteCoder32().encode(
                std::span{ control, controlSize },
                std::span{ packed, packedCapacity },
                std::span{ (uint32_t const*)input, nbElts });
    }

    uint8_t unfilledElts = nbElts % 2;
    uint8_t header[2]    = { unfilledElts, (uint8_t)eltWidth };
    encoder.sendCodecHeader(header, sizeof(header));

    controlStream.commit(controlSize);
    packedStream.commit(packedSize);
}

SimpleCodecDescription VarByteDecoder::simpleCodecDescription() const
{
    return varByteCodecDescription();
}

void VarByteDecoder::decode(DecoderState& decoder) const
{
    auto& controlStream = decoder.singletonInputs()[0];
    auto& packedStream  = decoder.singletonInputs()[1];

    auto const controlSize = controlStream.numElts();
    auto const packedSize  = packedStream.numElts();

    auto header = decoder.getCodecHeader();
    if (header.size() != 2) {
        throw std::runtime_error("header size must be 2");
    }

    size_t const nbElts   = 2 * controlSize - header[0];
    size_t const eltWidth = header[1];

    auto outStream = decoder.createOutput(0, nbElts, eltWidth);

    uint8_t const* control = (uint8_t const*)controlStream.ptr();
    uint8_t const* packed  = (uint8_t const*)packedStream.ptr();
    void* out              = outStream.ptr();

    if (eltWidth == 2) {
        VarByteCoder16().decode(
                std::span{ (uint16_t*)out, nbElts },
                std::span{ control, controlSize },
                std::span{ packed, packedSize });
    } else {
        assert(eltWidth == 4);
        VarByteCoder32().decode(
                std::span{ (uint32_t*)out, nbElts },
                std::span{ control, controlSize },
                std::span{ packed, packedSize });
    }

    outStream.commit(nbElts);
}

} // namespace openzl::lz
