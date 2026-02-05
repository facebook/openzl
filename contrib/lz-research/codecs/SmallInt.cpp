// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "SmallInt.hpp"
#include "CodecIDs.hpp"

#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/simd_wrapper.h"

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

void SmallIntEncoder::encode(EncoderState& encoder) const
{
    auto& input         = encoder.inputs()[0];
    auto const nbElts   = input.numElts();
    auto const eltWidth = input.eltWidth();
    auto smallStream    = encoder.createOutput(0, nbElts, 1);
    auto largeStream    = encoder.createOutput(1, nbElts, eltWidth);

    uint8_t* const smallOut = (uint8_t*)smallStream.ptr();

    auto const encodeImpl = [nbElts, smallOut](auto* largeOut, auto const* in) {
        size_t nbLarge = 0;
        for (size_t i = 0; i < nbElts; ++i) {
            if (in[i] < 255) {
                smallOut[i] = in[i];
            } else {
                smallOut[i]         = 255;
                largeOut[nbLarge++] = in[i] - 255;
            }
        }
        return nbLarge;
    };

    size_t nbLarge;
    void* largePtr    = largeStream.ptr();
    void const* inPtr = input.ptr();
    switch (eltWidth) {
        case 1:
            memcpy(smallOut, inPtr, nbElts);
            nbLarge = 0;
            break;
        case 2:
            nbLarge = encodeImpl((uint16_t*)largePtr, (uint16_t const*)inPtr);
            break;
        case 4:
            nbLarge = encodeImpl((uint32_t*)largePtr, (uint32_t const*)inPtr);
            break;
        case 8:
            nbLarge = encodeImpl((uint64_t*)largePtr, (uint64_t const*)inPtr);
            break;
    };

    smallStream.commit(nbElts);
    largeStream.commit(nbLarge);
}

SimpleCodecDescription SmallIntDecoder::simpleCodecDescription() const
{
    return smallIntCodecDescription();
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

    auto decodeImpl = [&decoder, nbElts, nbLarge](
                              auto* out,
                              uint8_t const* __restrict smallIn,
                              auto const* largeIn) {
        size_t const nbLargeNeeded =
                std::count_if(smallIn, smallIn + nbElts, [](uint8_t val) {
                    return val == 255;
                });
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
    };

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
            decodeImpl((uint16_t*)outPtr, smallIn, (uint16_t const*)largePtr);
            break;
        case 4:
            decodeImpl((uint32_t*)outPtr, smallIn, (uint32_t const*)largePtr);
            break;
        case 8:
            decodeImpl((uint64_t*)outPtr, smallIn, (uint64_t const*)largePtr);
            break;
    }

    outStream.commit(nbElts);
}

} // namespace openzl::lz
