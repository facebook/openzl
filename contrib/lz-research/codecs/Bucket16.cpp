// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "Bucket16.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <vector>

#include "CodecIDs.hpp"
#include "utils/Portability.hpp"

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/data_stats.h"
#include "openzl/shared/histogram.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/utils.h"
#include "utils/Partition.hpp"

#if ZL_ARCH_X86_64
#    include <immintrin.h>
#endif

namespace openzl::lz {
namespace {

template <typename T>
T divUp(T a, T b)
{
    return (a + b - 1) / b;
}

SimpleCodecDescription bucket16CodecDescription()
{
    return SimpleCodecDescription{
        .id          = unsigned(CustomCodecIDs::Bucket16),
        .name        = "!lz_research.bucket16",
        .inputType   = Type::Numeric,
        .outputTypes = { Type::Serial, Type::Serial },
    };
}

class Bucket16EncoderState {
   public:
    Bucket16EncoderState(poly::span<const uint8_t> param, int maxSymbolValue)
            : numSymbols_(maxSymbolValue + 1)
    {
        if (param.size() % 2 != 0) {
            throw std::runtime_error("Param is not multiple of 2");
        }
        bases_ = { (const uint16_t*)param.data(), param.size() / 2 };
        if (bases_.empty()) {
            throw std::runtime_error("bases is empty");
        }
        if (bases_.size() > 256) {
            throw std::runtime_error("bases must be <= 256 entries");
        }
        for (size_t i = 1; i < bases_.size(); ++i) {
            if (bases_[i - 1] >= bases_[i]) {
                throw std::runtime_error("bases not strictly increasing");
            }
        }

        fixedBits_ = ZL_nextPow2(bases_.size());

        // Compute lutShift_: minimum trailing zeros across all non-zero bases.
        // Since all bases are multiples of 2^lutShift_, values can be shifted
        // right by lutShift_ before LUT lookup, reducing from 65536 entries.
        lutShift_ = 16;
        for (size_t i = 0; i < bases_.size(); ++i) {
            if (bases_[i] != 0) {
                lutShift_ =
                        std::min(lutShift_, size_t(__builtin_ctz(bases_[i])));
            }
        }
        if (lutShift_ == 16) {
            lutShift_ = 0;
        }

        const size_t lutSize = size_t(65536) >> lutShift_;
        valueToBucket16_.resize(lutSize);
        valueToBase_.resize(lutSize);
        valueToVarBits_.resize(lutSize);

        size_t nextBaseIdx = 0;
        size_t bits        = 0;
        maxVarBits_        = 0;
        for (size_t sv = 0; sv < lutSize; ++sv) {
            const size_t byte = sv << lutShift_;
            while (nextBaseIdx < bases_.size() && byte >= bases_[nextBaseIdx]) {
                bits        = ZL_nextPow2(getBucketSize(nextBaseIdx));
                maxVarBits_ = std::max(maxVarBits_, bits);
                ++nextBaseIdx;
            }
            valueToVarBits_[sv]  = bits;
            valueToBucket16_[sv] = nextBaseIdx - 1;
            valueToBase_[sv]     = bases_[nextBaseIdx - 1];
        }
    }

    size_t fixedBits() const
    {
        return fixedBits_;
    }

    size_t maxVarBits() const
    {
        return maxVarBits_;
    }

    poly::span<const uint16_t> bases() const
    {
        return bases_;
    }

    size_t fixedStreamSize(size_t numElts) const
    {
        return (fixedBits() * numElts + 7) / 8;
    }

    size_t varStreamBound(size_t numElts) const
    {
        // Extra 8 bytes ensures the fast-path encoder can safely do
        // 8-byte writes (ZL_writeLE64) near the end of the buffer.
        return (maxVarBits() * numElts + 7) / 8 + 8;
    }

    size_t
    encode(poly::span<const uint16_t> input, uint8_t* fixed, uint8_t* var) const
    {
        switch (fixedBits_) {
            case 3:
                return encode3(input, fixed, var);
            case 4:
                return encode4(input, fixed, var);
            case 5:
                return encode5(input, fixed, var);
            case 6:
                return encode6(input, fixed, var);
            default:
                return encodeGeneric(input, fixed, var);
        }
    }

   private:
    size_t getBucketSize(size_t baseIdx) const
    {
        if (baseIdx >= bases_.size()) {
            return 1;
        }
        size_t base = bases_[baseIdx];
        assert(base < numSymbols_);
        auto end = baseIdx + 1 == bases_.size() ? numSymbols_
                                                : size_t(bases_[baseIdx + 1]);
        assert(end > base);
        return end - base;
    }

    size_t encodeGeneric(
            poly::span<const uint16_t> input,
            uint8_t* fixed,
            uint8_t* var) const
    {
        ZS_BitCStreamFF fixedBitstream =
                ZS_BitCStreamFF_init(fixed, fixedStreamSize(input.size()));
        ZS_BitCStreamFF varBitstream =
                ZS_BitCStreamFF_init(var, varStreamBound(input.size()));

        for (const auto x : input) {
            const auto sx      = x >> lutShift_;
            const auto varBits = valueToVarBits_[sx];
            if (x - valueToBase_[sx] >= (1u << varBits)) {
                throw std::runtime_error("Invalid bases for input");
            }
            ZS_BitCStreamFF_write(
                    &fixedBitstream, valueToBucket16_[sx], fixedBits_);
            ZS_BitCStreamFF_flush(&fixedBitstream);
            ZS_BitCStreamFF_write(&varBitstream, x - valueToBase_[sx], varBits);
            ZS_BitCStreamFF_flush(&varBitstream);
        }

        unwrap(ZS_BitCStreamFF_finish(&fixedBitstream));
        return unwrap(ZS_BitCStreamFF_finish(&varBitstream));
    }

    size_t encodeTail(
            const uint16_t* in,
            size_t i,
            size_t numElts,
            size_t fixedBits,
            uint8_t* f,
            uint8_t* v,
            uint8_t* varBase,
            uint64_t varAcc,
            size_t varAccBits) const
    {
        uint64_t fixedAcc   = 0;
        size_t fixedAccBits = 0;
        for (; i < numElts; ++i) {
            const auto x  = in[i];
            const auto sx = x >> lutShift_;

            fixedAcc |= uint64_t(valueToBucket16_[sx]) << fixedAccBits;
            fixedAccBits += fixedBits;
            while (fixedAccBits >= 8) {
                *f++ = uint8_t(fixedAcc);
                fixedAcc >>= 8;
                fixedAccBits -= 8;
            }

            const uint64_t off = x - valueToBase_[sx];
            const size_t nbits = valueToVarBits_[sx];
            varAcc |= off << varAccBits;
            varAccBits += nbits;
            while (varAccBits >= 8) {
                *v++ = uint8_t(varAcc);
                varAcc >>= 8;
                varAccBits -= 8;
            }
        }
        if (fixedAccBits > 0) {
            *f++ = uint8_t(fixedAcc);
        }
        if (varAccBits > 0) {
            *v++ = uint8_t(varAcc);
        }
        return v - varBase;
    }

    template <size_t N>
    struct ExpandedLUT {
        static constexpr size_t kRawSize      = 1u << N;
        static constexpr size_t kExpandedSize = 1u << (2 * N);
        std::array<uint32_t, kExpandedSize> base;
        std::array<uint32_t, kExpandedSize> mask;
    };

    template <size_t N>
    ExpandedLUT<N> buildLUT() const
    {
        ExpandedLUT<N> lut;
        std::array<uint16_t, ExpandedLUT<N>::kRawSize> baseRaw{};
        std::array<uint16_t, ExpandedLUT<N>::kRawSize> maskRaw{};
        for (size_t b = 0; b < bases_.size(); ++b) {
            baseRaw[b] = bases_[b];
            maskRaw[b] = (1u << ZL_nextPow2(getBucketSize(b))) - 1;
        }
        constexpr size_t kMask = ExpandedLUT<N>::kRawSize - 1;
        for (size_t idx = 0; idx < ExpandedLUT<N>::kExpandedSize; ++idx) {
            const size_t lo = idx & kMask;
            const size_t hi = idx >> N;
            lut.base[idx] =
                    uint32_t(baseRaw[lo]) | (uint32_t(baseRaw[hi]) << 16);
            lut.mask[idx] =
                    uint32_t(maskRaw[lo]) | (uint32_t(maskRaw[hi]) << 16);
        }
        return lut;
    }

    size_t computeLimit(
            size_t i,
            size_t numElts,
            size_t kEltsPerIter,
            const uint8_t* v,
            const uint8_t* vBound) const
    {
        if (numElts < kEltsPerIter) {
            return 0;
        }
        if (maxVarBits_ == 0) {
            return numElts - kEltsPerIter;
        }
        assert(v <= vBound);
        const size_t vRemaining = vBound - v;
        if (vRemaining < 8) {
            return i;
        }
        const size_t vSafeElts = (8 * vRemaining - 7) / maxVarBits_;
        if (vSafeElts < kEltsPerIter) {
            return i;
        }
        return std::min(numElts - kEltsPerIter, i + vSafeElts - kEltsPerIter);
    }

