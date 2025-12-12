// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <optional>
#include <random>
#include <string>
#include <vector>

#include <folly/Conv.h>

#include "custom_transforms/parse/decode_parse.h"
#include "custom_transforms/parse/encode_parse.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_public_nodes.h"
#include "tools/zstrong_cpp.h"

namespace openzl::tests::parse {
inline ZL_SetStringLensInstructions setFieldSizes(
        ZL_SetStringLensState* state,
        ZL_Input const*)
{
    auto const& original = *(std::vector<std::string> const*)
                                   ZL_SetStringLensState_getOpaquePtr(state);
    uint32_t* fieldSizes =
            (uint32_t*)ZL_SetStringLensState_malloc(state, original.size() * 4);
    if (fieldSizes == nullptr) {
        return { nullptr, 0 };
    }
    for (size_t i = 0; i < original.size(); ++i) {
        fieldSizes[i] = uint32_t(original[i].size());
    }
    return { fieldSizes, original.size() };
}

enum class Type {
    Int64,
    Float64,
};

inline std::pair<std::string, std::vector<uint32_t>> flatten(
        std::vector<std::string> const& data)
{
    std::string out;
    std::vector<uint32_t> fieldSizes;
    for (auto const& x : data) {
        out.append(x);
        fieldSizes.push_back(uint32_t(x.size()));
    }
    return { std::move(out), std::move(fieldSizes) };
}

inline std::string compress(std::vector<std::string> const& data, Type type)
{
    zstrong::CGraph cgraph;
    auto node = type == Type::Int64
            ? ZS2_Compressor_registerParseInt64(cgraph.get(), 0)
            : ZS2_Compressor_registerParseFloat64(cgraph.get(), 1);
    std::vector<ZL_GraphID> store(3, ZL_GRAPH_STORE);
    ZL_GraphID graph = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph.get(), node, store.data(), store.size());
    auto setFieldSizesNode = ZL_Compressor_registerConvertSerialToStringNode(
            cgraph.get(), setFieldSizes, &data);
    graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph.get(), setFieldSizesNode, graph);
    cgraph.unwrap(ZL_Compressor_selectStartingGraphID(cgraph.get(), graph));

    auto const content       = flatten(data).first;
    auto const compressBound = data.size() * 5 + content.size() * 2 + 1000;

    zstrong::CCtx cctx;
    cctx.unwrap(ZL_CCtx_setParameter(
            cctx.get(), ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    cctx.unwrap(ZL_CCtx_refCompressor(cctx.get(), cgraph.get()));
    std::string compressed;
    compressed.resize(compressBound);
    auto const compressedSize = cctx.unwrap(ZL_CCtx_compress(
            cctx.get(),
            compressed.data(),
            compressed.size(),
            content.data(),
            content.size()));
    compressed.resize(compressedSize);
    return compressed;
}

inline std::string decompress(
        std::string_view compressed,
        Type type,
        std::optional<size_t> maxDstSize = std::nullopt)
{
    zstrong::DCtx dctx;
    if (type == Type::Int64) {
        dctx.unwrap(ZS2_DCtx_registerParseInt64(dctx.get(), 0));
    } else {
        dctx.unwrap(ZS2_DCtx_registerParseFloat64(dctx.get(), 1));
    }
    return zstrong::decompress(dctx, compressed, maxDstSize);
}

template <typename Gen>
int64_t genInt64(Gen&& gen)
{
    auto const value = std::uniform_int_distribution<uint64_t>{
        0, (uint64_t(1) << 63) - 1
    }(gen);
    auto const negative = std::uniform_int_distribution<uint64_t>{ 0, 1 }(gen);
    auto const bits     = std::uniform_int_distribution<int>{ 0, 63 }(gen);
    uint64_t const mask = (uint64_t(1) << bits) - 1;
    auto const val      = int64_t((value & mask) | (negative << 63));
    assert(negative == (val < 0));
    return val;
}

template <typename Gen>
std::vector<std::string> genData(Gen&& gen, size_t bytes, Type type)
{
    std::uniform_int_distribution<int64_t> intDist;
    std::uniform_real_distribution<double> doubleDist;
    std::vector<std::string> data;
    size_t contentSize = 0;
    while (contentSize < bytes) {
        std::string field;
        if (type == Type::Float64) {
            field = folly::to<std::string>(doubleDist(gen));
        } else {
            field = folly::to<std::string>(genInt64(gen));
        }
        if (contentSize + field.size() > bytes) {
            field.resize(contentSize + field.size() - bytes);
        }
        contentSize += field.size();
        data.push_back(std::move(field));
    }
    return data;
}

inline std::vector<std::string> genData(size_t bytes, Type type)
{
    std::mt19937 gen(0xdeadbeef ^ bytes);
    return genData(gen, bytes, type);
}

} // namespace openzl::tests::parse
