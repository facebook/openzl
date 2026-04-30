// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "IsoByte.hpp"

#include <array>

#include "CodecIDs.hpp"

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"

#if ZL_ARCH_X86_64
#    include <immintrin.h>
#endif

namespace openzl::lz {
namespace {
VariableOutputCodecDescription isoByteCodecDescription()
{
    return VariableOutputCodecDescription{
        .id                   = unsigned(CustomCodecIDs::IsoByte),
        .name                 = "!lz_research.iso_byte",
        .inputType            = Type::Numeric,
        .singletonOutputTypes = { Type::Serial, Type::Serial },
        .variableOutputTypes  = { Type::Serial },
    };
}

void setBit(uint8_t* bitmap, size_t idx)
{
    bitmap[idx / 8] |= (1 << (idx % 8));
}

bool getBit(const uint8_t* bitmap, size_t idx)
{
    return (bitmap[idx / 8] & (1 << (idx % 8))) != 0;
}

void isoByteDecodeNaive(
        std::span<uint16_t> dst,
        std::span<const uint8_t> bits,
        std::span<const uint8_t> lo,
        std::span<const uint8_t> hi8,
        std::span<const uint8_t> hi16)
{
    size_t idx8  = 0;
    size_t idx16 = 0;
    for (size_t i = 0; i < dst.size(); ++i) {
        if (getBit(bits.data(), i)) {
            dst[i] = (uint16_t(hi16[idx16]) << 8) | lo[idx16];
            ++idx16;
        } else {
            dst[i] = hi8[idx8++];
        }
    }
}

#if ZL_ARCH_X86_64
template <typename UInt>
class IsoByteDecode {
   public:
    static_assert(sizeof(UInt) == 2);
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

