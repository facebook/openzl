// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "custom_parsers/dependency_registration.h"
#include "custom_parsers/gguf/gguf_lexer.h"
#include "custom_parsers/gguf/gguf_parser.h"
#include "custom_parsers/gguf/tests/gguf_builder.h"
#include "openzl/cpp/CompressIntrospectionHooks.hpp"
#include "openzl/openzl.hpp"
#include "openzl/zl_output.h"
#include "openzl/zl_reflection.h"

namespace openzl::custom_parsers {
namespace {

using testing::buildGGUF;
using testing::GGUFTensorSpec;
using testing::GGUFWriter;
using testing::pseudoRandomBytes;

struct LexedToken {
    ZL_GGUFTokenType type;
    size_t offset;
    size_t size;
    std::string name;
    uint32_t ggmlType;
    std::vector<uint64_t> ne;

    bool operator==(const LexedToken&) const = default;
};

LexedToken headerToken(size_t size)
{
    return { ZL_GGUFTokenType_header, 0, size, "", 0, {} };
}

LexedToken gapToken(size_t offset, size_t size)
{
    return { ZL_GGUFTokenType_gap, offset, size, "", 0, {} };
}

LexedToken tensorToken(
        size_t offset,
        size_t size,
        std::string name,
        uint32_t ggmlType,
        std::vector<uint64_t> ne)
{
    return { ZL_GGUFTokenType_tensor, offset,   size,
             std::move(name),         ggmlType, std::move(ne) };
}

struct LexResult {
    ZL_ErrorCode error;
    std::vector<LexedToken> tokens;
};

LexResult lex(std::string_view file)
{
    const ZL_Report maxTokens =
            ZL_GGUF_lex(file.data(), file.size(), nullptr, 0, nullptr);
    if (ZL_isError(maxTokens)) {
        return { ZL_errorCode(maxTokens), {} };
    }
    std::vector<ZL_GGUFToken> out(ZL_validResult(maxTokens));
    const ZL_Report nbTokens = ZL_GGUF_lex(
            file.data(), file.size(), out.data(), out.size(), nullptr);
    if (ZL_isError(nbTokens)) {
        return { ZL_errorCode(nbTokens), {} };
    }
    LexResult result{ ZL_ErrorCode_no_error, {} };
    for (size_t i = 0; i < ZL_validResult(nbTokens); ++i) {
        const auto& t       = out[i];
        const size_t offset = size_t(t.ptr - file.data());
        if (t.type == ZL_GGUFTokenType_tensor) {
            result.tokens.push_back(tensorToken(
                    offset,
                    t.size,
                    std::string(t.name, t.nameSize),
                    t.ggmlType,
                    { t.ne, t.ne + t.ndims }));
        } else {
            result.tokens.push_back({ t.type, offset, t.size, "", 0, {} });
        }
    }
    return result;
}

TEST(GGUFLexerTest, ReturnsTensorsInDataOrder)
{
    // The data section holds a's block, padding, b's payload, then padding.
    const auto file = buildGGUF(
            {
                    { "b", ZL_GGUFType_f32, { 2 }, std::string(8, 'b'), 64 },
                    { "a", ZL_GGUFType_q8_0, { 32 }, std::string(64, 'a'), 0 },
            });
    const size_t dataStart                 = file.size() - 96;
    const std::vector<LexedToken> expected = {
        headerToken(dataStart),
        tensorToken(dataStart, 34, "a", ZL_GGUFType_q8_0, { 32 }),
        gapToken(dataStart + 34, 30),
        tensorToken(dataStart + 64, 8, "b", ZL_GGUFType_f32, { 2 }),
        gapToken(dataStart + 72, 24),
    };
    const auto result = lex(file);
    ASSERT_EQ(result.error, ZL_ErrorCode_no_error);
    EXPECT_EQ(result.tokens, expected);
}

TEST(GGUFLexerTest, AlignsTheDataSection)
{
    const std::vector<GGUFTensorSpec> tensors = {
        { "t", ZL_GGUFType_i8, { 3 }, "abc" },
    };
    const auto byDefault = buildGGUF(tensors);
    const auto custom    = buildGGUF(tensors, 256);

    const auto r32  = lex(byDefault);
    const auto r256 = lex(custom);

    ASSERT_EQ(r32.error, ZL_ErrorCode_no_error);
    ASSERT_EQ(r256.error, ZL_ErrorCode_no_error);
    ASSERT_FALSE(r32.tokens.empty());
    ASSERT_FALSE(r256.tokens.empty());
    EXPECT_EQ(r32.tokens[0].size, byDefault.size() - 3);
    EXPECT_EQ(r32.tokens[0].size % 32, 0u);
    EXPECT_EQ(r256.tokens[0].size, custom.size() - 3);
    EXPECT_EQ(r256.tokens[0].size % 256, 0u);
}

TEST(GGUFLexerTest, LeavesTensorsOfUnknownSizeToGaps)
{
    const auto file = buildGGUF(
            {
                    { "future", 99, { 8 }, std::string(8, 'x') },
                    { "partialBlock",
                      ZL_GGUFType_q8_0,
                      { 33 },
                      std::string(40, 'y') },
                    // sizes that wrap around to their payload sizes
                    { "manyElements",
                      ZL_GGUFType_i8,
                      { (1ull << 63) + 1, 2 },
                      "ab" },
                    { "manyBytes",
                      ZL_GGUFType_f32,
                      { (1ull << 62) + 1 },
                      "cdef" },
            });
    const size_t dataStart                 = file.size() - 132;
    const std::vector<LexedToken> expected = {
        headerToken(dataStart),
        gapToken(dataStart, 132),
    };
    const auto result = lex(file);
    ASSERT_EQ(result.error, ZL_ErrorCode_no_error);
    EXPECT_EQ(result.tokens, expected);
}

TEST(GGUFLexerTest, ReadsTensorsWithoutDimensionsAsOneElement)
{
    // One element of a quantized type does not fill a block.
    const auto file = buildGGUF(
            {
                    { "s", ZL_GGUFType_f32, {}, "abcd" },
                    { "q", ZL_GGUFType_q8_0, {}, std::string(34, 'q') },
            });
    const size_t dataStart                 = file.size() - 66;
    const std::vector<LexedToken> expected = {
        headerToken(dataStart),
        tensorToken(dataStart, 4, "s", ZL_GGUFType_f32, {}),
        gapToken(dataStart + 4, 62),
    };
    const auto result = lex(file);
    ASSERT_EQ(result.error, ZL_ErrorCode_no_error);
    EXPECT_EQ(result.tokens, expected);
}

TEST(GGUFLexerTest, ReadsTensorsOfFourDimensions)
{
    const size_t size = 2 * 3 * 4 * 34; // Q8_0 blocks of 32 elements
    const auto file   = buildGGUF(
            {
                    { "t",
                        ZL_GGUFType_q8_0,
                        { 32, 2, 3, 4 },
                        std::string(size, 't') },
            });
    const size_t dataStart                 = file.size() - size;
    const std::vector<LexedToken> expected = {
        headerToken(dataStart),
        tensorToken(dataStart, size, "t", ZL_GGUFType_q8_0, { 32, 2, 3, 4 }),
    };
    const auto result = lex(file);
    ASSERT_EQ(result.error, ZL_ErrorCode_no_error);
    EXPECT_EQ(result.tokens, expected);
}

TEST(GGUFLexerTest, CountsTokensWithoutReadingTensorInfos)
{
    // Overlapping tensors: only the full lexing checks the layout.
    const auto file = buildGGUF(
            {
                    { "a", ZL_GGUFType_i8, { 2 }, "xy", 0 },
                    { "b", ZL_GGUFType_i8, { 2 }, "z", 1 },
            });
    const ZL_Report maxTokens =
            ZL_GGUF_lex(file.data(), file.size(), nullptr, 0, nullptr);

    ASSERT_FALSE(ZL_isError(maxTokens));
    EXPECT_EQ(ZL_validResult(maxTokens), 6u); // header, 2 tensors and 3 gaps
    EXPECT_EQ(lex(file).error, ZL_ErrorCode_graph_parser_malformedInput);
}

TEST(GGUFLexerTest, RejectsTooManyTensors)
{
    GGUFWriter atCap;
    atCap.header(3, ZL_GGUF_MAX_TENSORS, 0);
    atCap.out.append(24 * ZL_GGUF_MAX_TENSORS, '\0');
    GGUFWriter capped;
    capped.header(3, ZL_GGUF_MAX_TENSORS + 1, 0);
    capped.out.append(24 * (ZL_GGUF_MAX_TENSORS + 1), '\0');
    GGUFWriter beyondInput;
    beyondInput.header(3, 1000, 0);
    beyondInput.out.append(100, '\0');

    EXPECT_FALSE(ZL_isError(ZL_GGUF_lex(
            atCap.out.data(), atCap.out.size(), nullptr, 0, nullptr)));
    EXPECT_EQ(lex(capped.out).error, ZL_ErrorCode_graph_parser_unhandledInput);
    EXPECT_EQ(
            lex(beyondInput.out).error,
            ZL_ErrorCode_graph_parser_malformedInput);
}

TEST(GGUFLexerTest, RejectsTooSmallTokenArrays)
{
    // The header, a, a gap, then b.
    const auto file = buildGGUF(
            {
                    { "a", ZL_GGUFType_i8, { 2 }, "xy" },
                    { "b", ZL_GGUFType_i8, { 2 }, "zw" },
            });
    // Fewer tokens than tensors, then than the tokens of the file.
    for (const size_t capacity : { 1u, 3u }) {
        std::vector<ZL_GGUFToken> tokens(capacity);
        const ZL_Report report = ZL_GGUF_lex(
                file.data(), file.size(), tokens.data(), capacity, nullptr);
        ASSERT_TRUE(ZL_isError(report)) << capacity;
        EXPECT_EQ(ZL_errorCode(report), ZL_ErrorCode_parameter_invalid)
                << capacity;
    }
    std::vector<ZL_GGUFToken> tokens(4);
    const ZL_Report report = ZL_GGUF_lex(
            file.data(), file.size(), tokens.data(), tokens.size(), nullptr);
    ASSERT_FALSE(ZL_isError(report));
    EXPECT_EQ(ZL_validResult(report), 4u);
}

TEST(GGUFLexerTest, AcceptsEmptyTensorsAtTheEndOfTheData)
{
    // An unnamed empty tensor where the data section ends.
    const auto file = buildGGUF(
            {
                    { "a", ZL_GGUFType_i8, { 2 }, "xy" },
                    { "", ZL_GGUFType_f32, { 0 }, "" },
            });
    const auto pastTheEnd = buildGGUF(
            {
                    { "a", ZL_GGUFType_i8, { 2 }, "xy" },
                    { "", ZL_GGUFType_f32, { 0 }, "", 33 },
            });
    const size_t dataStart                 = file.size() - 32;
    const std::vector<LexedToken> expected = {
        headerToken(dataStart),
        tensorToken(dataStart, 2, "a", ZL_GGUFType_i8, { 2 }),
        gapToken(dataStart + 2, 30),
    };

    const auto result = lex(file);

    ASSERT_EQ(result.error, ZL_ErrorCode_no_error);
    EXPECT_EQ(result.tokens, expected);
    EXPECT_EQ(lex(pastTheEnd).error, ZL_ErrorCode_graph_parser_malformedInput);
}

std::string withAlignment(uint32_t valueType, uint64_t value)
{
    GGUFWriter w;
    w.header(3, 0, 1);
    w.str("general.alignment");
    w.u32(valueType);
    if (valueType == 10) {
        w.u64(value);
    } else {
        w.u32((uint32_t)value);
    }
    return w.out;
}

/// A file whose metadata value is @p depth nested arrays.
std::string withNestedArrays(int depth)
{
    GGUFWriter w;
    w.header(3, 0, 1);
    w.str("k");
    w.u32(9); // array
    for (int d = 1; d < depth; ++d) {
        w.u32(9); // of one array
        w.u64(1);
    }
    w.u32(7); // of no bools
    w.u64(0);
    return w.out;
}

std::string withVersion(std::string file, uint32_t version)
{
    std::memcpy(file.data() + 4, &version, sizeof(version));
    return file;
}

TEST(GGUFLexerTest, ReadsFilesWithoutTensors)
{
    // Their data section can start past their end.
    const std::vector<std::string> files = {
        buildGGUF({}),
        withAlignment(4, 1u << 31),
    };
    for (const auto& file : files) {
        const std::vector<LexedToken> expected = { headerToken(file.size()) };

        const auto result = lex(file);

        ASSERT_EQ(result.error, ZL_ErrorCode_no_error);
        EXPECT_EQ(result.tokens, expected);
    }
}

TEST(GGUFLexerTest, LimitsTheNestingOfArrays)
{
    EXPECT_EQ(lex(withNestedArrays(8)).error, ZL_ErrorCode_no_error);
    EXPECT_EQ(
            lex(withNestedArrays(9)).error,
            ZL_ErrorCode_graph_parser_malformedInput);
}

TEST(GGUFLexerTest, ReadsVersion2LikeVersion3)
{
    const auto file = buildGGUF(
            {
                    { "t", ZL_GGUFType_f32, { 4 }, std::string(16, 't') },
            });

    const auto v3 = lex(file);
    const auto v2 = lex(withVersion(file, 2));

    ASSERT_EQ(v3.error, ZL_ErrorCode_no_error);
    ASSERT_EQ(v2.error, ZL_ErrorCode_no_error);
    EXPECT_EQ(v2.tokens, v3.tokens);
}

TEST(GGUFLexerTest, RejectsMalformedOrUnsupportedHeaders)
{
    const auto valid = buildGGUF(
            {
                    { "t", ZL_GGUFType_f32, { 4 }, std::string(16, 't') },
            });
    GGUFWriter badArray;
    badArray.header(3, 0, 1);
    badArray.str("k");
    badArray.u32(9);  // array
    badArray.u32(42); // of an invalid type
    badArray.u64(1);
    GGUFWriter badValue;
    badValue.header(3, 0, 1);
    badValue.str("k");
    badValue.u32(13); // invalid type
    GGUFWriter hugeAlignment;
    hugeAlignment.header(3, 1, 1);
    hugeAlignment.str("general.alignment");
    hugeAlignment.u32(4); // u32
    hugeAlignment.u32(1u << 31);
    hugeAlignment.str("t");
    hugeAlignment.u32(1);
    hugeAlignment.u64(1);
    hugeAlignment.u32(ZL_GGUFType_i8);
    hugeAlignment.u64(0);
    hugeAlignment.out += 'x';
    GGUFWriter duplicateAlignment;
    duplicateAlignment.header(3, 0, 2);
    for (int i = 0; i < 2; ++i) {
        duplicateAlignment.str("general.alignment");
        duplicateAlignment.u32(4); // u32
        duplicateAlignment.u32(32);
    }
    GGUFWriter wrappingArray;
    wrappingArray.header(3, 0, 1);
    wrappingArray.str("k");
    wrappingArray.u32(9);          // array
    wrappingArray.u32(10);         // of u64
    wrappingArray.u64(1ull << 61); // whose byte size wraps around to 0
    GGUFWriter tooManyDims;
    tooManyDims.header(3, 1, 0);
    tooManyDims.str("t");
    tooManyDims.u32(5);
    for (int d = 0; d < 6; ++d) {
        tooManyDims.u64(1);
    }

    const std::vector<std::pair<std::string, std::string>> malformed = {
        { "bad magic", "GGUX" + valid.substr(4) },
        { "truncated metadata", valid.substr(0, 60) },
        { "zero alignment", withAlignment(4, 0) },
        { "alignment not a power of 2", withAlignment(4, 48) },
        { "alignment of the wrong type", withAlignment(10, 64) },
        { "duplicate alignment", duplicateAlignment.out },
        { "array of an invalid type", badArray.out },
        { "value of an invalid type", badValue.out },
        { "array size wrapping around", wrappingArray.out },
        { "data section past the end", hugeAlignment.out },
        { "too many dimensions", tooManyDims.out },
        { "missing data", valid.substr(0, valid.size() - 1) },
        { "out of bounds",
          buildGGUF({ { "t", ZL_GGUFType_i8, { 4 }, "abcd", 1 } }) },
    };
    for (const auto& [what, file] : malformed) {
        EXPECT_EQ(lex(file).error, ZL_ErrorCode_graph_parser_malformedInput)
                << what;
    }
    // Version 1 has 32-bit counts; version 3 can be big-endian.
    for (const uint32_t version : { 1u, 0x03000000u }) {
        EXPECT_EQ(
                lex(withVersion(valid, version)).error,
                ZL_ErrorCode_graph_parser_unhandledInput)
                << version;
    }
}

/// Bytes of @p nbBlocks quantized blocks of @p type.
std::string blocksOf(uint32_t type, size_t nbBlocks, uint32_t seed)
{
    size_t blockElts = 0, blockBytes = 0;
    EXPECT_TRUE(ZL_GGUF_typeLayout(type, &blockElts, &blockBytes));
    return pseudoRandomBytes(nbBlocks * blockBytes, seed);
}

std::vector<GGUFTensorSpec> mixedTypeTensors()
{
    return {
        { "token_embd.weight",
          ZL_GGUFType_q6_k,
          { 512, 4 },
          blocksOf(ZL_GGUFType_q6_k, 8, 1) },
        { "blk.0.attn_norm.weight",
          ZL_GGUFType_f32,
          { 512 },
          pseudoRandomBytes(2048, 2) },
        { "blk.0.attn_q.weight",
          ZL_GGUFType_q4_k,
          { 512, 8 },
          blocksOf(ZL_GGUFType_q4_k, 16, 3) },
        { "blk.0.ffn_down.weight",
          ZL_GGUFType_q8_0,
          { 64, 40 },
          blocksOf(ZL_GGUFType_q8_0, 80, 4) },
        { "blk.0.ffn_up.weight",
          ZL_GGUFType_q5_0,
          { 64, 40 },
          blocksOf(ZL_GGUFType_q5_0, 80, 5) },
        { "blk.0.empty", ZL_GGUFType_f32, { 0, 4 }, "" },
        { "blk.1.w", ZL_GGUFType_f16, { 100, 30 }, pseudoRandomBytes(6000, 6) },
        { "blk.1.b", ZL_GGUFType_bf16, { 3000 }, pseudoRandomBytes(6000, 7) },
        { "blk.1.i", ZL_GGUFType_i32, { 30 }, pseudoRandomBytes(120, 8) },
        { "blk.1.q8k",
          ZL_GGUFType_q8_k,
          { 256, 2 },
          blocksOf(ZL_GGUFType_q8_k, 2, 9) },
        { "future", 99, { 10 }, pseudoRandomBytes(10, 10) },
        { "partialBlock", ZL_GGUFType_q4_0, { 40 }, pseudoRandomBytes(23, 11) },
    };
}

class GGUFCompressorTest : public ::testing::Test {
   protected:
    void useChunkSize(size_t chunkSize)
    {
        const auto gid = ZL_GGUF_registerGraph(compressor_.get(), chunkSize);
        ASSERT_FALSE(ZL_RES_isError(gid));
        compressor_.selectStartingGraph(ZL_RES_value(gid));
    }

