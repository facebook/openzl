// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "Bucket.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>

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

SimpleCodecDescription bucketCodecDescription()
{
    return SimpleCodecDescription{
        .id          = unsigned(CustomCodecIDs::Bucket),
        .name        = "!lz_research.bucket",
        .inputType   = Type::Serial,
        .outputTypes = { Type::Serial, Type::Serial },
    };
}

class BucketEncoderState {
   public:
    BucketEncoderState(poly::span<const uint8_t> bases, int maxSymbolValue)
            : bases_(bases), numSymbols_(maxSymbolValue + 1)
    {
        if (bases.empty()) {
            throw std::runtime_error("bases is empty");
        }
        for (size_t i = 1; i < bases.size(); ++i) {
            if (bases[i - 1] >= bases[i]) {
                throw std::runtime_error("bases not strictly increasing");
            }
        }

        fixedBits_ = ZL_nextPow2(bases_.size());

        size_t nextBaseIdx = 0;
        size_t bits        = 0;
        maxVarBits_        = 0;
        for (size_t byte = 0; byte < 256; ++byte) {
            const auto end =
                    nextBaseIdx == bases_.size() ? 256 : bases_[nextBaseIdx];
            if (byte >= end) {
                bits        = ZL_nextPow2(getBucketSize(nextBaseIdx));
                maxVarBits_ = std::max(maxVarBits_, bits);
                ++nextBaseIdx;
            }
            byteToVarBits_[byte] = bits;
            byteToBucket_[byte]  = nextBaseIdx - 1;
            byteToBase_[byte]    = bases_[nextBaseIdx - 1];
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

    poly::span<const uint8_t> bases() const
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

    size_t encode(poly::span<const uint8_t> input, uint8_t* fixed, uint8_t* var)
            const
    {
        ZS_BitCStreamFF fixedBitstream =
                ZS_BitCStreamFF_init(fixed, fixedStreamSize(input.size()));
        ZS_BitCStreamFF varBitstream =
                ZS_BitCStreamFF_init(var, varStreamBound(input.size()));

        for (const auto x : input) {
            const auto varBits = byteToVarBits_[x];
            if (x - byteToBase_[x] >= (1u << varBits)) {
                throw std::runtime_error("Invalid bases for input");
            }
            assert(x >= byteToBase_[x]);
            assert(x - byteToBase_[x] < (1u << varBits));
            ZS_BitCStreamFF_write(
                    &fixedBitstream, byteToBucket_[x], fixedBits_);
            ZS_BitCStreamFF_flush(&fixedBitstream);
            ZS_BitCStreamFF_write(&varBitstream, x - byteToBase_[x], varBits);
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
        assert(base < numSymbols_);
        auto end = baseIdx + 1 == bases_.size() ? numSymbols_
                                                : size_t(bases_[baseIdx + 1]);
        assert(end > base);
        return end - base;
    }

    poly::span<const uint8_t> bases_;
    size_t numSymbols_;
    size_t fixedBits_;
    std::array<uint8_t, 256> byteToBucket_;
    std::array<uint8_t, 256> byteToBase_;
    std::array<uint8_t, 256> byteToVarBits_;
    size_t maxVarBits_;
};

class BucketDecoderState {
   public:
    BucketDecoderState(poly::span<const uint8_t> header)
    {
        if (header.size() < 2) {
            throw std::runtime_error("header is empty");
        }
        unusedFixedBits_      = header[0] % 8;
        size_t maxSymbolValue = header[1];
        numSymbols_           = maxSymbolValue + 1;
        const auto bases      = header.subspan(2);
        if (bases.empty()) {
            throw std::runtime_error("bases is empty");
        }
        if (bases.size() > bases_.size()) {
            throw std::runtime_error("bases is > 256");
        }
        if (bases.back() > maxSymbolValue) {
            throw std::runtime_error("bases[-1] is > maxSymbolValue");
        }
        for (size_t i = 1; i < bases.size(); ++i) {
            if (bases[i - 1] >= bases[i]) {
                throw std::runtime_error("bases not strictly increasing");
            }
        }
        memcpy(bases_.data(), bases.data(), bases.size());
        numBases_ = bases.size();

        maxVarBits_ = 0;
        for (size_t i = 0; i < bases.size(); ++i) {
            varBits_[i] = ZL_nextPow2(getBucketSize(i));
            maxVarBits_ = std::max<size_t>(maxVarBits_, varBits_[i]);
            assert(varBits_[i] <= 8);
        }

        fixedBits_ = ZL_nextPow2(bases.size());
        assert(fixedBits_ <= 8);
    }

    size_t fixedBits() const
    {
        return fixedBits_;
    }

    poly::span<const uint8_t> bases() const
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

    static std::array<uint16_t, 256> expandLUT(poly::span<const uint8_t> lut)
    {
        std::array<uint16_t, 256> expanded;
        for (size_t byte = 0; byte < 256; ++byte) {
            const size_t lo = byte & 0xF;
            const size_t hi = byte >> 4;
            expanded[byte]  = uint16_t(lut[lo]) | (uint16_t(lut[hi]) << 8);
        }
        return expanded;
    }

    std::array<uint8_t, 16> makeBaseLUT() const
    {
        std::array<uint8_t, 16> lut{};
        memcpy(lut.data(), bases_.data(), numBases_);
        return lut;
    }

    std::array<uint8_t, 16> makeMaskLUT() const
    {
        std::array<uint8_t, 16> lut;
        for (size_t i = 0; i < 16; ++i) {
            if (i < numBases_) {
                lut[i] = (1u << varBits_[i]) - 1;
                if (0)
                    fprintf(stderr,
                            "npartitions = %zu, varbits[%zu] = %u, base = %u\n",
                            numBases_,
                            i,
                            (unsigned)varBits_[i],
                            (unsigned)bases_[i]);
                assert(varBits_[i] <= 7);
            } else {
                lut[i] = 0;
            }
        }
        return lut;
    }

    void decode4(
            poly::span<const uint8_t> fixed,
            poly::span<const uint8_t> var,
            uint8_t* out) const
    {
        const auto baseLUT = expandLUT(makeBaseLUT());
        const auto maskLUT = expandLUT(makeMaskLUT());
        const auto size    = numElts(fixed.size());

        uint8_t* o                = out;
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
                const uint64_t base = uint64_t(baseLUT[f[0]])
                        | (uint64_t(baseLUT[f[1]]) << 16)
                        | (uint64_t(baseLUT[f[2]]) << 32)
                        | (uint64_t(baseLUT[f[3]]) << 48);
                const uint64_t mask = uint64_t(maskLUT[f[0]])
                        | (uint64_t(maskLUT[f[1]]) << 16)
                        | (uint64_t(maskLUT[f[2]]) << 32)
                        | (uint64_t(maskLUT[f[3]]) << 48);
                f += 4;

                const uint64_t vData  = ZL_readLE64(v) >> bitsConsumed;
                const uint64_t offset = utils::bitDeposit(vData, mask);

                ZL_writeLE64(o, base + offset);
                o += 8;

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

    void decode(
            poly::span<const uint8_t> fixed,
            poly::span<const uint8_t> var,
            uint8_t* output) const
    {
        if (fixedBits_ == 4) {
            decode4(fixed, var, output);
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

    std::array<uint8_t, 256> bases_{};
    std::array<uint8_t, 256> varBits_{};
    size_t unusedFixedBits_;
    size_t fixedBits_;
    size_t numBases_;
    size_t maxVarBits_;
    size_t numSymbols_;
};
} // namespace

SimpleCodecDescription BucketEncoder::simpleCodecDescription() const
{
    return bucketCodecDescription();
}

void BucketEncoder::encode(EncoderState& encoder) const
{
    auto bases = encoder.getLocalParam(0);
    if (!bases.has_value()) {
        throw std::runtime_error("bases not present");
    }
    auto maxSymbolValue = encoder.getLocalIntParam(1);
    if (!maxSymbolValue.has_value()) {
        throw std::runtime_error("maxSymbolValue not present");
    }
    BucketEncoderState state(*bases, *maxSymbolValue);

    const auto& input    = encoder.inputs()[0];
    const size_t numElts = input.numElts();

    uint8_t header[258];
    // first byte is the # of unused bits in the bitstream
    header[0] = (8 - ((state.fixedBits() * numElts) % 8)) % 8;
    header[1] = (uint8_t)*maxSymbolValue;
    memcpy(header + 2, state.bases().data(), state.bases().size());

    encoder.sendCodecHeader(header, 2 + state.bases().size());

    auto fixedStream =
            encoder.createOutput(0, state.fixedStreamSize(numElts), 1);
    auto varStream = encoder.createOutput(1, state.varStreamBound(numElts), 1);

    auto varStreamSize = state.encode(
            { (const uint8_t*)input.ptr(), input.numElts() },
            (uint8_t*)fixedStream.ptr(),
            (uint8_t*)varStream.ptr());

    fixedStream.commit(state.fixedStreamSize(numElts));
    varStream.commit(varStreamSize);
}
/* static */ Edge::RunNodeResult BucketEncoder::runNode(
        Edge& edge,
        NodeID node,
        poly::span<const uint8_t> bases,
        uint8_t maxSymbolValue)
{
    NodeParameters params;
    params.localParams.emplace();
    params.localParams->addRefParam(0, bases.data(), bases.size());
    params.localParams->addIntParam(1, maxSymbolValue);
    auto r = edge.runNode(node, std::move(params));
    return r;
}

SimpleCodecDescription BucketDecoder::simpleCodecDescription() const
{
    return bucketCodecDescription();
}

void BucketDecoder::decode(DecoderState& decoder) const
{
    BucketDecoderState state(decoder.getCodecHeader());

    const auto& fixedStream = decoder.singletonInputs()[0];
    const auto& varStream   = decoder.singletonInputs()[1];
    const auto fixedSize    = fixedStream.numElts();

    auto outStream = decoder.createOutput(0, state.numElts(fixedSize), 1);

    state.decode(
            { (const uint8_t*)fixedStream.ptr(), fixedSize },
            { (const uint8_t*)varStream.ptr(), varStream.numElts() },
            (uint8_t*)outStream.ptr());

    outStream.commit(state.numElts(fixedSize));
}

/* static */ GraphID BucketGraph::create(
        Compressor& compressor,
        bool entropyCompressFixed)
{
    auto graph = compressor.getGraph("lz_research.bucket_graph");
    if (!graph.has_value()) {
        graph = compressor.registerFunctionGraph(
                std::make_shared<BucketGraph>());
    }

    auto node = compressor.getNode("lz_research.bucket");
    if (!node.has_value()) {
        node = compressor.registerCustomEncoder(
                std::make_shared<BucketEncoder>());
    }

    LocalParams params;
    params.addIntParam(0, !!entropyCompressFixed);

    return compressor.parameterizeGraph(
            *graph,
            GraphParameters{ .customNodes = { { *node } },
                             .localParams = std::move(params) });
}

FunctionGraphDescription BucketGraph::functionGraphDescription() const
{
    return FunctionGraphDescription{
        .name           = "!lz_research.bucket_graph",
        .inputTypeMasks = { TypeMask::Serial },
    };
}

namespace {
constexpr uint32_t kBucketOverheadBits = 40;

uint32_t entropyCompressedBucketCost(
        poly::span<const uint32_t> bucket,
        uint32_t totalCount)
{
    const uint32_t bucketCount =
            std::accumulate(bucket.begin(), bucket.end(), 0);
    if (bucketCount == 0) {
        return kBucketOverheadBits;
    }
    // TODO(terrelln): Replace log2 with an estimate
    const uint32_t bucketCost = double(bucketCount)
            * std::log2(double(totalCount) / double(bucketCount));
    const uint32_t offsetBits = ZL_nextPow2(bucket.size());
    const uint32_t offsetCost = bucketCount * offsetBits;

    if (0)
        fprintf(stderr,
                "totalCount = %u, bucketCount = %u, bucketSize = %zu, bucketCost = %u, offsetCost = %u\n",
                totalCount,
                bucketCount,
                bucket.size(),
                bucketCost,
                offsetCost);

    return kBucketOverheadBits + bucketCost + offsetCost;
}

uint32_t entropyCompressedBucketCost(
        poly::span<const uint32_t> histogram,
        poly::span<const uint8_t> partitions)
{
    const uint32_t totalCount =
            std::accumulate(histogram.begin(), histogram.end(), 0);
    uint32_t cost = 0;
    for (size_t i = 0; i < partitions.size(); ++i) {
        const auto b = partitions[i];
        const auto e = i + 1 == partitions.size() ? histogram.size()
                                                  : partitions[i + 1];
        assert(b < e);
        cost += entropyCompressedBucketCost(
                histogram.subspan(b, e - b), totalCount);
    }
    return cost;
}

// uint32_t fixedBucketCost(
//         poly::span<const uint32_t> bucket,
//         uint32_t totalCount,
//         uint32_t bucketBits)
// {
//     const uint32_t bucketCount =
//             std::accumulate(bucket.begin(), bucket.end(), 0);
//     if (bucketCount == 0) {
//         return 0;
//     }
//     const uint32_t entropyCost =
//             bucketCount * std::log2(double(totalCount) /
//             double(bucketCount));
//     // ZL_calculateEntropy(&bucketCount, 0, totalCount);
//     const uint32_t fixedCost = bucketCount * bucketBits;
//     const auto cost          = fixedCost - entropyCost;
//     return cost >= 0 ? cost : -cost;
// }
float fixedBucketCost(poly::span<const uint32_t> bucket, uint32_t fixedCost)
{
    if (bucket.size() > 128) {
        // Reject buckets that are too large
        return std::numeric_limits<float>::infinity();
    }
    const float bucketCount = std::accumulate(bucket.begin(), bucket.end(), 0);
    return bucketCount * (fixedCost + ZL_nextPow2(bucket.size()));
}

uint32_t fixedBucketCost(
        poly::span<const uint32_t> histogram,
        poly::span<const uint8_t> partitions)
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
        cost += kBucketOverheadBits + bucketCount * offsetBits;
    }
    return cost;
}

std::vector<uint8_t> toUint8(const std::vector<size_t>& v)
{
    std::vector<uint8_t> result;
    result.reserve(v.size());
    for (auto i : v) {
        result.push_back(i);
    }
    return result;
}

std::vector<uint8_t> entropyPartition(poly::span<const uint32_t> histogram)
{
    const uint32_t totalCount =
            std::accumulate(histogram.begin(), histogram.end(), 0);
    return toUint8(utils::partition(histogram, 16, [totalCount](auto bucket) {
        return entropyCompressedBucketCost(bucket, totalCount);
    }));
}

std::vector<uint8_t> fixedPartition(poly::span<const uint32_t> histogram)
{
    const uint32_t totalCount =
            std::accumulate(histogram.begin(), histogram.end(), 0);
    std::vector<uint8_t> bestPartitions = { 0 };
    uint32_t bestCost = fixedBucketCost(histogram, bestPartitions);
    for (size_t bucketBits = 1; bucketBits < 8; ++bucketBits) {
        auto partitions =
                toUint8(utils::partition(
                        histogram,
                        1u << bucketBits,
                        [totalCount, bucketBits](auto bucket) {
                            return fixedBucketCost(bucket, bucketBits);
                        }));
        auto cost = fixedBucketCost(histogram, partitions);
        if (cost < bestCost) {
            bestCost       = cost;
            bestPartitions = std::move(partitions);
        }
    }
    return bestPartitions;
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

    std::vector<uint8_t> run()
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
        if (partitions_.back() >= 128) {
            auto c = cost(partitions_);
            if (c < bestCost_) {
                bestCost_       = c;
                bestPartitions_ = partitions_;
            }
        }
        if (partitions_.size() < numPartitions_) {
            auto smallPartition = getSmallPartition();
            if (smallPartition < hist_.size()) {
                partitions_.push_back(smallPartition);
                if (smallPartition - partitions_.back() <= 128) {
                    go();
                }
                auto largePartition = getLargePartition();
                if (largePartition < hist_.size()
                    && largePartition - partitions_.back() <= 128) {
                    partitions_.back() = getLargePartition();
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

    uint32_t cost(poly::span<const uint8_t> partitions) const
    {
        auto cost = fixedBucketCost(hist_, partitions);
        return cost;
    }

    uint32_t bestCost_{ std::numeric_limits<uint32_t>::max() };
    std::vector<uint8_t> bestPartitions_ = { 0 };
    std::vector<uint8_t> partitions_{};
    poly::span<const uint32_t> hist_;
    size_t numPartitions_;
    uint32_t targetCount_;
};
} // namespace

void BucketGraph::graph(GraphState& state) const
{
    auto& inputEdge         = state.edges()[0];
    const auto& inputStream = inputEdge.getInput();
    const size_t numElts    = inputStream.numElts();

    if (numElts < 10) {
        inputEdge.setDestination(graphs::Store{}());
        return;
    }

    ZL_Histogram8 histogram;
    ZL_Histogram_init(&histogram.base, 255);
    ZL_Histogram_build(&histogram.base, inputStream.ptr(), numElts, 1);

    std::vector<uint8_t> partitions;
    uint32_t bitCost;

    poly::span<const uint32_t> hist{ histogram.base.count,
                                     histogram.base.maxSymbol + 1 };
    const bool entropyCompressFixed = !!state.getLocalIntParam(0).value_or(0);
    if (entropyCompressFixed) {
        partitions = entropyPartition(hist);
        bitCost    = entropyCompressedBucketCost(hist, partitions);
    } else {
        partitions = fixedPartition(hist);
        // TODO: Allow for 1, 2, 4 #buckets
        // partitions = OptimalFixedPartition{ hist, 16 }.run();
        bitCost = fixedBucketCost(hist, partitions);
        if (0)
            for (size_t i = 0; i < partitions.size(); ++i) {
                fprintf(stderr, "p[%zu] = %d\n", i, int(partitions[i]));
            }
    }
    if (0)
        fprintf(stderr,
                "numElts = %zu, usingEntropy = %u, #partitions = %u, cost = %u\n",
                numElts,
                (unsigned)entropyCompressFixed,
                (unsigned)partitions.size(),
                bitCost / 8);

    if (bitCost / 8 >= 95 * numElts / 100) {
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

    auto outEdges = BucketEncoder::runNode(
            inputEdge, nodes[0], partitions, histogram.base.maxSymbol);
    assert(outEdges.size() == 2);

    if (entropyCompressFixed) {
        outEdges[0].setDestination(graphs::Huffman{}());
    } else {
        outEdges[0].setDestination(graphs::Store{}());
    }
    outEdges[1].setDestination(graphs::Store{}());
}

} // namespace openzl::lz
