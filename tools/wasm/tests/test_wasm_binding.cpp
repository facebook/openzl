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

TEST(WasmBindingTest, ProfileEnumMatchesTable)
{
    // The enum keys the profile table, and js/wasm_api.js mirrors the same
    // numbering with nothing checking it. If this fails, the JS probably needs
    // the same edit.
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