    void attachHooks(CompressIntrospectionHooks& hooks)
    {
        openzl::unwrap(
                ZL_CCtx_attachIntrospectionHooks(
                        cctx_.get(), hooks.getRawHooks()),
                "Failed to attach introspection hooks",
                cctx_.get());
    }

    /// @returns the compressed size
    size_t expectRoundTrip(std::string_view input)
    {
        cctx_.setParameter(CParam::FormatVersion, formatVersion_);
        cctx_.refCompressor(compressor_);
        const auto compressed = cctx_.compressSerial(input);
        EXPECT_EQ(dctx_.decompressSerial(compressed), input);
        return compressed.size();
    }

    Compressor compressor_;
    CCtx cctx_;
    DCtx dctx_;
    int formatVersion_ = ZL_MAX_FORMAT_VERSION;
};

TEST_F(GGUFCompressorTest, RoundTripsMixedTypes)
{
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);
    expectRoundTrip(buildGGUF(mixedTypeTensors()));
}

TEST_F(GGUFCompressorTest, RoundTripsWithChunksSmallerThanTensors)
{
    auto tensors = mixedTypeTensors();
    tensors.push_back(
            { "blk.2.big",
              ZL_GGUFType_q8_0,
              { 4096, 64 },
              blocksOf(ZL_GGUFType_q8_0, 8192, 12) });
    useChunkSize(2 * ZL_MIN_CHUNK_SIZE);
    expectRoundTrip(buildGGUF(tensors));
}

TEST_F(GGUFCompressorTest, RoundTripsInvalidInputs)
{
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);
    const auto valid = buildGGUF(mixedTypeTensors());
    expectRoundTrip("");
    expectRoundTrip("GGUF");
    expectRoundTrip(pseudoRandomBytes(10000, 7));
    expectRoundTrip(valid.substr(0, valid.size() / 2));
}

