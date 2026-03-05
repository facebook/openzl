// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/zl_reflection.h"

#include "security/lionhead/utils/lib_ftest/ftest.h" // @manual
#include "tests/datagen/structures/CompressorProducer.h"
#include "tests/fuzz_utils.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/utils.h"

namespace openzl::tests {
namespace {

/**
 * Partitions a string into random segments and returns the lengths of each
 * segment. This is done by numbering a number of segments, and selecting the
 * desired number without replacement.
 *
 * @param input The string to partition. Will be shuffled in place.
 * @param gen Random number generator for determining segment count and
 * shuffling.
 * @return Vector of segment lengths that sum to input.size(), or empty if
 *         input is empty.
 */
std::vector<uint32_t> getPartitionedStringLengths(
        std::string& input,
        datagen::DataGen& gen)
{
    if (input.size() == 0) {
        return {};
    }
    int numSegments = gen.i32_range(
            "num_segments", 1, std::min<size_t>(50, input.size()));
    if (numSegments == 1) {
        return { (uint32_t)input.size() };
    }
    std::vector<uint32_t> partitions;
    partitions.reserve(input.size());
    for (size_t i = 1; i < input.size(); i++) {
        partitions.push_back(i);
    }
    std::mt19937 rng(gen.u32("shuffle"));
    std::shuffle(partitions.begin(), partitions.end(), rng);
    std::sort(partitions.begin(), partitions.begin() + numSegments - 1);
    std::vector<uint32_t> partitionLengths(numSegments);
    partitionLengths[0] = partitions[0];
    for (size_t i = 1; i < numSegments - 1; i++) {
        partitionLengths[i] = partitions[i] - partitions[i - 1];
    }
    partitionLengths[numSegments - 1] =
            input.size() - partitions[numSegments - 2];
    return partitionLengths;
}

/**
 * Generates a random input that is compatible with the given graph's input
 * requirements. Inspects the graph's input mask to determine valid input types
 * and generates random data of a compatible type.
 *
 * @param compressor The compressor containing the graph.
 * @param gen Random number generator for input generation.
 * @param startingGraph The graph ID to generate compatible input for.
 * @return A unique pointer to an OpenZLInput compatible with the graph.
 */
std::unique_ptr<OpenZLInput> generateGraphCompatibleInput(
        Compressor& compressor,
        datagen::DataGen& gen,
        ZL_GraphID startingGraph)
{
    std::vector<std::unique_ptr<OpenZLInput>> openzlInputs;
    auto numInputs =
            ZL_Compressor_Graph_getNumInputs(compressor.get(), startingGraph);
    // Generate inputs of the correct type for non variable inputs
    for (size_t i = 0; i < numInputs; i++) {
        std::unique_ptr<OpenZLInput> randomInput;
        std::vector<ZL_Type> choices;
        auto mask = ZL_Compressor_Graph_getInputMask(
                compressor.get(), startingGraph, i);
        for (size_t shift = 0; shift < 4; shift++) {
            auto type = (ZL_Type)(1 << shift);
            if (mask & type) {
                choices.push_back(type);
            }
        }
        auto type = gen.choices("type", choices);
        switch (type) {
            case ZL_Type_serial: {
                auto inputStr = gen.randString("input", 4096);
                randomInput   = SerialOpenZLInput::make(inputStr);
                break;
            }
            case ZL_Type_numeric: {
                auto width =
                        gen.choices("width", (std::vector<int>){ 1, 2, 4, 8 });
                auto inputStr =
                        gen.randStringWithQuantizedLength("input", 4096, width);
                randomInput = makeNumericInput(inputStr, width);
                break;
            }
            case ZL_Type_struct: {
                auto width =
                        gen.choices("width", (std::vector<int>){ 1, 2, 4, 8 });
                auto inputStr =
                        gen.randStringWithQuantizedLength("input", 4096, width);
                randomInput = StructOpenZLInput::make(inputStr, width);
                break;
            }
            case ZL_Type_string: {
                auto inputStr = gen.randString("input", 4096);
                randomInput   = StringOpenZLInput::make(
                        inputStr, getPartitionedStringLengths(inputStr, gen));
                break;
            }
            default:
                throw std::runtime_error("Unexpected type!");
        }
        if (numInputs == 1) {
            return randomInput;
        } else {
            openzlInputs.push_back(std::move(randomInput));
        }
    }
    return MultiOpenZLInput::make(std::move(openzlInputs));
}

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
        openzlInputs.push_back(generateGraphCompatibleInput(
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
