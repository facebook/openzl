// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "benchmark/micro/micro_bench.h"

#include <benchmark/benchmark.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "benchmark/benchmark_config.h"
#include "openzl/codecs/pivco_huffman/arch/decode_pivco_arch.h"
#include "openzl/codecs/pivco_huffman/arch/encode_pivco_arch.h"
#include "openzl/common/assertion.h"

namespace zstrong::bench::micro {
namespace {

struct EncodeArch {
    std::string name;
    const ZL_PivCoHuffmanEncode* kernels;
};

struct DecodeArch {
    std::string name;
    const ZL_PivCoHuffmanDecode* kernels;
};

std::vector<EncodeArch> supportedEncodeArchs()
{
    ZL_cpuid_t const cpuid = ZL_cpuid();
    std::vector<EncodeArch> out;
    std::vector<EncodeArch> const archs = {
        { "generic", &ZL_PivCoHuffmanEncode_generic },
        { "avx512", &ZL_PivCoHuffmanEncode_avx512 }
    };
    for (auto const& arch : archs) {
        if (arch.kernels->supported(&cpuid)) {
            out.push_back(arch);
        }
    }
    return out;
}

std::vector<DecodeArch> supportedDecodeArchs()
{
    ZL_cpuid_t const cpuid = ZL_cpuid();
    std::vector<DecodeArch> out;
    std::vector<DecodeArch> const archs = {
        { "generic", &ZL_PivCoHuffmanDecode_generic },
        { "avx512", &ZL_PivCoHuffmanDecode_avx512 },
    };
    for (auto const& arch : archs) {
        if (arch.kernels->supported(&cpuid)) {
            out.push_back(arch);
        }
    }
    return out;
}

const EncodeArch& defaultEncodeArch(const std::vector<EncodeArch>& archs)
{
    // Match the arch the engine would pick at runtime.
    const ZL_PivCoHuffmanEncode* const selected =
            ZL_PivCoHuffmanEncode_select(nullptr);
    for (auto const& arch : archs) {
        if (arch.kernels == selected) {
            return arch;
        }
    }
    ZL_ABORT();
}

const DecodeArch& defaultDecodeArch(const std::vector<DecodeArch>& archs)
{
    // Match the arch the engine would pick at runtime.
    const ZL_PivCoHuffmanDecode* const selected =
            ZL_PivCoHuffmanDecode_select(nullptr);
    for (auto const& arch : archs) {
        if (arch.kernels == selected) {
            return arch;
        }
    }
    ZL_ABORT();
}

size_t bitmapBytes(size_t bits)
{
    return (bits + 7) / 8;
}

template <typename T>
void requireEqualPrefix(
        const std::vector<T>& lhs,
        const std::vector<T>& rhs,
        size_t size)
{
    ZL_REQUIRE_GE(lhs.size(), size);
    ZL_REQUIRE_GE(rhs.size(), size);
    for (size_t i = 0; i < size; ++i) {
        ZL_REQUIRE_EQ(lhs[i], rhs[i]);
    }
}

struct PartitionData {
    explicit PartitionData(size_t n)
            : ranks(n + ZL_PIVCO_HUFFMAN_SLOP),
              bitmap(bitmapBytes(n) + ZL_PIVCO_HUFFMAN_SLOP),
              lhs(n + ZL_PIVCO_HUFFMAN_SLOP),
              rhs(n + ZL_PIVCO_HUFFMAN_SLOP),
              size(n)
    {
        // Ranks cycle through the whole byte range so roughly half land on each
        // side of rightRank.
        for (size_t i = 0; i < n; ++i) {
            ranks[i] = (uint8_t)(((i * 257) + 0x1234) & 0xFF);
        }
    }

