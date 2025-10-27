// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/zstrong_json.h"

#include <memory>
#include <mutex>

#include <folly/FileUtil.h>
#include <folly/Memory.h>
#include <folly/base64.h>
#include <folly/lang/Bits.h>

#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_reflection.h"

namespace zstrong {
JsonGraph::JsonGraph(
        folly::dynamic graph,
        ZL_Type inputType,
        std::optional<TransformMap> customTransforms,
        std::optional<GraphMap> customGraphs,
        std::optional<SelectorMap> customSelectors)
        : graph_(std::move(graph)),
          inputType_(inputType),
          customTransforms_(
                  std::move(customTransforms).value_or(TransformMap{})),
          customGraphs_(std::move(customGraphs).value_or(GraphMap{})),
          customSelectors_(std::move(customSelectors).value_or(SelectorMap{}))
{
    auto const& standardTransforms = getStandardTransforms();
    auto const& standardGraphs     = getStandardGraphs();
    auto const& standardSelectors  = getStandardSelectors();

    auto const shadowsStandardName = [&](std::string const& name) {
        return standardTransforms.contains(name)
                || standardGraphs.contains(name)
                || standardSelectors.contains(name);
    };

    for (auto const& [name, _] : customTransforms_) {
        if (shadowsStandardName(name)) {
            throw std::runtime_error(
                    "Custom transform shadows standard name: " + name);
        }
        if (customGraphs_.contains(name)) {
            throw std::runtime_error(
                    "Custom transform shadows custom graph name: " + name);
        }
        if (customSelectors_.contains(name)) {
            throw std::runtime_error(
                    "Custom transform shadows custom selector name: " + name);
        }
    }
    for (auto const& [name, _] : customGraphs_) {
        if (shadowsStandardName(name)) {
            throw std::runtime_error(
                    "Custom graph shadows standard name: " + name);
        }
        if (customSelectors_.contains(name)) {
            throw std::runtime_error(
                    "Custom graph shadows custom selector name: " + name);
        }
    }
    for (auto const& [name, _] : customSelectors_) {
        if (shadowsStandardName(name)) {
            throw std::runtime_error(
                    "Custom selector shadows standard name: " + name);
        }
    }
}

ZL_GraphID JsonGraph::registerGraph(ZL_Compressor& cgraph) const
{
    std::unordered_map<std::string_view, ZL_GraphID> customGraphs;
    for (auto const& [name, graph] : customGraphs_) {
        auto const node = graph->registerGraph(cgraph);
        customGraphs.emplace(name, node);
    }
    if (graph_.count(kGlobalParamsKey)) {
        for (const auto& [key, value] : graph_[kGlobalParamsKey].items()) {
            if (ZL_isError(ZL_Compressor_setParameter(
                        &cgraph, (ZL_CParam)key.asInt(), value.asInt()))) {
                throw std::runtime_error{ "Failed to set parameter" };
            }
        }
    }
    return registerGraph(cgraph, customGraphs, graph_);
}

ZL_GraphID JsonGraph::registerGraph(
        ZL_Compressor& cgraph,
        std::unordered_map<std::string_view, ZL_GraphID> const& customGraphs,
        folly::dynamic const& graph) const
{
    auto const& standardTransforms = getStandardTransforms();
    auto const& standardGraphs     = getStandardGraphs();
    auto const& standardSelectors  = getStandardSelectors();

    auto const& name = graph[kNameKey].asString();

    // Handle cases that don't have successors
    if (graph.count(kSuccessorsKey) == 0) {
        if (standardGraphs.contains(name)) {
            return standardGraphs.at(name)->registerGraph(cgraph);
        }
        if (customGraphs.contains(name)) {
            return customGraphs.at(name);
        }

        throw std::runtime_error("Unknown graph: " + name);
    }

    auto const& successorGraphs = graph[kSuccessorsKey];
    std::vector<ZL_GraphID> successors;
    successors.reserve(successorGraphs.size());
    for (auto const& successorGraph : successorGraphs) {
        successors.push_back(
                registerGraph(cgraph, customGraphs, successorGraph));
    }

    std::vector<ZL_IntParam> intParams;
    if (graph.count(kIntParamsKey)) {
        auto const& intParamsField = graph[kIntParamsKey];
        for (auto const& [key, val] : intParamsField.items()) {
            intParams.push_back(
                    ZL_IntParam{ (int)key.asInt(), (int)val.asInt() });
        }
    }

    std::vector<std::string> genericParamsStorage;
    std::vector<ZL_CopyParam> genericParams;
    if (graph.count(kGenericBinaryParamsKey)) {
        auto const& genericParamsField = graph[kGenericBinaryParamsKey];
        for (auto const& [key, val] : genericParamsField.items()) {
            int const k = (int)key.asInt();
            genericParamsStorage.push_back(folly::base64Decode(val.asString()));
            auto const& v = genericParamsStorage.back();
            genericParams.push_back(
                    ZL_CopyParam{ .paramId   = k,
                                  .paramPtr  = v.data(),
                                  .paramSize = v.size() });
        }
    }
    if (graph.count(kGenericStringParamsKey)) {
        auto const& genericParamsField = graph[kGenericStringParamsKey];
        for (auto const& [key, val] : genericParamsField.items()) {
            int const k = (int)key.asInt();
            genericParamsStorage.push_back(val.asString());
            auto const& v = genericParamsStorage.back();
            genericParams.push_back(
                    ZL_CopyParam{ .paramId   = k,
                                  .paramPtr  = v.data(),
                                  .paramSize = v.size() });
        }
    }

    ZL_LocalParams localParams = {
        .intParams     = { intParams.data(), intParams.size() },
        .genericParams = { genericParams.data(), genericParams.size() }
    };

    if (standardTransforms.contains(name)) {
        auto const& transform = *standardTransforms.at(name);
        return transform.registerTransform(cgraph, successors, localParams);
    }

    if (customTransforms_.contains(name)) {
        auto const& transform = *customTransforms_.at(name);
        return transform.registerTransform(cgraph, successors, localParams);
    }

    if (standardSelectors.contains(name)) {
        auto const& selector = *standardSelectors.at(name);
        return selector.registerSelector(cgraph, successors, localParams);
    }

    if (customSelectors_.contains(name)) {
        auto const& selector = *customSelectors_.at(name);
        return selector.registerSelector(cgraph, successors, localParams);
    }

    throw std::runtime_error("Unknown node: " + name);
}

void JsonGraph::registerGraph(ZL_DCtx& dctx) const
{
    for (auto const& [_name, transform] : customTransforms_) {
        transform->registerTransform(dctx);
    }
    for (auto const& [_name, graph] : customGraphs_) {
        graph->registerGraph(dctx);
    }
}

std::vector<ExtractedStream> splitExtractedStreams(std::string_view data)
{
    std::vector<ExtractedStream> streams;
    while (!data.empty()) {
        if (data.size() < 17) {
            throw std::runtime_error("Need 17-byte header");
        }
        auto const type   = (ZL_Type)data[0];
        auto const nbElts = folly::Endian::little(
                folly::loadUnaligned<uint64_t>(data.data() + 1));
        auto const eltWidth = folly::Endian::little(
                folly::loadUnaligned<uint64_t>(data.data() + 9));
        data              = data.substr(17);
        auto const length = nbElts * eltWidth;

        if (data.size() < length) {
            throw std::runtime_error("Not enough bytes in data");
        }
        streams.push_back(
                ExtractedStream{
                        type, nbElts, eltWidth, data.substr(0, length) });
        data = data.substr(length);
    }
    return streams;
}

} // namespace zstrong
