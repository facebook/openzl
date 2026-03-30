// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "openzl/zl_config.h"

#if ZL_ALLOW_INTROSPECTION

#    include <gtest/gtest.h>

#    include <cstring>
#    include <filesystem>
#    include <fstream>

#    include "cpp/tests/experimental/trace/TraceTestHelpers.hpp"
#    include "openzl/cpp/CCtx.hpp"
#    include "openzl/cpp/Compressor.hpp"
#    include "openzl/cpp/CustomEncoder.hpp"
#    include "openzl/cpp/Exception.hpp"
#    include "openzl/cpp/FunctionGraph.hpp"
#    include "openzl/cpp/codecs/Conversion.hpp"
#    include "openzl/zl_errors.h"
#    include "openzl/zl_segmenter.h"

using namespace ::testing;

namespace openzl {
namespace {

// ---------------------------------------------------------------------------
// Reusable custom encoders / graphs
// ---------------------------------------------------------------------------

// A custom encoder that copies input to output unchanged (always succeeds).
class NoOpEncoder : public CustomEncoder {
    SimpleCodecDescription simpleCodecDescription() const override
    {
        return SimpleCodecDescription{
            .id          = 9998,
            .name        = "NoOpEncoder",
            .inputType   = Type::Numeric,
            .outputTypes = { Type::Numeric },
        };
    }

    void encode(EncoderState& encoder) const override
    {
        auto& input = encoder.inputs()[0];
        auto output =
                encoder.createOutput(0, input.numElts(), input.eltWidth());
        memcpy(output.ptr(), input.ptr(), input.contentSize());
        output.commit(input.numElts());
    }
};

// A custom encoder that always fails with a known error message.
// Accepts serial input so it can be chained after SeparateStringComponents.
class FailingEncoder : public CustomEncoder {
    SimpleCodecDescription simpleCodecDescription() const override
    {
        return SimpleCodecDescription{
            .id          = 9999,
            .name        = "FailingEncoder",
            .inputType   = Type::Serial,
            .outputTypes = { Type::Serial },
        };
    }

    void encode(EncoderState& /* encoder */) const override
    {
        throw std::runtime_error("deliberate codec failure");
    }
};

// FunctionGraph that splits string input via SeparateStringComponents, then
// sends the content (serial) stream to a failing codec. The lengths (numeric)
// stream is left unconsumed and should become "zl.#in_progress" on failure.
class SplitThenFailGraph : public FunctionGraph {
   public:
    explicit SplitThenFailGraph(NodeID failingNode) : failingNode_(failingNode)
    {
    }

    FunctionGraphDescription functionGraphDescription() const override
    {
        return {
            .name           = "SplitThenFail",
            .inputTypeMasks = { TypeMask::String },
            .customNodes    = { nodes::SeparateStringComponents::node,
                                failingNode_ },
        };
    }

    void graph(GraphState& state) const override
    {
        auto& edge = state.edges()[0];
        // SeparateStringComponents: string -> [serial content, numeric lengths]
        auto outputs = edge.runNode(nodes::SeparateStringComponents::node);
        // Send content (output 0) to the failing encoder
        outputs[0].runNode(failingNode_);
        // Leave lengths (output 1) unconsumed
    }