    std::vector<uint8_t> ranks;
    std::vector<uint8_t> bitmap;
    std::vector<uint8_t> lhs;
    std::vector<uint8_t> rhs;
    uint8_t rightRank = 0x80;
    size_t size;
};

struct FlatData {
    FlatData(size_t n, size_t d)
            : ranks(n + ZL_PIVCO_HUFFMAN_SLOP),
              bitmap(bitmapBytes(n * d) + ZL_PIVCO_HUFFMAN_SLOP),
              symbols((size_t)1 << d),
              out(n + ZL_PIVCO_HUFFMAN_SLOP),
              size(n),
              depth(d),
              // At depth 8 the index spans the whole byte range, so rankBegin
              // must be 0 to keep rankBegin + index within a byte.
              rankBegin(d == 8 ? 0 : 17)
    {
        // Each rank's offset from rankBegin must fit in `depth` bits, so keep
        // ranks within [rankBegin, rankBegin + 2^depth).
        uint8_t const indexMask = (uint8_t)(((size_t)1 << d) - 1);
        for (size_t i = 0; i < n; ++i) {
            ranks[i] = (uint8_t)(rankBegin + (uint8_t)(i & indexMask));
        }
        for (size_t i = 0; i < symbols.size(); ++i) {
            symbols[i] = (uint8_t)((i * 37 + d * 11) & 0xFF);
        }
        ZL_PivCoHuffmanEncode_generic.packFlatDepth(
                bitmap.data(), depth, ranks.data(), size, rankBegin);
    }

    std::vector<uint8_t> ranks;
    std::vector<uint8_t> bitmap;
    std::vector<uint8_t> symbols;
    std::vector<uint8_t> out;
    size_t size;
    size_t depth;
    uint8_t rankBegin;
};

struct MergeData {
    explicit MergeData(size_t n)
            : bitmap(bitmapBytes(n) + ZL_PIVCO_HUFFMAN_SLOP),
              out(n + ZL_PIVCO_HUFFMAN_SLOP),
              totalSize(n)
    {
        for (size_t i = 0; i < n; ++i) {
            bool const bit = ((i * 11 + n) % 5) < 2;
            if (bit) {
                bitmap[i / 8] |= (uint8_t)(1u << (i & 7));
                rhs.push_back((uint8_t)(0x80u + ((rhs.size() * 13) & 0x7F)));
            } else {
                lhs.push_back((uint8_t)(0x10u + ((lhs.size() * 17) & 0x6F)));
            }
        }
        lhsSize = lhs.size();
        rhsSize = rhs.size();
        lhs.resize(lhsSize + ZL_PIVCO_HUFFMAN_SLOP, 0xEE);
        rhs.resize(rhsSize + ZL_PIVCO_HUFFMAN_SLOP, 0xDD);
    }

