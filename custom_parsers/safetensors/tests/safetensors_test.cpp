// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "custom_parsers/dependency_registration.h"
#include "custom_parsers/safetensors/safetensors_header.h"
#include "custom_parsers/safetensors/safetensors_parser.h"
#include "custom_parsers/safetensors/tests/safetensors_builder.h"
#include "openzl/cpp/CompressIntrospectionHooks.hpp"
#include "openzl/openzl.hpp"
#include "openzl/zl_reflection.h"

namespace openzl::custom_parsers {
namespace {

using testing::buildSafetensors;
using testing::pseudoRandomBytes;
using testing::TensorSpec;

struct ParsedTensor {
    std::string name;
    ZL_SafetensorsDtype dtype;
    std::vector<uint64_t> shape;
    uint64_t begin;
    uint64_t end;

    bool operator==(const ParsedTensor&) const = default;
};

struct ParseResult {
    const char* error;
    std::vector<ParsedTensor> tensors;
    size_t dataStart;
};

ParseResult parse(std::string_view file)
{
    size_t nbTensors = 0;
    size_t dataStart = 0;
    if (const char* error = ZL_Safetensors_parseHeader(
                file.data(), file.size(), nullptr, 0, &nbTensors, &dataStart)) {
        return { error, {}, 0 };
    }
    std::vector<ZL_SafetensorsTensor> out(nbTensors);
    ParseResult result{ ZL_Safetensors_parseHeader(
                                file.data(),
                                file.size(),
                                out.data(),
                                out.size(),
                                &nbTensors,
                                &dataStart),
                        {},
                        dataStart };
    result.tensors.reserve(nbTensors);
    for (size_t i = 0; i < nbTensors; ++i) {
        const auto& t = out[i];
        result.tensors.push_back(
                { std::string(t.name, t.nameSize),
                  t.dtype,
                  { t.shape, t.shape + t.ndims },
                  t.begin,
                  t.end });
    }
    return result;
}

std::string withHeader(std::string_view json, std::string_view data = "")
{
    std::string out(8, '\0');
    const uint64_t size = json.size();
    for (size_t i = 0; i < 8; ++i) {
        out[i] = (char)(size >> (8 * i));
    }
    return out + std::string(json) + std::string(data);
}

TEST(SafetensorsHeaderTest, ReturnsTensorsSortedByOffset)
{
    const auto file = withHeader(
            R"({"b":{"dtype":"F32","shape":[2],"data_offsets":[4,12]},)"
            R"("a":{"dtype":"BF16","shape":[1,2],"data_offsets":[0,4]}})",
            std::string(12, 'x'));
    const std::vector<ParsedTensor> expected = {
        { "a", ZL_SafetensorsDtype_bf16, { 1, 2 }, 0, 4 },
        { "b", ZL_SafetensorsDtype_f32, { 2 }, 4, 12 },
    };
    const auto result = parse(file);
    ASSERT_EQ(result.error, nullptr);
    EXPECT_EQ(result.tensors, expected);
    EXPECT_EQ(result.dataStart, file.size() - 12);
}

TEST(SafetensorsHeaderTest, SkipsMetadataAndPadding)
{
    const auto file = withHeader(
            R"({"__metadata__":{"format":"pt","k":"{\"x\":[1]}"},)"
            R"("t":{"dtype":"U8","shape":[3],"data_offsets":[0,3]}}   )",
            "abc");
    const std::vector<ParsedTensor> expected = {
        { "t", ZL_SafetensorsDtype_u8, { 3 }, 0, 3 },
    };
    const auto result = parse(file);
    ASSERT_EQ(result.error, nullptr);
    EXPECT_EQ(result.tensors, expected);
}

TEST(SafetensorsHeaderTest, ReportsInconsistentOrUnknownTensorsAsUnknown)
{
    const auto file = withHeader(
            R"({"mismatch":{"dtype":"F16","shape":[3],"data_offsets":[0,4]},)"
            R"("future":{"dtype":"F4","shape":[2],"data_offsets":[4,5]}})",
            "12345");
    const std::vector<ParsedTensor> expected = {
        { "mismatch", ZL_SafetensorsDtype_unknown, { 3 }, 0, 4 },
        { "future", ZL_SafetensorsDtype_unknown, { 2 }, 4, 5 },
    };
    const auto result = parse(file);
    ASSERT_EQ(result.error, nullptr);
    EXPECT_EQ(result.tensors, expected);
}

TEST(SafetensorsHeaderTest, CountsTensorsWithoutCheckingLayout)
{
    // Overlapping offsets: only the full parse checks the layout.
    const auto file = withHeader(
            R"({"a":{"dtype":"U8","shape":[2],"data_offsets":[0,2]},)"
            R"("b":{"dtype":"U8","shape":[2],"data_offsets":[1,3]}})",
            "xyz");
    size_t nbTensors = 0;
    size_t dataStart = 0;

    EXPECT_EQ(
            ZL_Safetensors_parseHeader(
                    file.data(),
                    file.size(),
                    nullptr,
                    0,
                    &nbTensors,
                    &dataStart),
            nullptr);
    EXPECT_EQ(nbTensors, 2u);
}

TEST(SafetensorsHeaderTest, CountingRejectsInvalidHeaderSize)
{
    std::string file(64, '\0');
    std::memset(file.data(), 0xff, 8);
    size_t nbTensors = 0;
    size_t dataStart = 0;

    EXPECT_NE(
            ZL_Safetensors_parseHeader(
                    file.data(),
                    file.size(),
                    nullptr,
                    0,
                    &nbTensors,
                    &dataStart),
            nullptr);
}

TEST(SafetensorsHeaderTest, RejectsTooManyTensors)
{
    std::string json = "{";
    for (size_t i = 0; i <= ZL_SAFETENSORS_MAX_TENSORS; ++i) {
        json += (i ? ",\"" : "\"") + std::to_string(i)
                + R"(":{"dtype":"U8","shape":[0],"data_offsets":[0,0]})";
    }
    const auto file  = withHeader(json + "}");
    size_t nbTensors = 0;
    size_t dataStart = 0;

