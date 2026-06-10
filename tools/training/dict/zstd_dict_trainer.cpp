// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/dict/zstd_dict_trainer.h"

#include <algorithm>

#include "openzl/codecs/zstd/common_zstd.h"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/Input.hpp"
#include "openzl/zl_reflection.h"
#include "tools/logger/Logger.h"

#include <zdict.h>
#include <zstd.h>

using namespace openzl::tools::logger;

namespace openzl::training {

std::vector<DictNodeInfo> ZstdDictTrainer::findDictNodes(
        const Compressor& compressor)
{
    // find all zstd nodes with the name `zl.trainable.zstd`
    std::vector<DictNodeInfo> dictNodes;
    openzl::unwrap(ZL_Compressor_forEachNode(
            compressor.get(),
            [](void* opaque, const ZL_Compressor* c, ZL_NodeID node) noexcept
                    -> ZL_Report {
                const char* name = ZL_Compressor_Node_getName(c, node);
                constexpr poly::string_view trainableZstdPrefix{
                    "zl.trainable.zstd"
                };
                if (name != nullptr
                    && poly::string_view(name).substr(
                               0, trainableZstdPrefix.size())
                            == trainableZstdPrefix) {
                    auto* nodes =
                            static_cast<std::vector<DictNodeInfo>*>(opaque);
                    nodes->push_back(
                            DictNodeInfo{
                                    .nodeID  = node,
                                    .codecID = ZL_Compressor_Node_getCodecID(
                                            c, node),
                                    .nodeName = std::string(name),
                            });
                }
                return ZL_returnSuccess();
            },
            &dictNodes));
    return dictNodes;
}

poly::optional<std::string> ZstdDictTrainer::trainDict(
        const std::vector<MultiInput>& inputs,
        const Compressor& compressor,
        ZL_LocalParams localParams)
{
    std::vector<poly::span<const uint8_t>> samples;
    samples.reserve(inputs.size());
    for (const auto& mi : inputs) {
        for (const auto& input : *mi) {
            const auto* ptr   = static_cast<const uint8_t*>(input.ptr());
            size_t const size = input.contentSize();
            if (ptr != nullptr && size > 0) {
                samples.emplace_back(ptr, size);
            }
        }
    }

    std::string rawDict = trainRawDict(samples);

    int32_t clevel = compressor.getParameter(CParam::CompressionLevel);
    for (size_t i = 0; i < localParams.intParams.nbIntParams; ++i) {
        if (localParams.intParams.intParams[i].paramId
            == ZSTD_c_compressionLevel) {
            clevel = localParams.intParams.intParams[i].paramValue;
            break;
        }
    }

    size_t const contentSize = ZL_TrainedZstdContent_packedSize(rawDict.size());
    std::string packed(contentSize, '\0');
    size_t const written = ZL_TrainedZstdContent_pack(
            packed.data(),
            packed.size(),
            clevel,
            rawDict.data(),
            rawDict.size());
    if (written == 0) {
        throw Exception("ZL_TrainedZstdContent_pack failed");
    }
    packed.resize(written);
    return packed;
}

std::string ZstdDictTrainer::trainRawDict(
        const std::vector<poly::span<const uint8_t>>& samples)
{
    if (samples.empty()) {
        throw Exception("trainRawDict: no samples provided");
    }

    std::vector<size_t> sampleSizes;
    sampleSizes.reserve(samples.size());
    size_t totalSize = 0;
    for (const auto& sample : samples) {
        sampleSizes.push_back(sample.size());
        totalSize += sample.size();
    }

    std::vector<uint8_t> concatenated;
    concatenated.reserve(totalSize);
    for (const auto& sample : samples) {
        concatenated.insert(concatenated.end(), sample.begin(), sample.end());
    }

    size_t const effectiveDictSize =
            std::min(maxDictSize_, std::max(totalSize / 100, minDictSize_));
    std::string dictBuffer(effectiveDictSize, '\0');

    Logger::log_c(
            VERBOSE1,
            "Training zstd dict: %zu samples, %zu total bytes, "
            "max dict %zu bytes",
            samples.size(),
            totalSize,
            effectiveDictSize);

    // the train command mutates the cover params, so we need to create a copy
    auto myCoverParams    = coverParams_;
    size_t const dictSize = ZDICT_optimizeTrainFromBuffer_cover(
            dictBuffer.data(),
            dictBuffer.size(),
            concatenated.data(),
            sampleSizes.data(),
            static_cast<unsigned>(sampleSizes.size()),
            &myCoverParams);

    if (ZDICT_isError(dictSize)) {
        throw Exception(
                std::string("ZDICT_optimizeTrainFromBuffer_cover failed: ")
                + ZDICT_getErrorName(dictSize));
    } else {
        dictBuffer.resize(dictSize);
    }

    Logger::log_c(
            VERBOSE1,
            "Trained zstd dict: %zu bytes (cover params: d=%u, k=%u)",
            dictBuffer.size(),
            myCoverParams.d,
            myCoverParams.k);

    return dictBuffer;
}

} // namespace openzl::training
