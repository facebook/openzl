// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "custom_parsers/dependency_registration.h"
#include "custom_parsers/onnx/onnx_lexer.h"
#include "custom_parsers/onnx/onnx_parser.h"
#include "custom_parsers/onnx/tests/onnx_builder.h"
#include "openzl/cpp/CompressIntrospectionHooks.hpp"
#include "openzl/openzl.hpp"

namespace openzl::custom_parsers {
namespace {

using testing::buildONNX;
using testing::constantNode;
using testing::graphProto;
using testing::ifNode;
using testing::ONNXTensorSpec;
using testing::ProtoWriter;
using testing::pseudoRandomBytes;
using testing::tensorProto;

struct LexedToken {
    ZL_ONNXTokenType type;
    size_t offset;
    size_t size;
    std::string name;
    uint32_t dataType;
    std::vector<uint64_t> dims;

    bool operator==(const LexedToken&) const = default;
};

LexedToken tensorToken(
        size_t offset,
        size_t size,
        std::string name,
        uint32_t dataType,
        std::vector<uint64_t> dims)
{
    return { ZL_ONNXTokenType_tensor, offset,   size,
             std::move(name),         dataType, std::move(dims) };
}

struct LexResult {
    ZL_ErrorCode error;
    std::vector<LexedToken> tokens;
};

LexResult lex(std::string_view file)
{
    const ZL_Report maxTokens =
            ZL_ONNX_lex(file.data(), file.size(), nullptr, 0, nullptr);
    if (ZL_isError(maxTokens)) {
        return { ZL_errorCode(maxTokens), {} };
    }
    std::vector<ZL_ONNXToken> out(ZL_validResult(maxTokens));
    const ZL_Report nbTokens = ZL_ONNX_lex(
            file.data(), file.size(), out.data(), out.size(), nullptr);
    if (ZL_isError(nbTokens)) {
        return { ZL_errorCode(nbTokens), {} };
    }
    LexResult result{ ZL_ErrorCode_no_error, {} };
    for (size_t i = 0; i < ZL_validResult(nbTokens); ++i) {
        const auto& t       = out[i];
        const size_t offset = size_t(t.ptr - file.data());
        if (t.type == ZL_ONNXTokenType_tensor) {
            result.tokens.push_back(tensorToken(
                    offset,
                    t.size,
                    std::string(t.name, t.nameSize),
                    t.dataType,
                    { t.dims, t.dims + t.ndims }));
        } else {
            result.tokens.push_back({ t.type, offset, t.size, "", 0, {} });
        }
    }
    return result;
}

std::vector<std::string> tensorNames(const LexResult& result)
{
    std::vector<std::string> names;
    for (const auto& t : result.tokens) {
        if (t.type == ZL_ONNXTokenType_tensor) {
            names.push_back(t.name);
        }
    }
    return names;
}

/// @returns the data types of the tensors of a file of @p tensors.
std::vector<uint32_t> lexedDataTypes(const std::vector<ONNXTensorSpec>& tensors)
{
    std::vector<uint32_t> types;
    for (const auto& t : lex(buildONNX(tensors)).tokens) {
        if (t.type == ZL_ONNXTokenType_tensor) {
            types.push_back(t.dataType);
        }
    }
    return types;
}

TEST(ONNXLexerTest, ReturnsTensorsInFileOrder)
{
    const auto a    = pseudoRandomBytes(24, 1);
    const auto file = buildONNX(
            {
                    { "a", ZL_ONNXDataType_float, { 2, 3 }, a },
                    { "b", ZL_ONNXDataType_int8, { 5 }, "bbbbb" },
            });
    const size_t offA                      = file.find(a);
    const size_t offB                      = file.find("bbbbb");
    const std::vector<LexedToken> expected = {
        { ZL_ONNXTokenType_header, 0, offA, "", 0, {} },
        tensorToken(offA, 24, "a", ZL_ONNXDataType_float, { 2, 3 }),
        { ZL_ONNXTokenType_gap, offA + 24, offB - offA - 24, "", 0, {} },
        tensorToken(offB, 5, "b", ZL_ONNXDataType_int8, { 5 }),
        { ZL_ONNXTokenType_gap, offB + 5, file.size() - offB - 5, "", 0, {} },
    };

    const auto result = lex(file);

    ASSERT_EQ(result.error, ZL_ErrorCode_no_error);
    EXPECT_EQ(result.tokens, expected);
}

TEST(ONNXLexerTest, FindsTensorsInNodesAndSubgraphs)
{
    const auto tensor = [](std::string name) {
        return ONNXTensorSpec{
            std::move(name), ZL_ONNXDataType_int8, { 1 }, "x"
        };
    };
    const auto subgraph =
            graphProto({ constantNode(tensor("d")) }, { tensor("e") });
    const auto file                         = buildONNX(graphProto(
            { constantNode(tensor("c")), ifNode(subgraph) }, { tensor("f") }));
    const std::vector<std::string> expected = { "c", "d", "e", "f" };

    const auto result = lex(file);

    ASSERT_EQ(result.error, ZL_ErrorCode_no_error);
    EXPECT_EQ(tensorNames(result), expected);
}

TEST(ONNXLexerTest, IgnoresGraphsNestedTooDeep)
{
    const auto nested = [](int depth) {
        auto graph = graphProto(
                { constantNode({ "t", ZL_ONNXDataType_int8, { 1 }, "x" }) },
                {});
        for (int i = 0; i < depth; ++i) {
            graph = graphProto({ ifNode(graph) }, {});
        }
        return buildONNX(graph);
    };

    const auto deepest = lex(nested(ZL_ONNX_MAX_GRAPH_DEPTH));
    const auto tooDeep = lex(nested(ZL_ONNX_MAX_GRAPH_DEPTH + 1));

    ASSERT_EQ(deepest.error, ZL_ErrorCode_no_error);
    ASSERT_EQ(tooDeep.error, ZL_ErrorCode_no_error);
    EXPECT_EQ(tensorNames(deepest), std::vector<std::string>{ "t" });
    EXPECT_TRUE(tensorNames(tooDeep).empty());
}

TEST(ONNXLexerTest, ReadsDimsWrittenOneByOne)
{
    ProtoWriter graph;
    graph.bytesField(
            5,
            tensorProto(
                    { "t",
                      ZL_ONNXDataType_int16,
                      { 2, 3 },
                      std::string(12, 'x') },
                    false));
    const auto file = buildONNX(graph.out);

    const auto result = lex(file);

    ASSERT_EQ(result.error, ZL_ErrorCode_no_error);
    ASSERT_EQ(result.tokens.size(), 3u);
    EXPECT_EQ(result.tokens[1].dims, (std::vector<uint64_t>{ 2, 3 }));
    EXPECT_EQ(result.tokens[1].dataType, ZL_ONNXDataType_int16);
}

TEST(ONNXLexerTest, ReportsInconsistentTensorsAsUndefined)
{
    const std::vector<uint64_t> nineDims(ZL_ONNX_MAX_DIMS + 1, 1);
    const std::vector<ONNXTensorSpec> tensors = {
        { "short", ZL_ONNXDataType_float, { 3 }, std::string(8, 'x') },
        { "unknown type", 99, { 4 }, std::string(4, 'x') },
        { "string", ZL_ONNXDataType_string, { 4 }, std::string(4, 'x') },
        { "too many dims",
          ZL_ONNXDataType_float,
          nineDims,
          std::string(4, 'x') },
        { "packed", ZL_ONNXDataType_int4, { 3 }, std::string(2, 'x') },
        { "short packed", ZL_ONNXDataType_int4, { 3 }, std::string(1, 'x') },
        { "scalar", ZL_ONNXDataType_double, {}, std::string(8, 'x') },
    };
    const std::vector<uint32_t> expected = {
        ZL_ONNXDataType_undefined, ZL_ONNXDataType_undefined,
        ZL_ONNXDataType_undefined, ZL_ONNXDataType_undefined,
        ZL_ONNXDataType_int4,      ZL_ONNXDataType_undefined,
        ZL_ONNXDataType_double,
    };

    EXPECT_EQ(lexedDataTypes(tensors), expected);
}

TEST(ONNXLexerTest, LeavesTensorsWithoutRawDataToTheHeader)
{
    // float_data, external data and empty raw_data
    ProtoWriter typed;
    typed.varintField(2, ZL_ONNXDataType_float);
    typed.bytesField(4, std::string(8, 'f'));
    ProtoWriter external;
    external.varintField(2, ZL_ONNXDataType_float);
    ProtoWriter location;
    location.bytesField(1, "location");
    location.bytesField(2, "weights.bin");
    external.bytesField(13, location.out);
    external.varintField(14, 1);
    ProtoWriter graph;
    graph.bytesField(5, typed.out);
    graph.bytesField(5, external.out);
    graph.bytesField(
            5, tensorProto({ "empty", ZL_ONNXDataType_float, { 0 }, "" }));
    const auto file = buildONNX(graph.out);

    const auto result = lex(file);

    ASSERT_EQ(result.error, ZL_ErrorCode_no_error);
    const std::vector<LexedToken> expected = {
        { ZL_ONNXTokenType_header, 0, file.size(), "", 0, {} },
    };
    EXPECT_EQ(result.tokens, expected);
}

TEST(ONNXLexerTest, CountsTokensWithoutWriting)
{
    const auto file = buildONNX(
            {
                    { "a", ZL_ONNXDataType_int8, { 1 }, "a" },
                    { "b", ZL_ONNXDataType_int8, { 1 }, "b" },
            });

    const ZL_Report count =
            ZL_ONNX_lex(file.data(), file.size(), nullptr, 0, nullptr);

    ASSERT_FALSE(ZL_isError(count));
    EXPECT_EQ(ZL_validResult(count), 5u);
    EXPECT_EQ(lex(file).tokens.size(), 5u);
}

TEST(ONNXLexerTest, RejectsInputsThatAreNotONNXModels)
{
    const auto valid = buildONNX(
            { { "t", ZL_ONNXDataType_float, { 4 }, std::string(16, 't') } });
    ProtoWriter noGraph;
    noGraph.varintField(1, 8);
    ProtoWriter noIrVersion;
    noIrVersion.bytesField(7, graphProto({}, {}));
    ProtoWriter truncatedTensor;
    truncatedTensor.bytesField(
            5, std::string("\x4a\x10", 2)); // raw_data of 16 bytes
    const std::vector<std::string> malformed = {
        "",
        pseudoRandomBytes(1000, 1),
        valid.substr(0, valid.size() / 2),
        noGraph.out,
        noIrVersion.out,
        valid + std::string(11, '\xff'),    // varint of more than 10 bytes
        valid + "\x0b",                     // group, wire type 3
        valid + std::string("\x00\x00", 2), // field number 0
        buildONNX(truncatedTensor.out),
    };
    for (const auto& file : malformed) {
        EXPECT_EQ(lex(file).error, ZL_ErrorCode_graph_parser_malformedInput)
                << "input of " << file.size() << " bytes";
    }
}

TEST(ONNXLexerTest, RejectsTooManyTensors)
{
    ProtoWriter graph;
    const auto tensor = tensorProto({ "", ZL_ONNXDataType_int8, { 1 }, "x" });
    for (size_t i = 0; i <= ZL_ONNX_MAX_TENSORS; ++i) {
        graph.bytesField(5, tensor);
    }

    EXPECT_EQ(
            lex(buildONNX(graph.out)).error,
            ZL_ErrorCode_graph_parser_unhandledInput);
}

/// Tensors of every class, a Constant node, and a tensor of inconsistent size.
std::vector<ONNXTensorSpec> mixedTypeTensors()
{
    return {
        { "f32",
          ZL_ONNXDataType_float,
          { 64, 48 },
          pseudoRandomBytes(12288, 1) },
        { "f16",
          ZL_ONNXDataType_float16,
          { 3000 },
          pseudoRandomBytes(6000, 2) },
        { "bf16",
          ZL_ONNXDataType_bfloat16,
          { 10, 100 },
          pseudoRandomBytes(2000, 3) },
        { "f64", ZL_ONNXDataType_double, {}, pseudoRandomBytes(8, 4) },
        { "i64", ZL_ONNXDataType_int64, { 30 }, pseudoRandomBytes(240, 5) },
        { "i32", ZL_ONNXDataType_int32, { 30 }, pseudoRandomBytes(120, 6) },
        { "u16", ZL_ONNXDataType_uint16, { 30 }, pseudoRandomBytes(60, 7) },
        { "u8", ZL_ONNXDataType_uint8, { 4096 }, pseudoRandomBytes(4096, 8) },
        { "mask", ZL_ONNXDataType_bool, { 64, 64 }, std::string(4096, '\1') },
        { "int4", ZL_ONNXDataType_int4, { 100 }, pseudoRandomBytes(50, 9) },
        { "fp8",
          ZL_ONNXDataType_float8e4m3fn,
          { 1000 },
          pseudoRandomBytes(1000, 10) },
        { "c64",
          ZL_ONNXDataType_complex64,
          { 20 },
          pseudoRandomBytes(160, 11) },
        { "short", ZL_ONNXDataType_float, { 3 }, pseudoRandomBytes(8, 12) },
    };
}

std::string mixedTypeFile()
{
    return buildONNX(graphProto(
            { constantNode({ "c", ZL_ONNXDataType_float, { 2 }, "constant" }) },
            mixedTypeTensors()));
}

class ONNXCompressorTest : public ::testing::Test {
   protected:
    void useChunkSize(size_t chunkSize)
    {
        const auto gid = ZL_ONNX_registerGraph(compressor_.get(), chunkSize);
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
        cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        cctx_.refCompressor(compressor_);
        const auto compressed = cctx_.compressSerial(input);
        EXPECT_EQ(dctx_.decompressSerial(compressed), input);
        return compressed.size();
    }

