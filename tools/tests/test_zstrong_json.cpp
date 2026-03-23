// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include <folly/FileUtil.h>
#include <folly/Memory.h>
#include <folly/testing/TestUtil.h>

#include "openzl/zl_data.h"
#include "tools/zstrong_cpp.h"
#include "tools/zstrong_json.h" // @manual

using namespace ::testing;

namespace zstrong {
namespace {
folly::dynamic store()
{
    return folly::dynamic::object()(kNameKey, "store");
}

folly::dynamic fse()
{
    return folly::dynamic::object()(kNameKey, "fse");
}

folly::dynamic fieldLz()
{
    return folly::dynamic::object()(kNameKey, "field_lz");
}

folly::dynamic delta(const folly::dynamic& successor)
{
    folly::dynamic graph  = folly::dynamic::object();
    graph[kNameKey]       = "delta_int";
    graph[kSuccessorsKey] = folly::dynamic::array(successor);
    return graph;
}

folly::dynamic tokenize(
        const folly::dynamic& alphabet,
        const folly::dynamic& indices)
{
    folly::dynamic graph = folly::dynamic::object();
    graph[kNameKey]      = "tokenize";
    graph[kIntParamsKey] =
            folly::dynamic::object()(0, int(ZL_Type_struct))(1, int(false));
    graph[kSuccessorsKey] = folly::dynamic::array(alphabet, indices);
    return graph;
}

folly::dynamic tokenizeSorted(
        const folly::dynamic& alphabet,
        const folly::dynamic& indices)
{
    folly::dynamic graph = folly::dynamic::object();
    graph[kNameKey]      = "tokenize";
    graph[kIntParamsKey] =
            folly::dynamic::object()(0, int(ZL_Type_numeric))(1, int(true));
    graph[kSuccessorsKey] = folly::dynamic::array(alphabet, indices);
    return graph;
}

folly::dynamic convertSerialToToken(
        size_t eltWidth,
        const folly::dynamic& successor)
{
    folly::dynamic graph  = folly::dynamic::object();
    graph[kNameKey]       = "convert_serial_to_token";
    graph[kIntParamsKey]  = folly::dynamic::object()(1, eltWidth);
    graph[kSuccessorsKey] = folly::dynamic::array(successor);
    return graph;
}

folly::dynamic interpretAsIntLE(
        size_t eltWidth,
        const folly::dynamic& successor)
{
    folly::dynamic graph  = folly::dynamic::object();
    graph[kNameKey]       = "interpret_as_le" + std::to_string(8 * eltWidth);
    graph[kSuccessorsKey] = folly::dynamic::array(successor);
    return graph;
}

folly::dynamic bruteForce(std::initializer_list<folly::dynamic> successors)
{
    folly::dynamic graph  = folly::dynamic::object();
    graph[kNameKey]       = "brute_force";
    graph[kSuccessorsKey] = folly::dynamic::array();
    for (auto const& successor : successors) {
        graph[kSuccessorsKey].push_back(successor);
    }
    return graph;
}

folly::dynamic extract(std::string_view prefix, const folly::dynamic& successor)
{
    folly::dynamic graph           = folly::dynamic::object();
    graph[kNameKey]                = "extract";
    graph[kSuccessorsKey]          = folly::dynamic::array(successor);
    graph[kGenericStringParamsKey] = folly::dynamic::object()(1, prefix);
    return graph;
}

class EveryOtherTransform : public CustomTransform {
   public:
    using CustomTransform::CustomTransform;

    ZL_Report encode(
            ZL_Encoder* eictx,
            ZL_Input const* inputs[],
            size_t nbInputs) const override
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
        ZL_ERR_IF_NE(nbInputs, 1, node_invalid_input);
        ZL_ERR_IF_NULL(inputs, node_invalid_input);
        const auto* input   = inputs[0];
        size_t const nbElts = ZL_Input_numElts(input);
        ZL_Output* output0 =
                ZL_Encoder_createTypedStream(eictx, 0, (nbElts + 1) / 2, 1);
        ZL_Output* output1 =
                ZL_Encoder_createTypedStream(eictx, 1, nbElts / 2, 1);
        if (output0 == nullptr || output1 == nullptr) {
            return ZL_REPORT_ERROR(allocation);
        }