    static void decode(
            std::span<UInt> dst,
            std::span<const uint8_t> bits,
            std::span<const uint8_t> lo,
            std::span<const uint8_t> hi8,
            std::span<const uint8_t> hi16)
    {
        const uint8_t* l              = lo.data();
        const uint8_t* h8             = hi8.data();
        const uint8_t* const h8Limit  = h8 + hi8.size() - 16;
        const uint8_t* h16            = hi16.data();
        const uint8_t* const h16Limit = h16 + hi16.size() - 16;

        size_t i;
        // for (i = 0; h8 < h8Limit && h16 < h16Limit; i += 8) {
        //     // TODO: Can half the number of data loads
        //     // Reuse the data by adding a constant to the shuffles

        //     const int bitmask = bits[i / 8];
        //     auto lV = _mm_loadu_si128((const __m128i_u*)l);
        //     auto lShuffleV =
        //             _mm_load_si128((const __m128i*)&kLoShuffleLUT_[bitmask]);
        //     lV = _mm_shuffle_epi8(lV, lShuffleV);

        //     auto h8V        = _mm_loadu_si128((const __m128i_u*)h8);
        //     auto h8ShuffleV = _mm_load_si128(
        //             (const __m128i*)&kLoShuffleLUT_[~bitmask & 0xFF]);
        //     h8V = _mm_shuffle_epi8(h8V, h8ShuffleV);

        //     auto h16V = _mm_loadu_si128((const __m128i_u*)h16);
        //     auto h16ShuffleV =
        //             _mm_load_si128((const __m128i*)&kHiShuffleLUT_[bitmask]);
        //     h16V = _mm_shuffle_epi8(h16V, h16ShuffleV);

        //     auto out = _mm_or_si128(h8V, _mm_or_si128(lV, h16V));
        //     _mm_storeu_si128((__m128i_u*)&dst[i], out);

        //     const auto count16 = __builtin_popcount(bitmask);
        //     const auto count8  = 8 - count16;

        //     h16 += count16;
        //     l += count16;
        //     h8 += count8;
        // }
        for (i = 0; h8 < h8Limit && h16 < h16Limit; i += 16) {
            // TODO: Could combine the high bits to reduce loads & shuffles by
            // 33%
            // auto lV   = _mm_loadu_si128((const __m128i_u*)l);
            // auto h16V = _mm_loadu_si128((const __m128i_u*)h16);
            auto lV   = _mm_set_epi64x(0, ZL_readLE64(l));
            auto h16V = _mm_set_epi64x(0, ZL_readLE64(h16));
            // auto lV   = _mm_loadu_si128((const __m128i_u*)l);
            // auto h16V = _mm_loadu_si128((const __m128i_u*)h16);
            auto h8V = _mm_set_epi64x(0, ZL_readLE64(h8));
            // auto h8V = _mm_loadu_si128((const __m128i_u*)h8);

            int bitmask  = bits[i / 8];
            auto count16 = __builtin_popcount(bitmask);
            auto count8  = 8 - count16;
            // const auto count16V = _mm_set1_epi8(count16);
            // const auto count8V = _mm_set1_epi8(count8);

            h16 += count16;
            l += count16;
            h8 += count8;

            // if 8-bit:
            // - high byte should start with 0x80
            // - low byte should be <= 0xF

            // if 16-bit:
            // - high byte should be <= 0xF
            // - low byte should be < 0xF

            // at most 8 bytes
            auto lhV = _mm_unpacklo_epi8(lV, h16V);
            lV       = _mm_set_epi64x(0, ZL_readLE64(l));
            h16V     = _mm_set_epi64x(0, ZL_readLE64(h16));

            auto h8ShuffleV = _mm_load_si128(
                    (const __m128i*)&kLoShuffleLUT_[~bitmask & 0xFF]);
            auto outH8V = _mm_shuffle_epi8(h8V, h8ShuffleV);
            h8V         = _mm_set_epi64x(0, ZL_readLE64(h8));

            auto lhShuffleV =
                    _mm_load_si128((const __m128i*)&kLoHiShuffleLUT_[bitmask]);
            auto outLHV = _mm_shuffle_epi8(lhV, lhShuffleV);
            // auto lShuffleV =
            //         _mm_load_si128((const __m128i*)&kLoShuffleLUT_[bitmask]);
            // auto outLV = _mm_shuffle_epi8(lV, lShuffleV);

            // at most 16

            auto out = _mm_or_si128(outLHV, outH8V);

            // at most 8 bytes
            // auto h16ShuffleV =
            //         _mm_load_si128((const __m128i*)&kHiShuffleLUT_[bitmask]);
            // auto outH16V = _mm_shuffle_epi8(h16V, h16ShuffleV);

            // out = _mm_or_si128(out, outH16V);
            _mm_storeu_si128((__m128i_u*)&dst[i], out);

            bitmask = bits[i / 8 + 1];
            count16 = __builtin_popcount(bitmask);
            count8  = 8 - count16;

            lhV = _mm_unpacklo_epi8(lV, h16V);
            lhShuffleV =
                    _mm_load_si128((const __m128i*)&kLoHiShuffleLUT_[bitmask]);
            outLHV = _mm_shuffle_epi8(lhV, lhShuffleV);
            // auto lShuffleV =
            //         _mm_load_si128((const __m128i*)&kLoShuffleLUT_[bitmask]);
            // auto outLV = _mm_shuffle_epi8(lV, lShuffleV);

            // at most 16
            h8ShuffleV = _mm_load_si128(
                    (const __m128i*)&kLoShuffleLUT_[~bitmask & 0xFF]);
            outH8V = _mm_shuffle_epi8(h8V, h8ShuffleV);

            out = _mm_or_si128(outLHV, outH8V);

            // at most 8 bytes
            // auto h16ShuffleV =
            //         _mm_load_si128((const __m128i*)&kHiShuffleLUT_[bitmask]);
            // auto outH16V = _mm_shuffle_epi8(h16V, h16ShuffleV);

            // out = _mm_or_si128(out, outH16V);
            _mm_storeu_si128((__m128i_u*)&dst[i + 8], out);

            h16 += count16;
            l += count16;
            h8 += count8;

            // lShuffleV =
            //         _mm_load_si128((const __m128i*)&kLoShuffleLUT_[bitmask]);
            // lShuffleV = _mm_add_epi8(lShuffleV, count16V);
            // outLV     = _mm_shuffle_epi8(lV, lShuffleV);

            // h8ShuffleV = _mm_load_si128(
            //         (const __m128i*)&kLoShuffleLUT_[~bitmask & 0xFF]);
            // h8ShuffleV = _mm_add_epi8(h8ShuffleV, count8V);
            // outH8V     = _mm_shuffle_epi8(h8V, h8ShuffleV);

            // out = _mm_or_si128(outLV, outH8V);

            // h16ShuffleV =
            //         _mm_load_si128((const __m128i*)&kHiShuffleLUT_[bitmask]);
            // h16ShuffleV = _mm_add_epi8(h16ShuffleV, count16V);
            // outH16V     = _mm_shuffle_epi8(h16V, h16ShuffleV);

            // out = _mm_or_si128(out, outH16V);
            // _mm_storeu_si128((__m128i_u*)&dst[i + 8], out);

            // h16 += count16;
            // l += count16;
            // h8 += count8;
        }

        for (; i < dst.size(); ++i) {
            if (getBit(bits.data(), i)) {
                dst[i] = (uint16_t(*h16++) << 8) | *l++;
            } else {
                dst[i] = *h8++;
            }
        }
    }

