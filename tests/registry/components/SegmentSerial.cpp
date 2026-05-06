// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_segmenters.h"
#include "openzl/zl_compress.h" // ZL_MIN_CHUNK_SIZE
#include "tests/datagen/structures/CompressibleStringProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

class SegmentSerialComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "SegmentSerial";
    }

    int minFormatVersion() const override
    {
        return ZL_MIN_FORMAT_VERSION;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        compressor.selectStartingGraph(ZL_SEGMENT_SERIAL);
        return { ZL_SEGMENT_SERIAL };
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<GraphID> graphs;
        graphs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            // Pick chunk sizes >= ZL_MIN_CHUNK_SIZE (the public builder
            // rejects smaller positive values). Combined with the bare
            // predefined graph (default chunking), this exercises both
            // single-chunk and multi-chunk paths.
            size_t const chunkSizeBytes = gen.usize_range(
                    "chunk_size_bytes",
                    ZL_MIN_CHUNK_SIZE,
                    ZL_MIN_CHUNK_SIZE * 4);
            graphs.push_back(
                    compressor.unwrap(ZL_Compressor_buildSerialSegmenter2(
                            compressor.get(),
                            chunkSizeBytes,
                            ZL_GRAPH_COMPRESS_GENERIC)));
        }
        return graphs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make(std::string(1, 'a')));
        inputs.push_back(SerialOpenZLInput::make(std::string(64, '\xab')));
        // Larger input with varied bytes to exercise multi-chunk paths.
        {
            std::string data(8192, '\0');
            for (size_t i = 0; i < data.size(); ++i) {
                data[i] = static_cast<char>(i & 0xFF);
            }
            inputs.push_back(SerialOpenZLInput::make(std::move(data)));
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
            size_t const size = gen.usize_range("input_size", 0, maxInputSize);
            datagen::CompressibleStringProducer producer(
                    gen.getRandWrapper(),
                    size,
                    gen.u32_range("match_prob", 0, 100) / 100.0);
            inputs.push_back(SerialOpenZLInput::make(producer("input")));
        }
        return inputs;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeSegmentSerialComponent()
{
    return std::make_unique<SegmentSerialComponent>();
}
} // namespace openzl::tests::components
