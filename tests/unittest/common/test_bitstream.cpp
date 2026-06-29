// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/codecs/common/bitstream/bf_bitstream.h"
#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/fse/bitstream.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_portability.h"
#include "tests/utils.h"

namespace openzl {
namespace tests {
namespace {

enum class COp {
    FLUSH,
    WRITE,
};

enum class DOp {
    READ,
    RELOAD,
};

enum class Mode {
    BENCHMARK,
    TEST,
};

enum class BitstreamImpl {
    ZS,
    ZS_BF,
    FSE,
};

class RoundTripTest {
   public:
    explicit RoundTripTest(BitstreamImpl impl = BitstreamImpl::ZS) : impl_(impl)
    {
    }

    void add(size_t value, size_t nbBits)
    {
        values_.push_back(value);
        nbBits_.push_back(nbBits);
        totalBits_ += nbBits;
        maxBits_ = std::max(nbBits, maxBits_);
    }

    void prepareForEncode()
    {
        encoded_.resize((totalBits_ + 7) / 8 + 16);
    }
    template <size_t kNbUnrolls>
    bool encodeImpl() noexcept
    {
        if (impl_ == BitstreamImpl::ZS)
            return zsEncodeImpl<kNbUnrolls>();
        else if (impl_ == BitstreamImpl::ZS_BF) {
            return zsBfEncodeImpl<kNbUnrolls>();
        } else
            return fseEncodeImpl<kNbUnrolls>();
    }

    template <size_t kNbUnrolls>
    bool fseEncodeImpl() noexcept
    {
        BIT_CStream_t bits;
        BIT_initCStream(&bits, encoded_.data(), encoded_.size());
        assert(kNbUnrolls * maxBits_ <= 8 * sizeof(size_t) - 8);
        size_t const iterations = values_.size() / kNbUnrolls;
        size_t const limit      = iterations * kNbUnrolls;
        assert(limit <= values_.size());
        assert(values_.size() == nbBits_.size());

        size_t i = values_.size();
        while (i > limit) {
            --i;
            BIT_addBits(&bits, values_[i], (unsigned)nbBits_[i]);
            BIT_flushBits(&bits);
        }

        for (; i > 0; i -= kNbUnrolls) {
            for (size_t u = 1; u <= kNbUnrolls; ++u) {
                BIT_addBits(&bits, values_[i - u], (unsigned)nbBits_[i - u]);
            }
            BIT_flushBits(&bits);
        }
        assert(i == 0);

        size_t const ret = BIT_closeCStream(&bits);
        if (ret == 0)
            return false;
        encodedSize_ = ret;
        return true;
    }

    template <size_t kNbUnrolls>
    bool zsEncodeImpl() noexcept
    {
        ZS_BitCStreamFF bits =
                ZS_BitCStreamFF_init(encoded_.data(), encoded_.size());
        assert(kNbUnrolls * maxBits_ <= 8 * sizeof(size_t) - 8);
        size_t const iterations = values_.size() / kNbUnrolls;
        size_t const limit      = iterations * kNbUnrolls;
        assert(limit <= values_.size());
        assert(values_.size() == nbBits_.size());

        size_t i;
        for (i = 0; i < limit; i += kNbUnrolls) {
            for (size_t u = 0; u < kNbUnrolls; ++u) {
                ZS_BitCStreamFF_write(&bits, values_[i + u], nbBits_[i + u]);
            }
            ZS_BitCStreamFF_flush(&bits);
        }

        for (; i < values_.size(); ++i) {
            ZS_BitCStreamFF_write(&bits, values_[i], nbBits_[i]);
            ZS_BitCStreamFF_flush(&bits);
        }

        ZL_Report report = ZS_BitCStreamFF_finish(&bits);
        if (ZL_isError(report))
            return false;
        assert(ZL_validResult(report) == (totalBits_ + 7) / 8);
        encodedSize_ = ZL_validResult(report);
        return true;
    }