TEST_F(GGUFCompressorTest, ReportsWhyInvalidInputsAreNotParsed)
{
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);
    const auto valid = buildGGUF(mixedTypeTensors());

    expectRoundTrip(valid.substr(0, valid.size() / 2));

    const auto warnings = get_warning_strings(cctx_);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(
            warnings[0].second.find("tensor data out of bounds"),
            std::string::npos)
            << warnings[0].second;
}

TEST_F(GGUFCompressorTest, FallsBackOnFormatVersionsWithoutChunks)
{
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);
    formatVersion_ = ZL_CHUNK_VERSION_MIN - 1;

    expectRoundTrip(buildGGUF(mixedTypeTensors()));

    const auto warnings = get_warning_strings(cctx_);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(
            warnings[0].second.find("format version without chunks"),
            std::string::npos)
            << warnings[0].second;
}

TEST_F(GGUFCompressorTest, ValidInputsProduceNoWarning)
{
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);

    expectRoundTrip(buildGGUF(mixedTypeTensors()));

    EXPECT_TRUE(get_warning_strings(cctx_).empty());
}

TEST_F(GGUFCompressorTest, CompressesManyTinyTensors)
{
    // One stream per tensor would exceed the stream limit of a chunk.
    const size_t nbTensors = 20000;
    std::vector<GGUFTensorSpec> tensors;
    tensors.reserve(nbTensors);
    for (size_t i = 0; i < nbTensors; ++i) {
        tensors.push_back(
                { "t" + std::to_string(i),
                  i % 2 ? ZL_GGUFType_f32 : ZL_GGUFType_q8_0,
                  { i % 2 ? 8u : 32u },
                  pseudoRandomBytes(i % 2 ? 32 : 34, (uint32_t)i) });
    }
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);

    expectRoundTrip(buildGGUF(tensors));
}

