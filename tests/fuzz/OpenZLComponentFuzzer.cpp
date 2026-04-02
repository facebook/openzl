// (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/limits.h"
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/zl_reflection.h"
#include "tests/datagen/DataGen.h"
#include "tests/fuzz_utils.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/utils.h"

namespace openzl::tests {
namespace {

constexpr bool kDebug = false;

void fuzzRoundTrip(
        const OpenZLComponent& component,
        Compressor& compressor,
        const OpenZLInput& input,
        GraphID graph,
        int formatVersion)
{
    auto inputs = input.inputs();
    if (kDebug) {
        fprintf(stderr, "# inputs = %zu\n", inputs.size());
        for (size_t i = 0; i < inputs.size(); ++i) {
            fprintf(stderr,
                    "input %zu: type=%d, width=%zu, numElts=%zu, contentSize=%zu\n",
                    i,
                    int(inputs[i].type()),
                    inputs[i].eltWidth(),
                    inputs[i].numElts(),
                    inputs[i].contentSize());
        }
    }
    std::string compressed;
    compressed.resize(component.compressBound(inputs));
    if (kDebug) {
        fprintf(stderr, "compressed bound = %zu\n", compressed.size());
    }
    if (inputs.empty()) {
        return;
    }

    CCtx cctx;
    DCtx dctx;
    component.registerComponent(dctx);
    testRoundTrip(
            compressed, compressor, cctx, dctx, graph, formatVersion, inputs);
}

GraphID getGraph(
        const OpenZLComponent& component,
        datagen::DataGen& gen,
        Compressor& compressor)
{
    std::vector<GraphID> graphs = component.predefinedGraphs(compressor);
    for (auto graph : component.generateGraphs(compressor, gen, 1)) {
        graphs.push_back(graph);
    }
    for (auto node : component.predefinedNodes(compressor)) {
        graphs.push_back(buildTrivialGraph(compressor.get(), node));
    }
    for (auto node : component.generateNodes(compressor, gen, 1)) {
        graphs.push_back(buildTrivialGraph(compressor.get(), node));
    }
    return gen.choices("graph", graphs);
}

int getFormatVersion(const OpenZLComponent& component, datagen::DataGen& gen)
{
    auto min = std::max(component.minFormatVersion(), ZL_MIN_FORMAT_VERSION);
    auto max = std::min(component.maxFormatVersion(), ZL_MAX_FORMAT_VERSION);
    if (min > max) {
        throw std::runtime_error("Invalid format version range");
    }
    return gen.i32_range("format_version", min, max);
}

std::unique_ptr<OpenZLInput>
generateInput(datagen::DataGen& gen, TypeMask typeMask, int formatVersion)
{
    std::vector<Type> supportedTypes;
    if ((typeMask & TypeMask::Serial) != TypeMask::None) {
        supportedTypes.push_back(Type::Serial);
    }
    if ((typeMask & TypeMask::Struct) != TypeMask::None) {
        supportedTypes.push_back(Type::Struct);
    }
    if ((typeMask & TypeMask::Numeric) != TypeMask::None) {
        supportedTypes.push_back(Type::Numeric);
    }
    if ((typeMask & TypeMask::String) != TypeMask::None
        && formatVersion >= 10) {
        supportedTypes.push_back(Type::String);
    }
    auto type = gen.choices("type", supportedTypes);
    switch (type) {
        case Type::Serial:
            return SerialOpenZLInput::make(gen.randString("serial_data"));
        case Type::Struct: {
            auto width = gen.i32_range("struct_width", 1, 10);
            return StructOpenZLInput::make(
                    gen.randStringWithQuantizedLength("struct_data", width),
                    width);
        }
        case Type::Numeric: {
            auto width =
                    gen.choices("num_width", std::vector<size_t>{ 1, 2, 4, 8 });
            return makeNumericInput(
                    gen.randStringWithQuantizedLength("num_data", width),
                    width);
        }
        case Type::String: {
            auto content = gen.randString("string_content");
            std::vector<uint32_t> lens;
            size_t totalLen = 0;
            while (gen.has_more_data() && totalLen < content.size()) {
                auto len =
                        gen.u32_range("str_len", 0, content.size() - totalLen);
                lens.push_back(len);
                totalLen += len;
            }
            if (totalLen < content.size()) {
                lens.push_back(content.size() - totalLen);
            }
            return StringOpenZLInput::make(std::move(content), std::move(lens));
        }
    }
}

std::unique_ptr<OpenZLInput> generateInput(
        datagen::DataGen& gen,
        const Compressor& compressor,
        GraphID graph,
        int formatVersion)
{
    const auto numInputs =
            ZL_Compressor_Graph_getNumInputs(compressor.get(), graph);
    const auto isVariableInput =
            ZL_Compressor_Graph_isVariableInput(compressor.get(), graph);

    std::vector<std::unique_ptr<OpenZLInput>> inputs;
    for (size_t i = 0; i < numInputs; ++i) {
        if (i + 1 == numInputs && isVariableInput) {
            while (gen.has_more_data() && gen.boolean("more_variable_inputs")
                   && inputs.size() < ZL_runtimeInputLimit(formatVersion)) {
                inputs.push_back(generateInput(
                        gen,
                        TypeMask(ZL_Compressor_Graph_getInputMask(
                                compressor.get(), graph, i)),
                        formatVersion));
            }
        } else {
            inputs.push_back(generateInput(
                    gen,
                    TypeMask(ZL_Compressor_Graph_getInputMask(
                            compressor.get(), graph, 0)),
                    formatVersion));
        }
    }

    if (inputs.size() == 1) {
        return std::move(inputs[0]);
    } else {
        return MultiOpenZLInput::make(std::move(inputs));
    }
}
} // namespace

/**
 * Fuzz on inputs generated by the component.
 * These inputs are guaranteed to be valid inputs for the component,
 * so we can check that compression always succeeds.
 */
FUZZ(OpenZLComponentFuzzer, FuzzRoundTrip)
{
    datagen::DataGen gen = fromFDP(f);
    auto id              = gen.i32_range(
            "component", 0, int(OpenZLComponentID::NumComponents) - 1);
    auto component = makeOpenZLComponent(OpenZLComponentID(id));

    Compressor compressor;
    component->registerComponent(compressor);
    compressor.setParameter(openzl::CParam::MinStreamSize, -1);
    compressor.setParameter(
            openzl::CParam::StoreOnExpansion, ZL_TernaryParam_disable);
    // Compression must succeed on inputs generated by the component
    compressor.setParameter(openzl::CParam::PermissiveCompression, 0);

    auto graph         = getGraph(*component, gen, compressor);
    auto formatVersion = getFormatVersion(*component, gen);

    if (kDebug) {
        fprintf(stderr,
                "component = %s, formatVersion = %d\n",
                component->name().c_str(),
                formatVersion);
    }

    while (gen.has_more_data()) {
        const size_t size = gen.usize_range("input_size", 1, 4096);
        auto inputs =
                component->generateInputs(gen, 1, size, compressor, graph);
        if (inputs.empty()) {
            break;
        }
        for (const auto& input : inputs) {
            fuzzRoundTrip(*component, compressor, *input, graph, formatVersion);
        }
    }
}

/**
 * Fuzz on randomly generated inputs.
 * We ensure the types match the component's inputs, but the
 * inputs are generated completely by the fuzzer, and aren't
 * guaranteed to be valid inputs for the component, so we
 * enable permissive mode, since compression is allowed to fail.
 */
FUZZ(OpenZLComponentFuzzer, FuzzCompress)
{
    datagen::DataGen gen = fromFDP(f);
    auto id              = gen.i32_range(
            "component", 0, int(OpenZLComponentID::NumComponents) - 1);
    auto component = makeOpenZLComponent(OpenZLComponentID(id));

    Compressor compressor;
    component->registerComponent(compressor);
    compressor.setParameter(openzl::CParam::MinStreamSize, -1);
    compressor.setParameter(
            openzl::CParam::StoreOnExpansion, ZL_TernaryParam_disable);
    // No guarantee that compression will succeed
    compressor.setParameter(openzl::CParam::PermissiveCompression, 1);

    auto graph         = getGraph(*component, gen, compressor);
    auto formatVersion = getFormatVersion(*component, gen);

    if (kDebug) {
        fprintf(stderr,
                "component = %s, formatVersion = %d\n",
                component->name().c_str(),
                formatVersion);
    }

    while (gen.has_more_data()) {
        auto input = generateInput(gen, compressor, graph, formatVersion);
        try {
            // TODO: Permissive mode doesn't guarantee success when hitting
            // runtime limits on the number of nodes or streams.
            fuzzRoundTrip(*component, compressor, *input, graph, formatVersion);
        } catch (...) {
            // Raising exceptions is okay, just can't crash
        }
    }
}

/**
 * Fuzz decompression on random data.
 * This validates that decompression doesn't crash on invalid inputs.
 */
FUZZ(OpenZLComponentFuzzer, FuzzDecompress)
{
    DCtx dctx;
    for (const auto& component : getAllOpenZLComponents()) {
        component->registerComponent(dctx);
    }
    std::string_view data{ (const char*)Data, Size };
    try {
        (void)dctx.decompress(data);
    } catch (...) {
        // Raising exceptions is okay, just can't crash
    }
}

} // namespace openzl::tests
