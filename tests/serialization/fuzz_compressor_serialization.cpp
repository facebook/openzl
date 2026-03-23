// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/Input.hpp"
#include "openzl/zl_reflection.h"

#include "security/lionhead/utils/lib_ftest/ftest.h" // @manual
#include "tests/datagen/structures/CompressorProducer.h"
#include "tests/fuzz_utils.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/serialization/GraphBuilder.h"
#include "tests/serialization/GraphBuilderUtils.h"
#include "tests/utils.h"

namespace openzl::tests {
namespace {

FUZZ(CompressorSerializationTest, FuzzDeserializeAndCompressSimple)
{
    // Must at least have 4 bytes for the random seed and 2 bytes for the
    // component ID.
    if (Size < 6) {
        return;
    }
    uint32_t randomSeed = *reinterpret_cast<const uint32_t*>(Data);
    OpenZLComponentID componentId =
            (OpenZLComponentID) * reinterpret_cast<const uint16_t*>(Data + 4);
    if (componentId >= OpenZLComponentID::NumComponents) {
        return;
    }
    std::string serialized = std::string((const char*)Data + 6, Size - 6);
    datagen::DataGen gen(randomSeed);
    // Deserialize the graph
    Compressor deserializedCompressor;
    // Register all components to ensure deserialization can happen
    for (const auto& component : getAllOpenZLComponents()) {
        component->registerComponent(deserializedCompressor);
    }
    try {
        deserializedCompressor.deserialize(serialized);

        ZL_GraphID startingGraph;
        ZL_Compressor_getStartingGraphID(
                deserializedCompressor.get(), &startingGraph);

        auto openzlComponent = makeOpenZLComponent(componentId);

        std::vector<std::unique_ptr<OpenZLInput>> openzlInputs;
        try {
            auto inputs = openzlComponent->generateInputs(
                    gen,
                    1 /* num */,
                    4096 /* maxInputSize */,
                    deserializedCompressor,
                    startingGraph);
            openzlInputs = std::move(inputs);
        } catch (const std::exception&) {
            // Exceptions in generateInputs can happen due to invalid
            // localParams. This is acceptable if caught and an error is thrown.
        }
        for (auto& predfinedInput : openzlComponent->predefinedInputs()) {
            openzlInputs.push_back(std::move(predfinedInput));
        }
        // Also generate one random compatible input
        openzlInputs.push_back(generateInputCompatibleWithGraph(
                deserializedCompressor, gen, startingGraph));
        std::string compressed;
        for (auto& openzlInput : openzlInputs) {
            auto inputs = openzlInput->inputs();
            compressed.resize(openzlComponent->compressBound(inputs));

            CCtx cctx;
            DCtx dctx;
            testRoundTrip(
                    compressed,
                    deserializedCompressor,
                    cctx,
                    dctx,
                    startingGraph,
                    ZL_MAX_FORMAT_VERSION,
                    inputs);
        }
    } catch (const Exception&) {
        // Ignore the exception as long as ZL_Result is returned since not
        // crashing is acceptable.
    }
}

FUZZ(CompressorSerializationTest, FuzzDeserializeAndCompressSimpleRestricted)
{
    // Must have at least 4 bytes to use for the random seed.
    if (Size < 4) {
        return;
    }
    uint32_t randomSeed = *reinterpret_cast<const uint32_t*>(Data);
    std::mt19937 seededGen(randomSeed);

    datagen::DataGen gen = fromFDP(f);
    // Use openzl components to additionally produce a corpus
    auto openzlComponents = getAllOpenZLComponents();
    while (gen.has_more_data()) {
        size_t componentIdx = gen.usize_range(
                "component_idx", 0, openzlComponents.size() - 1);
        auto& openzlComponent = openzlComponents[componentIdx];
        if (!openzlComponent->supportsSerialization()) {
            continue;
        }
        Compressor compressor;
        std::vector<GraphID> graphs =
                openzlComponent->predefinedGraphs(compressor);
        for (auto graph : openzlComponent->generateGraphs(compressor, gen, 1)) {
            graphs.push_back(graph);
        }

        for (auto node : openzlComponent->predefinedNodes(compressor)) {
            graphs.push_back(buildTrivialGraph(compressor.get(), node));
        }
        for (auto node : openzlComponent->generateNodes(compressor, gen, 1)) {
            graphs.push_back(buildTrivialGraph(compressor.get(), node));
        }
        if (graphs.empty()) {
            continue;
        }
        std::shuffle(graphs.begin(), graphs.end(), seededGen);
        // Reuse cctx and dctx for each component
        CCtx cctx;
        DCtx dctx;
        openzlComponent->registerComponent(dctx);
        // Test all graphs in list
        for (auto graph : graphs) {
            if (!gen.has_more_data()) {
                break;
            }
            compressor.selectStartingGraph(graph);
            auto serialized = compressor.serialize();

            // Deserialize the graph
            Compressor deserializedCompressor;
            // Register the component in case it is non-standard
            openzlComponent->registerComponent(deserializedCompressor);
            try {
                deserializedCompressor.deserialize(serialized);
            } catch (const std::exception&) {
                throw std::runtime_error(
                        "Deserialization failed for component: "
                        + openzlComponent->name());
            }
            ZL_GraphID startingGraph{ ZL_GRAPH_ILLEGAL };
            ZL_Compressor_getStartingGraphID(
                    deserializedCompressor.get(), &startingGraph);

            // Compress with the deserialized graph using generated
            // inputs and predefined inputs
            const size_t inputSize = gen.usize_range("input_size", 1, 4096);
            auto openzlInputs      = openzlComponent->generateInputs(
                    gen, 1, inputSize, deserializedCompressor, startingGraph);
            for (auto& predfinedInput : openzlComponent->predefinedInputs()) {
                openzlInputs.push_back(std::move(predfinedInput));
            }
            if (openzlInputs.empty()) {
                continue;
            }
            for (const auto& input : openzlInputs) {
                auto inputs = input->inputs();
                if (inputs.empty()) {
                    continue;
                }
                std::string compressed;
                compressed.resize(openzlComponent->compressBound(inputs));

                testRoundTrip(
                        compressed,
                        deserializedCompressor,
                        cctx,
                        dctx,
                        startingGraph,
                        ZL_MAX_FORMAT_VERSION,
                        inputs);
            }
        }
    }
}

FUZZ(CompressorSerializationTest, FuzzRandomGraphsSerializesAndCompresses)
{
    datagen::DataGen dg = fromFDP(f);
    Compressor compressor;
    auto input = dg.randString("input");
    GraphBuilder builder(dg, compressor);
    builder.addAllComponents();
    builder.buildCompressor();
    // Do a serialization round trip on compressor
    auto serialized = compressor.serialize();
    Compressor deserializedCompressor;
    for (const auto& component : getAllOpenZLComponents()) {
        component->registerComponent(deserializedCompressor);
    }
    deserializedCompressor.deserialize(serialized);

    // Try a compression round trip
    try {
        ZL_GraphID startingGraph;
        ZL_Compressor_getStartingGraphID(
                deserializedCompressor.get(), &startingGraph);
        auto openzlInput = generateInputCompatibleWithGraph(
                deserializedCompressor, dg, startingGraph);
        std::string compressed;
        compressed.resize(getCompressedBound(openzlInput));
        CCtx cctx;
        DCtx dctx;
        for (const auto& component : getAllOpenZLComponents()) {
            component->registerComponent(dctx);
        }
        testRoundTrip(
                compressed,
                deserializedCompressor,
                cctx,
                dctx,
                startingGraph,
                ZL_MAX_FORMAT_VERSION,
                openzlInput->inputs());
    } catch (const Exception&) {
        // Ignore the exception as long as ZL_Result is returned since not
        // crashing is acceptable.
    }
}

FUZZ(CompressorSerializationTest, FuzzDeserializeAndCompressRandom)
{
    // Must at least have 4 bytes for the random seed
    if (Size < 4) {
        return;
    }
    Compressor compressor;
    std::string serialized(reinterpret_cast<const char*>(Data), Size);
    // Use the first 4 bytes as a seed for the random number generator
    uint32_t randSeed = *(const uint32_t*)Data;
    datagen::DataGen gen(randSeed);

    // Deserialize the graph
    Compressor deserializedCompressor;
    try {
        deserializedCompressor.deserialize(serialized);
        // Compress with the deserialized graph using generated inputs

        CCtx cctx;
        DCtx dctx;
        ZL_GraphID startingGraph;
        ZL_Compressor_getStartingGraphID(
                deserializedCompressor.get(), &startingGraph);
        auto input = generateInputCompatibleWithGraph(
                deserializedCompressor, gen, startingGraph);
        std::string compressed;
        compressed.resize(getCompressedBound(input));
        testRoundTrip(
                compressed,
                deserializedCompressor,
                cctx,
                dctx,
                startingGraph,
                ZL_MAX_FORMAT_VERSION,
                input->inputs());
    } catch (const Exception&) {
        // Ignore the exception as long as ZL_Result is returned since not
        // crashing is acceptable.
    }
}

FUZZ(CompressorSerializationTest, FuzzDeserialization)
{
    std::string input = gen_str(f, "input_data", InputLengthInBytes(1));

    try {
        Compressor compressor;
        compressor.deserialize(input);
    } catch (const Exception&) {
        // Ignore the exception as long as ZL_Result is returned since it is
        // valid to deserialize unsucessfully
    }
}

FUZZ(CompressorSerializationTest, FuzzRandomCompressorDeserializesSuccessfully)
{
    datagen::DataGen dg = fromFDP(f);
    auto rw             = dg.getRandWrapper();
    auto producer       = datagen::CompressorProducer(rw);
    auto zlCompressor   = producer.make();
    CompressorRef compressor(zlCompressor.get());
    auto serialized = compressor.serialize();
    compressor.deserialize(serialized);
}
} // namespace
} // namespace openzl::tests