    const char* const error = ZL_Safetensors_parseHeader(
            file.data(), file.size(), nullptr, 0, &nbTensors, &dataStart);

    ASSERT_NE(error, nullptr);
    EXPECT_STREQ(error, "too many tensors");
}

TEST(SafetensorsHeaderTest, RejectsMalformedHeaders)
{
    const std::string deep = std::string(100, '[') + std::string(100, ']');
    const std::vector<std::string> malformed = {
        std::string("\x05\0\0\0\0\0\0\0{}", 10), // header size beyond input
        withHeader("[]"),
        withHeader(R"({"t":{"dtype":"U8","shape":[1]}})"),
        withHeader(
                R"({"t":{"dtype":"U8","shape":[-1],"data_offsets":[0,1]}})",
                "x"),
        withHeader(
                R"({"t":{"dtype":"U8","shape":[1],"data_offsets":[1,0]}})",
                "x"),
        withHeader(
                R"({"t":{"dtype":"U8","shape":[2],"data_offsets":[0,2]}})",
                "x"),
        withHeader(
                R"({"a":{"dtype":"U8","shape":[2],"data_offsets":[0,2]},)"
                R"("b":{"dtype":"U8","shape":[2],"data_offsets":[1,3]}})",
                "xyz"),
        withHeader(
                R"({"t":{"dtype":"U8","shape":[1],"data_offsets":[0,1]}} x)",
                "x"),
        withHeader(R"({"__metadata__":)" + deep + "}"),
        withHeader(
                R"({"t":{"dtype":"U8","shape":[1],"data_offsets":[0,1])", "x"),
    };
    for (const auto& file : malformed) {
        EXPECT_NE(parse(file).error, nullptr) << file.substr(8);
    }
}