    size_t encode3(
            poly::span<const uint16_t> input,
            uint8_t* fixed,
            uint8_t* var) const
    {
        const size_t numElts        = input.size();
        const uint16_t* in          = input.data();
        uint8_t* f                  = fixed;
        uint8_t* v                  = var;
        const uint8_t* const vBound = var + varStreamBound(numElts);

        const auto lut = buildLUT<3>();

        uint64_t varAcc               = 0;
        size_t varAccBits             = 0;
        constexpr size_t kEltsPerIter = 16;
        size_t i                      = 0;

        size_t limit = computeLimit(i, numElts, kEltsPerIter, v, vBound);
        for (;; i += kEltsPerIter) {
            if (i >= limit) {
                limit = computeLimit(i, numElts, kEltsPerIter, v, vBound);
                if (i >= limit) {
                    break;
                }
            }
            static_assert(kEltsPerIter % 8 == 0);
            for (size_t u = 0; u < kEltsPerIter; u += 8) {
                const uint16_t* inp = in + i + u;

                const auto b0 = valueToBucket16_[inp[0] >> lutShift_];
                const auto b1 = valueToBucket16_[inp[1] >> lutShift_];
                const auto b2 = valueToBucket16_[inp[2] >> lutShift_];
                const auto b3 = valueToBucket16_[inp[3] >> lutShift_];
                const auto b4 = valueToBucket16_[inp[4] >> lutShift_];
                const auto b5 = valueToBucket16_[inp[5] >> lutShift_];
                const auto b6 = valueToBucket16_[inp[6] >> lutShift_];
                const auto b7 = valueToBucket16_[inp[7] >> lutShift_];

                const std::array<uint32_t, 4> fs = {
                    uint32_t(b0 | (b1 << 3)),
                    uint32_t(b2 | (b3 << 3)),
                    uint32_t(b4 | (b5 << 3)),
                    uint32_t(b6 | (b7 << 3)),
                };
                const uint32_t F =
                        fs[0] | (fs[1] << 6) | (fs[2] << 12) | (fs[3] << 18);
                f[0] = F & 0xFF;
                f[1] = (F >> 8) & 0xFF;
                f[2] = (F >> 16) & 0xFF;
                f += 3;

                for (size_t k = 0; k < 4; k += 2) {
                    const uint64_t base = uint64_t(lut.base[fs[k]])
                            | (uint64_t(lut.base[fs[k + 1]]) << 32);
                    const uint64_t mask = uint64_t(lut.mask[fs[k]])
                            | (uint64_t(lut.mask[fs[k + 1]]) << 32);

                    const uint64_t data    = ZL_readLE64(inp + k * 2);
                    const uint64_t offset  = data - base;
                    const uint64_t varBits = utils::bitExtract(offset, mask);

                    varAcc |= varBits << varAccBits;
                    varAccBits += __builtin_popcountll(mask);

                    ZL_writeLE64(v, varAcc);
                    const size_t flushBytes = varAccBits >> 3;
                    v += flushBytes;
                    varAcc >>= (flushBytes << 3);
                    varAccBits &= 7;
                }
            }
        }
        return encodeTail(in, i, numElts, 3, f, v, var, varAcc, varAccBits);
    }

    size_t encode4(
            poly::span<const uint16_t> input,
            uint8_t* fixed,
            uint8_t* var) const
    {
        const size_t numElts        = input.size();
        const uint16_t* in          = input.data();
        uint8_t* f                  = fixed;
        uint8_t* v                  = var;
        const uint8_t* const vBound = var + varStreamBound(numElts);

        const auto lut = buildLUT<4>();

        uint64_t varAcc               = 0;
        size_t varAccBits             = 0;
        constexpr size_t kEltsPerIter = 16;
        size_t i                      = 0;

        size_t limit = computeLimit(i, numElts, kEltsPerIter, v, vBound);
        for (;; i += kEltsPerIter) {
            if (i >= limit) {
                limit = computeLimit(i, numElts, kEltsPerIter, v, vBound);
                if (i >= limit) {
                    break;
                }
            }
            static_assert(kEltsPerIter % 4 == 0);
            for (size_t u = 0; u < kEltsPerIter; u += 4) {
                const uint16_t* inp = in + i + u;

                const auto b0 = valueToBucket16_[inp[0] >> lutShift_];
                const auto b1 = valueToBucket16_[inp[1] >> lutShift_];
                const auto b2 = valueToBucket16_[inp[2] >> lutShift_];
                const auto b3 = valueToBucket16_[inp[3] >> lutShift_];

                const uint8_t fb0 = b0 | (b1 << 4);
                const uint8_t fb1 = b2 | (b3 << 4);

                const uint64_t base = uint64_t(lut.base[fb0])
                        | (uint64_t(lut.base[fb1]) << 32);
                const uint64_t mask = uint64_t(lut.mask[fb0])
                        | (uint64_t(lut.mask[fb1]) << 32);

                f[0] = fb0;
                f[1] = fb1;
                f += 2;

                const uint64_t data    = ZL_readLE64(inp);
                const uint64_t offset  = data - base;
                const uint64_t varBits = utils::bitExtract(offset, mask);

                varAcc |= varBits << varAccBits;
                varAccBits += __builtin_popcountll(mask);

                ZL_writeLE64(v, varAcc);
                const size_t flushBytes = varAccBits >> 3;
                v += flushBytes;
                varAcc >>= (flushBytes << 3);
                varAccBits &= 7;
            }
        }
        return encodeTail(in, i, numElts, 4, f, v, var, varAcc, varAccBits);
    }

    size_t encode5(
            poly::span<const uint16_t> input,
            uint8_t* fixed,
            uint8_t* var) const
    {
        const size_t numElts        = input.size();
        const uint16_t* in          = input.data();
        uint8_t* f                  = fixed;
        uint8_t* v                  = var;
        const uint8_t* const vBound = var + varStreamBound(numElts);

        const auto lut = buildLUT<5>();

        uint64_t varAcc               = 0;
        size_t varAccBits             = 0;
        constexpr size_t kEltsPerIter = 16;
        size_t i                      = 0;

        size_t limit = computeLimit(i, numElts, kEltsPerIter, v, vBound);
        for (;; i += kEltsPerIter) {
            if (i >= limit) {
                limit = computeLimit(i, numElts, kEltsPerIter, v, vBound);
                if (i >= limit) {
                    break;
                }
            }
            static_assert(kEltsPerIter % 8 == 0);
            for (size_t u = 0; u < kEltsPerIter; u += 8) {
                const uint16_t* inp = in + i + u;

                const auto b0 = valueToBucket16_[inp[0] >> lutShift_];
                const auto b1 = valueToBucket16_[inp[1] >> lutShift_];
                const auto b2 = valueToBucket16_[inp[2] >> lutShift_];
                const auto b3 = valueToBucket16_[inp[3] >> lutShift_];
                const auto b4 = valueToBucket16_[inp[4] >> lutShift_];
                const auto b5 = valueToBucket16_[inp[5] >> lutShift_];
                const auto b6 = valueToBucket16_[inp[6] >> lutShift_];
                const auto b7 = valueToBucket16_[inp[7] >> lutShift_];

                const std::array<uint64_t, 4> fs = {
                    uint64_t(b0 | (b1 << 5)),
                    uint64_t(b2 | (b3 << 5)),
                    uint64_t(b4 | (b5 << 5)),
                    uint64_t(b6 | (b7 << 5)),
                };
                const uint64_t F =
                        fs[0] | (fs[1] << 10) | (fs[2] << 20) | (fs[3] << 30);
                f[0] = F & 0xFF;
                f[1] = (F >> 8) & 0xFF;
                f[2] = (F >> 16) & 0xFF;
                f[3] = (F >> 24) & 0xFF;
                f[4] = (F >> 32) & 0xFF;
                f += 5;

                for (size_t k = 0; k < 4; k += 2) {
                    const uint64_t base = uint64_t(lut.base[fs[k]])
                            | (uint64_t(lut.base[fs[k + 1]]) << 32);
                    const uint64_t mask = uint64_t(lut.mask[fs[k]])
                            | (uint64_t(lut.mask[fs[k + 1]]) << 32);

                    const uint64_t data    = ZL_readLE64(inp + k * 2);
                    const uint64_t offset  = data - base;
                    const uint64_t varBits = utils::bitExtract(offset, mask);

                    varAcc |= varBits << varAccBits;
                    varAccBits += __builtin_popcountll(mask);

                    ZL_writeLE64(v, varAcc);
                    const size_t flushBytes = varAccBits >> 3;
                    v += flushBytes;
                    varAcc >>= (flushBytes << 3);
                    varAccBits &= 7;
                }
            }
        }
        return encodeTail(in, i, numElts, 5, f, v, var, varAcc, varAccBits);
    }

    size_t encode6(
            poly::span<const uint16_t> input,
            uint8_t* fixed,
            uint8_t* var) const
    {
        const size_t numElts        = input.size();
        const uint16_t* in          = input.data();
        uint8_t* f                  = fixed;
        uint8_t* v                  = var;
        const uint8_t* const vBound = var + varStreamBound(numElts);

        const auto lut = buildLUT<6>();

        uint64_t varAcc               = 0;
        size_t varAccBits             = 0;
        constexpr size_t kEltsPerIter = 16;
        size_t i                      = 0;

        size_t limit = computeLimit(i, numElts, kEltsPerIter, v, vBound);
        for (;; i += kEltsPerIter) {
            if (i >= limit) {
                limit = computeLimit(i, numElts, kEltsPerIter, v, vBound);
                if (i >= limit) {
                    break;
                }
            }
            static_assert(kEltsPerIter % 4 == 0);
            for (size_t u = 0; u < kEltsPerIter; u += 4) {
                const uint16_t* inp = in + i + u;

                const auto b0 = valueToBucket16_[inp[0] >> lutShift_];
                const auto b1 = valueToBucket16_[inp[1] >> lutShift_];
                const auto b2 = valueToBucket16_[inp[2] >> lutShift_];
                const auto b3 = valueToBucket16_[inp[3] >> lutShift_];

                const uint32_t f0 = b0 | (b1 << 6);
                const uint32_t f1 = b2 | (b3 << 6);
                const uint32_t F  = f0 | (f1 << 12);
                f[0]              = F & 0xFF;
                f[1]              = (F >> 8) & 0xFF;
                f[2]              = (F >> 16) & 0xFF;
                f += 3;

                const uint64_t base =
                        uint64_t(lut.base[f0]) | (uint64_t(lut.base[f1]) << 32);
                const uint64_t mask =
                        uint64_t(lut.mask[f0]) | (uint64_t(lut.mask[f1]) << 32);

                const uint64_t data    = ZL_readLE64(inp);
                const uint64_t offset  = data - base;
                const uint64_t varBits = utils::bitExtract(offset, mask);

                varAcc |= varBits << varAccBits;
                varAccBits += __builtin_popcountll(mask);

                ZL_writeLE64(v, varAcc);
                const size_t flushBytes = varAccBits >> 3;
                v += flushBytes;
                varAcc >>= (flushBytes << 3);
                varAccBits &= 7;
            }
        }
        return encodeTail(in, i, numElts, 6, f, v, var, varAcc, varAccBits);
    }