   private:
    NodeID failingNode_;
};

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class CompressTraceErrorTest : public Test {
   protected:
    Compressor compressor_;
    CCtx cctx_;
    std::string rawTrace_;

    void SetUp() override
    {
        compressor_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    }

    void TearDown() override
    {
        if (rawTrace_.empty()) {
            return;
        }
        // Write the raw CBOR trace to /tmp for manual visualizer testing.
        // Enable by changing `if (0)` to `if (1)`.
        if ((0)) {
            std::filesystem::create_directories("/tmp/openzl_trace");
            auto* info       = UnitTest::GetInstance()->current_test_info();
            std::string path = std::string("/tmp/openzl_trace/")
                    + info->test_suite_name() + "_" + info->name() + ".cbor";
            std::ofstream out(path, std::ios::binary);
            if (out) {
                out.write(rawTrace_.data(), rawTrace_.size());
            }
            out.close();
        }
    }

    // Compress with tracing, store the raw trace, and return the parsed result.
    // Expects compression to succeed.
    test::ParsedTrace compressAndParse(Input& input)
    {
        cctx_.refCompressor(compressor_);
        cctx_.writeTraces(true);

        cctx_.compressOne(input);
        return extractTrace();
    }

    // Compress with tracing, expecting compression to throw.
    // Stores the raw trace and returns the parsed result.
    test::ParsedTrace compressExpectFailAndParse(Input& input)
    {
        cctx_.refCompressor(compressor_);
        cctx_.writeTraces(true);

        EXPECT_THROW(cctx_.compressOne(input), Exception);
        return extractTrace();
    }

    // Look up a codec by (substring) name in a chunk. Returns nullptr if not
    // found.
    static const test::ParsedCodec* findCodec(
            const test::ParsedChunk& chunk,
            const std::string& name)
    {
        for (const auto& codec : chunk.codecs) {
            if (codec.name.find(name) != std::string::npos) {
                return &codec;
            }
        }
        return nullptr;
    }

    // Look up a graph by (substring) name in a chunk.
    static const test::ParsedGraph* findGraph(
            const test::ParsedChunk& chunk,
            const std::string& name)
    {
        for (const auto& g : chunk.graphs) {
            if (g.gName.find(name) != std::string::npos) {
                return &g;
            }
        }
        return nullptr;
    }

   private:
    test::ParsedTrace extractTrace()
    {
        auto traceResult = cctx_.getLatestTrace();
        rawTrace_ =
                std::string(traceResult.first.data(), traceResult.first.size());
        EXPECT_FALSE(rawTrace_.empty()) << "Trace should be non-empty";

        auto parsed = test::parseTrace(traceResult.first);
        EXPECT_TRUE(parsed.has_value()) << "Failed to parse trace CBOR";
        EXPECT_FALSE(parsed->chunks.empty()) << "Trace should have chunks";
        return parsed.value();
    }
};

// Helper to build string input from a list of strings.
Input makeStringInput(
        const std::vector<std::string>& strings,
        std::string& contentBuf,
        std::vector<uint32_t>& lengthsBuf)
{
    contentBuf.clear();
    lengthsBuf.clear();
    for (const auto& s : strings) {
        contentBuf += s;
        lengthsBuf.push_back(static_cast<uint32_t>(s.size()));
    }
    return Input::refString(
            contentBuf.data(),
            contentBuf.size(),
            lengthsBuf.data(),
            lengthsBuf.size());
}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Smoke test: verify TraceTestHelpers can parse a successful compression trace.
TEST_F(CompressTraceErrorTest, ParseHelperSmokeTest)
{
    compressor_.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);

    std::vector<int64_t> data(1000, 42);
    auto input = Input::refNumeric(data.data(), sizeof(int64_t), data.size());

    auto parsed = compressAndParse(input);

    EXPECT_EQ(parsed.operationType, 0) << "Should be compression";