    template <size_t kNbUnrolls>
    bool zsBfEncodeImpl() noexcept
    {
        ZS_BitCStreamBF bits =
                ZS_BitCStreamBF_init(encoded_.data(), encoded_.size());
        assert(kNbUnrolls * maxBits_ <= 8 * sizeof(size_t) - 8);
        size_t const iterations = values_.size() / kNbUnrolls;
        size_t const limit      = iterations * kNbUnrolls;
        assert(limit <= values_.size());
        assert(values_.size() == nbBits_.size());

        size_t i = values_.size();
        while (i > limit) {
            --i;
            ZS_BitCStreamBF_write(&bits, values_[i], nbBits_[i]);
            ZS_BitCStreamBF_flush(&bits);
        }

        for (; i > 0; i -= kNbUnrolls) {
            for (size_t u = 1; u <= kNbUnrolls; ++u) {
                ZS_BitCStreamBF_write(&bits, values_[i - u], nbBits_[i - u]);
            }
            ZS_BitCStreamBF_flush(&bits);
        }

        ZL_Report report = ZS_BitCStreamBF_finish(&bits);
        if (ZL_isError(report))
            return false;
        assert(ZL_validResult(report) == (totalBits_ / 8 + 1));
        encodedSize_ = ZL_validResult(report);
        return true;
    }

    bool encode()
    {
        if (maxBits_ <= 7)
            return encodeImpl<8>();
        else if (maxBits_ <= 9)
            return encodeImpl<6>();
        else if (maxBits_ <= 14)
            return encodeImpl<4>();
        else if (maxBits_ <= 18)
            return encodeImpl<3>();
        else if (maxBits_ <= 28)
            return encodeImpl<2>();
        else if (maxBits_ <= 56)
            return encodeImpl<1>();
        else
            throw std::invalid_argument("Max bits must be <= 56");
    }

    void prepareForDecode()
    {
        decoded_.resize(values_.size());
    }
    template <size_t kNbUnrolls>
    bool decodeImpl(bool extra) noexcept
    {
        if (impl_ == BitstreamImpl::ZS)
            return zsDecodeImpl<kNbUnrolls>(extra);
        else if (impl_ == BitstreamImpl::ZS_BF)
            return zsBfDecodeImpl<kNbUnrolls>();
        else
            return fseDecodeImpl<kNbUnrolls>();
    }

    template <size_t kNbUnrolls>
    bool fseDecodeImpl() noexcept
    {
        BIT_DStream_t bits;
        if (ERR_isError(BIT_initDStream(&bits, encoded_.data(), encodedSize_)))
            return false;
        assert(kNbUnrolls * maxBits_ <= 8 * sizeof(size_t) - 8);
        size_t const iterations = values_.size() / kNbUnrolls;
        size_t const limit      = iterations * kNbUnrolls;
        assert(limit <= values_.size());
        assert(values_.size() == nbBits_.size());

        size_t i = 0;
        for (; i < limit; i += kNbUnrolls) {
            for (size_t u = 0; u < kNbUnrolls; ++u) {
                decoded_[i + u] =
                        BIT_readBitsFast(&bits, (unsigned)nbBits_[i + u]);
            }
            BIT_reloadDStream(&bits);
        }
        assert(i == limit);

        for (; i < values_.size(); ++i) {
            decoded_[i] = BIT_readBitsFast(&bits, (unsigned)nbBits_[i]);
            BIT_reloadDStream(&bits);
        }

        if (!BIT_endOfDStream(&bits))
            return false;
        return true;
    }

