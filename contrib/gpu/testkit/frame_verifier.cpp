// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/testkit/frame_verifier.h"

#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include "openzl/common/wire_format.h"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/decompress/dctx2.h"
#include "openzl/decompress/dictx.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_reflection.h"

namespace openzl::gpu::testkit {

namespace {

// Frees a reflection context, so the context is freed even if a getter
// throws.
struct ReflectionCtxDeleter {
    void operator()(ZL_ReflectionCtx* rctx) const
    {
        ZL_ReflectionCtx_free(rctx);
    }
};
using ReflectionCtxPtr =
        std::unique_ptr<ZL_ReflectionCtx, ReflectionCtxDeleter>;

ReflectionCtxPtr makeReflectionCtx(const std::string& frame)
{
    ReflectionCtxPtr rctx{ ZL_ReflectionCtx_create() };
    if (rctx == nullptr) {
        throw std::runtime_error("ZL_ReflectionCtx_create() failed");
    }
    const ZL_Report report = ZL_ReflectionCtx_setCompressedFrame(
            rctx.get(), frame.data(), frame.size());
    if (ZL_isError(report)) {
        throw std::runtime_error(
                "ZL_ReflectionCtx_setCompressedFrame() failed to decode frame");
    }
    return rctx;
}

class PerChunkCodecCollector : public openzl::DecompressIntrospectionHooks {
   public:
    const std::vector<std::vector<uint32_t>>& codecsPerChunk() const
    {
        return codecsPerChunk_;
    }

    bool sawChunks() const
    {
        return sawChunks_;
    }

    void on_decompressChunk_start(ZL_DCtx* /* dctx */, size_t chunkIndex)
            override
    {
        if (codecsPerChunk_.size() <= chunkIndex) {
            codecsPerChunk_.resize(chunkIndex + 1);
        }
        sawChunks_       = true;
        currentChunk_    = chunkIndex;
        nextNodeToMatch_ = 0;
    }

    void on_codecDecode_start(
            ZL_Decoder* dictx,
            const ZL_Data* const* /* inStreams */,
            size_t /* nbInStreams */) override
    {
        if (!sawChunks_ || dictx == nullptr || dictx->dt == nullptr) {
            return;
        }

        const DFH_Struct* const frameHeader = DCtx_getFrameHeader(dictx->dctx);
        if (frameHeader == nullptr) {
            return;
        }

        const ZL_IDType codecID = dictx->dt->miGraphDesc.CTid;
        for (size_t i = nextNodeToMatch_; i < VECTOR_SIZE(frameHeader->nodes);
             ++i) {
            const PublicTransformInfo transformInfo =
                    VECTOR_AT(frameHeader->nodes, i).trpid;
            if (transformInfo.trid != codecID) {
                continue;
            }

            nextNodeToMatch_ = i + 1;
            if (transformInfo.trt == trt_standard) {
                codecsPerChunk_[currentChunk_].push_back(transformInfo.trid);
            }
            return;
        }
    }

