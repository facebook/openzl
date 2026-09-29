// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/BruteForce.hpp"
#include "openzl/cpp/codecs/Bitpack.hpp"
#include "openzl/cpp/codecs/Compress.hpp"
#include "openzl/cpp/codecs/Entropy.hpp"
#include "openzl/cpp/codecs/FieldLz.hpp"
#include "openzl/cpp/codecs/Store.hpp"
#include "openzl/cpp/codecs/Zstd.hpp"
#include "tests/datagen/structures/CompressibleStringProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"
#include "tests/utils.h"

namespace openzl::tests::components {
namespace {

/// Successors the selector picks from. They deliberately cover a mix of input
/// types: the selector must skip the ones that can't accept the input, and
/// fall back to store when none of them apply.
const std::vector<GraphID>& candidateSuccessors()
{
    static const auto* candidates = new std::vector<GraphID>{
        graphs::Store::graph,   graphs::Compress::graph, graphs::Zstd::graph,
        graphs::Entropy::graph, graphs::FieldLz::graph,  graphs::Bitpack::graph,
    };
    return *candidates;
}

class BruteForceComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "BruteForce";
    }

    int minFormatVersion() const override
    {
        return 10;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        return {
            // Single successor that accepts every type
            graphs::BruteForce{ graphs::Store::graph }.parameterize(compressor),
            graphs::BruteForce{ graphs::Compress::graph }.parameterize(
                    compressor),
            // Two successors that accept every type
            graphs::BruteForce{ graphs::Compress::graph, graphs::Store::graph }
                    .parameterize(compressor),
            // Successors that only accept a subset of the types
            graphs::BruteForce{ graphs::Zstd::graph,
                                graphs::Entropy::graph,
                                graphs::FieldLz::graph,
                                graphs::Bitpack::graph }
                    .parameterize(compressor),
            // All of the above
            graphs::BruteForce{ candidateSuccessors() }.parameterize(
                    compressor),
        };
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        const auto& candidates = candidateSuccessors();
        std::vector<GraphID> result;
        result.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            std::vector<GraphID> successors;
            for (const auto candidate : candidates) {
                if (gen.boolean("use_successor")) {
                    successors.push_back(candidate);
                }
            }
            if (successors.empty()) {
                successors.push_back(
                        candidates[gen.usize_range(
                                "successor", 0, candidates.size() - 1)]);
            }
            result.push_back(
                    graphs::BruteForce{ std::move(successors) }.parameterize(
                            compressor));
        }
        return result;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make("x"));
        inputs.push_back(SerialOpenZLInput::make(kLoremTestInput));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 1, 2, 3, 4, 5 }));
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06\x07\x08", 4));
        {
            std::vector<std::string> strs = { "hello", "world", "foo" };
            inputs.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    strs.data(), strs.size())));
        }
        return inputs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor&,
            GraphID) const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            switch (gen.usize_range("type", 0, 3)) {
                case 0: {
                    datagen::CompressibleStringProducer producer(
                            gen.getRandWrapper(),
                            gen.usize_range("input_size", 0, maxInputSize),
                            gen.u32_range("match_prob", 0, 100) / 100.0);
                    inputs.push_back(
                            SerialOpenZLInput::make(producer("input")));
                    break;
                }
                case 1: {
                    const auto maxElts = maxInputSize / sizeof(uint32_t);
                    inputs.push_back(
                            U32OpenZLInput::make(gen.randVector<uint32_t>(
                                    "values", 0, UINT32_MAX, maxElts)));
                    break;
                }
                case 2: {
                    const auto width = gen.usize_range("width", 1, 8);
                    inputs.push_back(
                            StructOpenZLInput::make(
                                    gen.randStringWithQuantizedLength(
                                            "data", maxInputSize, width),
                                    width));
                    break;
                }
                case 3: {
                    const auto size = gen.usize_range("size", 0, maxInputSize);
                    std::string data;
                    std::vector<uint32_t> lens;
                    while (gen.has_more_data() && data.size() < size) {
                        auto str = gen.randString("str", size - data.size());
                        lens.push_back((uint32_t)str.size());
                        data += str;
                    }
                    inputs.push_back(
                            std::make_unique<StringOpenZLInput>(
                                    std::move(data), std::move(lens)));
                    break;
                }
            }
        }
        return inputs;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeBruteForceComponent()
{
    return std::make_unique<BruteForceComponent>();
}
} // namespace openzl::tests::components