/// Records the size of each chunk handed to the inner parser graph.
class ChunkRecorder : public CompressIntrospectionHooks {
   public:
    void on_migraphEncode_start(
            ZL_Graph*,
            const ZL_Compressor* compressor,
            ZL_GraphID gid,
            ZL_Edge* inputs[],
            size_t nbInputs) override
    {
        const std::string_view name =
                ZL_Compressor_Graph_getName(compressor, gid);
        if (nbInputs == 1 && name.starts_with("GGUF Parser")) {
            chunkSizes.push_back(
                    ZL_Input_contentSize(ZL_Edge_getData(inputs[0])));
        }
    }

    std::vector<size_t> chunkSizes;
};

TEST_F(GGUFCompressorTest, SplitsLargeQuantizedTensorsOnRowBoundaries)
{
    const size_t rowBytes = 8 * 34; // 256 elements of Q8_0
    const size_t nbRows   = 1000;
    const auto file       = buildGGUF(
            { { "w",
                      ZL_GGUFType_q8_0,
                      { 256, nbRows },
                      blocksOf(ZL_GGUFType_q8_0, 8 * nbRows, 1) } });
    const size_t headerBytes = file.size() - nbRows * rowBytes;
    const size_t chunkSize   = 2 * ZL_MIN_CHUNK_SIZE;
    useChunkSize(chunkSize);
    ChunkRecorder recorder;
    attachHooks(recorder);

    expectRoundTrip(file);

    ASSERT_GT(recorder.chunkSizes.size(), 2u);
    EXPECT_EQ((recorder.chunkSizes[0] - headerBytes) % rowBytes, 0u);
    size_t total = 0;
    for (size_t i = 0; i < recorder.chunkSizes.size(); ++i) {
        EXPECT_LE(recorder.chunkSizes[i], chunkSize);
        EXPECT_EQ(i == 0 ? 0 : recorder.chunkSizes[i] % rowBytes, 0u);
        total += recorder.chunkSizes[i];
    }
    EXPECT_EQ(total, file.size());
}