    template <size_t kNbUnrolls>
    bool zsDecodeImpl(bool extra) noexcept
    {
        size_t const bitSize = (totalBits_ + 7) / 8;
        ZS_BitDStreamFF bits = ZS_BitDStreamFF_init(
                encoded_.data(), extra ? encoded_.size() : bitSize);
        assert(kNbUnrolls * maxBits_ <= 8 * sizeof(size_t) - 8);
        size_t const iterations = values_.size() / kNbUnrolls;
        size_t const limit      = iterations * kNbUnrolls;
        assert(limit <= values_.size());
        assert(values_.size() == nbBits_.size());

        size_t i = 0;
        for (; i < limit; i += kNbUnrolls) {
            for (size_t u = 0; u < kNbUnrolls; ++u) {
                decoded_[i + u] = ZS_BitDStreamFF_read(&bits, nbBits_[i + u]);
            }
            ZS_BitDStreamFF_reload(&bits);
        }
        assert(i == limit);

        for (; i < values_.size(); ++i) {
            decoded_[i] = ZS_BitDStreamFF_read(&bits, nbBits_[i]);
            ZS_BitDStreamFF_reload(&bits);
        }

        ZL_Report const report = ZS_BitDStreamFF_finish(&bits);
        if (ZL_isError(report)) {
            return false;
        }
        // finish() reports the exact number of bytes consumed, which must equal
        // the encoded size regardless of any extra trailing capacity.
        return ZL_validResult(report) == bitSize;
    }

    template <size_t kNbUnrolls>
    bool zsBfDecodeImpl() noexcept
    {
        ZS_BitDStreamBF bits = ZS_BitDStreamBF_init(
                encoded_.data() + (encoded_.size() - encodedSize_),
                encodedSize_);
        assert(kNbUnrolls * maxBits_ <= 8 * sizeof(size_t) - 8);
        size_t const iterations = values_.size() / kNbUnrolls;
        size_t const limit      = iterations * kNbUnrolls;
        assert(limit <= values_.size());
        assert(values_.size() == nbBits_.size());

        size_t i = 0;
        for (; i < limit; i += kNbUnrolls) {
            for (size_t u = 0; u < kNbUnrolls; ++u) {
                decoded_[i + u] = ZS_BitDStreamBF_read(&bits, nbBits_[i + u]);
            }
            ZS_BitDStreamBF_reload(&bits);
        }
        assert(i == limit);

        for (; i < values_.size(); ++i) {
            decoded_[i] = ZS_BitDStreamBF_read(&bits, nbBits_[i]);
            ZS_BitDStreamBF_reload(&bits);
        }

        return !ZL_isError(ZS_BitDStreamBF_finish(&bits));
    }

    bool decode(bool extra = true)
    {
        if (maxBits_ <= 7)
            return decodeImpl<8>(extra);
        else if (maxBits_ <= 9)
            return decodeImpl<6>(extra);
        else if (maxBits_ <= 14)
            return decodeImpl<4>(extra);
        else if (maxBits_ <= 18)
            return decodeImpl<3>(extra);
        else if (maxBits_ <= 28)
            return decodeImpl<2>(extra);
        else if (maxBits_ <= 56)
            return decodeImpl<1>(extra);
        else
            throw std::invalid_argument("Max bits must be <= 56");
    }

    bool check() const
    {
        for (size_t i = 0; i < values_.size(); ++i) {
            size_t const mask  = ((size_t)1 << nbBits_[i]) - 1;
            size_t const value = values_[i] & mask;
            if (value != decoded_[i]) {
                fprintf(stderr, "%zu != %zu\n", value, decoded_[i]);
                return false;
            }
        }
        return true;
    }

    void test()
    {
        prepareForEncode();
        if (!encode())
            throw std::runtime_error("Encode failed");
        prepareForDecode();
        if (!decode(true))
            throw std::runtime_error("Decode failed");
        if (!check())
            throw std::runtime_error("Check failed");
        if (!decode(false))
            throw std::runtime_error("Decode failed");
        if (!check())
            throw std::runtime_error("Check failed");
    }

