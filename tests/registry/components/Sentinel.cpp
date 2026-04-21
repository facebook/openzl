// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>

#include "openzl/codecs/zl_sentinel.h"
#include "openzl/codecs/zl_store.h"
#include "openzl/cpp/codecs/Sentinel.hpp"
#include "openzl/shared/utils.h"
#include "openzl/zl_reflection.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

class SentinelNumComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "SentinelNum";
    }

    int minFormatVersion() const override
    {
        return 24;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        std::vector<size_t> noIdx;
        std::vector<size_t> idx_0123 = { 0, 1, 2, 3 };
        std::vector<size_t> idx_13   = { 1, 3 };
        std::vector<size_t> idx_0    = { 0 };
        std::vector<size_t> idx_3    = { 3 };
        std::vector<size_t> idx_12   = { 1, 2 };
        return {
            nodes::Sentinel{ noIdx, UINT64_MAX }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            nodes::Sentinel{ idx_0123, 42 }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            nodes::Sentinel{ idx_13, 0 }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            nodes::Sentinel{ idx_0, 100 }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            nodes::Sentinel{ idx_3, UINT64_MAX }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            nodes::Sentinel{ idx_12, 1 }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
        };
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<GraphID> result;
        result.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            size_t numElts       = gen.usize_range("numElts", 1, 256);
            size_t numExceptions = gen.usize_range("numExceptions", 0, numElts);

            // Generate sorted unique exception indices
            std::vector<size_t> indices;
            indices.reserve(numExceptions);
            for (size_t j = 0; j < numExceptions; ++j) {
                size_t idx = gen.usize_range("index", 0, numElts - 1);
                indices.push_back(idx);
            }
            std::sort(indices.begin(), indices.end());
            indices.erase(
                    std::unique(indices.begin(), indices.end()), indices.end());

            // Pick a sentinel value
            bool useDefault = gen.boolean("useDefaultSentinel");
            poly::optional<uint64_t> sentinel;
            if (!useDefault) {
                sentinel = gen.u64("sentinel");
            }

            result.push_back(
                    nodes::Sentinel{ indices, sentinel }(
                            compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE));
        }
        return result;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        return {};
    }

    /// Extract sentinel parameters from a graph via the reflection API.
    void getSentinelParams(
            const Compressor& compressor,
            GraphID graphID,
            std::vector<size_t>& indices,
            uint64_t& sentinel) const
    {
        auto node = ZL_Compressor_Graph_getHeadNode(compressor.get(), graphID);
        auto localParams =
                ZL_Compressor_Node_getLocalParams(compressor.get(), node);

        indices.clear();
        sentinel = UINT64_MAX;
        for (size_t i = 0; i < localParams.copyParams.nbCopyParams; ++i) {
            const auto& cp = localParams.copyParams.copyParams[i];
            if (cp.paramId == ZL_SENTINEL_INDICES_PID) {
                auto* data   = static_cast<const size_t*>(cp.paramPtr);
                size_t count = cp.paramSize / sizeof(size_t);
                indices.assign(data, data + count);
            } else if (cp.paramId == ZL_SENTINEL_VALUE_PID) {
                sentinel = *static_cast<const uint64_t*>(cp.paramPtr);
            }
        }
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor& compressor,
            GraphID graphID) const override
    {
        std::vector<size_t> indices;
        uint64_t sentinelBase;
        getSentinelParams(compressor, graphID, indices, sentinelBase);

        size_t minElts = indices.empty() ? 0 : indices.back() + 1;

        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            auto width     = gen.choices<size_t>("width", { 1, 2, 4, 8 });
            size_t maxElts = maxInputSize / width;
            if (maxElts < minElts) {
                maxElts = minElts;
            }
            size_t numElts = gen.usize_range("numElts", minElts, maxElts);

            // Compute sentinel masked to this width
            uint64_t maxVal   = ZL_maxValueForWidth(width);
            uint64_t sentinel = sentinelBase & maxVal;

            // Build a set of exception positions for fast lookup
            auto isException = [&](size_t idx) {
                return std::binary_search(indices.begin(), indices.end(), idx);
            };

            // Generate values: non-exception positions must not contain
            // sentinel
            switch (width) {
                case 1: {
                    std::vector<uint8_t> vec(numElts);
                    uint8_t s8 = static_cast<uint8_t>(sentinel);
                    for (size_t j = 0; j < numElts; ++j) {
                        if (isException(j)) {
                            vec[j] = gen.u8_range("exc", 0, UINT8_MAX);
                        } else {
                            // Avoid sentinel value
                            vec[j] = gen.u8_range("val", 0, UINT8_MAX);
                            if (vec[j] == s8) {
                                ++vec[j];
                            }
                        }
                    }
                    inputs.push_back(U8OpenZLInput::make(std::move(vec)));
                    break;
                }
                case 2: {
                    std::vector<uint16_t> vec(numElts);
                    uint16_t s16 = static_cast<uint16_t>(sentinel);
                    for (size_t j = 0; j < numElts; ++j) {
                        if (isException(j)) {
                            vec[j] = gen.u16_range("exc", 0, UINT16_MAX);
                        } else {
                            vec[j] = gen.u16_range("val", 0, UINT16_MAX);
                            if (vec[j] == s16) {
                                ++vec[j];
                            }
                        }
                    }
                    inputs.push_back(U16OpenZLInput::make(std::move(vec)));
                    break;
                }
                case 4: {
                    std::vector<uint32_t> vec(numElts);
                    uint32_t s32 = static_cast<uint32_t>(sentinel);
                    for (size_t j = 0; j < numElts; ++j) {
                        if (isException(j)) {
                            vec[j] = gen.u32_range("exc", 0, UINT32_MAX);
                        } else {
                            vec[j] = gen.u32_range("val", 0, UINT32_MAX);
                            if (vec[j] == s32) {
                                ++vec[j];
                            }
                        }
                    }
                    inputs.push_back(U32OpenZLInput::make(std::move(vec)));
                    break;
                }
                case 8: {
                    std::vector<uint64_t> vec(numElts);
                    for (size_t j = 0; j < numElts; ++j) {
                        if (isException(j)) {
                            vec[j] = gen.u64_range("exc", 0, UINT64_MAX);
                        } else {
                            vec[j] = gen.u64_range("val", 0, UINT64_MAX);
                            if (vec[j] == sentinel) {
                                ++vec[j];
                            }
                        }
                    }
                    inputs.push_back(U64OpenZLInput::make(std::move(vec)));
                    break;
                }
            }
        }
        return inputs;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeSentinelNumComponent()
{
    return std::make_unique<SentinelNumComponent>();
}

} // namespace openzl::tests::components