/// Records the element width of the outputs of each split_by_struct codec, and
/// the first graph or codec each output reaches.
class StructSplitRecorder : public CompressIntrospectionHooks {
   public:
    void on_codecEncode_start(
            ZL_Encoder*,
            const ZL_Compressor* compressor,
            ZL_NodeID nid,
            const ZL_Input* inputs[],
            size_t nbInputs) override
    {
        const std::string_view name =
                ZL_Compressor_Node_getName(compressor, nid);
        // Type conversions inserted before a codec keep the same data.
        for (size_t i = 0; i < nbInputs && !name.starts_with("zl.convert");
             ++i) {
            route(ZL_Input_ptr(inputs[i]), name);
        }
        inSplit_ = name.starts_with("zl.split_by_struct");
    }

    void on_codecEncode_end(
            ZL_Encoder*,
            const ZL_Output* outStreams[],
            size_t nbOutputs,
            ZL_Report) override
    {
        if (!inSplit_) {
            return;
        }
        std::vector<size_t> widths;
        widths.reserve(nbOutputs);
        for (size_t i = 0; i < nbOutputs; ++i) {
            widths.push_back(ZL_validResult(ZL_Output_eltWidth(outStreams[i])));
            fields_.push_back(ZL_Output_constPtr(outStreams[i]));
            fieldRoutes.emplace_back();
        }
        fieldWidths.push_back(widths);
        inSplit_ = false;
    }

    void on_migraphEncode_start(
            ZL_Graph*,
            const ZL_Compressor* compressor,
            ZL_GraphID gid,
            ZL_Edge* inputs[],
            size_t nbInputs) override
    {
        for (size_t i = 0; i < nbInputs; ++i) {
            route(ZL_Input_ptr(ZL_Edge_getData(inputs[i])),
                  ZL_Compressor_Graph_getName(compressor, gid));
        }
    }

    std::vector<std::vector<size_t>> fieldWidths;
    std::vector<std::string> fieldRoutes;

