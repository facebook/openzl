// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "Bucket16.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>

#include "CodecIDs.hpp"

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/data_stats.h"
#include "openzl/shared/histogram.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/utils.h"
#include "utils/Partition.hpp"

namespace openzl::lz {
namespace {
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
    Bucket16EncoderState(poly::span<const uint8_t> param)
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

        size_t nextBaseIdx = 0;
        size_t bits        = 0;
        maxVarBits_        = 0;
        for (size_t byte = 0; byte < 65536; ++byte) {
            const auto end =
                    nextBaseIdx == bases_.size() ? 65536 : bases_[nextBaseIdx];
            if (byte >= end) {
                bits        = ZL_nextPow2(getBucketSize(nextBaseIdx));
                maxVarBits_ = std::max(maxVarBits_, bits);
                ++nextBaseIdx;
            }
            valueToVarBits_[byte]  = bits;
            valueToBucket16_[byte] = nextBaseIdx - 1;
            valueToBase_[byte]     = bases_[nextBaseIdx - 1];
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
        return (maxVarBits() * numElts + 7) / 8;
    }

    size_t
    encode(poly::span<const uint16_t> input, uint8_t* fixed, uint8_t* var) const
    {
        ZS_BitCStreamFF fixedBitstream =
                ZS_BitCStreamFF_init(fixed, fixedStreamSize(input.size()));
        ZS_BitCStreamFF varBitstream =
                ZS_BitCStreamFF_init(var, varStreamBound(input.size()));

        for (const auto x : input) {
            const auto varBits = valueToVarBits_[x];
            if (x - valueToBase_[x] >= (1u << varBits)) {
                throw std::runtime_error("Invalid bases for input");
            }
            assert(x >= valueToBase_[x]);
            assert(x - valueToBase_[x] < (1u << varBits));
            ZS_BitCStreamFF_write(
                    &fixedBitstream, valueToBucket16_[x], fixedBits_);
            ZS_BitCStreamFF_flush(&fixedBitstream);
            ZS_BitCStreamFF_write(&varBitstream, x - valueToBase_[x], varBits);
            ZS_BitCStreamFF_flush(&varBitstream);
        }

        unwrap(ZS_BitCStreamFF_finish(&fixedBitstream));
        return unwrap(ZS_BitCStreamFF_finish(&varBitstream));
    }

   private:
    size_t getBucketSize(size_t baseIdx) const
    {
        if (baseIdx >= bases_.size()) {
            return 1;
        }
        size_t base = bases_[baseIdx];
        assert(base < 65536);
        auto end = baseIdx + 1 == bases_.size() ? size_t(65536)
                                                : size_t(bases_[baseIdx + 1]);
        assert(end > base);
        return end - base;
    }

    poly::span<const uint16_t> bases_;
    size_t fixedBits_;
    // TODO(terrelln): Fix this memory usage
    // Idea: Ensure bases transition when low e.g. 4 bits == 0
    // Then the LUT can be 2^12 entries. Could also do low 5, 6, 7, 8 bits, etc.
    std::array<uint16_t, 65536> valueToBucket16_;
    std::array<uint16_t, 65536> valueToBase_;
    std::array<uint16_t, 65536> valueToVarBits_;
    size_t maxVarBits_;
};

class Bucket16DecoderState {
   public:
    Bucket16DecoderState(poly::span<const uint8_t> header)
    {
        if (header.empty()) {
            throw std::runtime_error("header is empty");
        }
        unusedFixedBits_ = header[0] % 8;
        header           = header.subspan(1);
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
                const uint64_t offset = _pdep_u64(vData, mask);

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
                    const uint64_t offset = _pdep_u64(vData, mask);

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
                    const uint64_t offset = _pdep_u64(vData, mask);

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
        assert(base < 65536);
        auto end = baseIdx + 1 == numBases_ ? size_t(65536)
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
    Bucket16EncoderState state(*bases);

    const auto& input    = encoder.inputs()[0];
    const size_t numElts = input.numElts();

    if (input.eltWidth() != 2) {
        throw std::runtime_error("Unsupported width != 2");
    }

    uint8_t header[513];
    // first byte is the # of unused bits in the bitstream
    header[0] = (8 - ((state.fixedBits() * numElts) % 8)) % 8;
    // TODO: Handle big endian
    memcpy(header + 1, state.bases().data(), state.bases().size() * 2);

    encoder.sendCodecHeader(header, 1 + state.bases().size() * 2);

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
        poly::span<const uint16_t> bases)
{
    NodeParameters params;
    params.localParams.emplace();
    params.localParams->addRefParam(0, bases.data(), bases.size_bytes());
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

    std::array<uint32_t, kNumBuckets> bucketedHistogram{};

    auto bits = [](size_t val) { return ZL_highbit32(val | 1) >> 1; };

    for (size_t i = 0; i < histogram.size(); ++i) {
        auto b      = bits(i);
        auto base   = bitsToBase(b);
        auto bucket = bitsOffset[b] + ((i - base) >> b);
        assert(bucket < kNumBuckets);
        bucketedHistogram[bucket] += histogram[i];
    }

    auto bucketedPartitions = utils::partition(
            poly::span<const uint32_t>{ bucketedHistogram },
            numBuckets,
            [&, fixed = ZL_highbit32(numBuckets)](auto bucket) {
                auto beginIdx = bucket.data() - bucketedHistogram.data();
                auto endIdx   = beginIdx + bucket.size();
                auto begin    = bucketToBase(beginIdx);
                auto end      = bucketToBase(endIdx);
                assert(begin < end);
                if (end - begin > kMaxBucketSize) {
                    // Reject buckets that are too big
                    return std::numeric_limits<float>::infinity();
                }
                const uint32_t bucketCount =
                        std::accumulate(bucket.begin(), bucket.end(), 0);
                return float(bucketCount * (fixed + ZL_nextPow2(end - begin)));
            });
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
    for (const auto numBuckets : { 4, 8, 16, 32, 64 }) {
        auto partitions = fixedPartition(histogram, numBuckets);
        auto cost       = fixedBucketCost(histogram, partitions);
        if (cost < 0.99 * bestCost) {
            bestCost = cost;
            best     = std::move(partitions);
        }
    }
    return best;
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

    ZL_Histogram16 histogram;
    ZL_Histogram_init(&histogram.base, 65535);
    ZL_Histogram_build(&histogram.base, inputStream.ptr(), numElts, 2);

    poly::span<const uint32_t> hist{ histogram.base.count, 65536 };
    // partitions = fixedPartition(hist);
    // TODO: Allow for 1, 2, 4, 8, 32, 64, 128, 256 #buckets
    // const auto partitions = OptimalFixedPartition{ hist, 16 }.run();
    const auto partitions = fixedPartition(hist);
    const auto bitCost    = fixedBucketCost(hist, partitions);
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

    if (bitCost / 8 >= 2 * 95 * numElts / 100) {
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

    auto outEdges = Bucket16Encoder::runNode(inputEdge, nodes[0], partitions);
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