    poly::span<const uint16_t> bases_;
    size_t numSymbols_;
    size_t fixedBits_;
    size_t lutShift_;
    std::vector<uint16_t> valueToBucket16_;
    std::vector<uint16_t> valueToBase_;
    std::vector<uint16_t> valueToVarBits_;
    size_t maxVarBits_;
};

class Bucket16DecoderState {
   public:
    Bucket16DecoderState(poly::span<const uint8_t> header)
    {
        if (header.size() < 3) {
            throw std::runtime_error("header is empty");
        }
        unusedFixedBits_      = header[0] % 8;
        size_t maxSymbolValue = ZL_readLE16(header.data() + 1);
        numSymbols_           = maxSymbolValue + 1;
        header                = header.subspan(3);
        if (header.empty()) {
            throw std::runtime_error("bases is empty");
        }
        if (header.size() % 2 != 0) {
            throw std::runtime_error("not a multiple of 2");
        }
        if (header.size() > 2 * bases_.size()) {
            throw std::runtime_error("bases is > 256");
        }
        // TODO: Handle big endian
        memcpy(bases_.data(), header.data(), header.size());
        numBases_ = header.size() / 2;
        if (bases_.back() > maxSymbolValue) {
            throw std::runtime_error("bases[-1] is > maxSymbolValue");
        }
        for (size_t i = 1; i < numBases_; ++i) {
            if (bases_[i - 1] >= bases_[i]) {
                throw std::runtime_error("bases not strictly increasing");
            }
        }

        maxVarBits_ = 0;
        for (size_t i = 0; i < numBases_; ++i) {
            varBits_[i] = ZL_nextPow2(getBucketSize(i));
            maxVarBits_ = std::max<size_t>(maxVarBits_, varBits_[i]);
        }
        if (maxVarBits_ >= 15) {
            // Keeping <= 14 means that we can get away with a single 64-bit
            // load
            throw std::runtime_error("unsupported varbits >= 15");
        }

        fixedBits_ = ZL_nextPow2(numBases_);
        assert(fixedBits_ <= 8);
    }

    size_t fixedBits() const
    {
        return fixedBits_;
    }

    poly::span<const uint16_t> bases() const
    {
        return { bases_.data(), numBases_ };
    }

    size_t numElts(size_t fixedSize) const
    {
        const auto totalFixedBits = fixedSize * 8;
        if (totalFixedBits < unusedFixedBits_) {
            throw std::runtime_error("bad number of bits");
        }
        if ((totalFixedBits - unusedFixedBits_) % fixedBits_ != 0) {
            throw std::runtime_error("leftover bits");
        }
        return (totalFixedBits - unusedFixedBits_) / fixedBits_;
    }

    static std::array<uint32_t, 64> expandLUT3(poly::span<const uint16_t> lut)
    {
        std::array<uint32_t, 64> expanded;
        for (size_t byte = 0; byte < 64; ++byte) {
            const size_t lo = byte & 7;
            const size_t hi = byte >> 3;
            expanded[byte]  = uint32_t(lut[lo]) | (uint32_t(lut[hi]) << 16);
        }
        return expanded;
    }

    std::array<uint16_t, 8> makeBaseLUT3() const
    {
        std::array<uint16_t, 8> lut{};
        memcpy(lut.data(), bases_.data(), numBases_ * 2);
        return lut;
    }

    std::array<uint16_t, 8> makeMaskLUT3() const
    {
        std::array<uint16_t, 8> lut;
        for (size_t i = 0; i < 8; ++i) {
            if (i < numBases_) {
                lut[i] = (1u << varBits_[i]) - 1;
                assert(varBits_[i] <= 14);
            } else {
                lut[i] = 0;
            }
        }
        return lut;
    }

    void decode3(
            poly::span<const uint8_t> fixed,
            poly::span<const uint8_t> var,
            uint16_t* out) const
    {
        const auto baseLUT = expandLUT3(makeBaseLUT3());
        const auto maskLUT = expandLUT3(makeMaskLUT3());
        const auto size    = numElts(fixed.size());

        uint16_t* o               = out;
        const uint8_t* f          = fixed.data();
        const uint8_t* v          = var.data();
        const uint8_t* const vEnd = v + var.size();

        constexpr size_t kEltsPerIter = 16;
        size_t i                      = 0;

        const auto computeLimit = [&] {
            if (size < kEltsPerIter) {
                return i;
            }
            assert(v <= vEnd);
            const size_t vSafeNumIters = (8 * (vEnd - v) - 7) / maxVarBits_;
            if (vSafeNumIters < kEltsPerIter) {
                return i;
            }
            const auto limit = std::min(
                    size - kEltsPerIter, i + vSafeNumIters - kEltsPerIter);
            return limit;
        };

        size_t bitsConsumed = 0;
        size_t limit        = computeLimit();
        for (;; i += kEltsPerIter) {
            if (i >= limit) {
                limit = computeLimit();
                if (i >= limit) {
                    break;
                }
            }
            static_assert(kEltsPerIter % 8 == 0);
            for (size_t u = 0; u < kEltsPerIter; u += 8) {
                uint32_t const F                 = ZL_readLE32(f);
                const std::array<uint32_t, 4> fs = {
                    (F >> 0) & 63,
                    (F >> 6) & 63,
                    (F >> 12) & 63,
                    (F >> 18) & 63,
                };
                f += 3;
                for (size_t k = 0; k < 4; k += 2) {
                    const uint64_t base = uint64_t(baseLUT[fs[k + 0]])
                            | (uint64_t(baseLUT[fs[k + 1]]) << 32);
                    const uint64_t mask = uint64_t(maskLUT[fs[k + 0]])
                            | (uint64_t(maskLUT[fs[k + 1]]) << 32);

                    const uint64_t vData  = ZL_readLE64(v) >> bitsConsumed;
                    const uint64_t offset = utils::bitDeposit(vData, mask);

                    ZL_writeLE64(o, base + offset);
                    o += 4;

                    bitsConsumed += __builtin_popcountll(mask);
                    assert(bitsConsumed <= 64);
                    v += bitsConsumed >> 3;
                    bitsConsumed &= 7;
                }
            }
        }

        assert(v <= vEnd);
        auto fixedBitstream =
                ZS_BitDStreamFF_init(f, (fixed.data() + fixed.size()) - f);
        auto varBitstream = ZS_BitDStreamFF_init(v, vEnd - v);
        ZS_BitDStreamFF_skip(&varBitstream, bitsConsumed);
        for (; i < size; ++i) {
            auto bucket = ZS_BitDStreamFF_read(&fixedBitstream, 3);
            ZS_BitDStreamFF_reload(&fixedBitstream);

            auto base = bases_[bucket];
            auto bits = varBits_[bucket];

            auto offset = ZS_BitDStreamFF_read(&varBitstream, bits);
            ZS_BitDStreamFF_reload(&varBitstream);
            out[i] = base + offset;
        }
        auto report = ZS_BitDStreamFF_finish(&varBitstream);
        if (ZL_isError(report)) {
            throw std::runtime_error("read too many varbits");
        }
    }

    static std::array<uint32_t, 256> expandLUT(poly::span<const uint16_t> lut)
    {
        std::array<uint32_t, 256> expanded;
        for (size_t byte = 0; byte < 256; ++byte) {
            const size_t lo = byte & 0xF;
            const size_t hi = byte >> 4;
            expanded[byte]  = uint32_t(lut[lo]) | (uint32_t(lut[hi]) << 16);
        }
        return expanded;
    }

    std::array<uint16_t, 16> makeBaseLUT() const
    {
        std::array<uint16_t, 16> lut{};
        memcpy(lut.data(), bases_.data(), numBases_ * 2);
        return lut;
    }

    std::array<uint16_t, 16> makeMaskLUT() const
    {
        std::array<uint16_t, 16> lut;
        for (size_t i = 0; i < 16; ++i) {
            if (i < numBases_) {
                lut[i] = (1u << varBits_[i]) - 1;
                assert(varBits_[i] <= 14);
            } else {
                lut[i] = 0;
            }
        }
        return lut;
    }

    void decode4(
            poly::span<const uint8_t> fixed,
            poly::span<const uint8_t> var,
            uint16_t* out) const
    {
        const auto baseLUT = expandLUT(makeBaseLUT());
        const auto maskLUT = expandLUT(makeMaskLUT());
        const auto size    = numElts(fixed.size());

        uint16_t* o               = out;
        const uint8_t* f          = fixed.data();
        const uint8_t* v          = var.data();
        const uint8_t* const vEnd = v + var.size();

        constexpr size_t kEltsPerIter = 16;
        size_t i                      = 0;

        const auto computeLimit = [&] {
            if (size < kEltsPerIter) {
                return i;
            }
            assert(v <= vEnd);
            const size_t vSafeNumIters = (8 * (vEnd - v) - 7) / maxVarBits_;
            if (vSafeNumIters < kEltsPerIter) {
                return i;
            }
            const auto limit = std::min(
                    size - kEltsPerIter, i + vSafeNumIters - kEltsPerIter);
            return limit;
        };

        size_t bitsConsumed = 0;
        size_t limit        = computeLimit();
        for (;; i += kEltsPerIter) {
            if (i >= limit) {
                limit = computeLimit();
                if (i >= limit) {
                    break;
                }
            }
            static_assert(kEltsPerIter % 4 == 0);
            for (size_t u = 0; u < kEltsPerIter; u += 4) {
                const uint64_t base = uint64_t(baseLUT[f[0]])
                        | (uint64_t(baseLUT[f[1]]) << 32);
                const uint64_t mask = uint64_t(maskLUT[f[0]])
                        | (uint64_t(maskLUT[f[1]]) << 32);
                f += 2;

                const uint64_t vData  = ZL_readLE64(v) >> bitsConsumed;
                const uint64_t offset = utils::bitDeposit(vData, mask);

                ZL_writeLE64(o, base + offset);
                o += 4;

                bitsConsumed += __builtin_popcountll(mask);
                assert(bitsConsumed <= 64);
                v += bitsConsumed >> 3;
                bitsConsumed &= 7;
            }
        }

        assert(v <= vEnd);
        auto varBitstream = ZS_BitDStreamFF_init(v, vEnd - v);
        ZS_BitDStreamFF_skip(&varBitstream, bitsConsumed);
        for (; i < size; ++i) {
            size_t bucket = fixed[i / 2];
            if (i % 2 == 0) {
                bucket &= 0xF;
            } else {
                bucket >>= 4;
            }

            auto base = bases_[bucket];
            auto bits = varBits_[bucket];

            auto offset = ZS_BitDStreamFF_read(&varBitstream, bits);
            ZS_BitDStreamFF_reload(&varBitstream);
            out[i] = base + offset;
        }
        auto report = ZS_BitDStreamFF_finish(&varBitstream);
        if (ZL_isError(report)) {
            throw std::runtime_error("read too many varbits");
        }
    }