   private:
    void route(const void* data, std::string_view name)
    {
        for (size_t i = 0; i < fields_.size(); ++i) {
            if (fields_[i] == data && fieldRoutes[i].empty()) {
                fieldRoutes[i] = name;
            }
        }
    }

    std::vector<const void*> fields_;
    bool inSplit_ = false;
};

enum class FieldKind {
    quants, ///< quantized values or sub-block scales
    f16,    ///< a float16 scale
    f8,     ///< 8-bit float scales
};

struct BlockField {
    size_t size;
    FieldKind kind;
};

/// Block layouts of ggml (ggml-common.h), whose ggml_half fields are float16
/// scales.
std::vector<std::pair<uint32_t, std::vector<BlockField>>> blockLayouts()
{
    constexpr auto q   = FieldKind::quants;
    constexpr auto f16 = FieldKind::f16;
    constexpr auto f8  = FieldKind::f8;
    return {
        { ZL_GGUFType_q4_0, { { 2, f16 }, { 16, q } } },
        { ZL_GGUFType_q4_1, { { 2, f16 }, { 2, f16 }, { 16, q } } },
        { ZL_GGUFType_q5_0, { { 2, f16 }, { 4, q }, { 16, q } } },
        { ZL_GGUFType_q5_1, { { 2, f16 }, { 2, f16 }, { 4, q }, { 16, q } } },
        { ZL_GGUFType_q8_0, { { 2, f16 }, { 32, q } } },
        { ZL_GGUFType_q2_k, { { 16, q }, { 64, q }, { 2, f16 }, { 2, f16 } } },
        { ZL_GGUFType_q3_k, { { 32, q }, { 64, q }, { 12, q }, { 2, f16 } } },
        { ZL_GGUFType_q4_k, { { 2, f16 }, { 2, f16 }, { 12, q }, { 128, q } } },
        { ZL_GGUFType_q5_k,
          { { 2, f16 }, { 2, f16 }, { 12, q }, { 32, q }, { 128, q } } },
        { ZL_GGUFType_q6_k, { { 128, q }, { 64, q }, { 16, q }, { 2, f16 } } },
        { ZL_GGUFType_iq2_xxs, { { 2, f16 }, { 64, q } } },
        { ZL_GGUFType_iq2_xs, { { 2, f16 }, { 64, q }, { 8, q } } },
        { ZL_GGUFType_iq3_xxs, { { 2, f16 }, { 64, q }, { 32, q } } },
        { ZL_GGUFType_iq1_s, { { 2, f16 }, { 32, q }, { 16, q } } },
        { ZL_GGUFType_iq4_nl, { { 2, f16 }, { 16, q } } },
        { ZL_GGUFType_iq3_s,
          { { 2, f16 }, { 64, q }, { 8, q }, { 32, q }, { 4, q } } },
        { ZL_GGUFType_iq2_s,
          { { 2, f16 }, { 32, q }, { 32, q }, { 8, q }, { 8, q } } },
        { ZL_GGUFType_iq4_xs, { { 2, f16 }, { 2, q }, { 4, q }, { 128, q } } },
        { ZL_GGUFType_iq1_m, { { 32, q }, { 16, q }, { 8, q } } },
        { ZL_GGUFType_tq1_0, { { 48, q }, { 4, q }, { 2, f16 } } },
        { ZL_GGUFType_tq2_0, { { 64, q }, { 2, f16 } } },
        { ZL_GGUFType_mxfp4, { { 1, f8 }, { 16, q } } },
        { ZL_GGUFType_nvfp4, { { 4, f8 }, { 32, q } } },
        { ZL_GGUFType_q1_0, { { 2, f16 }, { 16, q } } },
        { ZL_GGUFType_q2_0, { { 2, f16 }, { 16, q } } },
    };
}

/// A tensor of 64 blocks of @p type.
std::string fileOfBlocks(uint32_t type)
{
    size_t blockElts = 0, blockBytes = 0;
    EXPECT_TRUE(ZL_GGUF_typeLayout(type, &blockElts, &blockBytes));
    return buildGGUF(
            { { "w", type, { blockElts * 64 }, blocksOf(type, 64, type) } });
}

TEST_F(GGUFCompressorTest, SplitsQuantizedBlocksIntoTheirFields)
{
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);
    for (const auto& [type, fields] : blockLayouts()) {
        StructSplitRecorder recorder;
        attachHooks(recorder);

        expectRoundTrip(fileOfBlocks(type));

        std::vector<size_t> widths;
        for (const auto& field : fields) {
            widths.push_back(field.size);
        }
        const std::vector<std::vector<size_t>> expected = { widths };
        EXPECT_EQ(recorder.fieldWidths, expected) << "type " << type;
        size_t blockElts = 0, blockBytes = 0;
        ASSERT_TRUE(ZL_GGUF_typeLayout(type, &blockElts, &blockBytes));
        EXPECT_EQ(
                std::accumulate(widths.begin(), widths.end(), size_t{ 0 }),
                blockBytes)
                << "type " << type;
    }
}