        uint8_t const* in = (uint8_t const*)ZL_Input_ptr(input);
        uint8_t* out0     = (uint8_t*)ZL_Output_ptr(output0);
        uint8_t* out1     = (uint8_t*)ZL_Output_ptr(output1);
        uint8_t* out[]    = { out0, out1 };
        for (size_t i = 0; i < nbElts; ++i) {
            *out[i % 2]++ = in[i];
        }

        ZL_ERR_IF_ERR(ZL_Output_commit(output0, (nbElts + 1) / 2));
        ZL_ERR_IF_ERR(ZL_Output_commit(output1, nbElts / 2));

        return ZL_returnValue(2);
    }

    ZL_Report decode(ZL_Decoder* dictx, ZL_Input const* inputs[]) const override
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
        size_t const nbElts0 = ZL_Input_numElts(inputs[0]);
        size_t const nbElts1 = ZL_Input_numElts(inputs[1]);
        uint8_t const* in0   = (uint8_t const*)ZL_Input_ptr(inputs[0]);
        uint8_t const* in1   = (uint8_t const*)ZL_Input_ptr(inputs[1]);
        uint8_t const* ins[] = { in0, in1 };

        size_t const nbElts = nbElts0 + nbElts1;
        ZL_Output* output   = ZL_Decoder_create1OutStream(dictx, nbElts, 1);
        if (output == nullptr) {
            return ZL_REPORT_ERROR(allocation);
        }

        if (nbElts0 < nbElts1 || nbElts0 > nbElts1 + 1) {
            return ZL_REPORT_ERROR(corruption);
        }

        uint8_t* out = (uint8_t*)ZL_Output_ptr(output);
        for (size_t i = 0; i < nbElts; ++i) {
            out[i] = *ins[i % 2]++;
        }

        ZL_ERR_IF_ERR(ZL_Output_commit(output, nbElts));

        return ZL_returnValue(1);
    }

    size_t nbInputs() const override
    {
        return 1;
    }
    size_t nbSuccessors() const override
    {
        return 2;
    }

    ZL_Type inputType(size_t) const override
    {
        return ZL_Type_serial;
    }

    ZL_Type outputType(size_t _) const override
    {
        return inputType(_);
    }

    std::string description() const override
    {
        return "Puts even indexed elements in stream 0, and odd indexed elements in stream 1";
    }
};

class DelayedDecodeTransfom : public CustomTransform {
   public:
    explicit DelayedDecodeTransfom(size_t milliseconds, ZL_IDType transformID)
            : CustomTransform(transformID), milliseconds_(milliseconds)
    {
    }

    ZL_Report encode(
            ZL_Encoder* eictx,
            ZL_Input const* inputs[],
            size_t nbInputs) const override
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
        ZL_ERR_IF_NE(nbInputs, 1, node_invalid_input);
        ZL_ERR_IF_NULL(inputs, node_invalid_input);
        const auto* input   = inputs[0];
        size_t const nbElts = ZL_Input_numElts(input);
        ZL_Output* output   = ZL_Encoder_createTypedStream(eictx, 0, nbElts, 1);

        if (output == nullptr) {
            return ZL_REPORT_ERROR(allocation);
        }

        uint8_t const* in = (uint8_t const*)ZL_Input_ptr(input);
        uint8_t* out      = (uint8_t*)ZL_Output_ptr(output);
        memcpy(out, in, nbElts);

        ZL_ERR_IF_ERR(ZL_Output_commit(output, nbElts));