std::vector<TensorSpec> mixedDtypeTensors()
{
    return {
        { "model.layers.0.w", "BF16", { 64, 48 }, pseudoRandomBytes(6144, 1) },
        { "model.layers.0.h", "F16", { 3000 }, pseudoRandomBytes(6000, 2) },
        { "model.layers.0.empty", "F32", { 0, 4 }, "" },
        { "model.layers.1.f", "F32", { 10, 100 }, pseudoRandomBytes(4000, 3) },
        { "model.layers.1.d", "F64", {}, pseudoRandomBytes(8, 4), 5 },
        { "model.layers.1.i", "I64", { 30 }, pseudoRandomBytes(240, 5) },
        { "mask", "BOOL", { 1, 64, 64 }, std::string(4096, '\1') },
        { "future", "F4", { 10 }, pseudoRandomBytes(5, 6) },
    };
}

class SafetensorsCompressorTest : public ::testing::Test {
   protected:
    void useChunkSize(size_t chunkSize)
    {
        const auto gid =
                ZL_Safetensors_registerGraph(compressor_.get(), chunkSize);
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

TEST_F(SafetensorsCompressorTest, RoundTripsMixedDtypes)
{
    useChunkSize(ZL_SAFETENSORS_DEFAULT_CHUNK_SIZE);
    expectRoundTrip(buildSafetensors(mixedDtypeTensors(), R"({"a":"b"})"));
}

TEST_F(SafetensorsCompressorTest, RoundTripsWithChunksSmallerThanTensors)
{
    auto tensors = mixedDtypeTensors();
    tensors.push_back(
            { "model.layers.2.big",
              "F32",
              { 300, 300 },
              pseudoRandomBytes(360000, 8) });
    useChunkSize(2 * ZL_MIN_CHUNK_SIZE);
    expectRoundTrip(buildSafetensors(tensors));
}

TEST_F(SafetensorsCompressorTest, RoundTripsInvalidInputs)
{
    useChunkSize(ZL_SAFETENSORS_DEFAULT_CHUNK_SIZE);
    const auto valid = buildSafetensors(mixedDtypeTensors());
    expectRoundTrip("");
    expectRoundTrip("tiny");
    expectRoundTrip(pseudoRandomBytes(10000, 7));
    expectRoundTrip(valid.substr(0, valid.size() / 2));
}

TEST_F(SafetensorsCompressorTest, ReportsWhyInvalidInputsAreNotParsed)
{
    useChunkSize(ZL_SAFETENSORS_DEFAULT_CHUNK_SIZE);
    const auto valid = buildSafetensors(mixedDtypeTensors());

    expectRoundTrip(valid.substr(0, valid.size() / 2));

    const auto warnings = get_warning_strings(cctx_);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(
            warnings[0].second.find("tensor data out of bounds"),
            std::string::npos)
            << warnings[0].second;
}

TEST_F(SafetensorsCompressorTest, FallsBackOnHugeDeclaredHeaderSize)
{
    useChunkSize(ZL_SAFETENSORS_DEFAULT_CHUNK_SIZE);
    std::string file(1 << 20, '\0');
    std::memset(file.data(), 0xff, 8);

    expectRoundTrip(file);

    const auto warnings = get_warning_strings(cctx_);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(warnings[0].second.find("invalid header size"), std::string::npos)
            << warnings[0].second;
}

TEST_F(SafetensorsCompressorTest, CompressesManyTinyTensorsOfMixedDtypes)
{
    // One stream per tensor would exceed the stream limit of a chunk.
    const size_t nbTensors = 60000;
    std::vector<TensorSpec> tensors;
    tensors.reserve(nbTensors);
    for (size_t i = 0; i < nbTensors; ++i) {
        const bool bf16 = i % 2 == 0;
        tensors.push_back(
                { "t" + std::to_string(i),
                  bf16 ? "BF16" : "F32",
                  { 1 },
                  pseudoRandomBytes(bf16 ? 2 : 4, (uint32_t)i) });
    }
    useChunkSize(ZL_SAFETENSORS_DEFAULT_CHUNK_SIZE);

    expectRoundTrip(buildSafetensors(tensors));
}

TEST_F(SafetensorsCompressorTest, FallsBackOnFormatVersionsWithoutChunks)
{
    useChunkSize(ZL_SAFETENSORS_DEFAULT_CHUNK_SIZE);
    formatVersion_ = ZL_CHUNK_VERSION_MIN - 1;

    expectRoundTrip(buildSafetensors(mixedDtypeTensors()));

    const auto warnings = get_warning_strings(cctx_);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(
            warnings[0].second.find("format version without chunks"),
            std::string::npos)
            << warnings[0].second;
}

TEST_F(SafetensorsCompressorTest, ValidInputsProduceNoWarning)
{
    useChunkSize(ZL_SAFETENSORS_DEFAULT_CHUNK_SIZE);

    expectRoundTrip(buildSafetensors(mixedDtypeTensors()));

    EXPECT_TRUE(get_warning_strings(cctx_).empty());
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
        if (nbInputs == 1 && name.starts_with("Safetensors Parser")) {
            chunkSizes.push_back(
                    ZL_Input_contentSize(ZL_Edge_getData(inputs[0])));
        }
    }

    std::vector<size_t> chunkSizes;
};

class SafetensorsLayerChunkTest : public SafetensorsCompressorTest {
   protected:
    /// Two layers of two tensors, with room for the header and one layer only.
    void expectOneChunkPerLayer(
            const std::string& layer0,
            const std::string& layer1)
    {
        const size_t tensorBytes              = 40000;
        const std::vector<TensorSpec> tensors = {
            { layer0 + "a",
              "BF16",
              { 20000 },
              pseudoRandomBytes(tensorBytes, 1) },
            { layer0 + "b",
              "BF16",
              { 20000 },
              pseudoRandomBytes(tensorBytes, 2) },
            { layer1 + "a",
              "BF16",
              { 20000 },
              pseudoRandomBytes(tensorBytes, 3) },
            { layer1 + "b",
              "BF16",
              { 20000 },
              pseudoRandomBytes(tensorBytes, 4) },
        };
        const auto file          = buildSafetensors(tensors);
        const size_t headerBytes = file.size() - 4 * tensorBytes;
        useChunkSize(headerBytes + 3 * tensorBytes);
        ChunkRecorder recorder;
        attachHooks(recorder);

        expectRoundTrip(file);

        const std::vector<size_t> expected = { headerBytes + 2 * tensorBytes,
                                               2 * tensorBytes };
        EXPECT_EQ(recorder.chunkSizes, expected);
    }
};

TEST_F(SafetensorsLayerChunkTest, GroupsLayersSeparatedByDots)
{
    expectOneChunkPerLayer("model.layers.0.", "model.layers.1.");
}

TEST_F(SafetensorsLayerChunkTest, GroupsLayersSeparatedByDashes)
{
    expectOneChunkPerLayer("layer-0-", "layer-1-");
}

TEST_F(SafetensorsCompressorTest, CapsTheNumberOfSegmentsPerChunk)
{
    const size_t kMaxSegmentsPerChunk = 512; // ST_MAX_SEGMENTS_PER_CHUNK
    const size_t tensorBytes          = 16 << 10;
    std::vector<TensorSpec> tensors;
    tensors.reserve(kMaxSegmentsPerChunk);
    // With the header, one more segment than fits in a chunk.
    for (size_t i = 0; i < kMaxSegmentsPerChunk; ++i) {
        tensors.push_back(
                { "t" + std::to_string(i),
                  "U8",
                  { tensorBytes },
                  std::string(tensorBytes, (char)i) });
    }
    const auto file = buildSafetensors(tensors);
    useChunkSize(size_t(1) << 30);
    ChunkRecorder recorder;
    attachHooks(recorder);

    expectRoundTrip(file);

    EXPECT_EQ(recorder.chunkSizes.size(), 2u);
}

TEST_F(SafetensorsCompressorTest, SplitsLargeTensorsOnRowBoundaries)
{
    const size_t rowBytes = 500;
    const auto file       = buildSafetensors(
            { { "w", "BF16", { 1000, 250 }, pseudoRandomBytes(500000, 1) } });
    const size_t headerBytes = file.size() - 500000;
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

std::string causalMaskF32(size_t n)
{
    std::vector<float> mask(n * n);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            mask[i * n + j] = j <= i ? 1.0f : 0.0f;
        }
    }
    std::string bytes(mask.size() * sizeof(float), '\0');
    std::memcpy(bytes.data(), mask.data(), bytes.size());
    return bytes;
}