TEST_F(GGUFCompressorTest, RoutesEachFieldToTheGraphOfItsKind)
{
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);
    const auto graphName = [this](ZL_GraphID graph) {
        return std::string(
                ZL_Compressor_Graph_getName(compressor_.get(), graph));
    };
    const auto scale16 = compressor_.getGraph("Tensor Scale16");
    ASSERT_TRUE(scale16.has_value());
    const auto graphOf = [&](FieldKind kind) {
        switch (kind) {
            case FieldKind::f16:
                return graphName(*scale16);
            case FieldKind::f8:
                return graphName(ZL_GRAPH_FSE);
            case FieldKind::quants:
            default:
                return graphName(ZL_GRAPH_ZSTD);
        }
    };
    for (const auto& [type, fields] : blockLayouts()) {
        StructSplitRecorder recorder;
        attachHooks(recorder);

        expectRoundTrip(fileOfBlocks(type));

        std::vector<std::string> expected;
        for (const auto& field : fields) {
            expected.push_back(graphOf(field.kind));
        }
        EXPECT_EQ(recorder.fieldRoutes, expected) << "type " << type;
    }
}

/// A TQ2_0 tensor of rows of @p rowBlocks blocks, whose quantized values are
/// zero and whose blocks have the scales @p scales.
std::string ternaryFileWithScales(
        const std::vector<uint16_t>& scales,
        size_t rowBlocks)
{
    size_t blockElts = 0, blockBytes = 0;
    EXPECT_TRUE(ZL_GGUF_typeLayout(ZL_GGUFType_tq2_0, &blockElts, &blockBytes));
    std::string blocks(scales.size() * blockBytes, '\0');
    for (size_t i = 0; i < scales.size(); ++i) {
        // TQ2_0 blocks end with their scale.
        char* const end = &blocks[(i + 1) * blockBytes];
        end[-2]         = (char)(scales[i] & 0xFF);
        end[-1]         = (char)(scales[i] >> 8);
    }
    return buildGGUF(
            { { "w",
                ZL_GGUFType_tq2_0,
                { blockElts * rowBlocks, scales.size() / rowBlocks },
                blocks } });
}

/// @p nbScales pseudo-random scales taking @p nbValues values.
std::vector<uint16_t>
pseudoRandomScales(size_t nbScales, uint32_t nbValues, uint32_t seed)
{
    std::vector<uint16_t> scales;
    scales.reserve(nbScales);
    uint32_t state = seed;
    for (size_t i = 0; i < nbScales; ++i) {
        state = state * 1664525 + 1013904223;
        scales.push_back((uint16_t)(0x1000 + (state >> 8) % nbValues));
    }
    return scales;
}

constexpr size_t kRowBlocks = 256;

/// One scale per row, for @p nbRows rows.
std::vector<uint16_t> scalesSharedByRows(size_t nbRows)
{
    std::vector<uint16_t> scales;
    for (size_t r = 0; r < nbRows; ++r) {
        scales.insert(scales.end(), kRowBlocks, (uint16_t)(0x2000 + 7 * r));
    }
    return scales;
}

/// The same pseudo-random row of scales, @p nbRows times.
std::vector<uint16_t> scalesRepeatingEveryRow(size_t nbRows)
{
    const auto row = pseudoRandomScales(kRowBlocks, 4096, 1);
    std::vector<uint16_t> scales;
    for (size_t r = 0; r < nbRows; ++r) {
        scales.insert(scales.end(), row.begin(), row.end());
    }
    return scales;
}

/// @p nbScales pseudo-random scales taking 16 values from 0x1c00 to 0x1e2b,
/// with pseudo-random signs if @p withSigns.
std::vector<uint16_t>
largeScales(size_t nbScales, bool withSigns, uint32_t seed)
{
    auto scales = pseudoRandomScales(nbScales, withSigns ? 32 : 16, seed);
    for (auto& scale : scales) {
        const uint32_t k = scale - 0x1000u;
        scale = (uint16_t)((k & 16) << 11 | (0x1c00 + 37 * (k & 15)));
    }
    return scales;
}

/// @p nbScales scales, each pseudo-random one twice in a row.
std::vector<uint16_t> duplicatedScales(size_t nbScales)
{
    std::vector<uint16_t> scales;
    for (uint16_t scale : pseudoRandomScales(nbScales / 2, 4096, 2)) {
        scales.insert(scales.end(), 2, scale);
    }
    return scales;
}

TEST_F(GGUFCompressorTest, CodesScalesSharedByARowInLessThanOneBitEach)
{
    // Ternary models share a scale per row or per tensor, which 16-bit
    // Huffman cannot code in less than one bit per scale.
    const auto scales = scalesSharedByRows(128);
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);

    const size_t compressedSize =
            expectRoundTrip(ternaryFileWithScales(scales, kRowBlocks));

    EXPECT_LT(compressedSize, scales.size() / 8);
}

TEST_F(GGUFCompressorTest, CodesScalesRepeatingEveryRowInLessThanOneBitEach)
{
    // No scale repeats the previous one: only the period of a row shows.
    const auto scales = scalesRepeatingEveryRow(400);
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);

    const size_t compressedSize =
            expectRoundTrip(ternaryFileWithScales(scales, kRowBlocks));

    EXPECT_LT(compressedSize, scales.size() / 8);
}

TEST_F(GGUFCompressorTest, CodesDuplicatedScalesInLessThanUnduplicatedOnes)
{
    // Scales duplicated by a conversion from groups twice larger take many
    // values: repeating them only pays off over the whole tensor.
    const size_t nbScales = 400 * kRowBlocks;
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);

    const size_t duplicatedSize = expectRoundTrip(
            ternaryFileWithScales(duplicatedScales(nbScales), kRowBlocks));
    const size_t unduplicatedSize = expectRoundTrip(ternaryFileWithScales(
            pseudoRandomScales(nbScales, 4096, 3), kRowBlocks));

    EXPECT_LT(duplicatedSize, unduplicatedSize * 4 / 5);
}