    Compressor compressor_;
    CCtx cctx_;
    DCtx dctx_;
};

TEST_F(ONNXCompressorTest, RoundTripsMixedTypes)
{
    useChunkSize(ZL_ONNX_DEFAULT_CHUNK_SIZE);
    expectRoundTrip(mixedTypeFile());
}

TEST_F(ONNXCompressorTest, RoundTripsWithChunksSmallerThanTensors)
{
    useChunkSize(1);
    expectRoundTrip(buildONNX(
            {
                    { "w",
                      ZL_ONNXDataType_float,
                      { 1024, 1024 },
                      pseudoRandomBytes(1 << 22, 1) },
                    { "b",
                      ZL_ONNXDataType_float,
                      { 1024 },
                      pseudoRandomBytes(4096, 2) },
            }));
}

TEST_F(ONNXCompressorTest, RoundTripsInvalidInputs)
{
    useChunkSize(ZL_ONNX_DEFAULT_CHUNK_SIZE);
    const auto valid = mixedTypeFile();
    expectRoundTrip("");
    expectRoundTrip(pseudoRandomBytes(10000, 7));
    expectRoundTrip(valid.substr(0, valid.size() / 2));
}

TEST_F(ONNXCompressorTest, ReportsWhyInvalidInputsAreNotParsed)
{
    useChunkSize(ZL_ONNX_DEFAULT_CHUNK_SIZE);
    const auto valid = mixedTypeFile();

    expectRoundTrip(valid.substr(0, valid.size() / 2));

    const auto warnings = get_warning_strings(cctx_);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(
            warnings[0].second.find("Input not parsed as ONNX"),
            std::string::npos)
            << warnings[0].second;
}

TEST_F(ONNXCompressorTest, ValidInputsProduceNoWarning)
{
    useChunkSize(ZL_ONNX_DEFAULT_CHUNK_SIZE);

    expectRoundTrip(mixedTypeFile());

    EXPECT_TRUE(get_warning_strings(cctx_).empty());
}

/// Counts the runs of @p node.
class NodeCounter : public CompressIntrospectionHooks {
   public:
    explicit NodeCounter(ZL_NodeID node) : node_(node) {}

