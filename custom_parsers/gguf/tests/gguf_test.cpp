// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstring>
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
            });
    const size_t dataStart                 = file.size() - 72;
    const std::vector<LexedToken> expected = {
        headerToken(dataStart),
        gapToken(dataStart, 72),
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
    GGUFWriter capped;
    capped.header(3, ZL_GGUF_MAX_TENSORS + 1, 0);
    capped.out.append(24 * (ZL_GGUF_MAX_TENSORS + 1), '\0');
    GGUFWriter beyondInput;
    beyondInput.header(3, 1000, 0);
    beyondInput.out.append(100, '\0');

    EXPECT_EQ(lex(capped.out).error, ZL_ErrorCode_graph_parser_unhandledInput);
    EXPECT_EQ(
            lex(beyondInput.out).error,
            ZL_ErrorCode_graph_parser_malformedInput);
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

std::string withVersion(std::string file, uint32_t version)
{
    std::memcpy(file.data() + 4, &version, sizeof(version));
    return file;
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
        { "array of an invalid type", badArray.out },
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
        { "blk.1.iq",
          ZL_GGUFType_iq4_xs,
          { 256, 2 },
          blocksOf(ZL_GGUFType_iq4_xs, 2, 9) },
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

/// Records the element width of the outputs of each split_by_struct codec.
class StructSplitRecorder : public CompressIntrospectionHooks {
   public:
    void on_codecEncode_start(
            ZL_Encoder*,
            const ZL_Compressor* compressor,
            ZL_NodeID nid,
            const ZL_Input*[],
            size_t) override
    {
        const std::string_view name =
                ZL_Compressor_Node_getName(compressor, nid);
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
        }
        fieldWidths.push_back(widths);
        inSplit_ = false;
    }

    std::vector<std::vector<size_t>> fieldWidths;

   private:
    bool inSplit_ = false;
};

TEST_F(GGUFCompressorTest, SplitsQuantizedBlocksIntoTheirFields)
{
    // Block layouts of ggml (ggml-common.h).
    const std::vector<std::pair<uint32_t, std::vector<size_t>>> layouts = {
        { ZL_GGUFType_q4_0, { 2, 16 } },
        { ZL_GGUFType_q4_1, { 2, 2, 16 } },
        { ZL_GGUFType_q5_0, { 2, 4, 16 } },
        { ZL_GGUFType_q5_1, { 2, 2, 4, 16 } },
        { ZL_GGUFType_q8_0, { 2, 32 } },
        { ZL_GGUFType_q2_k, { 16, 64, 2, 2 } },
        { ZL_GGUFType_q3_k, { 32, 64, 12, 2 } },
        { ZL_GGUFType_q4_k, { 2, 2, 12, 128 } },
        { ZL_GGUFType_q5_k, { 2, 2, 12, 32, 128 } },
        { ZL_GGUFType_q6_k, { 128, 64, 16, 2 } },
    };
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);
    for (const auto& [type, fields] : layouts) {
        size_t blockElts = 0, blockBytes = 0;
        ASSERT_TRUE(ZL_GGUF_typeLayout(type, &blockElts, &blockBytes));
        const auto file = buildGGUF(
                { { "w",
                    type,
                    { blockElts * 64 },
                    blocksOf(type, 64, type) } });
        StructSplitRecorder recorder;
        attachHooks(recorder);

        expectRoundTrip(file);

        const std::vector<std::vector<size_t>> expected = { fields };
        EXPECT_EQ(recorder.fieldWidths, expected) << "type " << type;
    }
}

TEST_F(GGUFCompressorTest, SerializedCompressorCanBeReloaded)
{
    useChunkSize(ZL_GGUF_DEFAULT_CHUNK_SIZE);
    const auto reloaded =
            createCompressorFromSerialized(compressor_.serialize(), "");
    CCtx cctx;
    cctx.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    cctx.refCompressor(*reloaded);
    const auto input = buildGGUF(mixedTypeTensors());
    EXPECT_EQ(dctx_.decompressSerial(cctx.compressSerial(input)), input);
}

} // namespace
} // namespace openzl::custom_parsers
