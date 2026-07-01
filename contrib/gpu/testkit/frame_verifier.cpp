// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/dev/contrib/gpu/testkit/frame_verifier.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

#include "openzl/cpp/DCtx.hpp"
#include "openzl/decompress/dctx2.h"
#include "openzl/decompress/dictx.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_reflection.h"

namespace openzl::gpu::testkit {

namespace {

// RAII wrapper so the reflection context is always freed, even if a getter
// throws.
struct ReflectionCtxDeleter {
    void operator()(ZL_ReflectionCtx* rctx) const
    {
        ZL_ReflectionCtx_free(rctx);
    }
};
using ReflectionCtxPtr =
        std::unique_ptr<ZL_ReflectionCtx, ReflectionCtxDeleter>;

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

} // namespace openzl::gpu::testkit