   private:
    static std::tuple<__m128i, __m128i, __m128i> getShuffles(uint8_t bitmap)
    {
        auto lV = _mm_load_si128((const __m128i*)&kLoShuffleLUT_[bitmap]);
        auto h8V =
                _mm_load_si128((const __m128i*)&kLoShuffleLUT_[~bitmap & 0xFF]);
        auto h16V = _mm_load_si128((const __m128i*)&kHiShuffleLUT_[bitmap]);
        return { lV, h8V, h16V };
    }

    static constexpr std::array<std::array<uint8_t, 16>, 256> makeShuffleLUT(
            bool lo)
    {
        std::array<std::array<uint8_t, 16>, 256> lut{};
        for (size_t byte = 0; byte < 256; ++byte) {
            size_t offset = 0;
            for (size_t bit = 0; bit < 8; ++bit) {
                if ((byte & (1 << bit)) != 0) {
                    if (lo) {
                        lut[byte][2 * bit + 0] = offset;
                        lut[byte][2 * bit + 1] = 0x80;
                    } else {
                        lut[byte][2 * bit + 0] = 0x80;
                        lut[byte][2 * bit + 1] = offset;
                    }
                    ++offset;
                } else {
                    lut[byte][2 * bit + 0] = 0x80;
                    lut[byte][2 * bit + 1] = 0x80;
                }
            }
        }
        return lut;
    }

    static constexpr std::array<std::array<uint8_t, 16>, 256>
    makeLoHiShuffleLUT()
    {
        std::array<std::array<uint8_t, 16>, 256> lut{};
        for (size_t byte = 0; byte < 256; ++byte) {
            size_t offset = 0;
            for (size_t bit = 0; bit < 8; ++bit) {
                if ((byte & (1 << bit)) != 0) {
                    lut[byte][2 * bit + 0] = offset + 0;
                    lut[byte][2 * bit + 1] = offset + 1;
                    offset += 2;
                } else {
                    lut[byte][2 * bit + 0] = 0x80;
                    lut[byte][2 * bit + 1] = 0x80;
                }
            }
        }
        return lut;
    }

    static constexpr std::array<std::array<uint16_t, 8>, 256> makeIs16LUT()
    {
        std::array<std::array<uint16_t, 8>, 256> lut{};
        for (size_t byte = 0; byte < 256; ++byte) {
            for (size_t bit = 0; bit < 8; ++bit) {
                lut[byte][bit] = ((byte & (1 << bit)) != 0) ? 0xFFFF : 0;
            }
        }
        return lut;
    }