    static std::array<uint32_t, 1024> expandLUT5(poly::span<const uint16_t> lut)
    {
        std::array<uint32_t, 1024> expanded;
        for (size_t byte = 0; byte < 1024; ++byte) {
            const size_t lo = byte & 31;
            const size_t hi = byte >> 5;
            expanded[byte]  = uint32_t(lut[lo]) | (uint32_t(lut[hi]) << 16);
        }
        return expanded;
    }

    std::array<uint16_t, 32> makeBaseLUT5() const
    {
        std::array<uint16_t, 32> lut{};
        memcpy(lut.data(), bases_.data(), numBases_ * 2);
        return lut;
    }

    std::array<uint16_t, 32> makeMaskLUT5() const
    {
        std::array<uint16_t, 32> lut;
        for (size_t i = 0; i < 32; ++i) {
            if (i < numBases_) {
                lut[i] = (1u << varBits_[i]) - 1;
                assert(varBits_[i] <= 14);
            } else {
                lut[i] = 0;
            }
        }
        return lut;
    }

    void decode5(
            poly::span<const uint8_t> fixed,
            poly::span<const uint8_t> var,
            uint16_t* out) const
    {
        const auto baseLUT = expandLUT5(makeBaseLUT5());
        const auto maskLUT = expandLUT5(makeMaskLUT5());
        const auto size    = numElts(fixed.size());

        uint16_t* o               = out;
        const uint8_t* f          = fixed.data();
        const uint8_t* v          = var.data();
        const uint8_t* const vEnd = v + var.size();

        constexpr size_t kEltsPerIter = 16;
        size_t i                      = 0;

        const auto computeLimit = [&] {
            if (size < kEltsPerIter) {
                return i;
            }
            assert(v <= vEnd);
            const size_t vSafeNumIters = (8 * (vEnd - v) - 7) / maxVarBits_;
            if (vSafeNumIters < kEltsPerIter) {
                return i;
            }
            const auto limit = std::min(
                    size - kEltsPerIter, i + vSafeNumIters - kEltsPerIter);
            return limit;
        };

        size_t bitsConsumed = 0;
        size_t limit        = computeLimit();
        for (;; i += kEltsPerIter) {
            if (i >= limit) {
                limit = computeLimit();
                if (i >= limit) {
                    break;
                }
            }
            static_assert(kEltsPerIter % 8 == 0);
            for (size_t u = 0; u < kEltsPerIter; u += 8) {
                auto const F                     = ZL_readLE64(f);
                const std::array<uint64_t, 4> fs = {
                    (F >> 00) & 1023,
                    (F >> 10) & 1023,
                    (F >> 20) & 1023,
                    (F >> 30) & 1023,
                };
                f += 5;
                for (size_t k = 0; k < 4; k += 2) {
                    const uint64_t base = uint64_t(baseLUT[fs[k + 0]])
                            | (uint64_t(baseLUT[fs[k + 1]]) << 32);
                    const uint64_t mask = uint64_t(maskLUT[fs[k + 0]])
                            | (uint64_t(maskLUT[fs[k + 1]]) << 32);

                    const uint64_t vData  = ZL_readLE64(v) >> bitsConsumed;
                    const uint64_t offset = utils::bitDeposit(vData, mask);

                    ZL_writeLE64(o, base + offset);
                    o += 4;

                    bitsConsumed += __builtin_popcountll(mask);
                    assert(bitsConsumed <= 64);
                    v += bitsConsumed >> 3;
                    bitsConsumed &= 7;
                }
            }
        }

        assert(v <= vEnd);
        auto fixedBitstream =
                ZS_BitDStreamFF_init(f, (fixed.data() + fixed.size()) - f);
        auto varBitstream = ZS_BitDStreamFF_init(v, vEnd - v);
        ZS_BitDStreamFF_skip(&varBitstream, bitsConsumed);
        for (; i < size; ++i) {
            auto bucket = ZS_BitDStreamFF_read(&fixedBitstream, 5);
            ZS_BitDStreamFF_reload(&fixedBitstream);

            auto base = bases_[bucket];
            auto bits = varBits_[bucket];

            auto offset = ZS_BitDStreamFF_read(&varBitstream, bits);
            ZS_BitDStreamFF_reload(&varBitstream);
            out[i] = base + offset;
        }
        auto report = ZS_BitDStreamFF_finish(&varBitstream);
        if (ZL_isError(report)) {
            throw std::runtime_error("read too many varbits");
        }
    }

    static std::array<uint32_t, 4096> expandLUT6(poly::span<const uint16_t> lut)
    {
        std::array<uint32_t, 4096> expanded;
        for (size_t byte = 0; byte < 4096; ++byte) {
            const size_t lo = byte & 63;
            const size_t hi = byte >> 6;
            expanded[byte]  = uint32_t(lut[lo]) | (uint32_t(lut[hi]) << 16);
        }
        return expanded;
    }

    std::array<uint16_t, 64> makeBaseLUT6() const
    {
        std::array<uint16_t, 64> lut{};
        memcpy(lut.data(), bases_.data(), numBases_ * 2);
        return lut;
    }

    std::array<uint16_t, 64> makeMaskLUT6() const
    {
        std::array<uint16_t, 64> lut;
        for (size_t i = 0; i < 64; ++i) {
            if (i < numBases_) {
                lut[i] = (1u << varBits_[i]) - 1;
                assert(varBits_[i] <= 14);
            } else {
                lut[i] = 0;
            }
        }
        return lut;
    }

    void decode6(
            poly::span<const uint8_t> fixed,
            poly::span<const uint8_t> var,
            uint16_t* out) const
    {
        const auto baseLUT = expandLUT6(makeBaseLUT6());
        const auto maskLUT = expandLUT6(makeMaskLUT6());
        const auto size    = numElts(fixed.size());

        uint16_t* o               = out;
        const uint8_t* f          = fixed.data();
        const uint8_t* v          = var.data();
        const uint8_t* const vEnd = v + var.size();

        constexpr size_t kEltsPerIter = 16;
        size_t i                      = 0;

        const auto computeLimit = [&] {
            if (size < kEltsPerIter) {
                return i;
            }
            assert(v <= vEnd);
            const size_t vSafeNumIters = (8 * (vEnd - v) - 7) / maxVarBits_;
            if (vSafeNumIters < kEltsPerIter) {
                return i;
            }
            const auto limit = std::min(
                    size - kEltsPerIter, i + vSafeNumIters - kEltsPerIter);
            return limit;
        };

        size_t bitsConsumed = 0;
        size_t limit        = computeLimit();
        for (;; i += kEltsPerIter) {
            if (i >= limit) {
                limit = computeLimit();
                if (i >= limit) {
                    break;
                }
            }
            static_assert(kEltsPerIter % 4 == 0);
            for (size_t u = 0; u < kEltsPerIter; u += 4) {
                uint32_t const F = ZL_readLE32(f);
                const auto f0    = F & 4095;
                const auto f1    = (F >> 12) & 4095;
                f += 3;
                {
                    const uint64_t base = uint64_t(baseLUT[f0])
                            | (uint64_t(baseLUT[f1]) << 32);
                    const uint64_t mask = uint64_t(maskLUT[f0])
                            | (uint64_t(maskLUT[f1]) << 32);

                    const uint64_t vData  = ZL_readLE64(v) >> bitsConsumed;
                    const uint64_t offset = utils::bitDeposit(vData, mask);

                    ZL_writeLE64(o, base + offset);
                    o += 4;

                    bitsConsumed += __builtin_popcountll(mask);
                    assert(bitsConsumed <= 64);
                    v += bitsConsumed >> 3;
                    bitsConsumed &= 7;
                }
            }
        }

        assert(v <= vEnd);
        auto fixedBitstream =
                ZS_BitDStreamFF_init(f, (fixed.data() + fixed.size()) - f);
        auto varBitstream = ZS_BitDStreamFF_init(v, vEnd - v);
        ZS_BitDStreamFF_skip(&varBitstream, bitsConsumed);
        for (; i < size; ++i) {
            auto bucket = ZS_BitDStreamFF_read(&fixedBitstream, 6);
            ZS_BitDStreamFF_reload(&fixedBitstream);

            auto base = bases_[bucket];
            auto bits = varBits_[bucket];

            auto offset = ZS_BitDStreamFF_read(&varBitstream, bits);
            ZS_BitDStreamFF_reload(&varBitstream);
            out[i] = base + offset;
        }
        auto report = ZS_BitDStreamFF_finish(&varBitstream);
        if (ZL_isError(report)) {
            throw std::runtime_error("read too many varbits");
        }
    }

    void decode(
            poly::span<const uint8_t> fixed,
            poly::span<const uint8_t> var,
            uint16_t* output) const
    {
        if (fixedBits_ == 3) {
            decode3(fixed, var, output);
            return;
        }
        if (fixedBits_ == 4) {
            decode4(fixed, var, output);
            return;
        }
        if (fixedBits_ == 5) {
            decode5(fixed, var, output);
            return;
        }
        if (fixedBits_ == 6) {
            decode6(fixed, var, output);
            return;
        }
        auto fixedBitstream = ZS_BitDStreamFF_init(fixed.data(), fixed.size());
        auto varBitstream   = ZS_BitDStreamFF_init(var.data(), var.size());
        const auto size     = numElts(fixed.size());

        for (size_t i = 0; i < size; ++i) {
            const auto bucket =
                    ZS_BitDStreamFF_read(&fixedBitstream, fixedBits_);
            assert(bucket < 256);
            const auto base = bases_[bucket];
            const auto offset =
                    ZS_BitDStreamFF_read(&varBitstream, varBits_[bucket]);
            output[i] = base + offset;

            ZS_BitDStreamFF_reload(&fixedBitstream);
            ZS_BitDStreamFF_reload(&varBitstream);
        }
    }

   private:
    size_t getBucketSize(size_t baseIdx) const
    {
        if (baseIdx >= numBases_) {
            return 1;
        }
        size_t base = bases_[baseIdx];
        assert(base < numSymbols_);
        auto end = baseIdx + 1 == numBases_ ? numSymbols_
                                            : size_t(bases_[baseIdx + 1]);
        assert(end > base);
        return end - base;
    }