    bool foundCodecs = false;
    for (const auto& chunk : parsed.chunks) {
        if (!chunk.codecs.empty()) {
            foundCodecs = true;
        }
    }
    EXPECT_TRUE(foundCodecs) << "Should have codecs in at least one chunk";
}

// Step 1: Verify that a codec encode failure is captured in the trace.
// Uses SeparateStringComponents to produce two outputs: the content stream
// goes to the FailingEncoder (which fails), and the lengths stream goes to
// STORE but remains unconsumed due to the failure (becomes zl.#in_progress).
TEST_F(CompressTraceErrorTest, CodecEncodeFailure)
{
    auto failingNode = CustomEncoder::registerCustomEncoder(
            compressor_, std::make_shared<FailingEncoder>());
    auto failGraph =
            compressor_.buildStaticGraph(failingNode, { ZL_GRAPH_STORE });
    // SeparateStringComponents: string -> [serial content, numeric lengths]
    // content (output 0) -> failGraph, lengths (output 1) -> STORE
    auto graph = nodes::SeparateStringComponents{}(
            compressor_, failGraph, ZL_GRAPH_STORE);
    compressor_.selectStartingGraph(graph);

    std::string content;
    std::vector<uint32_t> lengths;
    auto input =
            makeStringInput({ "hello", "world", "test" }, content, lengths);

    auto parsed = compressExpectFailAndParse(input);
    EXPECT_EQ(parsed.operationType, 0) << "Should be compression";

    const auto& chunk = parsed.chunks[0];

    // Find the failing codec by name (name may have a suffix like "#0")
    auto* failingCodec = findCodec(chunk, "FailingEncoder");
    ASSERT_NE(failingCodec, nullptr) << "Should find FailingEncoder in trace";

    // The codec should have a failure string
    EXPECT_FALSE(failingCodec->cFailureString.empty())
            << "Failed codec should have a non-empty cFailureString";
    EXPECT_NE(
            failingCodec->cFailureString.find("deliberate codec failure"),
            std::string::npos)
            << "cFailureString should contain the error message, got: "
            << failingCodec->cFailureString;

    // Failed codec should have inputs but no outputs
    EXPECT_FALSE(failingCodec->inputStreams.empty())
            << "Failed codec should still have input streams";
    EXPECT_TRUE(failingCodec->outputStreams.empty())
            << "Failed codec should have no output streams";

    // The unconsumed lengths stream should get a "zl.#in_progress" terminal
    bool hasInProgress = false;
    for (const auto& codec : chunk.codecs) {
        if (codec.name == "zl.#in_progress") {
            hasInProgress = true;
        }
        EXPECT_NE(codec.name, "zl.store")
                << "Failed compression should not have zl.store terminals";
    }
    EXPECT_TRUE(hasInProgress)
            << "Failed compression should have zl.#in_progress terminals";
}

// Step 2a: Verify that a graph failure (with no codec errors) is captured in
// the trace. When a FunctionGraph::graph() throws before running any codecs,
// the graph gets gFailure set and a synthetic "zl.#in_progress" placeholder
// codec is injected.
TEST_F(CompressTraceErrorTest, GraphFailureNoCodecErrors)
{
    // A FunctionGraph that immediately throws without running any codecs.
    class AlwaysFailGraph : public FunctionGraph {
        FunctionGraphDescription functionGraphDescription() const override
        {
            return {
                .name           = "AlwaysFailGraph",
                .inputTypeMasks = { TypeMask::Numeric },
            };
        }

        void graph(GraphState& /* state */) const override
        {
            throw std::runtime_error("deliberate graph failure");
        }
    };

    auto graph = FunctionGraph::registerFunctionGraph(
            compressor_, std::make_shared<AlwaysFailGraph>());
    compressor_.selectStartingGraph(graph);

    std::vector<int64_t> data(100, 42);
    auto input = Input::refNumeric(data.data(), sizeof(int64_t), data.size());

    auto parsed       = compressExpectFailAndParse(input);
    const auto& chunk = parsed.chunks[0];

    // The trace should contain a graph with gFailureString
    auto* failedGraph = findGraph(chunk, "AlwaysFailGraph");
    ASSERT_NE(failedGraph, nullptr)
            << "Should find AlwaysFailGraph in trace graphs";

    EXPECT_FALSE(failedGraph->gFailureString.empty())
            << "Failed graph should have a non-empty gFailureString";
    EXPECT_NE(
            failedGraph->gFailureString.find("deliberate graph failure"),
            std::string::npos)
            << "gFailureString should contain the error message, got: "
            << failedGraph->gFailureString;

    // Since the graph failed before running any codecs, a synthetic
    // "zl.#in_progress" placeholder codec should be injected into the graph.
    ASSERT_FALSE(failedGraph->codecIDs.empty())
            << "Failed graph should have a placeholder codec in codecIDs";

    // Find the placeholder codec and verify it
    bool foundPlaceholder = false;
    for (int64_t codecID : failedGraph->codecIDs) {
        for (const auto& codec : chunk.codecs) {
            if (static_cast<int64_t>(codec.cID) == codecID
                && codec.name == "zl.#in_progress") {
                foundPlaceholder = true;
                // The placeholder should consume the graph's input streams
                EXPECT_FALSE(codec.inputStreams.empty())
                        << "Placeholder codec should have input streams from "
                           "the graph's edges";
            }
        }
    }
    EXPECT_TRUE(foundPlaceholder)
            << "Should find a zl.#in_progress placeholder codec in the "
               "failed graph's codecIDs";
}

// Step 2b: Verify that a graph failure is captured even when some codecs ran
// successfully. The graph throws *after* a codec completes (not inside a
// codec), so codecs.size() > 0 but codecsHaveErrors == false. The trace
// should set gFailureString on the graph without injecting a placeholder
// codec.
TEST_F(CompressTraceErrorTest, GraphFailureAfterSuccessfulCodec)
{
    // A FunctionGraph that runs a NoOpEncoder successfully, then throws.
    class FailAfterCodecGraph : public FunctionGraph {
       public:
        explicit FailAfterCodecGraph(NodeID noopNode) : noopNode_(noopNode) {}

        FunctionGraphDescription functionGraphDescription() const override
        {
            return {
                .name           = "FailAfterCodecGraph",
                .inputTypeMasks = { TypeMask::Numeric },
                .customNodes    = { noopNode_ },
            };
        }

        void graph(GraphState& state) const override
        {
            auto& edge = state.edges()[0];
            // Run the NoOpEncoder -- this succeeds
            edge.runNode(noopNode_);
            // Then fail outside of any codec
            throw std::runtime_error("graph failure after codec");
        }

       private:
        NodeID noopNode_;
    };

    auto noopNode = CustomEncoder::registerCustomEncoder(
            compressor_, std::make_shared<NoOpEncoder>());
    auto graph = FunctionGraph::registerFunctionGraph(
            compressor_, std::make_shared<FailAfterCodecGraph>(noopNode));
    compressor_.selectStartingGraph(graph);

    std::vector<int64_t> data(100, 42);
    auto input = Input::refNumeric(data.data(), sizeof(int64_t), data.size());

    auto parsed       = compressExpectFailAndParse(input);
    const auto& chunk = parsed.chunks[0];

    // Find the failed graph
    auto* failedGraph = findGraph(chunk, "FailAfterCodecGraph");
    ASSERT_NE(failedGraph, nullptr)
            << "Should find FailAfterCodecGraph in trace graphs";

    // The graph should have gFailureString set
    EXPECT_FALSE(failedGraph->gFailureString.empty())
            << "Failed graph should have a non-empty gFailureString";
    EXPECT_NE(
            failedGraph->gFailureString.find("graph failure after codec"),
            std::string::npos)
            << "gFailureString should contain the error message, got: "
            << failedGraph->gFailureString;

    // The graph should have codec(s) from the successful NoOpEncoder run
    EXPECT_FALSE(failedGraph->codecIDs.empty())
            << "Graph should have codecs from the successful NoOpEncoder";

    // Find the NoOpEncoder codec -- it should have no cFailureString
    auto* noopCodec = findCodec(chunk, "NoOpEncoder");
    ASSERT_NE(noopCodec, nullptr) << "Should find NoOpEncoder in trace codecs";
    EXPECT_TRUE(noopCodec->cFailureString.empty())
            << "NoOpEncoder should have no failure (it succeeded), got: "
            << noopCodec->cFailureString;

    // No placeholder codec should be injected since codecs.size() > 0
    bool foundPlaceholder = false;
    for (int64_t codecID : failedGraph->codecIDs) {
        for (const auto& codec : chunk.codecs) {
            if (static_cast<int64_t>(codec.cID) == codecID
                && codec.name == "zl.#in_progress") {
                foundPlaceholder = true;
            }
        }
    }
    EXPECT_FALSE(foundPlaceholder)
            << "Graph with successful codecs should NOT have a placeholder "
               "codec injected";
}

// Step 3: Verify error deduplication -- when a codec inside a graph fails,
// the graph-level gFailureString should be suppressed (empty) because the
// codec already reported the error via cFailureString.
TEST_F(CompressTraceErrorTest, GraphFailureWithCodecErrorDeduplication)
{
    auto failingNode = CustomEncoder::registerCustomEncoder(
            compressor_, std::make_shared<FailingEncoder>());
    auto graph = FunctionGraph::registerFunctionGraph(
            compressor_, std::make_shared<SplitThenFailGraph>(failingNode));
    compressor_.selectStartingGraph(graph);

    // String input for SeparateStringComponents
    std::string content;
    std::vector<uint32_t> lengths;
    auto input =
            makeStringInput({ "hello", "world", "test" }, content, lengths);

    auto parsed       = compressExpectFailAndParse(input);
    const auto& chunk = parsed.chunks[0];

    // The FailingEncoder codec should have cFailureString
    auto* failingCodec = findCodec(chunk, "FailingEncoder");
    ASSERT_NE(failingCodec, nullptr)
            << "Should find FailingEncoder in trace codecs";
    EXPECT_FALSE(failingCodec->cFailureString.empty())
            << "FailingEncoder should have a cFailureString";
    EXPECT_NE(
            failingCodec->cFailureString.find("deliberate codec failure"),
            std::string::npos)
            << "cFailureString should contain the error message, got: "
            << failingCodec->cFailureString;

    // The SplitThenFail graph should exist but its gFailureString should be
    // empty -- the error is already reported by the codec, so the graph-level
    // error is suppressed (deduplication).
    auto* splitGraph = findGraph(chunk, "SplitThenFail");
    ASSERT_NE(splitGraph, nullptr)
            << "Should find SplitThenFail graph in trace";
    EXPECT_TRUE(splitGraph->gFailureString.empty())
            << "Graph gFailureString should be suppressed when a child codec "
               "already has the error, got: "
            << splitGraph->gFailureString;
}

// Step 4: Verify that a type conversion failure is captured in the trace.
// When the CCtx tries to convert input data to match a subgraph's expected
// type and the conversion fails, the error is attributed to the codec that
// would have consumed the mistyped stream.
//
// The conversion check happens in CCTX_runSupervisedGraphID_internal *before*
// the subgraph's on_migraphEncode_start hook fires. To get trace content, we
// use a FunctionGraph that accepts any type and runs a codec (NoOpEncoder for
// serial input), then routes one output to a numeric-only subgraph. The parent
// graph's hooks fire and record trace data; the subgraph's conversion failure
// is then captured.
TEST_F(CompressTraceErrorTest, TypeConversionFailure)
{
    // A custom encoder that accepts serial and outputs serial (succeeds).
    class SerialNoOpEncoder : public CustomEncoder {
        SimpleCodecDescription simpleCodecDescription() const override
        {
            return SimpleCodecDescription{
                .id          = 9997,
                .name        = "SerialNoOpEncoder",
                .inputType   = Type::Serial,
                .outputTypes = { Type::Serial },
            };
        }

        void encode(EncoderState& encoder) const override
        {
            auto& input = encoder.inputs()[0];
            auto output =
                    encoder.createOutput(0, input.numElts(), input.eltWidth());
            memcpy(output.ptr(), input.ptr(), input.contentSize());
            output.commit(input.numElts());
        }
    };

    // A FunctionGraph that runs a serial codec, then routes its output to a
    // numeric-only subgraph (which will fail type conversion).
    class SerialThenNumericGraph : public FunctionGraph {
       public:
        SerialThenNumericGraph(NodeID serialNode, GraphID numericSubgraph)
                : serialNode_(serialNode), numericSubgraph_(numericSubgraph)
        {
        }

        FunctionGraphDescription functionGraphDescription() const override
        {
            return {
                .name           = "SerialThenNumericGraph",
                .inputTypeMasks = { TypeMask::Serial },
                .customNodes    = { serialNode_ },
            };
        }

        void graph(GraphState& state) const override
        {
            auto& edge   = state.edges()[0];
            auto outputs = edge.runNode(serialNode_);
            // Route the serial output to a numeric-only subgraph — this will
            // trigger a conversion error since serial → numeric is unsupported
            outputs[0].setDestination(numericSubgraph_);
        }

       private:
        NodeID serialNode_;
        GraphID numericSubgraph_;
    };

    // Build the numeric-only subgraph: NoOpEncoder (numeric) → STORE
    auto noopNode = CustomEncoder::registerCustomEncoder(
            compressor_, std::make_shared<NoOpEncoder>());
    auto numericSubgraph =
            compressor_.buildStaticGraph(noopNode, { ZL_GRAPH_STORE });

    // Build the parent graph that runs serial codec then routes to numeric
    auto serialNode = CustomEncoder::registerCustomEncoder(
            compressor_, std::make_shared<SerialNoOpEncoder>());
    auto parentGraph = FunctionGraph::registerFunctionGraph(
            compressor_,
            std::make_shared<SerialThenNumericGraph>(
                    serialNode, numericSubgraph));
    compressor_.selectStartingGraph(parentGraph);

    // Supply serial data
    std::vector<uint8_t> serialData(1000, 0x42);
    auto input = Input::refSerial(serialData.data(), serialData.size());

    auto parsed       = compressExpectFailAndParse(input);
    const auto& chunk = parsed.chunks[0];

    // Some codec should have a cFailureString from the conversion error
    const test::ParsedCodec* failedCodec = nullptr;
    for (const auto& codec : chunk.codecs) {
        if (!codec.cFailureString.empty()) {
            failedCodec = &codec;
            break;
        }
    }
    ASSERT_NE(failedCodec, nullptr)
            << "Should find a codec with cFailureString from conversion error";

    // Graphs should not have gFailureString -- the error is a conversion
    // failure attributed to a codec, not a graph-level error.
    for (const auto& g : chunk.graphs) {
        EXPECT_TRUE(g.gFailureString.empty())
                << "Graph '" << g.gName
                << "' should not have gFailureString for a conversion error, "
                   "got: "
                << g.gFailureString;
    }

    // Terminal streams should be "zl.#in_progress" (compression failed)
    bool hasInProgress = false;
    for (const auto& codec : chunk.codecs) {
        if (codec.name == "zl.#in_progress") {
            hasInProgress = true;
        }
        EXPECT_NE(codec.name, "zl.store")
                << "Failed compression should not have zl.store terminals";
    }
    EXPECT_TRUE(hasInProgress)
            << "Failed compression should have zl.#in_progress terminals";
}

// Step 5: Baseline negative test — successful compression should have no
// error strings and terminal streams should end at "zl.store".
TEST_F(CompressTraceErrorTest, SuccessfulCompressionNoErrors)
{
    compressor_.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);