    alignas(16) static constexpr std::
            array<std::array<uint16_t, 8>, 256> kIs16LUT_{ makeIs16LUT() };
    alignas(16) static constexpr std::
            array<std::array<uint8_t, 16>, 256> kLoShuffleLUT_{
                makeShuffleLUT(true)
            };
    alignas(16) static constexpr std::
            array<std::array<uint8_t, 16>, 256> kHiShuffleLUT_{
                makeShuffleLUT(false)
            };
    alignas(16) static constexpr std::
            array<std::array<uint8_t, 16>, 256> kLoHiShuffleLUT_{
                makeLoHiShuffleLUT()
            };
};
#endif // ZL_ARCH_X86_64

} // namespace

VariableOutputCodecDescription IsoByteEncoder::variableOutputDescription() const
{
    return isoByteCodecDescription();
}

void IsoByteEncoder::encode(EncoderState& encoder) const
{
    auto& input = encoder.inputs()[0];

    uint8_t header = input.eltWidth();
    encoder.sendCodecHeader(&header, 1);

    if (input.eltWidth() != 2) {
        throw std::runtime_error("todo");
    }

    auto bitmap      = encoder.createOutput(0, (input.numElts() + 7) / 8, 1);
    auto lowBytes    = encoder.createOutput(1, input.numElts(), 1);
    auto highBytes8  = encoder.createOutput(2, input.numElts(), 1);
    auto highBytes16 = encoder.createOutput(2, input.numElts(), 1);

    const uint16_t* const src = (uint16_t*)input.ptr();
    const size_t srcSize      = input.numElts();

    uint8_t* bits = (uint8_t*)bitmap.ptr();
    uint8_t* hi8  = (uint8_t*)highBytes8.ptr();
    uint8_t* hi16 = (uint8_t*)highBytes16.ptr();
    uint8_t* lo   = (uint8_t*)lowBytes.ptr();

    memset(bits, 0, (srcSize + 7) / 8);

    for (size_t i = 0; i < srcSize; ++i) {
        if (src[i] < 256) {
            *hi8++ = src[i];
        } else {
            setBit(bits, i);
            *hi16++ = src[i] >> 8;
            *lo++   = src[i] & 0xFF;
        }
    }
    bitmap.commit((srcSize + 7) / 8);
    highBytes8.commit(hi8 - (uint8_t*)highBytes8.ptr());
    highBytes16.commit(hi16 - (uint8_t*)highBytes16.ptr());
    lowBytes.commit(lo - (uint8_t*)lowBytes.ptr());
}

VariableOutputCodecDescription IsoByteDecoder::variableOutputDescription() const
{
    return isoByteCodecDescription();
}

void IsoByteDecoder::decode(DecoderState& decoder) const
{
    auto header = decoder.getCodecHeader();
    if (header.size() != 1 || header[0] != 2) {
        throw std::runtime_error("todo");
    }
    if (decoder.variableInputs().size() != 2) {
        throw std::runtime_error("corruption");
    }

    auto& bitmap      = decoder.singletonInputs()[0];
    auto& lowBytes    = decoder.singletonInputs()[1];
    auto& highBytes8  = decoder.variableInputs()[0];
    auto& highBytes16 = decoder.variableInputs()[1];

    const size_t srcSize = highBytes8.numElts() + highBytes16.numElts();
    auto output          = decoder.createOutput(0, srcSize, 2);

#if ZL_ARCH_X86_64
    IsoByteDecode<uint16_t>::decode(
            { (uint16_t*)output.ptr(), srcSize },
            { (const uint8_t*)bitmap.ptr(), bitmap.numElts() },
            { (const uint8_t*)lowBytes.ptr(), lowBytes.numElts() },
            { (const uint8_t*)highBytes8.ptr(), highBytes8.numElts() },
            { (const uint8_t*)highBytes16.ptr(), highBytes16.numElts() });
#else
    isoByteDecodeNaive(
            { (uint16_t*)output.ptr(), srcSize },
            { (const uint8_t*)bitmap.ptr(), bitmap.numElts() },
            { (const uint8_t*)lowBytes.ptr(), lowBytes.numElts() },
            { (const uint8_t*)highBytes8.ptr(), highBytes8.numElts() },
            { (const uint8_t*)highBytes16.ptr(), highBytes16.numElts() });
#endif

    output.commit(srcSize);
}

} // namespace openzl::lz