    std::vector<uint8_t> bitmap;
    std::vector<uint8_t> lhs;
    std::vector<uint8_t> rhs;
    std::vector<uint8_t> out;
    size_t lhsSize = 0;
    size_t rhsSize = 0;
    size_t totalSize;
};

void verifyPartitionFull(const EncodeArch& arch, const PartitionData& data)
{
    std::vector<uint8_t> expectedBitmap(data.bitmap.size());
    std::vector<uint8_t> expectedLhs(data.lhs.size());
    std::vector<uint8_t> expectedRhs(data.rhs.size());
    size_t const expectedOnes = ZL_PivCoHuffmanEncode_generic.partitionFull(
            expectedBitmap.data(),
            expectedLhs.data(),
            expectedRhs.data(),
            data.ranks.data(),
            data.size,
            data.rightRank);

    std::vector<uint8_t> bitmap(data.bitmap.size());
    std::vector<uint8_t> lhs(data.lhs.size());
    std::vector<uint8_t> rhs(data.rhs.size());
    size_t const ones = arch.kernels->partitionFull(
            bitmap.data(),
            lhs.data(),
            rhs.data(),
            data.ranks.data(),
            data.size,
            data.rightRank);
    ZL_REQUIRE_EQ(ones, expectedOnes);
    requireEqualPrefix(bitmap, expectedBitmap, bitmapBytes(data.size));
    requireEqualPrefix(lhs, expectedLhs, data.size - expectedOnes);
    requireEqualPrefix(rhs, expectedRhs, expectedOnes);
}

void verifyPartitionLeft(const EncodeArch& arch, const PartitionData& data)
{
    std::vector<uint8_t> expectedBitmap(data.bitmap.size());
    std::vector<uint8_t> expectedLhs(data.lhs.size());
    size_t const expectedOnes = ZL_PivCoHuffmanEncode_generic.partitionLeft(
            expectedBitmap.data(),
            expectedLhs.data(),
            data.ranks.data(),
            data.size,
            data.rightRank);

    std::vector<uint8_t> bitmap(data.bitmap.size());
    std::vector<uint8_t> lhs(data.lhs.size());
    size_t const ones = arch.kernels->partitionLeft(
            bitmap.data(),
            lhs.data(),
            data.ranks.data(),
            data.size,
            data.rightRank);
    ZL_REQUIRE_EQ(ones, expectedOnes);
    requireEqualPrefix(bitmap, expectedBitmap, bitmapBytes(data.size));
    requireEqualPrefix(lhs, expectedLhs, data.size - expectedOnes);
}

void verifyPartitionRight(const EncodeArch& arch, const PartitionData& data)
{
    std::vector<uint8_t> expectedBitmap(data.bitmap.size());
    std::vector<uint8_t> expectedRhs(data.rhs.size());
    size_t const expectedOnes = ZL_PivCoHuffmanEncode_generic.partitionRight(
            expectedBitmap.data(),
            expectedRhs.data(),
            data.ranks.data(),
            data.size,
            data.rightRank);

    std::vector<uint8_t> bitmap(data.bitmap.size());
    std::vector<uint8_t> rhs(data.rhs.size());
    size_t const ones = arch.kernels->partitionRight(
            bitmap.data(),
            rhs.data(),
            data.ranks.data(),
            data.size,
            data.rightRank);
    ZL_REQUIRE_EQ(ones, expectedOnes);
    requireEqualPrefix(bitmap, expectedBitmap, bitmapBytes(data.size));
    requireEqualPrefix(rhs, expectedRhs, expectedOnes);
}

void verifyPartitionNone(const EncodeArch& arch, const PartitionData& data)
{
    std::vector<uint8_t> expectedBitmap(data.bitmap.size());
    ZL_PivCoHuffmanEncode_generic.partitionNone(
            expectedBitmap.data(),
            data.ranks.data(),
            data.size,
            data.rightRank);

    std::vector<uint8_t> bitmap(data.bitmap.size());
    arch.kernels->partitionNone(
            bitmap.data(), data.ranks.data(), data.size, data.rightRank);
    requireEqualPrefix(bitmap, expectedBitmap, bitmapBytes(data.size));
}

void verifyPackFlatDepth(const EncodeArch& arch, const FlatData& data)
{
    std::vector<uint8_t> expectedBitmap(data.bitmap.size());
    ZL_PivCoHuffmanEncode_generic.packFlatDepth(
            expectedBitmap.data(),
            data.depth,
            data.ranks.data(),
            data.size,
            data.rankBegin);

    std::vector<uint8_t> bitmap(data.bitmap.size());
    arch.kernels->packFlatDepth(
            bitmap.data(),
            data.depth,
            data.ranks.data(),
            data.size,
            data.rankBegin);
    requireEqualPrefix(
            bitmap, expectedBitmap, bitmapBytes(data.size * data.depth));
}

void verifyMergeVectorVector(const DecodeArch& arch, const MergeData& data)
{
    std::vector<uint8_t> expectedOut(data.out.size());
    size_t const expectedOnes = ZL_PivCoHuffmanDecode_generic.mergeVectorVector(
            expectedOut.data(),
            expectedOut.size(),
            data.bitmap.data(),
            data.bitmap.size(),
            data.lhs.data(),
            data.lhsSize,
            data.rhs.data(),
            data.rhsSize);

    std::vector<uint8_t> out(data.out.size());
    size_t const ones = arch.kernels->mergeVectorVector(
            out.data(),
            out.size(),
            data.bitmap.data(),
            data.bitmap.size(),
            data.lhs.data(),
            data.lhsSize,
            data.rhs.data(),
            data.rhsSize);
    ZL_REQUIRE_EQ(ones, expectedOnes);
    requireEqualPrefix(out, expectedOut, data.totalSize);
}

void verifyMergeConstantVector(const DecodeArch& arch, const MergeData& data)
{
    std::vector<uint8_t> expectedOut(data.out.size());
    uint8_t const lhs = 0x3C;
    size_t const expectedOnes =
            ZL_PivCoHuffmanDecode_generic.mergeConstantVector(
                    expectedOut.data(),
                    expectedOut.size(),
                    data.bitmap.data(),
                    data.bitmap.size(),
                    lhs,
                    data.lhsSize,
                    data.rhs.data(),
                    data.rhsSize);

    std::vector<uint8_t> out(data.out.size());
    size_t const ones = arch.kernels->mergeConstantVector(
            out.data(),
            out.size(),
            data.bitmap.data(),
            data.bitmap.size(),
            lhs,
            data.lhsSize,
            data.rhs.data(),
            data.rhsSize);
    ZL_REQUIRE_EQ(ones, expectedOnes);
    requireEqualPrefix(out, expectedOut, data.totalSize);
}

void verifyMergeVectorConstant(const DecodeArch& arch, const MergeData& data)
{
    std::vector<uint8_t> expectedOut(data.out.size());
    uint8_t const rhs = 0xC3;
    size_t const expectedOnes =
            ZL_PivCoHuffmanDecode_generic.mergeVectorConstant(
                    expectedOut.data(),
                    expectedOut.size(),
                    data.bitmap.data(),
                    data.bitmap.size(),
                    data.lhs.data(),
                    data.lhsSize,
                    rhs,
                    data.rhsSize);

    std::vector<uint8_t> out(data.out.size());
    size_t const ones = arch.kernels->mergeVectorConstant(
            out.data(),
            out.size(),
            data.bitmap.data(),
            data.bitmap.size(),
            data.lhs.data(),
            data.lhsSize,
            rhs,
            data.rhsSize);
    ZL_REQUIRE_EQ(ones, expectedOnes);
    requireEqualPrefix(out, expectedOut, data.totalSize);
}

void verifyMergeFlatDepth(const DecodeArch& arch, const FlatData& data)
{
    std::vector<uint8_t> expectedOut(data.out.size());
    ZL_PivCoHuffmanDecode_generic.mergeFlatDepth(
            expectedOut.data(),
            data.size,
            expectedOut.size(),
            data.bitmap.data(),
            data.bitmap.size(),
            data.depth,
            data.symbols.data());

    std::vector<uint8_t> out(data.out.size());
    arch.kernels->mergeFlatDepth(
            out.data(),
            data.size,
            out.size(),
            data.bitmap.data(),
            data.bitmap.size(),
            data.depth,
            data.symbols.data());
    requireEqualPrefix(out, expectedOut, data.size);
}

std::string benchName(const std::string& arch, const std::string& kernel)
{
    return "MCR / PivCoHuffman / " + arch + " / " + kernel;
}

void registerPartitionFullBenchmark(EncodeArch arch)
{
    auto data = std::make_shared<PartitionData>(64 * 1024);
    verifyPartitionFull(arch, *data);
    RegisterBenchmark(
            benchName(arch.name, "partitionFull"),
            [arch, data](benchmark::State& state) {
                size_t ones = 0;
                for (auto _ : state) {
                    ones += arch.kernels->partitionFull(
                            data->bitmap.data(),
                            data->lhs.data(),
                            data->rhs.data(),
                            data->ranks.data(),
                            data->size,
                            data->rightRank);
                }
                benchmark::DoNotOptimize(ones);
                state.SetBytesProcessed(
                        (int64_t)data->size * (int64_t)state.iterations());
            });
}

void registerPartitionLeftBenchmark(EncodeArch arch)
{
    auto data = std::make_shared<PartitionData>(64 * 1024);
    verifyPartitionLeft(arch, *data);
    RegisterBenchmark(
            benchName(arch.name, "partitionLeft"),
            [arch, data](benchmark::State& state) {
                size_t ones = 0;
                for (auto _ : state) {
                    ones += arch.kernels->partitionLeft(
                            data->bitmap.data(),
                            data->lhs.data(),
                            data->ranks.data(),
                            data->size,
                            data->rightRank);
                }
                benchmark::DoNotOptimize(ones);
                state.SetBytesProcessed(
                        (int64_t)data->size * (int64_t)state.iterations());
            });
}

void registerPartitionRightBenchmark(EncodeArch arch)
{
    auto data = std::make_shared<PartitionData>(64 * 1024);
    verifyPartitionRight(arch, *data);
    RegisterBenchmark(
            benchName(arch.name, "partitionRight"),
            [arch, data](benchmark::State& state) {
                size_t ones = 0;
                for (auto _ : state) {
                    ones += arch.kernels->partitionRight(
                            data->bitmap.data(),
                            data->rhs.data(),
                            data->ranks.data(),
                            data->size,
                            data->rightRank);
                }
                benchmark::DoNotOptimize(ones);
                state.SetBytesProcessed(
                        (int64_t)data->size * (int64_t)state.iterations());
            });
}

void registerPartitionNoneBenchmark(EncodeArch arch)
{
    auto data = std::make_shared<PartitionData>(64 * 1024);
    verifyPartitionNone(arch, *data);
    RegisterBenchmark(
            benchName(arch.name, "partitionNone"),
            [arch, data](benchmark::State& state) {
                for (auto _ : state) {
                    arch.kernels->partitionNone(
                            data->bitmap.data(),
                            data->ranks.data(),
                            data->size,
                            data->rightRank);
                }
                benchmark::DoNotOptimize(data->bitmap.data());
                state.SetBytesProcessed(
                        (int64_t)data->size * (int64_t)state.iterations());
            });
}

void registerPackFlatDepthBenchmark(EncodeArch arch, size_t depth)
{
    auto data = std::make_shared<FlatData>(64 * 1024, depth);
    verifyPackFlatDepth(arch, *data);
    RegisterBenchmark(
            benchName(arch.name, "packFlatDepth(depth=" + std::to_string(depth))
                    + ")",
            [arch, data](benchmark::State& state) {
                for (auto _ : state) {
                    arch.kernels->packFlatDepth(
                            data->bitmap.data(),
                            data->depth,
                            data->ranks.data(),
                            data->size,
                            data->rankBegin);
                }
                benchmark::DoNotOptimize(data->bitmap.data());
                state.SetBytesProcessed(
                        (int64_t)data->size * (int64_t)state.iterations());
            });
}

void registerMergeVectorVectorBenchmark(DecodeArch arch)
{
    auto data = std::make_shared<MergeData>(64 * 1024);
    verifyMergeVectorVector(arch, *data);
    RegisterBenchmark(
            benchName(arch.name, "mergeVectorVector"),
            [arch, data](benchmark::State& state) {
                size_t ones = 0;
                for (auto _ : state) {
                    ones += arch.kernels->mergeVectorVector(
                            data->out.data(),
                            data->out.size(),
                            data->bitmap.data(),
                            data->bitmap.size(),
                            data->lhs.data(),
                            data->lhsSize,
                            data->rhs.data(),
                            data->rhsSize);
                }
                benchmark::DoNotOptimize(ones);
                state.SetBytesProcessed(
                        (int64_t)data->totalSize * (int64_t)state.iterations());
            });
}

void registerMergeConstantVectorBenchmark(DecodeArch arch)
{
    auto data = std::make_shared<MergeData>(64 * 1024);
    verifyMergeConstantVector(arch, *data);
    RegisterBenchmark(
            benchName(arch.name, "mergeConstantVector"),
            [arch, data](benchmark::State& state) {
                size_t ones       = 0;
                uint8_t const lhs = 0x3C;
                for (auto _ : state) {
                    ones += arch.kernels->mergeConstantVector(
                            data->out.data(),
                            data->out.size(),
                            data->bitmap.data(),
                            data->bitmap.size(),
                            lhs,
                            data->lhsSize,
                            data->rhs.data(),
                            data->rhsSize);
                }
                benchmark::DoNotOptimize(ones);
                state.SetBytesProcessed(
                        (int64_t)data->totalSize * (int64_t)state.iterations());
            });
}

void registerMergeVectorConstantBenchmark(DecodeArch arch)
{
    auto data = std::make_shared<MergeData>(64 * 1024);
    verifyMergeVectorConstant(arch, *data);
    RegisterBenchmark(
            benchName(arch.name, "mergeVectorConstant"),
            [arch, data](benchmark::State& state) {
                size_t ones       = 0;
                uint8_t const rhs = 0xC3;
                for (auto _ : state) {
                    ones += arch.kernels->mergeVectorConstant(
                            data->out.data(),
                            data->out.size(),
                            data->bitmap.data(),
                            data->bitmap.size(),
                            data->lhs.data(),
                            data->lhsSize,
                            rhs,
                            data->rhsSize);
                }
                benchmark::DoNotOptimize(ones);
                state.SetBytesProcessed(
                        (int64_t)data->totalSize * (int64_t)state.iterations());
            });
}

void registerMergeFlatDepthBenchmark(DecodeArch arch, size_t depth)
{
    auto data = std::make_shared<FlatData>(64 * 1024, depth);
    verifyMergeFlatDepth(arch, *data);
    RegisterBenchmark(
            benchName(
                    arch.name, "mergeFlatDepth(depth=" + std::to_string(depth))
                    + ")",
            [arch, data](benchmark::State& state) {
                for (auto _ : state) {
                    arch.kernels->mergeFlatDepth(
                            data->out.data(),
                            data->size,
                            data->out.size(),
                            data->bitmap.data(),
                            data->bitmap.size(),
                            data->depth,
                            data->symbols.data());
                }
                benchmark::DoNotOptimize(data->out.data());
                state.SetBytesProcessed(
                        (int64_t)data->size * (int64_t)state.iterations());
            });
}

void registerEncodeBenchmarks(EncodeArch arch)
{
    ZL_REQUIRE_NN(arch.kernels->partitionFull);
    ZL_REQUIRE_NN(arch.kernels->partitionLeft);
    ZL_REQUIRE_NN(arch.kernels->partitionRight);
    ZL_REQUIRE_NN(arch.kernels->partitionNone);
    ZL_REQUIRE_NN(arch.kernels->packFlatDepth);
    registerPartitionFullBenchmark(arch);
    registerPartitionLeftBenchmark(arch);
    registerPartitionRightBenchmark(arch);
    registerPartitionNoneBenchmark(arch);
    for (size_t depth = 1; depth <= 8; ++depth) {
        registerPackFlatDepthBenchmark(arch, depth);
    }
}

void registerDecodeBenchmarks(DecodeArch arch)
{
    ZL_REQUIRE_NN(arch.kernels->mergeVectorVector);
    ZL_REQUIRE_NN(arch.kernels->mergeConstantVector);
    ZL_REQUIRE_NN(arch.kernels->mergeVectorConstant);
    ZL_REQUIRE_NN(arch.kernels->mergeFlatDepth);
    registerMergeVectorVectorBenchmark(arch);
    registerMergeConstantVectorBenchmark(arch);
    registerMergeVectorConstantBenchmark(arch);
    for (size_t depth = 1; depth <= 8; ++depth) {
        registerMergeFlatDepthBenchmark(arch, depth);
    }
}

} // namespace

void registerPivCoHuffmanBenchmarks()
{
    auto const encodeArchs = supportedEncodeArchs();
    auto const decodeArchs = supportedDecodeArchs();

    for (auto const& arch : encodeArchs) {
        registerEncodeBenchmarks(arch);
    }
    for (auto const& arch : decodeArchs) {
        registerDecodeBenchmarks(arch);
    }

    EncodeArch defaultEncode = defaultEncodeArch(encodeArchs);
    defaultEncode.name       = "default";
    registerEncodeBenchmarks(defaultEncode);

    DecodeArch defaultDecode = defaultDecodeArch(decodeArchs);
    defaultDecode.name       = "default";
    registerDecodeBenchmarks(defaultDecode);
}

} // namespace zstrong::bench::micro