   private:
    std::vector<size_t> values_;
    std::vector<size_t> nbBits_;
    size_t totalBits_{ 0 };
    size_t maxBits_{ 0 };
    size_t encodedSize_{ 0 };
    std::vector<uint8_t> encoded_;
    std::vector<size_t> decoded_;
    BitstreamImpl impl_;
};

class BitstreamTest : public testing::TestWithParam<BitstreamImpl> {
   public:
    void testRoundTrip(size_t maxBits, size_t nbValues)
    {
        std::mt19937 gen((unsigned)(maxBits * nbValues));
        std::uniform_int_distribution<size_t> value;
        std::uniform_int_distribution<size_t> nbBits(1, maxBits);
        RoundTripTest test(GetParam());
        for (size_t i = 0; i < nbValues; ++i) {
            test.add(value(gen), nbBits(gen));
        }
        test.test();
    }

    void testRoundTripForAllMaxBits(size_t nbValues)
    {
        for (size_t maxBits = 1; maxBits <= 31; ++maxBits) {
            ZL_LOG(V, "maxBits = %zu", maxBits);
            testRoundTrip(maxBits, nbValues);
        }
    }
};

TEST_P(BitstreamTest, testEmptyRoundTrip)
{
    RoundTripTest test(GetParam());
    test.test();
}

TEST_P(BitstreamTest, testSingleRoundTrip)
{
    for (size_t bits = 1; bits <= 31; ++bits) {
        ZL_LOG(V, "bits = %zu", bits);
        RoundTripTest test(GetParam());
        test.add(0x4242424242424242ULL, bits);
        test.test();
    }
}

TEST_P(BitstreamTest, testTinyRoundTrip)
{
    testRoundTripForAllMaxBits(2);
}

TEST_P(BitstreamTest, testSmallRoundTrip)
{
    testRoundTripForAllMaxBits(10);
}

TEST_P(BitstreamTest, testMediumRoundTrip)
{
    testRoundTripForAllMaxBits(100);
}

TEST_P(BitstreamTest, testLargeRoundTrip)
{
    testRoundTripForAllMaxBits(1000);
}

TEST_P(BitstreamTest, testHugeRoundTrip)
{
    testRoundTripForAllMaxBits(10000);
}

TEST_P(BitstreamTest, testExpGolomb)
{
    std::vector<uint8_t> encoded(1000, 0);
    for (size_t order = 0; order < 5; ++order) {
        auto bitC = ZS_BitCStreamFF_init(encoded.data(), encoded.size());
        for (uint32_t i = 0; i < 100; ++i) {
            ZS_BitCStreamFF_writeExpGolomb(&bitC, i, order);
            ZS_BitCStreamFF_flush(&bitC);
        }
        auto const csize = ZS_BitCStreamFF_finish(&bitC);
        ASSERT_ZS_VALID(csize);
        auto bitD = ZS_BitDStreamFF_init(encoded.data(), ZL_validResult(csize));
        for (uint32_t i = 0; i < 100; ++i) {
            auto const x = ZS_BitDStreamFF_readExpGolomb(&bitD, order);
            ZS_BitDStreamFF_reload(&bitD);
            ASSERT_EQ(i, x);
        }
        ASSERT_ZS_VALID(ZS_BitDStreamFF_finish(&bitD));
    }
}

INSTANTIATE_TEST_SUITE_P(
        BitstreamTest,
        BitstreamTest,
        testing::Values(
                BitstreamImpl::FSE,
                BitstreamImpl::ZS,
                BitstreamImpl::ZS_BF));

// Mask of the low @p nbBits bits.
size_t lowMask(size_t nbBits)
{
    if (nbBits == 0)
        return 0;
    if (nbBits >= sizeof(size_t) * 8)
        return ~(size_t)0;
    return ((size_t)1 << nbBits) - 1;
}

// Round-trips `leading` bits, a byte-aligned raw region holding `region` bits,
// then `trailing` bits. The region is written with the reserve/commit aligned
// API and read back with ZS_BitDStreamFF_popAlignedBits, mirroring how the
// Huffman Pivoted kernels embed raw bitmaps in the stream. Verifies every bit
// and the raw region survive. When `extra` is set the decoder gets slack
// capacity past the encoded size (the large-stream path); otherwise it sees the
// exact end, exercising the near-end and short-stream paths in popAlignedBits.
//
// `reload` reloads the window after every field read. The kernels do not do
// this (popAlignedBits refills the window itself), but it must still be a valid
// thing to do: popAlignedBits has to recover the exact bit position from any
// reloaded stream state, including a short stream whose reload ran past the
// end.
void roundTripAlignedField(
        const std::vector<std::pair<size_t, size_t>>& leading,
        const std::vector<bool>& region,
        const std::vector<std::pair<size_t, size_t>>& trailing,
        bool extra,
        bool reload)
{
    size_t const regionBytes = (region.size() + 7) / 8;
    size_t totalBits         = 0;
    for (auto const& w : leading)
        totalBits += w.second;
    for (auto const& w : trailing)
        totalBits += w.second;
    std::vector<uint8_t> buf(totalBits / 8 + regionBytes + 64, 0);

    ZS_BitCStreamFF writer = ZS_BitCStreamFF_init(buf.data(), buf.size());
    for (auto const& [value, nbBits] : leading) {
        ZS_BitCStreamFF_write(&writer, value, nbBits);
        ZS_BitCStreamFF_flush(&writer);
    }
    uint8_t* const slot =
            ZS_BitCStreamFF_reserveAlignedBits(&writer, region.size());
    ASSERT_NE(slot, nullptr);
    std::memset(slot, 0, regionBytes);
    for (size_t i = 0; i < region.size(); ++i) {
        if (region[i])
            slot[i / 8] |= (uint8_t)(1u << (i & 7));
    }
    ZS_BitCStreamFF_commitReservedBits(&writer);
    for (auto const& [value, nbBits] : trailing) {
        ZS_BitCStreamFF_write(&writer, value, nbBits);
        ZS_BitCStreamFF_flush(&writer);
    }
    ZL_Report const report = ZS_BitCStreamFF_finish(&writer);
    ASSERT_ZS_VALID(report);
    size_t const encodedSize = ZL_validResult(report);

    ZS_BitDStreamFF reader =
            ZS_BitDStreamFF_init(buf.data(), extra ? buf.size() : encodedSize);
    for (auto const& [value, nbBits] : leading) {
        EXPECT_EQ(
                ZS_BitDStreamFF_read(&reader, nbBits), value & lowMask(nbBits));
        if (reload)
            ZS_BitDStreamFF_reload(&reader);
    }
    uint8_t const* const popped =
            ZS_BitDStreamFF_popAlignedBits(&reader, region.size());
    ASSERT_NE(popped, nullptr);
    for (size_t i = 0; i < region.size(); ++i) {
        EXPECT_EQ((popped[i / 8] >> (i & 7)) & 1u, region[i] ? 1u : 0u)
                << "region bit " << i;
    }
    for (auto const& [value, nbBits] : trailing) {
        EXPECT_EQ(
                ZS_BitDStreamFF_read(&reader, nbBits), value & lowMask(nbBits));
        if (reload)
            ZS_BitDStreamFF_reload(&reader);
    }
    ASSERT_ZS_VALID(ZS_BitDStreamFF_finish(&reader));
}

std::vector<bool> makeRegionBits(size_t nbBits)
{
    std::vector<bool> bits(nbBits);
    for (size_t i = 0; i < nbBits; ++i)
        bits[i] = (((i * 7) + 3) % 5) < 2;
    return bits;
}

TEST(BitstreamAlignedBitsTest, ReserveCommitPopRoundTrip)
{
    using BitWrites          = std::vector<std::pair<size_t, size_t>>;
    BitWrites const someBits = {
        { 0x1, 1 }, { 0x2, 3 }, { 0xABCD, 13 }, { 0x7F, 7 }
    };

    struct Scenario {
        const char* name;
        BitWrites leading;
        size_t regionBits;
        BitWrites trailing;
    };
    std::vector<Scenario> const scenarios = {
        // Region alone, with nothing around it.
        { "empty_region", {}, 0, {} },
        { "byte_region_only", {}, 8, {} },
        { "aligned_region_only", {}, 64, {} },
        // Region followed by bits (window stays mid-stream).
        { "region_then_bits", {}, 24, someBits },
        // Bits then a region that ends the stream (window lands at the end).
        { "bits_then_region", someBits, 40, {} },
        // Region surrounded on both sides.
        { "bits_region_bits", someBits, 32, someBits },
        // Unaligned leading bits force the region past padding.
        { "unaligned_leading", { { 0x5, 3 } }, 16, { { 0x9, 5 } } },
        // Region whose bit count is not a multiple of 8 (partial trailing
        // byte).
        { "partial_region", someBits, 19, someBits },
        { "partial_region_at_end", someBits, 13, {} },
        // Tiny total so the exact-capacity decode hits the short-stream path.
        { "tiny_small_stream", { { 0x1, 2 } }, 8, {} },
        // Large region exercises the steady-state window refill.
        { "large_region", someBits, 1000, someBits },
    };

    for (auto const& s : scenarios) {
        for (bool extra : { true, false }) {
            for (bool reload : { false, true }) {
                SCOPED_TRACE(
                        std::string(s.name) + (extra ? " extra" : " exact")
                        + (reload ? " reload" : " noreload"));
                roundTripAlignedField(
                        s.leading,
                        makeRegionBits(s.regionBits),
                        s.trailing,
                        extra,
                        reload);
            }
        }
    }
}

TEST(BitstreamAlignedBitsTest, ReserveReturnsNullWhenBufferTooSmall)
{
    std::vector<uint8_t> buf(4, 0);
    ZS_BitCStreamFF writer = ZS_BitCStreamFF_init(buf.data(), buf.size());
    EXPECT_EQ(ZS_BitCStreamFF_reserveAlignedBits(&writer, 8 * 64), nullptr);
}

TEST(BitstreamAlignedBitsTest, PopReturnsNullPastEndOfStream)
{
    std::vector<uint8_t> buf(8, 0xAB);
    ZS_BitDStreamFF reader = ZS_BitDStreamFF_init(buf.data(), buf.size());
    EXPECT_EQ(ZS_BitDStreamFF_popAlignedBits(&reader, 8 * 64), nullptr);
}

struct BenchmarkResult {
    int64_t encodeNs{ 0 };
    int64_t decodeNs{ 0 };
    size_t nbValues{ 0 };
    size_t nbBits{ 0 };
    size_t maxBits{};