    std::vector<int64_t> data(1000, 42);
    auto input = Input::refNumeric(data.data(), sizeof(int64_t), data.size());

    auto parsed = compressAndParse(input);

    EXPECT_EQ(parsed.operationType, 0) << "Should be compression";

    for (const auto& chunk : parsed.chunks) {
        // No codec should have cFailureString
        for (const auto& codec : chunk.codecs) {
            EXPECT_TRUE(codec.cFailureString.empty())
                    << "Codec '" << codec.name
                    << "' should not have cFailureString in successful "
                       "compression, got: "
                    << codec.cFailureString;
            // No "zl.#in_progress" terminals in successful compression
            EXPECT_NE(codec.name, "zl.#in_progress")
                    << "Successful compression should not have "
                       "zl.#in_progress terminals";
        }

        // No graph should have gFailureString
        for (const auto& g : chunk.graphs) {
            EXPECT_TRUE(g.gFailureString.empty())
                    << "Graph '" << g.gName
                    << "' should not have gFailureString in successful "
                       "compression, got: "
                    << g.gFailureString;
        }

        // Should have "zl.store" terminal codecs
        bool hasStore = false;
        for (const auto& codec : chunk.codecs) {
            if (codec.name == "zl.store") {
                hasStore = true;
            }
        }
        EXPECT_TRUE(hasStore)
                << "Successful compression should have zl.store terminals";
    }
}