    std::array<uint16_t, 256> bases_{};
    std::array<uint8_t, 256> varBits_{};
    size_t unusedFixedBits_;
    size_t fixedBits_;
    size_t numBases_;
    size_t maxVarBits_;
    size_t numSymbols_;
};
} // namespace

SimpleCodecDescription Bucket16Encoder::simpleCodecDescription() const
{
    return bucket16CodecDescription();
}

void Bucket16Encoder::encode(EncoderState& encoder) const
{
    auto bases = encoder.getLocalParam(0);
    if (!bases.has_value()) {
        throw std::runtime_error("bases not present");
    }
    auto maxSymbolValue = encoder.getLocalIntParam(1);
    if (!maxSymbolValue.has_value()) {
        throw std::runtime_error("maxSymbolValue not present");
    }
    Bucket16EncoderState state(*bases, *maxSymbolValue);

    const auto& input    = encoder.inputs()[0];
    const size_t numElts = input.numElts();

    if (input.eltWidth() != 2) {
        throw std::runtime_error("Unsupported width != 2");
    }

    uint8_t header[515];
    // first byte is the # of unused bits in the bitstream
    header[0] = (8 - ((state.fixedBits() * numElts) % 8)) % 8;
    // next 2 bytes are the max symbol value
    ZL_writeLE16(header + 1, *maxSymbolValue);
    // TODO: Handle big endian
    memcpy(header + 3, state.bases().data(), state.bases().size() * 2);

    encoder.sendCodecHeader(header, 3 + state.bases().size() * 2);

    auto fixedStream =
            encoder.createOutput(0, state.fixedStreamSize(numElts), 1);
    auto varStream = encoder.createOutput(1, state.varStreamBound(numElts), 1);

    auto varStreamSize = state.encode(
            { (const uint16_t*)input.ptr(), input.numElts() },
            (uint8_t*)fixedStream.ptr(),
            (uint8_t*)varStream.ptr());

    fixedStream.commit(state.fixedStreamSize(numElts));
    varStream.commit(varStreamSize);
}
/* static */ Edge::RunNodeResult Bucket16Encoder::runNode(
        Edge& edge,
        NodeID node,
        poly::span<const uint16_t> bases,
        uint16_t maxSymbolValue)
{
    NodeParameters params;
    params.localParams.emplace();
    params.localParams->addRefParam(0, bases.data(), bases.size_bytes());
    params.localParams->addIntParam(1, maxSymbolValue);
    auto r = edge.runNode(node, std::move(params));
    return r;
}

SimpleCodecDescription Bucket16Decoder::simpleCodecDescription() const
{
    return bucket16CodecDescription();
}

void Bucket16Decoder::decode(DecoderState& decoder) const
{
    Bucket16DecoderState state(decoder.getCodecHeader());

    const auto& fixedStream = decoder.singletonInputs()[0];
    const auto& varStream   = decoder.singletonInputs()[1];
    const auto fixedSize    = fixedStream.numElts();

    auto outStream = decoder.createOutput(0, state.numElts(fixedSize), 2);

    state.decode(
            { (const uint8_t*)fixedStream.ptr(), fixedSize },
            { (const uint8_t*)varStream.ptr(), varStream.numElts() },
            (uint16_t*)outStream.ptr());

    outStream.commit(state.numElts(fixedSize));
}

/* static */ GraphID Bucket16Graph::create(Compressor& compressor)
{
    auto graph = compressor.getGraph("lz_research.bucket16_graph");
    if (!graph.has_value()) {
        graph = compressor.registerFunctionGraph(
                std::make_shared<Bucket16Graph>());
    }

    auto node = compressor.getNode("lz_research.bucket16");
    if (!node.has_value()) {
        node = compressor.registerCustomEncoder(
                std::make_shared<Bucket16Encoder>());
    }

    return compressor.parameterizeGraph(
            *graph, GraphParameters{ .customNodes = { { *node } } });
}

FunctionGraphDescription Bucket16Graph::functionGraphDescription() const
{
    return FunctionGraphDescription{
        .name           = "!lz_research.bucket16_graph",
        .inputTypeMasks = { TypeMask::Numeric },
    };
}

namespace {
constexpr uint32_t kBucket16OverheadBits = 40;
constexpr size_t kMaxBucketSize          = 1u << 14;

uint32_t fixedBucketCost(poly::span<const uint32_t> bucket, uint32_t fixedCost)
{
    const uint32_t bucketCount =
            std::accumulate(bucket.begin(), bucket.end(), 0);
    return bucketCount * (fixedCost + ZL_nextPow2(bucket.size()));
}

uint32_t fixedBucketCost(
        poly::span<const uint32_t> histogram,
        poly::span<const uint16_t> partitions)
{
    const uint32_t totalCount =
            std::accumulate(histogram.begin(), histogram.end(), 0);
    const uint32_t bucketBits = ZL_nextPow2(partitions.size());
    uint32_t cost             = totalCount * bucketBits;
    for (size_t i = 0; i < partitions.size(); ++i) {
        const auto b = partitions[i];
        const auto e = i + 1 == partitions.size() ? histogram.size()
                                                  : partitions[i + 1];
        assert(b < e);
        const uint32_t bucketCount = std::accumulate(
                histogram.begin() + b, histogram.begin() + e, 0);
        const uint32_t offsetBits = ZL_nextPow2(e - b);
        cost += kBucket16OverheadBits + bucketCount * offsetBits;
    }
    return cost;
}

std::vector<uint16_t> fixedPartition(
        poly::span<const uint32_t> histogram,
        size_t numBuckets)
{
#if 0
    constexpr size_t kNumBuckets       = 766;
    std::array<uint32_t, 8> bitsOffset = {
        0,
        0 + 4,
        0 + 4 + 6,
        0 + 4 + 6 + 12,
        0 + 4 + 6 + 12 + 24,
        0 + 4 + 6 + 12 + 24 + 48,
        0 + 4 + 6 + 12 + 24 + 48 + 96,
        0 + 4 + 6 + 12 + 24 + 48 + 96 + 192,
    };
    std::array<uint32_t, kNumBuckets + 1> bucketsToBits;
    for (size_t bucket = 0; bucket < bucketsToBits.size(); ++bucket) {
        size_t bits = bitsOffset.size() - 1;
        while (bucket < bitsOffset[bits]) {
            --bits;
        }
        bucketsToBits[bucket] = bits;
    }
    auto bitsToBase = [&](size_t bits) {
        return bits == 0 ? 0 : 1u << (bits << 1);
    };
    auto bucketToBase = [&](size_t bucket) {
        auto bits  = bucketsToBits[bucket];
        auto base0 = bitsToBase(bits);
        auto base  = base0 + ((bucket - bitsOffset[bits]) << bits);
        return base;
    };

    auto getBucket = [&](size_t i) {
        auto bits   = [](size_t val) { return ZL_highbit32(val | 1) >> 1; };
        auto b      = bits(i);
        auto base   = bitsToBase(b);
        auto bucket = bitsOffset[b] + ((i - base) >> b);
        return bucket;
    };
#else
    constexpr size_t kBucketBits = 3;
    constexpr size_t kNumBuckets = 1u << (16 - kBucketBits);

    auto getBucket    = [kBucketBits](size_t i) { return i >> kBucketBits; };
    auto bucketToBase = [kBucketBits](size_t bucket) {
        return bucket << kBucketBits;
    };
#endif

    std::array<uint32_t, kNumBuckets + 1> bucketedCumHist{};
    size_t bucketCount = bucketedCumHist.size();

    for (size_t i = 0; i < histogram.size(); ++i) {
        auto bucket = getBucket(i);
        assert(bucket < kNumBuckets);
        bucketedCumHist[bucket] += histogram[i];
    }
    while (bucketCount > 0 && bucketedCumHist[bucketCount - 1] == 0) {
        --bucketCount;
    }
    for (size_t i = 0, cum = 0; i < bucketedCumHist.size(); ++i) {
        const auto count   = bucketedCumHist[i];
        bucketedCumHist[i] = cum;
        cum += count;
    }

    poly::span<const uint32_t> hist({ bucketedCumHist.data(), bucketCount });

    auto bucketedPartitions = utils::partition(
            hist,
            numBuckets,
            getBucket(kMaxBucketSize),
            [&, fixed = ZL_highbit32(numBuckets)](auto bucket) {
                auto beginIdx = bucket.data() - hist.data();
                auto endIdx   = beginIdx + bucket.size();
                auto begin    = bucketToBase(beginIdx);
                auto end      = bucketToBase(endIdx);
                assert(begin < end);
                assert(end - begin <= kMaxBucketSize);
                // if (endIdx != hist.size() && !ZL_isPow2(end - begin)) {
                //     return std::numeric_limits<float>::infinity();
                // }
                const uint32_t bucketCount = *bucket.end() - *bucket.begin();
                auto cost =
                        float(bucketCount * (fixed + ZL_nextPow2(end - begin)));
                return cost;
            },
            [&](size_t idx) -> size_t { return bucketToBase(idx); });
    std::vector<uint16_t> p;
    for (const auto bucket : bucketedPartitions) {
        auto base = bucketToBase(bucket);
        p.push_back(base);
        // fprintf(stderr, "%zu -> %u\n", bucket, unsigned(base));
    }
    return p;
}

std::vector<uint16_t> fixedPartition(poly::span<const uint32_t> histogram)
{
    std::vector<uint16_t> best;
    uint32_t bestCost = std::numeric_limits<uint32_t>::max();
    for (const auto numBuckets : { 8, 16, 32, 64 }) {
        auto partitions = fixedPartition(histogram, numBuckets);
        auto cost       = fixedBucketCost(histogram, partitions);
        if (cost < 0.99 * bestCost) {
            bestCost = cost;
            best     = std::move(partitions);
        }
    }
    return best;
}
constexpr size_t kPrecisionLoss = 4;
constexpr size_t kCutoff        = 1u << 14;

size_t fixedBucketCostFast(
        poly::span<const uint32_t> cumHist,
        poly::span<const size_t> partitions,
        size_t numPartitions)
{
    auto bucket2idx = [](size_t bucket) { return bucket << kPrecisionLoss; };

    const uint32_t totalCount = cumHist.back();
    const uint32_t bucketBits = ZL_nextPow2(numPartitions);
    uint32_t cost             = totalCount * bucketBits;
    for (size_t i = 0; i < partitions.size(); ++i) {
        const auto b = partitions[i];
        const auto e = i + 1 == partitions.size() ? cumHist.size() - 1
                                                  : partitions[i + 1];
        assert(b < e);
        const uint32_t bucketCount = cumHist[e] - cumHist[b];
        const uint32_t offsetBits  = ZL_nextPow2(bucket2idx(e - b));
        cost += kBucket16OverheadBits + bucketCount * offsetBits;
    }
    return cost;
}

size_t fixedBucketCostFast(
        poly::span<const uint32_t> cumHist,
        poly::span<const size_t> partitions)
{
    return fixedBucketCostFast(cumHist, partitions, partitions.size());
}

std::vector<uint16_t> suffixPartition(
        poly::span<const uint32_t> hist,
        poly::span<const uint32_t> cumHist,
        size_t numPartitions)
{
    auto idx2bucket = [](size_t idx) { return idx >> kPrecisionLoss; };

    if (hist.empty()) {
        return {};
    }
    std::vector<uint16_t> partitions;
    partitions.push_back(0);

    int targetCount = cumHist.back() / numPartitions;

    while (partitions.back() != hist.size()) {
        const size_t begin = partitions.back();
        const size_t maxSize =
                std::min(idx2bucket(kMaxBucketSize), hist.size() - begin);

        size_t bestSize = 0;
        int bestDiff    = std::numeric_limits<int>::max();

        auto testSize = [&](size_t size) {
            const size_t end = begin + size;
            const int count  = cumHist[end] - cumHist[begin];
            const int diff   = std::abs(targetCount - count);
            if (diff < bestDiff) {
                bestSize = size;
                bestDiff = diff;
            }
        };

        for (size_t size = 1; size <= maxSize; size *= 2) {
            testSize(size);
        }
        if (maxSize == hist.size() - begin && bestSize != maxSize) {
            testSize(maxSize);
        }
        partitions.push_back(begin + bestSize);
    }
    partitions.pop_back();
    assert(partitions.size() <= numPartitions);
    return partitions;
}

size_t numMaxedBuckets(poly::span<const uint32_t> cumHist, size_t numPartitions)
{
    auto idx2bucket = [](size_t idx) { return idx >> kPrecisionLoss; };

    const uint32_t targetCount = cumHist.back() / numPartitions;
    size_t numMaxed            = 0;
    const auto maxBucketSize   = idx2bucket(kMaxBucketSize);
    const size_t histSize      = (cumHist.size() - 1);
    const size_t maxMaxed      = divUp(histSize, maxBucketSize);

    for (; numMaxed < maxMaxed; ++numMaxed) {
        auto begin = histSize - (maxBucketSize + 1) * numMaxed;
        auto end   = begin + maxBucketSize;
        auto count = cumHist[end] - cumHist[begin];
        if (count >= targetCount) {
            break;
        }
    }
    return numMaxed;
}

class GreedyOptimizer {
   public:
    GreedyOptimizer(
            poly::span<const uint32_t> cumHist,
            std::vector<size_t> prefixPartitions,
            size_t numPartitions)
            : cumHist_(cumHist),
              prefixPartitions_(std::move(prefixPartitions)),
              numPartitions_(numPartitions)
    {
        if (prefixPartitions_.empty()) {
            prefixPartitions_.push_back(0);
        }
        partitions_ = prefixPartitions_;
    }