std::string constantBf16(size_t nbElts)
{
    std::string bytes;
    for (size_t i = 0; i < nbElts; ++i) {
        bytes += "\x80\x3f"; // 1.0
    }
    return bytes;
}

TEST_F(SafetensorsCompressorTest, CompressesDegenerateFloatTensorsStrongly)
{
    const auto file = buildSafetensors(
            {
                    { "mask", "F32", { 256, 256 }, causalMaskF32(256) },
                    { "ones", "BF16", { 128, 128 }, constantBf16(128 * 128) },
            });
    useChunkSize(ZL_SAFETENSORS_DEFAULT_CHUNK_SIZE);

    EXPECT_LT(expectRoundTrip(file), file.size() / 50);
}

/// Counts the inputs handed to graphs whose name contains @p name.
class GraphCounter : public CompressIntrospectionHooks {
   public:
    explicit GraphCounter(std::string_view name) : name_(name) {}

    void on_migraphEncode_start(
            ZL_Graph*,
            const ZL_Compressor* compressor,
            ZL_GraphID gid,
            ZL_Edge*[],
            size_t) override
    {
        const std::string name = ZL_Compressor_Graph_getName(compressor, gid);
        count += name.find(name_) != std::string::npos;
        seen += name + " ";
    }

    size_t count = 0;
    std::string seen;

