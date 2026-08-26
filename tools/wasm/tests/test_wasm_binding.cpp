// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "tools/wasm/src/openzl_wasm.h"

namespace {

// C hands back a buffer the caller owns, so copy it out and release it
std::vector<uint8_t> copyAndFree(uint8_t* buf, size_t size)
{
    std::vector<uint8_t> out(buf, buf + size);
    openzl_wasm_free(buf);
    return out;
}

ZL_ErrorCode serializedCompressor(
        openzl_wasm_Profile profile,
        std::vector<uint8_t>* out)
{
    if (!out) {
        return ZL_ErrorCode_parameter_invalid;
    }
    out->clear();
    uint8_t* buf = nullptr;
    size_t size  = 0;
    ZL_ErrorCode code =
            openzl_wasm_getSerializedCompressor(profile, &buf, &size);
    if (code != ZL_ErrorCode_no_error) {
        EXPECT_EQ(buf, nullptr);
        return code;
    }
    *out = copyAndFree(buf, size);
    return code;
}

ZL_ErrorCode compress(
        const std::vector<uint8_t>& src,
        const std::vector<uint8_t>& compressor,
        std::vector<uint8_t>* out)
{
    if (!out) {
        return ZL_ErrorCode_parameter_invalid;
    }
    out->clear();
    uint8_t* buf      = nullptr;
    size_t size       = 0;
    ZL_ErrorCode code = openzl_wasm_compress(
            compressor.data(),
            compressor.size(),
            src.empty() ? nullptr : src.data(),
            src.size(),
            &buf,
            &size);
    if (code != ZL_ErrorCode_no_error) {
        EXPECT_EQ(buf, nullptr);
        return code;
    }
    *out = copyAndFree(buf, size);
    return code;
}

ZL_ErrorCode decompress(
        const std::vector<uint8_t>& frame,
        std::vector<uint8_t>* out)
{
    if (!out) {
        return ZL_ErrorCode_parameter_invalid;
    }
    out->clear();
    uint8_t* buf = nullptr;
    size_t size  = 0;
    ZL_ErrorCode code =
            openzl_wasm_decompress(frame.data(), frame.size(), &buf, &size);
    if (code != ZL_ErrorCode_no_error) {
        EXPECT_EQ(buf, nullptr);
        return code;
    }
    *out = copyAndFree(buf, size);
    return code;
}

struct BenchCompress {
    ZL_ErrorCode code;
    std::vector<uint8_t> frame;
    double ms;
};

BenchCompress benchmarkCompress(
        const std::vector<uint8_t>& src,
        const std::vector<uint8_t>& compressor,
        size_t iterations)
{
    uint8_t* buf      = nullptr;
    size_t size       = 0;
    double ms         = 0;
    ZL_ErrorCode code = openzl_wasm_benchmarkCompress(
            compressor.data(),
            compressor.size(),
            src.empty() ? nullptr : src.data(),
            src.size(),
            iterations,
            &buf,
            &size,
            &ms);
    if (code != ZL_ErrorCode_no_error) {
        EXPECT_EQ(buf, nullptr);
        return { code, {}, ms };
    }
    return { code, copyAndFree(buf, size), ms };
}

struct BenchDecompress {
    ZL_ErrorCode code;
    double ms;
};

BenchDecompress benchmarkDecompress(
        const std::vector<uint8_t>& frame,
        size_t iterations)
{
    double ms         = 0;
    ZL_ErrorCode code = openzl_wasm_benchmarkDecompress(
            frame.data(), frame.size(), iterations, &ms);
    return { code, ms };
}

std::vector<uint8_t> makeSerialData(size_t size)
{
    constexpr std::string_view kPattern = "hello openzl serial data ";
    std::vector<uint8_t> out(size);
    for (size_t i = 0; i < size; ++i) {
        out[i] = static_cast<uint8_t>(kPattern[i % kPattern.size()]);
    }
    return out;
}

std::vector<uint8_t> makeIntData(size_t eltWidth, size_t count, bool isSigned)
{
    std::vector<uint8_t> out;
    out.reserve(count * eltWidth);
    for (size_t i = 0; i < count; ++i) {
        const int64_t value = isSigned ? static_cast<int64_t>(i % 200) - 100
                                       : static_cast<int64_t>(i % 200);
        for (size_t b = 0; b < eltWidth; ++b) {
            out.push_back(static_cast<uint8_t>((value >> (b * 8)) & 0xFF));
        }
    }
    return out;
}

void expectRoundTrip(
        const std::vector<uint8_t>& src,
        openzl_wasm_Profile profile)
{
    SCOPED_TRACE(openzl_wasm_profileName(profile));

    std::vector<uint8_t> compressor;
    const ZL_ErrorCode serCode = serializedCompressor(profile, &compressor);
    ASSERT_EQ(serCode, ZL_ErrorCode_no_error)
            << openzl_wasm_errorString(serCode);

    std::vector<uint8_t> frame;
    const ZL_ErrorCode compCode = compress(src, compressor, &frame);
    ASSERT_EQ(compCode, ZL_ErrorCode_no_error)
            << openzl_wasm_errorString(compCode);

    if (!src.empty()) {
        EXPECT_LT(frame.size(), src.size());
    }
    std::vector<uint8_t> dec;
    const ZL_ErrorCode decCode = decompress(frame, &dec);
    ASSERT_EQ(decCode, ZL_ErrorCode_no_error)
            << openzl_wasm_errorString(decCode);
    EXPECT_EQ(dec, src);
}

} // namespace

