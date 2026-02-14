// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTD_STATIC_LINKING_ONLY
#    define ZSTD_STATIC_LINKING_ONLY
#endif
#include <zstd.h>

#include "openzl/cpp/codecs/Zstd.hpp"
#include "tests/datagen/structures/CompressibleStringProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"
#include "tests/utils.h"

namespace openzl::tests::components {
namespace {
class ZstdComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Zstd";
    }

    int minFormatVersion() const override
    {
        return ZL_MIN_FORMAT_VERSION;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        std::vector<GraphID> graphs;
        graphs.push_back(graphs::Zstd{}(compressor));
        graphs.push_back(graphs::Zstd{ 1 }(compressor));
        graphs.push_back(
                graphs::Zstd{ { { ZSTD_c_hashLog, 10 } } }(compressor));
        return graphs;
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<GraphID> graphs;
        graphs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            if (gen.boolean("use_compression_level")) {
                graphs.push_back(
                        graphs::Zstd{ gen.i32_range(
                                "compression_level", -5, 5) }(compressor));
            } else {
                graphs.push_back(
                        graphs::Zstd{ generateParams(gen) }(compressor));
            }
        }
        return graphs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(std::make_unique<SerialOpenZLInput>(""));
        inputs.push_back(std::make_unique<SerialOpenZLInput>("x"));
        inputs.push_back(
                std::make_unique<SerialOpenZLInput>(std::string(1000, 'x')));
        inputs.push_back(
                std::make_unique<SerialOpenZLInput>(
                        "foobar foo foo bar bar foobar foo foo bar"));
        inputs.push_back(std::make_unique<SerialOpenZLInput>(kLoremTestInput));
        inputs.push_back(
                std::make_unique<SerialOpenZLInput>(kAudioPCMS32LETestInput));
        return inputs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize) const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            datagen::CompressibleStringProducer producer(
                    gen.getRandWrapper(),
                    gen.usize_range("input_size", 0, maxInputSize),
                    gen.u32_range("match_prob", 0, 100) / 100.0);
            inputs.push_back(
                    std::make_unique<SerialOpenZLInput>(producer("input")));
        }
        return inputs;
    }

   private:
    std::unordered_map<int, int> generateParams(datagen::DataGen& gen) const
    {
        struct Param {
            int key;
            int minValue;
            int maxValue;
        };
        constexpr std::array<Param, 10> kParams = {
            Param{ ZSTD_c_compressionLevel, -5, 5 },
            Param{ ZSTD_c_windowLog, 10, 15 },
            Param{ ZSTD_c_hashLog, 10, 15 },
            Param{ ZSTD_c_chainLog, 10, 15 },
            Param{ ZSTD_c_searchLog, 0, 3 },
            Param{ ZSTD_c_minMatch, 4, 7 },
            Param{ ZSTD_c_strategy, 1, 3 },
            Param{ ZSTD_c_checksumFlag, 0, 1 },
            Param{ ZSTD_c_literalCompressionMode, 0, 2 },
            Param{ ZSTD_c_maxBlockSize, 1024, 128 * 1024 },
        };
        std::unordered_map<int, int> params;
        for (auto const& p : kParams) {
            if (gen.boolean("set_param")) {
                params[p.key] =
                        gen.i32_range("param_value", p.minValue, p.maxValue);
            }
        }
        return params;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeZstdComponent()
{
    return std::make_unique<ZstdComponent>();
}
} // namespace openzl::tests::components