   private:
    std::string name_;
};

TEST_F(SafetensorsCompressorTest, SendsOnlyLowCardinalityTensorsToTransformer)
{
    const auto weights = buildSafetensors(
            { { "w", "BF16", { 64, 64 }, pseudoRandomBytes(8192, 1) } });
    const auto mask = buildSafetensors(
            { { "mask", "F32", { 64, 64 }, causalMaskF32(64) } });
    useChunkSize(ZL_SAFETENSORS_DEFAULT_CHUNK_SIZE);
    GraphCounter onWeights("transformer_numeric");
    GraphCounter onMask("transformer_numeric");

    attachHooks(onWeights);
    expectRoundTrip(weights);
    attachHooks(onMask);
    expectRoundTrip(mask);

    EXPECT_EQ(onWeights.count, 0u);
    EXPECT_GT(onMask.count, 0u) << "graphs seen: " << onMask.seen;
}

TEST_F(SafetensorsCompressorTest, SerializedCompressorCanBeReloaded)
{
    useChunkSize(ZL_SAFETENSORS_DEFAULT_CHUNK_SIZE);
    const auto reloaded =
            createCompressorFromSerialized(compressor_.serialize(), "");
    CCtx cctx;
    cctx.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    cctx.refCompressor(*reloaded);
    const auto input = buildSafetensors(mixedDtypeTensors());
    EXPECT_EQ(dctx_.decompressSerial(cctx.compressSerial(input)), input);
}

} // namespace
} // namespace openzl::custom_parsers
