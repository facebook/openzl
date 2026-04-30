// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/zl_conversion.h"
#include "openzl/codecs/zl_field_lz.h"
#include "openzl/codecs/zl_segmenters.h"
#include "tests/datagen/structures/CompressibleStringProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

GraphID makeNumFromSerialSuccessorGraph(
        Compressor& compressor,
        size_t eltWidthBytes)
{
    return compressor.buildStaticGraph(
            ZL_Node_interpretAsLE(eltWidthBytes * 8), { ZL_GRAPH_FIELD_LZ });
}

size_t generateChunkSizeBytes(datagen::DataGen& gen, size_t eltWidthBytes)
{
    // Keep generated chunks large enough that component round-trip coverage
    // stays tractable while still varying width and alignment behavior.
    size_t const numEltsPerChunk =
            gen.usize_range("chunk_num_elts", 1024, 8192);
    // Widths above 1 can add a tail byte count to exercise alignment-down.
    size_t const extraBytes = eltWidthBytes == 1
            ? 0
            : gen.usize_range("chunk_size_remainder", 0, eltWidthBytes - 1);
    return (numEltsPerChunk * eltWidthBytes) + extraBytes;
}

// ---- SegmentNumFromSerial (bare macros, all 4 widths) ----

class SegmentNumFromSerialComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "SegmentNumFromSerial";
    }

    int minFormatVersion() const override
    {
        return ZL_MIN_FORMAT_VERSION;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        compressor.selectStartingGraph(ZL_SEGMENT_NUM8_FROM_SERIAL);
        return {
            ZL_SEGMENT_NUM8_FROM_SERIAL,
            ZL_SEGMENT_NUM16_FROM_SERIAL,
            ZL_SEGMENT_NUM32_FROM_SERIAL,
            ZL_SEGMENT_NUM64_FROM_SERIAL,
        };
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<GraphID> graphs;
        graphs.reserve(num);
        constexpr size_t kEltWidthBytes[] = { 1, 2, 4, 8 };
        for (size_t i = 0; i < num; ++i) {
            size_t const eltWidthBytes =
                    kEltWidthBytes[gen.usize_range("elt_width_idx", 0, 3)];
            GraphID const successorGraph =
                    makeNumFromSerialSuccessorGraph(compressor, eltWidthBytes);
            size_t const chunkSizeBytes =
                    generateChunkSizeBytes(gen, eltWidthBytes);
            graphs.push_back(compressor.unwrap(
                    ZL_Compressor_buildNumFromSerialSegmenter2(
                            compressor.get(),
                            eltWidthBytes,
                            chunkSizeBytes,
                            successorGraph)));
        }
        return graphs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        // All inputs must be multiples of 8 bytes (widest element width)
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make(std::string(8, 'a')));
        inputs.push_back(SerialOpenZLInput::make(std::string(64, '\xab')));
        inputs.push_back(SerialOpenZLInput::make(std::string(16, '\xcd')));
        // Larger input with varied bytes
        {
            std::string data(256, '\0');
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
            // Quantize to 8-byte alignment so all widths work
            size_t rawSize = gen.usize_range("input_size", 0, maxInputSize);
            size_t size    = rawSize - (rawSize % 8);
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

std::unique_ptr<OpenZLComponent> makeSegmentNumFromSerialComponent()
{
    return std::make_unique<SegmentNumFromSerialComponent>();
}
} // namespace openzl::tests::components