        return ZL_returnValue(1);
    }

    ZL_Report decode(ZL_Decoder* dictx, ZL_Input const* inputs[]) const override
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
        size_t const nbElts = ZL_Input_numElts(inputs[0]);
        uint8_t const* in   = (uint8_t const*)ZL_Input_ptr(inputs[0]);

        ZL_Output* output = ZL_Decoder_create1OutStream(dictx, nbElts, 1);
        if (output == nullptr) {
            return ZL_REPORT_ERROR(allocation);
        }

        uint8_t* out = (uint8_t*)ZL_Output_ptr(output);
        memcpy(out, in, nbElts);

        ZL_ERR_IF_ERR(ZL_Output_commit(output, nbElts));

        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds_));

        return ZL_returnValue(1);
    }

    size_t nbInputs() const override
    {
        return 1;
    }
    size_t nbSuccessors() const override
    {
        return 1;
    }

    ZL_Type inputType(size_t) const override
    {
        return ZL_Type_serial;
    }

    ZL_Type outputType(size_t idx) const override
    {
        return inputType(idx);
    }

    std::string description() const override
    {
        return "Doesn't change the data, only adds some delay";
    }

   private:
    size_t milliseconds_;
};

class PickSecondSelector : public CustomSelector {
   public:
    ZL_GraphID select(
            ZL_Selector const*,
            ZL_Input const*,
            std::span<ZL_GraphID const> successors) const override
    {
        return successors[1];
    }

    std::optional<size_t> expectedNbSuccessors() const override
    {
        return 2;
    }

    ZL_Type inputType() const override
    {
        return ZL_Type_numeric;
    }

    std::string description() const override
    {
        return "Picks the 2nd stream";
    }
};

void testRoundTrip(
        std::string_view data,
        folly::dynamic json,
        ZL_Type inputType                            = ZL_Type_serial,
        std::optional<TransformMap> customTransforms = std::nullopt,
        std::optional<GraphMap> customGraphs         = std::nullopt,
        std::optional<SelectorMap> customSelectors   = std::nullopt)
{
    JsonGraph graph(
            std::move(json),
            inputType,
            std::move(customTransforms),
            std::move(customGraphs),
            std::move(customSelectors));
    auto compressed   = compress(data, graph);
    auto decompressed = decompress(compressed, graph);
    ASSERT_EQ(data, decompressed);
}
} // namespace

TEST(ZstrongJsonTest, StoreGraph)
{
    auto graph = store();
    testRoundTrip("hello world I am some data!", graph);
}

TEST(ZstrongJsonTest, SimpleGraph)
{
    auto graph = convertSerialToToken(2, tokenize(store(), fse()));
    testRoundTrip("00010001000100000001020304050404040302fffffef0fe", graph);
}

TEST(ZstrongJsonTest, BruteForceGraph)
{
    auto tok      = tokenizeSorted(delta(fieldLz()), fieldLz());
    auto selector = bruteForce({ tok, fieldLz(), fse() });
    auto graph    = interpretAsIntLE(2, selector);
    testRoundTrip("00010001000100000001020304050404040302fffffef0fe", graph);
}

TEST(ZstrongJsonTest, CustomTransformGraph)
{
    TransformMap customTransforms;
    customTransforms["every_other"] = std::make_unique<EveryOtherTransform>(0);

    folly::dynamic graph = folly::dynamic::object(kNameKey, "every_other");
    graph[kSuccessorsKey] =
            folly::dynamic::array(fse(), interpretAsIntLE(1, delta(fse())));

    testRoundTrip(
            "0a0b0c0d0e0f0g0h0i0j0k0l0m0n0p0q0r0s0t0u0v0w0x0y0z",
            graph,
            ZL_Type_serial,
            std::move(customTransforms));
}

TEST(ZstrongJsonTest, CustomSelectorGraph)
{
    SelectorMap customSelectors;
    customSelectors["pick_second"] = std::make_unique<PickSecondSelector>();

    folly::dynamic graph  = folly::dynamic::object(kNameKey, "pick_second");
    graph[kSuccessorsKey] = folly::dynamic::array(store(), delta(store()));

    graph = interpretAsIntLE(1, graph);

    testRoundTrip(
            "01234567890000000000000000000000000000",
            graph,
            ZL_Type_serial,
            std::nullopt,
            std::nullopt,
            std::move(customSelectors));
}