    std::vector<size_t> run()
    {
        print("initial");
        while (partitions_.size() < numPartitions_) {
            if (!dividePartition()) {
                // fprintf(stderr, "cannot divide further\n");
                break;
            }
            print("divide");
        }
        iterativeImprovement();
        return partitions_;
    }

   private:
    void print(const char* msg) const
    {
        return;
        fprintf(stderr, "%s: cost = %f: ", msg, cost(partitions_));
        for (size_t i = 0; i < partitions_.size(); ++i) {
            fprintf(stderr, "%zu, ", partitions_[i]);
        }
        fprintf(stderr, "\n");
    }

    std::vector<size_t> growPartitions(size_t i) const
    {
        if (i + 1 == partitions_.size()) {
            return partitions_;
        }
        auto newPartitions = partitions_;

        auto begin           = partitions_[i];
        auto end             = partitions_[i + 1];
        auto size            = end - begin;
        const auto newSize   = 1u << (ZL_nextPow2(size) + 1);
        newPartitions[i + 1] = begin + newSize;

        for (size_t j = i + 1; j + 1 < partitions_.size(); ++j) {
            begin = partitions_[j];
            end   = partitions_[j + 1];
            size  = end - begin;
            // Ensure that sizes are increasing
            // Round up to a power of 2
            newPartitions[j + 1] = std::max(newSize, 1u << ZL_nextPow2(size));
        }
        return newPartitions;
    }

    std::vector<size_t> shrinkPartitions(size_t i) const
    {
        if (i + 1 == partitions_.size()) {
            return partitions_;
        }
        auto newPartitions = partitions_;

        auto begin = partitions_[i];
        auto end   = i + 1 == partitions_.size() ? cumHist_.size() - 1
                                                 : partitions_[i + 1];
        auto size  = end - begin;
        if (size == 1) {
            return partitions_;
        }

        const auto newSize   = 1u << (ZL_nextPow2(size) - 1);
        newPartitions[i + 1] = begin + newSize;

        for (size_t j = i + 1; j + 1 < partitions_.size(); ++j) {
            begin = partitions_[j];
            end   = partitions_[j + 1];
            size  = end - begin;
            // Round down to a power of 2
            if (!ZL_isPow2(size)) {
                newPartitions[j + 1] = 1u << (ZL_nextPow2(size) - 1);
            }
        }
        return newPartitions;
    }

    void iterativeImprovement()
    {
        assert(partitions_.size() <= numPartitions_);
        for (size_t idx = 0; idx + 1 < partitions_.size();) {
            auto partitions = growPartitions(idx);
            if (cost(partitions) < cost(partitions_)) {
                // fprintf(stderr, "grew idx=%zu\n", idx);
                partitions_ = std::move(partitions);
                print("iterative");
                continue;
            }
            partitions = shrinkPartitions(idx);
            if (cost(partitions) < cost(partitions_)) {
                // fprintf(stderr, "shrunk idx=%zu\n", idx);
                partitions_ = std::move(partitions);
                print("iterative");
                continue;
            }
            ++idx;
        }
    }

    void dividePartition(std::vector<size_t>& partitions, size_t i)
    {
        auto begin         = partitions_[i];
        auto end           = i + 1 == partitions_.size() ? (cumHist_.size() - 1)
                                                         : partitions_[i + 1];
        const auto size    = end - begin;
        const auto newSize = 1u << (ZL_nextPow2(size) - 1);
        // fprintf(stderr, "(%zu, %zu) = %zu -> %zu\n", begin, end, size,
        // newSize);
        partitions.insert(partitions.begin() + i + 1, begin + newSize);
    }

    float costWithDivide(size_t idx)
    {
        auto partitions = partitions_;
        dividePartition(partitions, idx);
        return cost(partitions);
    }

    void dividePartition(size_t i)
    {
        dividePartition(partitions_, i);
    }

    bool dividePartition()
    {
        float bestCost = std::numeric_limits<float>::max();
        auto bestIdx   = size_t(-1);
        assert(!prefixPartitions_.empty());
        for (size_t i = prefixPartitions_.size() - 1; i < partitions_.size();
             ++i) {
            auto begin = partitions_[i];
            auto end   = i + 1 == partitions_.size() ? (cumHist_.size() - 1)
                                                     : partitions_[i + 1];
            if (end - begin == 1) {
                continue;
            }
            if (!isLegal(begin, end)) {
                // fprintf(stderr, "illegal\n");
                dividePartition(i);
                return true;
            }
            auto cost = costWithDivide(i);
            if (cost < bestCost) {
                bestCost = cost;
                bestIdx  = i;
            }
        }
        if (bestIdx == size_t(-1)) {
            return false;
        }
        // fprintf(stderr, "dividing at idx = %zu\n", bestIdx);
        dividePartition(bestIdx);
        return true;
    }

    size_t idx2bucket(size_t idx) const
    {
        return idx >> kPrecisionLoss;
    }

    bool isLegal(size_t begin, size_t end) const
    {
        return begin < end && end - begin <= idx2bucket(kMaxBucketSize);
    }

    bool isLegal(poly::span<const size_t> partitions) const
    {
        if (partitions.empty()) {
            return false;
        }
        if (partitions.back() >= cumHist_.size() - 1) {
            return false;
        }
        for (size_t i = 0; i < partitions.size(); ++i) {
            auto begin = partitions[i];
            auto end   = i + 1 == partitions.size() ? (cumHist_.size() - 1)
                                                    : partitions[i + 1];
            if (!isLegal(begin, end)) {
                return false;
            }
        }
        return true;
    }

    float cost(poly::span<const size_t> partitions) const
    {
        if (!isLegal(partitions)) {
            return std::numeric_limits<float>::infinity();
        }
        return fixedBucketCostFast(cumHist_, partitions, numPartitions_);
    }