    BenchmarkResult operator&(BenchmarkResult const& other) const
    {
        BenchmarkResult result;
        result.encodeNs = encodeNs + other.encodeNs;
        result.decodeNs = decodeNs + other.decodeNs;
        result.nbValues = nbValues + other.nbValues;
        result.nbBits   = nbBits + other.nbBits;
        ZL_REQUIRE_EQ(maxBits, other.maxBits);
        result.maxBits = maxBits;
        return result;
    }

    BenchmarkResult operator|(BenchmarkResult const& other) const
    {
        BenchmarkResult result;
        result.encodeNs = std::min(encodeNs, other.encodeNs);
        result.decodeNs = std::min(decodeNs, other.decodeNs);
        ZL_REQUIRE_EQ(nbBits, other.nbBits);
        ZL_REQUIRE_EQ(nbValues, other.nbValues);
        ZL_REQUIRE_EQ(maxBits, other.maxBits);
        result.nbBits   = nbBits;
        result.nbValues = nbValues;
        result.maxBits  = maxBits;
        return result;
    }

    void print(std::string name) const
    {
        uint64_t const kConversion = 125;
        uint64_t const encode_mbps =
                (uint64_t(nbBits) * kConversion) / uint64_t(encodeNs);
        uint64_t const decode_mbps =
                (uint64_t(nbBits) * kConversion) / uint64_t(decodeNs);
        fprintf(stderr,
                "%s encode-MB/s = %u\tdecode-MB/s = %u\n",
                name.c_str(),
                (unsigned)encode_mbps,
                (unsigned)decode_mbps);
    }
};

BenchmarkResult benchmarkMaxBits(BitstreamImpl impl, size_t maxBits)
{
    size_t const kNbEntries = 10000;
    RoundTripTest test(impl);
    std::mt19937 gen((unsigned)(maxBits * kNbEntries));
    std::uniform_int_distribution<size_t> value;
    std::uniform_int_distribution<size_t> nbBits(1, maxBits);
    size_t totalBits = 0;
    for (size_t i = 0; i < kNbEntries; ++i) {
        size_t const n = nbBits(gen);
        test.add(value(gen), n);
        totalBits += n;
    }
    test.prepareForEncode();
    int64_t encodeNs;
    {
        auto const start = std::chrono::steady_clock::now();
        test.encode();
        auto const stop = std::chrono::steady_clock::now();
        encodeNs        = std::chrono::nanoseconds(stop - start).count();
    }
    test.prepareForDecode();
    int64_t decodeNs;
    {
        auto const start = std::chrono::steady_clock::now();
        test.decode();
        auto const stop = std::chrono::steady_clock::now();
        decodeNs        = std::chrono::nanoseconds(stop - start).count();
    }
    ZL_REQUIRE(test.check());

    BenchmarkResult result;
    result.encodeNs = encodeNs;
    result.decodeNs = decodeNs;
    result.nbValues = kNbEntries;
    result.maxBits  = maxBits;
    result.nbBits   = totalBits;
    return result;
}

void printResults(std::string name, std::function<BenchmarkResult()> bm)
{
    size_t const kNbRepeats = 1000;
    BenchmarkResult avg;
    BenchmarkResult min;
    {
        auto const result = bm();
        avg               = result;
        min               = result;
    }
    for (size_t i = 1; i < kNbRepeats; ++i) {
        auto const result = bm();
        avg               = avg & result;
        min               = min | result;
    }
    avg.print(name + "avg:");
    min.print(name + "min:");
    fprintf(stderr, "\n");
}

ZL_UNUSED_ATTR void benchmark(int argc, char** argv)
{
    (void)argc, (void)argv;
    auto zsBm = [](size_t maxBits) {
        printResults(
                "  ZS:" + std::string(maxBits < 10 ? " " : "")
                        + std::to_string(maxBits) + ":",
                [&maxBits] {
                    return benchmarkMaxBits(BitstreamImpl::ZS, maxBits);
                });
    };
    auto zsBfBm = [](size_t maxBits) {
        printResults(
                "ZSBF:" + std::string(maxBits < 10 ? " " : "")
                        + std::to_string(maxBits) + ":",
                [&maxBits] {
                    return benchmarkMaxBits(BitstreamImpl::ZS_BF, maxBits);
                });
    };
    auto fseBm = [](size_t maxBits) {
        printResults(
                " FSE:" + std::string(maxBits < 10 ? " " : "")
                        + std::to_string(maxBits) + ":",
                [&maxBits] {
                    return benchmarkMaxBits(BitstreamImpl::FSE, maxBits);
                });
    };
    for (auto maxBits : { 7, 9, 14, 18, 28, 31 }) {
        zsBm(size_t(maxBits));
        zsBfBm(size_t(maxBits));
        fseBm(size_t(maxBits));
    }
}

} // namespace
} // namespace tests
} // namespace openzl

#ifdef BENCHMARK_BITSTREAM
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int const ret = RUN_ALL_TESTS();
    if (ret != 0) {
        return ret;
    }
    openzl::tests::benchmark(argc, argv);
}
#endif