TEST(WasmBindingTest, SerialRoundTrip)
{
    expectRoundTrip(makeSerialData(4096), OPENZL_WASM_PROFILE_SERIAL);
}

TEST(WasmBindingTest, UnsignedIntRoundTrip)
{
    expectRoundTrip(makeIntData(1, 1024, false), OPENZL_WASM_PROFILE_U8);
    expectRoundTrip(makeIntData(2, 1024, false), OPENZL_WASM_PROFILE_U16);
    expectRoundTrip(makeIntData(4, 1024, false), OPENZL_WASM_PROFILE_U32);
    expectRoundTrip(makeIntData(8, 1024, false), OPENZL_WASM_PROFILE_U64);
}

TEST(WasmBindingTest, SignedIntRoundTrip)
{
    expectRoundTrip(makeIntData(1, 1024, true), OPENZL_WASM_PROFILE_I8);
    expectRoundTrip(makeIntData(2, 1024, true), OPENZL_WASM_PROFILE_I16);
    expectRoundTrip(makeIntData(4, 1024, true), OPENZL_WASM_PROFILE_I32);
    expectRoundTrip(makeIntData(8, 1024, true), OPENZL_WASM_PROFILE_I64);
}

TEST(WasmBindingTest, EmptyRoundTrip)
{
    expectRoundTrip({}, OPENZL_WASM_PROFILE_SERIAL);
}

TEST(WasmBindingTest, BenchmarkTimesBothDirections)
{
    const std::vector<uint8_t> src = makeSerialData(4096);
    std::vector<uint8_t> compressor;
    ASSERT_EQ(
            serializedCompressor(OPENZL_WASM_PROFILE_SERIAL, &compressor),
            ZL_ErrorCode_no_error);

    const BenchCompress bench = benchmarkCompress(src, compressor, 2);
    ASSERT_EQ(bench.code, ZL_ErrorCode_no_error)
            << openzl_wasm_errorString(bench.code);
    EXPECT_GE(bench.ms, 0.0); // timing is too machine-dependent to bound

    // The frame it hands back must be a real one, so a caller timing both
    // directions needs no extra compression to get something to decompress.
    std::vector<uint8_t> frame;
    ASSERT_EQ(compress(src, compressor, &frame), ZL_ErrorCode_no_error);
    EXPECT_EQ(bench.frame, frame);

    std::vector<uint8_t> decompressed;
    ASSERT_EQ(decompress(bench.frame, &decompressed), ZL_ErrorCode_no_error);
    EXPECT_EQ(decompressed, src);

    const BenchDecompress d = benchmarkDecompress(bench.frame, 2);
    ASSERT_EQ(d.code, ZL_ErrorCode_no_error) << openzl_wasm_errorString(d.code);
    EXPECT_GE(d.ms, 0.0);
}