    void on_codecEncode_start(
            ZL_Encoder*,
            const ZL_Compressor*,
            ZL_NodeID nid,
            const ZL_Input*[],
            size_t) override
    {
        count += nid.nid == node_.nid;
    }

    size_t count = 0;

   private:
    ZL_NodeID node_;
};

TEST_F(ONNXCompressorTest, CodesTensorsByTheirDataType)
{
    const auto bytes = pseudoRandomBytes(1 << 16, 1);
    const auto floats =
            buildONNX({ { "w", ZL_ONNXDataType_float, { 128, 128 }, bytes } });
    const auto undefined =
            buildONNX({ { "w", ZL_ONNXDataType_float, { 128, 127 }, bytes } });
    useChunkSize(ZL_ONNX_DEFAULT_CHUNK_SIZE);
    NodeCounter onFloats(ZL_NODE_FLOAT32_DECONSTRUCT);
    NodeCounter onUndefined(ZL_NODE_FLOAT32_DECONSTRUCT);

    attachHooks(onFloats);
    expectRoundTrip(floats);
    attachHooks(onUndefined);
    expectRoundTrip(undefined);

    EXPECT_GT(onFloats.count, 0u);
    EXPECT_EQ(onUndefined.count, 0u);
}

TEST_F(ONNXCompressorTest, SerializedCompressorCanBeReloaded)
{
    // Shifts the names of the graphs registered after it, unless anchored.
    ZL_Compressor_registerZstdGraph_withLevel(compressor_.get(), 5);
    useChunkSize(ZL_ONNX_DEFAULT_CHUNK_SIZE);
    // Debug builds log the dependencies left unresolved.
    ::testing::internal::CaptureStderr();
    const auto reloaded =
            createCompressorFromSerialized(compressor_.serialize(), "");
    const std::string log = ::testing::internal::GetCapturedStderr();
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(log.find("unresolved"), std::string::npos) << log;

    const auto file = mixedTypeFile();
    cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    cctx_.refCompressor(*reloaded);
    EXPECT_EQ(dctx_.decompressSerial(cctx_.compressSerial(file)), file);
}

} // namespace
} // namespace openzl::custom_parsers
