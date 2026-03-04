// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/clustering/compression_utils.h"
#include <atomic>
#include <chrono>
#include <limits>
#include <unordered_set>
#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/allocation.h"

#include "openzl/common/operation_context.h"
#include "openzl/cpp/CCtx.hpp"
#include "openzl/zl_reflection.h"
#include "tools/logger/Logger.h"
#include "tools/training/clustering/clustering_config_builder.h"

using namespace openzl::tools::logger;

namespace openzl::training {
namespace {
int getTag(const Input& input)
{
    auto meta = input.getIntMetadata(ZL_CLUSTERING_TAG_METADATA_ID);
    if (!meta) {
        throw Exception("Stream provided has no metadata");
    }
    return meta.value();
}

ColumnInfo getColumnInfo(const Input& input)
{
    return (ColumnInfo){
        .tag   = getTag(input),
        .type  = typeToCType(input.type()),
        .width = input.eltWidth(),
    };
}

} // namespace
ClusterInfo CompressionUtils::getBestClusterInfo(
        const std::unordered_set<int>& tags,
        ZL_Type type,
        size_t eltWidth,
        const ColumnMetadata& metadata) const
{
    ClusterInfo bestClusterInfo;
    if (tags.size() == 0) {
        throw Exception("No tags provided");
    }
    // Check that there is a config for each tag
    for (auto& tag : tags) {
        auto column =
                (ColumnInfo){ .tag = tag, .type = type, .width = eltWidth };
        if (metadata.count(column) == 0) {
            throw std::runtime_error(
                    "No tag found in metadata for provided type and eltWidth");
        }
    }

    // Set to compress only the relevant successors
    std::function<bool(ColumnInfo)> filter = [tags](ColumnInfo val) {
        return tags.count(val.tag) != 0;
    };

    auto configBuilder =
            ClusteringConfigBuilder::buildConfigSingleClusterWithSuccessor(
                    tags, type, eltWidth, 0, 0);
    // Set up a config that clusters tags together
    for (size_t i = 0; i < successors_.size(); i++) {
        configBuilder.setClusterSuccessor(0, i);
        auto succType =
                ZL_Compressor_Graph_getInput0Mask(compressor_, successors_[i]);
        // If the type is serial, allow automatic conversion from numeric/struct
        if (succType & 0b1) {
            succType = (ZL_Type)(succType | 0b110);
        }
        if (!(type & succType)
            || typeToClusteringCodecIdxsMap_.count(type) == 0) {
            continue;
        }
        auto clusteringCodecIdxs = typeToClusteringCodecIdxsMap_.at(type);
        for (size_t j = 0; j < clusteringCodecIdxs.size(); j++) {
            SizeTimePair cost{ 0, 0 };
            configBuilder.setClusteringCodec(0, clusteringCodecIdxs[j]);
            auto config = configBuilder.build();
            for (auto& sample : samples_) {
                cost = cost + compressSample(config, filter, sample);
            }
            if (cost < bestClusterInfo.cost) {
                bestClusterInfo = { .successorIdx = i,
                                    .clusteringCodecIdx =
                                            clusteringCodecIdxs[j],
                                    .cost = cost };
            }
        }
    }
    return bestClusterInfo;
}

SizeTimePair CompressionUtils::compressSample(
        const ClusteringConfig& config,
        const std::function<bool(ColumnInfo)>& filter,
        const MultiInput& sample) const
{
    // Set up local params for clustering
    uint8_t* dst   = NULL;
    size_t dstSize = 0;
    auto arena     = detail::NonNullUniqueCPtr<Arena>(
            ALLOC_HeapArena_create(), ALLOC_Arena_freeArena);
    A1C_Arena a1cArena = A1C_Arena_wrap(arena.get());
    openzl::CCtx cctx;
    auto errCtx = ZL_CCtx_getOperationContext(cctx.get())->defaultScopeContext;
    cctx.unwrap(
            ZL_Clustering_serializeClusteringConfig(
                    errCtx, &dst, &dstSize, config.get(), &a1cArena),
            "Failed to serialize clustering config");
    ZL_CopyParam configParam = (ZL_CopyParam){
        .paramId   = ZL_GENERIC_CLUSTERING_CONFIG_ID,
        .paramPtr  = dst,
        .paramSize = dstSize,
    };
    ZL_LocalParams clusteringParams = (ZL_LocalParams){
        .copyParams = { .copyParams = &configParam, .nbCopyParams = 1 },
    };
    ZL_RuntimeGraphParameters runtimeParams = (ZL_RuntimeGraphParameters){
        .customGraphs   = successors_.data(),
        .nbCustomGraphs = successors_.size(),
        .customNodes    = clusteringCodecs_.data(),
        .nbCustomNodes  = clusteringCodecs_.size(),
        .localParams    = &clusteringParams,
    };

    cctx.unwrap(ZL_CCtx_selectStartingGraphID(
            cctx.get(), compressor_, ZL_GRAPH_CLUSTERING, &runtimeParams));
    cctx.setParameter(openzl::CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    size_t compressBound = 0;
    std::vector<const ZL_Input*> constInputs;
    for (auto& input : *sample) {
        auto column = getColumnInfo(input);
        if (!filter(column)) {
            continue;
        }
        compressBound += ZL_compressBound(
                (input.contentSize() + input.numElts() * 4)
                * compressBoundFactor_);
        constInputs.push_back(input.get());
    }
    if (constInputs.empty()) {
        return (SizeTimePair){ 0, 0 };
    }
    std::string compressed(compressBound, 0);
    auto start      = std::chrono::high_resolution_clock::now();
    ZL_Report csize = ZL_CCtx_compressMultiTypedRef(
            cctx.get(),
            compressed.data(),
            compressed.size(),
            constInputs.data(),
            constInputs.size());
    auto stop = std::chrono::high_resolution_clock::now();
    auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
    // TODO: T231098760: This implementation is a hack to get around the current
    // state of csv successors
    if (ZL_isError(csize)) {
        static std::atomic<bool> errorLogged{ false };
        if (!errorLogged.exchange(true)) {
            // Only log the first occurrence of this error
            Logger::log(
                    ERRORS,
                    "Selected a successor that fails to compress on input, treating this as a candidate with a large compression cost. Suppressing future logs for this error.");
        }
        return SizeTimePair{ std::numeric_limits<uint32_t>::max(),
                             std::numeric_limits<uint32_t>::max() };
    }
    cctx.unwrap(csize);
    return SizeTimePair{ ZL_RES_value(csize), (size_t)duration.count() };
}

std::future<SizeTimePair> CompressionUtils::tryCompress(
        const ClusteringConfig& config,
        const std::function<bool(ColumnInfo)>& filter) const
{
    std::vector<std::future<SizeTimePair>> futures;
    // Copy clusteringConfig and filter into memory owned by ptrs and pass
    // shared_ptrs into functions
    auto configPtr = std::make_shared<const ClusteringConfig>(*config);
    auto funcPtr =
            std::make_shared<const std::function<bool(ColumnInfo)>>(filter);
    for (size_t i = 0; i < samples_.size(); i++) {
        auto task = [this,
                     i](std::shared_ptr<const ClusteringConfig> ccPtr,
                        std::shared_ptr<const std::function<bool(ColumnInfo)>>
                                fPtr) {
            return compressSample(*ccPtr, *fPtr, samples_.at(i));
        };
        futures.emplace_back(threadPool_->run(task, configPtr, funcPtr));
    }
    return threadPool_->run([futures = std::move(futures)]() mutable {
        SizeTimePair result{};
        for (auto& future : futures) {
            result = result + future.get();
        }
        return result;
    });
}

ColumnMetadata CompressionUtils::aggregateInputMetadata() const
{
    // TODO: Tags need to no longer uniquely idenify an input
    ColumnMetadata metadata;
    for (auto& sample : samples_) {
        for (auto& input : *sample) {
            metadata.insert(getColumnInfo(input));
        }
    }
    return metadata;
}

} // namespace openzl::training