TEST(WasmBindingTest, BenchmarkRejectsOutOfRangeIterations)
{
    const std::vector<uint8_t> src = makeSerialData(1024);
    std::vector<uint8_t> compressor;
    ASSERT_EQ(
            serializedCompressor(OPENZL_WASM_PROFILE_SERIAL, &compressor),
            ZL_ErrorCode_no_error);

    // Out-of-range counts are rejected rather than clamped, so the count a
    // caller passes is always the count that ran. js/wasm_api.js clamps before
    // calling, so it never trips this.
    EXPECT_EQ(
            benchmarkCompress(src, compressor, 0).code,
            ZL_ErrorCode_parameter_invalid);
    EXPECT_EQ(
            benchmarkCompress(
                    src, compressor, OPENZL_WASM_BENCHMARK_MAX_ITERATIONS + 1)
                    .code,
            ZL_ErrorCode_parameter_invalid);
    EXPECT_EQ(
            benchmarkCompress(src, compressor, 1).code, ZL_ErrorCode_no_error);
    EXPECT_EQ(
            benchmarkCompress(
                    src, compressor, OPENZL_WASM_BENCHMARK_MAX_ITERATIONS)
                    .code,
            ZL_ErrorCode_no_error);

    // benchmarkDecompress carries its own copy of the check.
    std::vector<uint8_t> frame;
    ASSERT_EQ(compress(src, compressor, &frame), ZL_ErrorCode_no_error);
    EXPECT_EQ(
            benchmarkDecompress(frame, 0).code, ZL_ErrorCode_parameter_invalid);
    EXPECT_EQ(
            benchmarkDecompress(frame, OPENZL_WASM_BENCHMARK_MAX_ITERATIONS + 1)
                    .code,
            ZL_ErrorCode_parameter_invalid);
    EXPECT_EQ(benchmarkDecompress(frame, 1).code, ZL_ErrorCode_no_error);
    EXPECT_EQ(
            benchmarkDecompress(frame, OPENZL_WASM_BENCHMARK_MAX_ITERATIONS)
                    .code,
            ZL_ErrorCode_no_error);
}

TEST(WasmBindingTest, ExposesMaxBenchmarkIterations)
{
    EXPECT_EQ(
            openzl_wasm_maxBenchmarkIterations(),
            OPENZL_WASM_BENCHMARK_MAX_ITERATIONS);
}

TEST(WasmBindingTest, ProfileEnumMatchesTable)
{
    // The enum keys the profile table, and js/wasm_api.js validates its mirror
    // against that table when the module initializes.
    EXPECT_STREQ(openzl_wasm_profileName(OPENZL_WASM_PROFILE_SERIAL), "serial");
    EXPECT_STREQ(openzl_wasm_profileName(OPENZL_WASM_PROFILE_U8), "u8");
    EXPECT_STREQ(openzl_wasm_profileName(OPENZL_WASM_PROFILE_I8), "i8");
    EXPECT_STREQ(openzl_wasm_profileName(OPENZL_WASM_PROFILE_U16), "u16");
    EXPECT_STREQ(openzl_wasm_profileName(OPENZL_WASM_PROFILE_I16), "i16");
    EXPECT_STREQ(openzl_wasm_profileName(OPENZL_WASM_PROFILE_U32), "u32");
    EXPECT_STREQ(openzl_wasm_profileName(OPENZL_WASM_PROFILE_I32), "i32");
    EXPECT_STREQ(openzl_wasm_profileName(OPENZL_WASM_PROFILE_U64), "u64");
    EXPECT_STREQ(openzl_wasm_profileName(OPENZL_WASM_PROFILE_I64), "i64");

    // Every value must have a row, or the table has a value-initialized gap.
    for (int i = 0; i < OPENZL_WASM_PROFILE_COUNT; ++i) {
        SCOPED_TRACE(i);
        std::vector<uint8_t> compressor;
        const ZL_ErrorCode code = serializedCompressor(
                static_cast<openzl_wasm_Profile>(i), &compressor);
        EXPECT_EQ(code, ZL_ErrorCode_no_error) << openzl_wasm_errorString(code);
        EXPECT_FALSE(compressor.empty());
    }
}

TEST(WasmBindingTest, RejectsUnknownProfile)
{
    const auto bad =
            static_cast<openzl_wasm_Profile>(OPENZL_WASM_PROFILE_COUNT);
    std::vector<uint8_t> compressor;
    EXPECT_NE(serializedCompressor(bad, &compressor), ZL_ErrorCode_no_error);
    EXPECT_TRUE(compressor.empty());
    EXPECT_EQ(openzl_wasm_profileName(bad), nullptr);
}

TEST(WasmBindingTest, RejectsGarbage)
{
    const std::vector<uint8_t> garbage(64, 0xAB);
    const std::vector<uint8_t> src = makeSerialData(128);

    // A garbage compressor, and a garbage frame in each of the three calls
    // that read one. The helpers assert that no buffer comes back.
    std::vector<uint8_t> output;
    EXPECT_NE(compress(src, garbage, &output), ZL_ErrorCode_no_error);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(decompress(garbage, &output), ZL_ErrorCode_no_error);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(benchmarkCompress(src, garbage, 2).code, ZL_ErrorCode_no_error);
    EXPECT_NE(benchmarkDecompress(garbage, 2).code, ZL_ErrorCode_no_error);

    size_t size = 0;
    EXPECT_NE(
            openzl_wasm_getDecompressedSize(
                    garbage.data(), garbage.size(), &size),
            ZL_ErrorCode_no_error);
}