TEST(ZstrongJsonTest, CustomGraphGraph)
{
    GraphMap customGraphs;
    {
        folly::dynamic graph = tokenizeSorted(delta(fieldLz()), fieldLz());
        customGraphs.emplace(
                "numeric", std::make_unique<JsonGraph>(graph, ZL_Type_numeric));
        graph = tokenize(fieldLz(), delta(fieldLz()));
        customGraphs.emplace(
                "fixed", std::make_unique<JsonGraph>(graph, ZL_Type_struct));
        graph = fse();
        customGraphs.emplace(
                "serial", std::make_unique<JsonGraph>(graph, ZL_Type_serial));
    }
    folly::dynamic const numeric =
            folly::dynamic::object()(kNameKey, "numeric");
    folly::dynamic const fixed  = folly::dynamic::object()(kNameKey, "fixed");
    folly::dynamic const serial = folly::dynamic::object()(kNameKey, "serial");
    auto graph                  = bruteForce(
            { serial,
                               convertSerialToToken(2, fixed),
                               interpretAsIntLE(2, numeric),
                               convertSerialToToken(
                      2,
                      tokenize(
                              bruteForce({ serial, fixed }),
                              bruteForce({ serial, numeric }))) });
    testRoundTrip(
            "0a0a0a0a0a0a0a0b0b0b0b0c0d0c0c0c0f0f0f0e0e0e0e0efffffffffefefefe",
            graph,
            ZL_Type_serial,
            std::nullopt,
            std::move(customGraphs));
}

TEST(ZstrongJsonTest, ExtractGraph)
{
    folly::test::TemporaryDirectory tmpDir;
    auto path = (tmpDir.path() / "extract").string();

    TransformMap customTransforms;
    customTransforms["every_other"] = std::make_unique<EveryOtherTransform>(0);

    auto graph0           = extract(path, store());
    auto graph1           = extract(path, store());
    graph1                = extract(path, interpretAsIntLE(1, delta(graph1)));
    folly::dynamic graph  = folly::dynamic::object(kNameKey, "every_other");
    graph[kSuccessorsKey] = folly::dynamic::array(graph0, graph1);

    testRoundTrip(
            "00010203040506070809",
            graph,
            ZL_Type_serial,
            std::move(customTransforms));

    std::string data;
    ASSERT_TRUE(folly::readFile(path.c_str(), data));

    auto const streams = splitExtractedStreams(data);
    ASSERT_EQ(streams.size(), 3);
    ASSERT_EQ(streams[0].type, ZL_Type_serial);
    ASSERT_EQ(streams[0].data, "0000000000");
    ASSERT_EQ(streams[1].type, ZL_Type_serial);
    ASSERT_EQ(streams[1].data, "0123456789");
    ASSERT_EQ(streams[2].type, ZL_Type_numeric);
    ASSERT_EQ(streams[2].data, "\x1\x1\x1\x1\x1\x1\x1\x1\x1");
}

TEST(ZstrongJsonTest, DISABLED_MeasureDecompressSpeed)
{
    auto measure = [](size_t input_size, size_t milliseconds) -> double {
        std::string data(input_size, 'x');
        TransformMap customTransforms;
        customTransforms["delay"] =
                std::make_unique<DelayedDecodeTransfom>(milliseconds, 0);
        folly::dynamic json  = folly::dynamic::object(kNameKey, "delay");
        json[kSuccessorsKey] = folly::dynamic::array(store());

        JsonGraph graph(json, ZL_Type_serial, std::move(customTransforms));

        return measureDecompressionSpeed(compress(data, graph), graph);
    };

    auto expectedSpeed = [](size_t input_size, size_t milliseconds) -> double {
        double const MB     = 1024 * 1024;
        double const sizeMb = (double)input_size / MB;
        double seconds      = (double)milliseconds / 1000.0;
        return sizeMb / seconds;
    };

    // Test that we have up to a 15% difference
    EXPECT_NEAR(measure(100, 10) / expectedSpeed(100, 10), 1, 0.15);
    EXPECT_NEAR(measure(1000, 20) / expectedSpeed(1000, 20), 1, 0.15);
}

} // namespace zstrong