// Step 6: Verify that a segmenter failure is captured in the trace.
// The segmenter is represented as a pseudo-codec named "segmenter" in the
// trace. When the segmenter function returns an error, on_segmenterEncode_end
// sets cFailure on that pseudo-codec.
TEST_F(CompressTraceErrorTest, SegmenterFailure)
{
    // A segmenter that immediately fails without processing any chunks.
    static auto failingSegmenterFn = [](ZL_Segmenter* sctx) -> ZL_Report {
        ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);
        ZL_ERR(transform_executionFailure, "deliberate segmenter failure");
    };

    static const ZL_Type segInputTypes[]               = { ZL_Type_numeric };
    static const ZL_SegmenterDesc failingSegmenterDesc = {
        .name                = "FailingSegmenter",
        .segmenterFn         = failingSegmenterFn,
        .inputTypeMasks      = segInputTypes,
        .numInputs           = 1,
        .lastInputIsVariable = false,
    };

    ZL_GraphID segId = ZL_Compressor_registerSegmenter(
            compressor_.get(), &failingSegmenterDesc);
    ASSERT_TRUE(ZL_GraphID_isValid(segId));
    compressor_.selectStartingGraph(segId);

    std::vector<int64_t> data(1000, 42);
    auto input = Input::refNumeric(data.data(), sizeof(int64_t), data.size());

    auto parsed       = compressExpectFailAndParse(input);
    const auto& chunk = parsed.chunks[0];

    // Find the segmenter pseudo-codec
    auto* segmenterCodec = findCodec(chunk, "segmenter");
    ASSERT_NE(segmenterCodec, nullptr)
            << "Should find a 'segmenter' pseudo-codec in the trace";

    EXPECT_FALSE(segmenterCodec->cFailureString.empty())
            << "Failed segmenter should have a non-empty cFailureString";
    EXPECT_NE(
            segmenterCodec->cFailureString.find("deliberate segmenter failure"),
            std::string::npos)
            << "cFailureString should contain the error message, got: "
            << segmenterCodec->cFailureString;

    // No "zl.store" terminals — compression failed
    for (const auto& codec : chunk.codecs) {
        EXPECT_NE(codec.name, "zl.store")
                << "Failed compression should not have zl.store terminals";
    }
}

} // namespace openzl

#endif // ZL_ALLOW_INTROSPECTION