TEST_F(GGUFCompressorTest, CodesScalesTakingFewValuesInLessThanTwoBitsEach)
{
    // Few values repeat often by chance alone, where zstd takes about 3 bits
    // per scale.
    const auto scales = pseudoRandomScales(400 * kRowBlocks, 3, 4);
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);

    const size_t compressedSize =
            expectRoundTrip(ternaryFileWithScales(scales, kRowBlocks));

    EXPECT_LT(compressedSize, scales.size() * 2 / 8);
}

/// Compressed size of float16 @p scales, as split from their blocks, coded by
/// @p graph of @p compressor.
size_t codedScalesSize(
        Compressor& compressor,
        ZL_GraphID graph,
        const std::vector<uint16_t>& scales)
{
    compressor.selectStartingGraph(graph);
    CCtx cctx;
    cctx.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    cctx.refCompressor(compressor);
    return cctx.compressOne(Input::refStruct(scales.data(), scales.size()))
            .size();
}

TEST_F(GGUFCompressorTest, ScaleSelectorPicksTheSmallestCandidate)
{
    // The candidates of the selector, each the smallest on some patterns.
    Compressor candidates;
    const size_t byteSizes[2]      = { 1, 1 };
    const ZL_GraphID byteGraphs[2] = { ZL_GRAPH_FSE, ZL_GRAPH_FSE };
    const ZL_GraphID graphs[]      = {
        ZL_GRAPH_HUFFMAN,
        ZL_Compressor_registerSplitByStructGraph(
                candidates.get(), byteSizes, byteGraphs, 2),
        ZL_Compressor_registerZstdGraph_withLevel(candidates.get(), 3),
    };
    const size_t nbScales = 400 * kRowBlocks;
    const std::vector<std::pair<std::string, std::vector<uint16_t>>>
            patterns = {
                { "shared by a row", scalesSharedByRows(128) },
                { "repeating every row", scalesRepeatingEveryRow(400) },
                { "duplicated", duplicatedScales(nbScales) },
                { "many values for their number",
                  pseudoRandomScales(20000, 4096, 5) },
                { "3 values", pseudoRandomScales(nbScales, 3, 4) },
                { "300 values", pseudoRandomScales(nbScales, 300, 6) },
                { "constant", std::vector<uint16_t>(nbScales, 0x3c00) },
                // Huffman bitpacks or stores small fields of large scales.
                { "few large values", largeScales(1000, false, 8) },
                { "few signed values", largeScales(4000, true, 9) },
            };
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);
    const auto selector = compressor_.getGraph("Tensor Scale16");
    ASSERT_TRUE(selector.has_value());

    for (const auto& [what, scales] : patterns) {
        size_t smallest = SIZE_MAX;
        for (const ZL_GraphID graph : graphs) {
            smallest = std::min(
                    smallest, codedScalesSize(candidates, graph, scales));
        }
        EXPECT_EQ(codedScalesSize(compressor_, *selector, scales), smallest)
                << what;
    }
}

TEST_F(GGUFCompressorTest, ScaleSelectorRejectsOtherCandidates)
{
    // A serialized compressor can give the selector other candidates.
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);
    const auto selector = compressor_.getGraph("Tensor Scale16");
    ASSERT_TRUE(selector.has_value());
    compressor_.selectStartingGraph(compressor_.parameterizeGraph(
            *selector, { .customGraphs = { { ZL_GRAPH_STORE } } }));
    cctx_.setParameter(CParam::FormatVersion, formatVersion_);
    cctx_.refCompressor(compressor_);

    EXPECT_THROW(cctx_.compressSerial(pseudoRandomBytes(4096, 1)), Exception);
}

TEST_F(GGUFCompressorTest, ScaleSelectorCodesInputsOtherThan16BitScales)
{
    // A custom compressor can give the selector any serial or struct input.
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);
    const auto selector = compressor_.getGraph("Tensor Scale16");
    ASSERT_TRUE(selector.has_value());
    compressor_.selectStartingGraph(*selector);
    const auto scales = pseudoRandomScales(100000, 200, 7);
    const std::vector<uint32_t> wideScales(scales.begin(), scales.end());
    const auto wideInput =
            Input::refStruct(wideScales.data(), wideScales.size());

    expectRoundTrip(
            std::string_view(
                    reinterpret_cast<const char*>(scales.data()),
                    2 * scales.size() - 1));
    cctx_.setParameter(CParam::FormatVersion, formatVersion_);
    cctx_.refCompressor(compressor_);
    EXPECT_TRUE(dctx_.decompressOne(cctx_.compressOne(wideInput)) == wideInput);
}

TEST_F(GGUFCompressorTest, SerializedCompressorCanBeReloaded)
{
    // Shifts the names of the graphs registered after it, unless anchored.
    ZL_Compressor_registerZstdGraph_withLevel(compressor_.get(), 5);
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);
    // Debug builds log the dependencies left unresolved.
    ::testing::internal::CaptureStderr();
    const auto reloaded =
            createCompressorFromSerialized(compressor_.serialize(), "");
    const std::string log = ::testing::internal::GetCapturedStderr();
    CCtx cctx;
    cctx.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    cctx.refCompressor(*reloaded);
    const auto input = buildGGUF(mixedTypeTensors());
    EXPECT_EQ(dctx_.decompressSerial(cctx.compressSerial(input)), input);
    EXPECT_EQ(log.find("unresolved"), std::string::npos) << log;
}

} // namespace
} // namespace openzl::custom_parsers