    poly::span<const uint32_t> cumHist_;
    std::vector<size_t> prefixPartitions_;
    std::vector<size_t> partitions_;
    size_t numPartitions_;
};

std::vector<size_t> completePartitions(
        poly::span<const uint32_t> cumHist,
        std::vector<size_t> partitions,
        size_t numPartitions)
{
    auto idx2bucket = [](size_t idx) { return idx >> kPrecisionLoss; };
    auto startIdx   = partitions.size();

    if (partitions.empty()) {
        partitions.push_back(0);
    }
    assert(partitions.size() <= numPartitions);

    auto begin      = partitions.back();
    int targetCount = (cumHist.back() - cumHist[begin])
            / (numPartitions - partitions.size());

    // fprintf(stderr, "target = %d\n", targetCount);

    size_t histSize = cumHist.size() - 1;
    while (partitions.back() != histSize) {
        const size_t begin = partitions.back();
        const size_t maxSize =
                std::min(idx2bucket(kMaxBucketSize), histSize - begin);

        size_t bestSize = 0;
        int bestDiff    = std::numeric_limits<int>::max();

        auto testSize = [&](size_t size) {
            const size_t end = begin + size;
            const int count  = cumHist[end] - cumHist[begin];
            const int diff   = std::abs(targetCount - count);
            if (diff < bestDiff) {
                bestSize = size;
                bestDiff = diff;
            }
        };

        for (size_t size = 1; size <= maxSize; size *= 2) {
            testSize(size);
        }
        if (maxSize == histSize - begin && bestSize != maxSize) {
            testSize(maxSize);
        }
        partitions.push_back(begin + bestSize);
        // fprintf(stderr,
        //         "add bucket of size = %zu, weight = %zu\n",
        //         bestSize,
        //         cumHist[begin + bestSize] - cumHist[begin]);
    }
    partitions.pop_back();
    // fprintf(stderr, "%zu ?= %zu\n", partitions.size(), numPartitions);
    while (partitions.size() < numPartitions) {
        size_t worstIdx = 0;
        int worstCost   = 0;
        for (size_t idx = startIdx + 1; idx < partitions.size(); ++idx) {
            auto begin      = partitions[idx - 1];
            auto end        = partitions[idx];
            const int count = cumHist[end] - cumHist[begin];
            const int cost  = count - targetCount;
            if (end - begin != 1 && cost > worstCost) {
                worstIdx  = idx;
                worstCost = cost;
            }
        }
        assert(worstIdx != 0);
        auto begin = partitions[worstIdx - 1];
        auto end   = partitions[worstIdx];
        assert(ZL_isPow2(end - begin));
        const auto newSize = (end - begin) / 2;
        partitions.insert(partitions.begin() + worstIdx - 1, begin + newSize);
    }
    // while (partitions.size() > numPartitions) {
    //     size_t bestIdx = 0;
    //     int bestCost   = 0;
    //     for (size_t idx = startIdx + 1; idx < partitions.size(); ++idx) {
    //         auto begin      = partitions[idx - 1];
    //         auto end        = partitions[idx];
    //         const int count = cumHist[end] - cumHist[begin];
    //         const int cost  = count - targetCount;
    //         if (end - begin != 1 && cost > worstCost) {
    //             worstIdx  = idx;
    //             worstCost = cost;
    //         }
    //     }
    //     assert(worstIdx != 0);
    //     auto begin = partitions[worstIdx - 1];
    //     auto end   = partitions[worstIdx];
    //     assert(ZL_isPow2(end - begin));
    //     const auto newSize = (end - begin) / 2;
    //     partitions.insert(partitions.begin() + worstIdx - 1, begin +
    //     newSize);
    // }

    return partitions;
}

// 1. Take advantage of buckets only getting larger for suffix.
// 2. Compute how many max-sized buckets we are going to have
//    Use that when computing the suffix count.
std::vector<size_t> fixedPartitionFast(
        poly::span<const uint32_t> cumHist,
        size_t numPartitions)
{
    auto idx2bucket = [](size_t idx) { return idx >> kPrecisionLoss; };
    auto bucket2idx = [](size_t bucket) { return bucket << kPrecisionLoss; };

    const size_t histSize = cumHist.size() - 1;
    const size_t cutoff   = std::min(histSize, idx2bucket(kCutoff));
    auto totalWeight      = cumHist.back() - cumHist[cutoff];
    auto numSuffixPartitions =
            totalWeight == 0 ? 0 : divUp<size_t>(cumHist.back(), totalWeight);
    numSuffixPartitions = totalWeight == 0
            ? 0
            : std::max(
                      divUp(histSize, idx2bucket(kMaxBucketSize)),
                      numSuffixPartitions);
    numSuffixPartitions = std::min(numPartitions, numSuffixPartitions);

    // fprintf(stderr,
    //         "num suffix partitions: %zu of %zu\n",
    //         numSuffixPartitions,
    //         numPartitions);
    assert(numSuffixPartitions <= numPartitions);

    const auto prefixCumHist       = cumHist.subspan(0, cutoff);
    const auto numPrefixPartitions = numPartitions - numSuffixPartitions;
    std::vector<size_t> prefixPartitions;
    if (numPrefixPartitions > 0) {
        prefixPartitions = utils::partition(
                prefixCumHist,
                numPrefixPartitions,
                idx2bucket(kMaxBucketSize),
                [&, fixed = ZL_highbit32(numPartitions)](auto bucket) {
                    assert(!bucket.empty());
                    assert(bucket.size() <= kMaxBucketSize);
                    const uint32_t bucketCount =
                            *bucket.end() - *bucket.begin();
                    auto cost = float(
                            bucketCount
                            * (fixed + ZL_nextPow2(bucket2idx(bucket.size()))));
                    return cost;
                },
                [&](size_t idx) -> size_t { return bucket2idx(idx); });
    }
    // fprintf(stderr, "prefix partitions: %zu\n", prefixPartitions.size());
    // for (size_t i = 0; i < prefixPartitions.size(); ++i) {
    //     fprintf(stderr, "%zu, ", prefixPartitions[i]);
    // }
    // fprintf(stderr, "\n");
    GreedyOptimizer opt(cumHist, prefixPartitions, numPartitions);
    auto complete = opt.run();
    // auto complete = completePartitions(
    //         cumHist, std::move(prefixPartitions), numPartitions);
    // fprintf(stderr, "complete partitions: %zu\n", complete.size());
    // for (size_t i = 0; i < complete.size(); ++i) {
    //     fprintf(stderr, "%zu, ", complete[i]);
    // }
    // fprintf(stderr, "\n");
    return complete;
}

std::tuple<std::vector<uint16_t>, size_t, size_t> fixedPartitionFast(
        poly::span<const uint16_t> data)
{
    auto idx2bucket = [](size_t idx) { return idx >> kPrecisionLoss; };
    auto bucket2idx = [](size_t idx) { return idx << kPrecisionLoss; };

    std::vector<uint32_t> hist(1u << (16 - kPrecisionLoss), uint32_t(0));
    for (auto val : data) {
        ++hist[idx2bucket(val)];
    }
    size_t histSize = hist.size();
    while (histSize > 0 && hist[histSize - 1] == 0) {
        --histSize;
    }

    std::vector<uint32_t> cumHist;
    cumHist.reserve(histSize + 1);
    uint32_t cum = 0;
    for (size_t i = 0; i < histSize; ++i) {
        auto count = hist[i];
        cumHist.push_back(cum);
        cum += count;
    }
    cumHist.push_back(cum);

    std::vector<size_t> best;
    uint32_t bestCost = std::numeric_limits<uint32_t>::max();
    for (const auto numBuckets : { 16, 32 }) {
        auto partitions = fixedPartitionFast(cumHist, numBuckets);
        auto cost       = fixedBucketCostFast(cumHist, partitions);
        if (cost < 0.99 * bestCost) {
            bestCost = cost;
            best     = std::move(partitions);
        } else {
            break;
        }
    }
    std::vector<uint16_t> partitions;
    partitions.reserve(best.size());
    for (auto p : best) {
        partitions.push_back(bucket2idx(p));
        // fprintf(stderr, "%u\n", unsigned(partitions.back()));
    }
    // fprintf(stderr, "\n");

    return std::make_tuple(
            std::move(partitions), bestCost, bucket2idx(histSize) - 1);
}

class OptimalFixedPartition {
   public:
    OptimalFixedPartition(
            poly::span<const uint32_t> histogram,
            size_t numPartitions)
            : hist_(histogram), numPartitions_(numPartitions)
    {
        auto totalCount =
                std::accumulate(histogram.begin(), histogram.end(), 0);
        targetCount_ = totalCount / numPartitions;
    }

    std::vector<uint16_t> run()
    {
        assert(partitions_.empty());
        size_t offset = 0;
        // TODO: Enable offset > 0
        // while (offset < hist_.size() && hist_[offset] == 0) {
        //     ++offset;
        // }
        partitions_.push_back(offset);
        go();
        partitions_.pop_back();
        assert(partitions_.empty());
        assert(bestPartitions_.size() <= numPartitions_);
        return bestPartitions_;
    }

   private:
    void go()
    {
        if (hist_.size() - size_t(partitions_.back()) <= kMaxBucketSize) {
            auto c = cost(partitions_);
            if (c < bestCost_) {
                bestCost_       = c;
                bestPartitions_ = partitions_;
            }
        }
        if (partitions_.size() < numPartitions_) {
            auto smallPartition = getSmallPartition();
            if (smallPartition < hist_.size()) {
                const auto begin = partitions_.back();
                partitions_.push_back(smallPartition);
                if (smallPartition - begin <= kMaxBucketSize) {
                    go();
                }
                auto largePartition = getLargePartition();
                if (largePartition < hist_.size()
                    && largePartition - begin <= kMaxBucketSize) {
                    partitions_.back() = largePartition;
                    go();
                }
                partitions_.pop_back();
            }
        }
    }

    size_t getSmallPartition() const
    {
        uint32_t count = 0;
        size_t end;
        for (end = partitions_.back() + 1; end < hist_.size(); ++end) {
            count += hist_[end];
            if (count > targetCount_) {
                break;
            }
        }
        size_t size = end - partitions_.back();
        while (!ZL_isPow2(size)) {
            --size;
        }
        return partitions_.back() + size;
    }

    size_t getLargePartition() const
    {
        uint32_t count = 0;
        size_t end;
        for (end = partitions_.back() + 1; end < hist_.size(); ++end) {
            count += hist_[end];
            if (count > targetCount_) {
                ++end;
                break;
            }
        }
        size_t size = end - partitions_.back();
        while (!ZL_isPow2(size)) {
            ++size;
        }
        return partitions_.back() + size;
    }

    uint32_t cost(poly::span<const uint16_t> partitions) const
    {
        auto cost = fixedBucketCost(hist_, partitions);
        return cost;
    }