   private:
    std::vector<std::vector<uint32_t>> codecsPerChunk_;
    size_t currentChunk_{ 0 };
    size_t nextNodeToMatch_{ 0 };
    bool sawChunks_{ false };
};

std::string streamLabel(const ZL_DataInfo* stream)
{
    const std::string width = std::to_string(ZL_DataInfo_getEltWidth(stream));
    switch (ZL_DataInfo_getType(stream)) {
        case ZL_Type_serial:
            return "b";
        case ZL_Type_struct:
            return "s" + width;
        case ZL_Type_numeric:
            return "n" + width;
        case ZL_Type_string:
            return "v" + width;
    }
    throw std::runtime_error("unknown stream type");
}

bool isSplitCodec(const ZL_CodecInfo* codec)
{
    if (!ZL_CodecInfo_isStandardCodec(codec)) {
        return false;
    }
    switch (ZL_CodecInfo_getCodecID(codec)) {
        case ZL_StandardTransformID_splitn:
        case ZL_StandardTransformID_splitn_struct:
        case ZL_StandardTransformID_splitn_num:
            return true;
        default:
            return false;
    }
}

bool isConversionCodec(const ZL_CodecInfo* codec)
{
    if (!ZL_CodecInfo_isStandardCodec(codec)) {
        return false;
    }
    switch (ZL_CodecInfo_getCodecID(codec)) {
        case ZL_StandardTransformID_convert_serial_to_struct:
        case ZL_StandardTransformID_convert_struct_to_serial:
        case ZL_StandardTransformID_convert_struct_to_num_le:
        case ZL_StandardTransformID_convert_num_to_struct_le:
        case ZL_StandardTransformID_convert_serial_to_num_le:
        case ZL_StandardTransformID_convert_num_to_serial_le:
        case ZL_StandardTransformID_convert_struct_to_num_be:
        case ZL_StandardTransformID_convert_serial_to_num_be:
            return true;
        default:
            return false;
    }
}

// Whether `stream` reaches the frame with no codec, or through conversion
// codecs only.
bool isStored(const ZL_DataInfo* stream)
{
    const ZL_CodecInfo* const codec = ZL_DataInfo_getConsumerCodec(stream);
    if (codec == nullptr) {
        return true;
    }
    return isConversionCodec(codec) && ZL_CodecInfo_getNumOutputs(codec) == 1
            && isStored(ZL_CodecInfo_getOutput(codec, 0));
}

// Returns the text of the subtree that regenerates `stream`. With
// `collapseSplits`, each split codec lists its distinct children once (see
// decodeGraphShape()).
std::string describeStream(const ZL_DataInfo* stream, bool collapseSplits)
{
    const ZL_CodecInfo* const codec = ZL_DataInfo_getConsumerCodec(stream);
    if (codec == nullptr) {
        return streamLabel(stream);
    }
    // A custom codec ID can equal a standard codec ID, so custom IDs get a "c"
    // prefix.
    const std::string id = (ZL_CodecInfo_isStandardCodec(codec) ? "" : "c")
            + std::to_string(ZL_CodecInfo_getCodecID(codec));
    // Reflection lists the outputs in reverse slot order.
    const size_t numOutputs = ZL_CodecInfo_getNumOutputs(codec);
    std::vector<std::string> children;
    std::set<std::string> coded;
    children.reserve(numOutputs);
    for (size_t slot = 0; slot < numOutputs; ++slot) {
        const ZL_DataInfo* const output =
                ZL_CodecInfo_getOutput(codec, numOutputs - 1 - slot);
        children.push_back(describeStream(output, collapseSplits));
        if (!isStored(output)) {
            coded.insert(children.back());
        }
    }
    std::string separator = ",";
    std::string suffix;
    if (collapseSplits && isSplitCodec(codec)) {
        const std::set<std::string> distinct(children.begin(), children.end());
        const std::set<std::string>& kept = coded.empty() ? distinct : coded;
        children.assign(kept.begin(), kept.end());
        separator = "|";
        suffix    = "*";
    }
    std::string text = id + ":" + streamLabel(stream) + "(";
    for (size_t i = 0; i < children.size(); ++i) {
        text += (i == 0 ? "" : separator) + children[i];
    }
    return text + ")" + suffix;
}

std::string describeGraph(const std::string& frame, bool collapseSplits)
{
    const ReflectionCtxPtr rctx = makeReflectionCtx(frame);
    std::string text;
    const size_t numStreams =
            ZL_ReflectionCtx_getNumStreams_lastChunk(rctx.get());
    for (size_t i = 0; i < numStreams; ++i) {
        const ZL_DataInfo* const stream =
                ZL_ReflectionCtx_getStream_lastChunk(rctx.get(), i);
        if (ZL_DataInfo_getProducerCodec(stream) != nullptr) {
            continue;
        }
        text += (text.empty() ? "" : ";")
                + describeStream(stream, collapseSplits);
    }
    return text;
}

} // namespace

std::vector<uint32_t> standardCodecsInFrame(const std::string& frame)
{
    const ReflectionCtxPtr rctx = makeReflectionCtx(frame);

    const size_t numCodecs =
            ZL_ReflectionCtx_getNumCodecs_lastChunk(rctx.get());

    std::vector<uint32_t> ids;
    ids.reserve(numCodecs);
    for (size_t i = 0; i < numCodecs; ++i) {
        const ZL_CodecInfo* const codec =
                ZL_ReflectionCtx_getCodec_lastChunk(rctx.get(), i);
        if (ZL_CodecInfo_isStandardCodec(codec)) {
            ids.push_back(ZL_CodecInfo_getCodecID(codec));
        }
    }
    return ids;
}

bool frameHasExactlyCodecs(
        const std::string& frame,
        const std::vector<uint32_t>& expected)
{
    std::vector<uint32_t> actual = standardCodecsInFrame(frame);
    if (actual.size() != expected.size()) {
        return false;
    }
    std::vector<uint32_t> sortedExpected = expected;
    std::sort(actual.begin(), actual.end());
    std::sort(sortedExpected.begin(), sortedExpected.end());
    return actual == sortedExpected;
}

std::vector<std::vector<uint32_t>> standardCodecsPerChunk(
        const std::string& frame)
{
    openzl::DCtx dctx;
    PerChunkCodecCollector collector;
    dctx.unwrap(ZL_DCtx_attachDecompressIntrospectionHooks(
            dctx.get(), collector.getRawHooks()));
    dctx.unwrap(ZL_DCtx_setParameter(
            dctx.get(), ZL_DParam_enableCodecFusion, ZL_TernaryParam_disable));

    (void)dctx.decompress(frame);

    if (!collector.sawChunks()) {
        throw std::runtime_error(
                "Decompression introspection hooks did not run");
    }
    return collector.codecsPerChunk();
}

std::string decodeGraphSignature(const std::string& frame)
{
    return describeGraph(frame, /* collapseSplits */ false);
}

std::string decodeGraphShape(const std::string& frame)
{
    return describeGraph(frame, /* collapseSplits */ true);
}

} // namespace openzl::gpu::testkit
