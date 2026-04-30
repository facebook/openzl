// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/zl_segmenters.h"
#include "openzl/zl_version.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"
#include "tests/utils.h"

namespace openzl::tests::components {
namespace {

// ---- SegmentNumeric (ZL_SEGMENT_NUMERIC, numeric input) ----

class SegmentNumericComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "SegmentNumeric";
    }

    int minFormatVersion() const override
    {
        // Numeric input requires typed input support.
        // Older versions serialize all inputs, wrapping the segmenter
        // as a successor graph, which segmenters don't support.
        return ZL_TYPED_INPUT_VERSION_MIN + 1;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        compressor.selectStartingGraph(ZL_SEGMENT_NUMERIC);
        return { ZL_SEGMENT_NUMERIC };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{ 42 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 1, 2, 3, 4, 5 }));
        inputs.push_back(
                U8OpenZLInput::make(
                        std::vector<uint8_t>{ 0, 1, 2, 3, 255, 128, 64 }));
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 100, 200, 300, 400, 500 }));
        inputs.push_back(
                U64OpenZLInput::make(
                        std::vector<uint64_t>{ 0, UINT64_MAX, 12345 }));
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
            auto widthChoice = gen.usize_range("width", 0, 3);
            switch (widthChoice) {
                case 0: {
                    auto maxElts = std::min(
                            maxInputSize / sizeof(uint8_t), size_t(131072));
                    inputs.push_back(
                            U8OpenZLInput::make(gen.randVector<uint8_t>(
                                    "values", 0, UINT8_MAX, maxElts)));
                    break;
                }
                case 1: {
                    auto maxElts = maxInputSize / sizeof(uint16_t);
                    inputs.push_back(
                            U16OpenZLInput::make(gen.randVector<uint16_t>(
                                    "values", 0, UINT16_MAX, maxElts)));
                    break;
                }
                case 2: {
                    auto maxElts = maxInputSize / sizeof(uint32_t);
                    inputs.push_back(
                            U32OpenZLInput::make(gen.randVector<uint32_t>(
                                    "values", 0, UINT32_MAX, maxElts)));
                    break;
                }
                case 3: {
                    auto maxElts = maxInputSize / sizeof(uint64_t);
                    inputs.push_back(
                            U64OpenZLInput::make(gen.randVector<uint64_t>(
                                    "values", 0, UINT64_MAX, maxElts)));
                    break;
                }
            }
        }
        return inputs;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeSegmentNumericComponent()
{
    return std::make_unique<SegmentNumericComponent>();
}
} // namespace openzl::tests::components
