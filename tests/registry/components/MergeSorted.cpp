// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>

#include "openzl/cpp/codecs/MergeSorted.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {
class MergeSortedComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "MergeSorted";
    }

    int minFormatVersion() const override
    {
        return 9;
    }

    // MergeSorted produces a u64 bitset per unique value plus the sorted
    // values, so the output can be up to 3x the input size.
    size_t compressBound(poly::span<const Input> inputs) const override
    {
        size_t totalSrcSize = 0;
        for (const auto& input : inputs) {
            totalSrcSize += input.contentSize();
        }
        return ZL_compressBound(4 * totalSrcSize);
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::MergeSorted{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        // Single sorted run
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 1, 2, 3 }));
        // Two sorted runs: [1,3,5], [2,4,6]
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ 1, 3, 5, 2, 4, 6 }));
        // Two runs with gaps: [5,10], [1,6]
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 5, 10, 1, 6 }));
        // Single element
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{ 42 }));
        // Runs of length 1 each (4 runs)
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 5, 3, 7, 1 }));
        // Runs with values near UINT32_MAX
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ UINT32_MAX - 2,
                                               UINT32_MAX - 1,
                                               UINT32_MAX,
                                               UINT32_MAX - 5,
                                               UINT32_MAX - 3 }));
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
            auto maxElts   = maxInputSize / sizeof(uint32_t);
            auto numRuns   = gen.usize_range("num_runs", 1, 64);
            auto totalElts = gen.usize_range("total_elts", 0, maxElts);
            std::vector<uint32_t> data;
            data.reserve(totalElts);
            for (size_t r = 0; r < numRuns && data.size() < totalElts; ++r) {
                auto remaining = totalElts - data.size();
                auto runLen    = gen.usize_range("run_len", 1, remaining);
                // Generate sorted unique values by picking a start and step
                auto start = gen.u32_range("start", 0, UINT32_MAX / 2);
                std::vector<uint32_t> run;
                run.reserve(runLen);
                uint32_t val = start;
                for (size_t j = 0; j < runLen; ++j) {
                    run.push_back(val);
                    auto step = gen.u32_range("step", 1, 1000);
                    if (val > UINT32_MAX - step)
                        break;
                    val += step;
                }
                data.insert(data.end(), run.begin(), run.end());
            }
            inputs.push_back(U32OpenZLInput::make(std::move(data)));
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeMergeSortedComponent()
{
    return std::make_unique<MergeSortedComponent>();
}
} // namespace openzl::tests::components