    uint32_t bestCost_{ std::numeric_limits<uint32_t>::max() };
    std::vector<uint16_t> bestPartitions_ = { 0 };
    std::vector<uint16_t> partitions_{};
    poly::span<const uint32_t> hist_;
    size_t numPartitions_;
    uint32_t targetCount_;
};

void improvePartitions(
        poly::span<const uint32_t> histogram,
        std::vector<uint16_t>& partitions,
        std::vector<uint32_t>& bucketWeights)
{
    partitions.push_back(histogram.size());
    auto bucketCost = [fixedCost = ZL_nextPow2(partitions.size())](
                              size_t bucketSize, uint32_t bucketWeight) {
        auto variableCost = ZL_nextPow2(bucketSize);
        return bucketWeight * (fixedCost + variableCost);
    };

    auto tryBoundry = [&](size_t currentCost,
                          auto prevBucketSize,
                          auto prevBucketWeight,
                          auto currBucketSize,
                          auto currBucketWeight,
                          auto idx,
                          auto newIdx) {
        // When moving right, the element at idx transfers from curr to prev.
        // When moving left, the element at newIdx transfers from prev to curr.
        auto weight = newIdx > idx ? histogram[idx] : histogram[newIdx];
        if (newIdx == idx + 1) {
            ++prevBucketSize;
            prevBucketWeight += weight;
            --currBucketSize;
            currBucketWeight -= weight;
        } else {
            assert(newIdx == idx - 1);
            --prevBucketSize;
            prevBucketWeight -= weight;
            ++currBucketSize;
            currBucketWeight += weight;
        }
        return bucketCost(prevBucketSize, prevBucketWeight)
                + bucketCost(currBucketSize, currBucketWeight);
    };

    for (size_t boundry = 1; boundry < partitions.size() - 1; ++boundry) {
        auto prevBucketSize   = partitions[boundry] - partitions[boundry - 1];
        auto prevBucketWeight = bucketWeights[boundry - 1];

        auto prevBucketCost =
                bucketCost(prevBucketSize, bucketWeights[boundry - 1]);

        auto currBucketSize   = partitions[boundry + 1] - partitions[boundry];
        auto currBucketWeight = bucketWeights[boundry];
        auto currBucketCost   = bucketCost(currBucketSize, currBucketWeight);

        auto bucketCost = prevBucketCost + currBucketCost;

        while (currBucketSize < kMaxBucketSize
               && partitions[boundry] > partitions[boundry - 1] + 1) {
            auto idx           = partitions[boundry];
            auto newBucketCost = tryBoundry(
                    bucketCost,
                    prevBucketSize,
                    prevBucketWeight,
                    currBucketSize,
                    currBucketWeight,
                    idx,
                    idx - 1);
            if (newBucketCost < bucketCost) {
                --prevBucketSize;
                prevBucketWeight -= histogram[idx - 1];
                ++currBucketSize;
                currBucketWeight += histogram[idx - 1];
                --partitions[boundry];
                bucketCost = newBucketCost;
            } else {
                break;
            }
        }
        while (prevBucketSize < kMaxBucketSize
               && partitions[boundry] + 1 < partitions[boundry + 1]) {
            auto idx           = partitions[boundry];
            auto newBucketCost = tryBoundry(
                    bucketCost,
                    prevBucketSize,
                    prevBucketWeight,
                    currBucketSize,
                    currBucketWeight,
                    idx,
                    idx + 1);
            if (newBucketCost < bucketCost) {
                ++prevBucketSize;
                prevBucketWeight += histogram[idx];
                --currBucketSize;
                currBucketWeight -= histogram[idx];
                ++partitions[boundry];
                bucketCost = newBucketCost;
            } else {
                break;
            }
        }
    }
    partitions.pop_back();
}

std::vector<uint16_t> greedyPartition(
        poly::span<const uint32_t> histogram,
        size_t totalCount,
        size_t numBuckets)
{
    std::vector<uint16_t> partitions;
    partitions.reserve(numBuckets);
    std::vector<uint32_t> bucketWeights;
    bucketWeights.reserve(numBuckets);
    // fprintf(stderr, "begin\n");
    {
        auto targetWeight =
                std::max<size_t>((totalCount + numBuckets - 1) / numBuckets, 1);
        size_t remainingWeight = totalCount;
        size_t currentWeight   = 0;
        size_t partitionEnd    = histogram.size();
        for (size_t idx = histogram.size(); idx-- > 0;) {
            currentWeight += histogram[idx];
            remainingWeight -= histogram[idx];

            const bool shouldPartition = currentWeight >= targetWeight;
            const bool mustPartition   = ((partitionEnd - idx) >= (1u << 14))
                    || (partitions.size() + idx + 1 == numBuckets);

            if (shouldPartition || mustPartition) {
                partitionEnd = idx;
                partitions.push_back(idx);
                bucketWeights.push_back(currentWeight);
                currentWeight = 0;

                size_t remainingBuckets = numBuckets - partitions.size();
                if (remainingBuckets == 0) {
                    break;
                }
                targetWeight = std::max<size_t>(
                        (remainingWeight + remainingBuckets - 1)
                                / remainingBuckets,
                        1);
            }
        }
        assert(remainingWeight == 0);
        if (partitions.back() != 0) {
            partitions.back() = 0;
            bucketWeights.back() += currentWeight;
        }
        (void)remainingWeight;
        if (partitions.size() > numBuckets) {
            throw std::runtime_error("impossible");
        }
        if (partitions.size() < numBuckets) {
            throw std::runtime_error("fix my code");
        }
        std::reverse(partitions.begin(), partitions.end());
    }
    // fprintf(stderr,
    //         "initial, %zu vs %zu: %zu\n",
    //         numBuckets,
    //         partitions.size(),
    //         histogram.size());
    // for (size_t i = 0; i < partitions.size(); ++i) {
    //     fprintf(stderr, "%d, ", (int)partitions[i]);
    // }
    // fprintf(stderr, "\n");
    // auto initialCost = fixedBucketCost(histogram, partitions);
    improvePartitions(histogram, partitions, bucketWeights);
    // auto improvedCost = fixedBucketCost(histogram, partitions);
    // fprintf(stderr, "%zu -> %zu\n", initialCost, improvedCost);

    // fprintf(stderr,
    //         "finished, %zu vs %zu: %zu\n",
    //         numBuckets,
    //         partitions.size(),
    //         histogram.size());
    // for (size_t i = 0; i < partitions.size(); ++i) {
    //     fprintf(stderr, "%d, ", (int)partitions[i]);
    // }
    // fprintf(stderr, "\n");

    return partitions;
}

std::vector<uint16_t> greedyPartition(
        poly::span<const uint32_t> histogram,
        size_t totalCount)
{
    std::vector<uint16_t> best;
    uint32_t bestCost = std::numeric_limits<uint32_t>::max();
    for (const auto numBuckets : { 4, 8, 16, 32, 64 }) {
        auto partitions = greedyPartition(histogram, totalCount, numBuckets);
        auto cost       = fixedBucketCost(histogram, partitions);
        if (cost < 0.99 * bestCost) {
            bestCost = cost;
            best     = std::move(partitions);
        }
    }
    return best;
}
} // namespace

void Bucket16Graph::graph(GraphState& state) const
{
    auto& inputEdge         = state.edges()[0];
    const auto& inputStream = inputEdge.getInput();
    const size_t numElts    = inputStream.numElts();

    if (numElts < 10) {
        inputEdge.setDestination(graphs::Store{}());
        return;
    }

    // ZL_Histogram16 histogram;
    // ZL_Histogram_init(&histogram.base, 65535);
    // ZL_Histogram_build(&histogram.base, inputStream.ptr(), numElts, 2);

    // poly::span<const uint32_t> hist{ histogram.base.count,
    //                                  histogram.base.maxSymbol + 1 };
    // partitions = fixedPartition(hist);
    // TODO: Allow for 1, 2, 4, 8, 32, 64, 128, 256 #buckets
    // const auto partitions = OptimalFixedPartition{ hist, 16 }.run();
    const auto [partitions, bitCost, maxSymbolValue] =
            fixedPartitionFast({ (const uint16_t*)inputStream.ptr(), numElts });
    // const auto partitions = fixedPartition(hist);

    if (partitions.size() == 1) {
        inputEdge.setDestination(graphs::Bitpack{}());
        return;
    }
    bool allOne = true;
    for (size_t i = 0; i < partitions.size(); ++i) {
        // TODO: Fix the decoder to handle this case...
        auto begin = partitions[i];
        auto end   = i + 1 == partitions.size() ? maxSymbolValue + 1
                                                : partitions[i + 1];
        if (begin + 1 != end) {
            allOne = false;
            break;
        }
    }
    if (allOne) {
        inputEdge.setDestination(graphs::Bitpack{}());
        return;
    }
    // const auto partitions = greedyPartition(hist, numElts);
    // const auto bitCost = fixedBucketCost(hist, partitions);
#if 0
    if (1) {
        std::vector<size_t> partitionSizes;
        bool print = false;
        for (size_t i = 0; i < partitions.size(); ++i) {
            auto begin = partitions[i];
            auto end   = i == partitions.size() - 1 ? hist.size()
                                                    : partitions[i + 1];
            auto size  = end - begin;
            if (i != 0 && i != partitions.size() - 1
                && size < partitionSizes.back()) {
                // print = true;
            }
            if (i != partitions.size() - 1 && !ZL_isPow2(size)) {
                // print = true;
            }
            partitionSizes.push_back(end - begin);
        }
        if (print) {
            for (size_t i = 0; i < partitions.size(); ++i) {
                fprintf(stderr, "%d, ", (int)partitionSizes[i]);
            }
            fprintf(stderr, "\n");
        }
    }
    if (0)
        for (size_t i = 0; i < partitions.size(); ++i) {
            const size_t size =
                    (i + 1 == partitions.size() ? 65536 : partitions[i + 1])
                    - partitions[i];
            fprintf(stderr,
                    "p[%zu] = %d | size = %zu\n",
                    i,
                    int(partitions[i]),
                    size);
        }
    if (0)
        fprintf(stderr,
                "numElts = %zu, #partitions = %u, cost = %u\n",
                numElts,
                (unsigned)partitions.size(),
                bitCost / 8);
#endif

    if (bitCost / 8 >= 2 * 99 * numElts / 100) {
        // Not enough gain => skip
        if (0)
            fprintf(stderr, "skipping...\n");
        inputEdge.setDestination(graphs::Store{}());
        return;
    }

    const auto nodes = state.customNodes();
    if (nodes.size() != 1) {
        throw std::runtime_error("CustomNodes must have exactly one node");
    }

    auto outEdges = Bucket16Encoder::runNode(
            inputEdge, nodes[0], partitions, maxSymbolValue);
    assert(outEdges.size() == 2);

    if (0)
        fprintf(stderr,
                "actual = %zu: %zu, %zu\n",
                outEdges[0].getInput().numElts()
                        + outEdges[1].getInput().numElts(),
                outEdges[0].getInput().numElts(),
                outEdges[1].getInput().numElts());

    outEdges[0].setDestination(graphs::Store{}());
    outEdges[1].setDestination(graphs::Store{}());
}

} // namespace openzl::lz
