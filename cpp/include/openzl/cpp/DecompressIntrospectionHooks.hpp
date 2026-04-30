// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/zl_introspection.h"

namespace openzl {

class DecompressIntrospectionHooks {
   public:
    DecompressIntrospectionHooks();
    virtual ~DecompressIntrospectionHooks() = default;

    DecompressIntrospectionHooks(const DecompressIntrospectionHooks&) = delete;
    DecompressIntrospectionHooks& operator=(
            const DecompressIntrospectionHooks&)                 = delete;
    DecompressIntrospectionHooks(DecompressIntrospectionHooks&&) = delete;
    DecompressIntrospectionHooks& operator=(DecompressIntrospectionHooks&&) =
            delete;

    ZL_DecompressIntrospectionHooks* getRawHooks()
    {
        return &rawHooks_;
    }

    virtual void on_ZL_DCtx_decompressMultiTBuffer_start(
            ZL_DCtx* dctx,
            size_t nbOutputs,
            const void* framePtr,
            size_t frameSize)
    {
        (void)dctx;
        (void)nbOutputs;
        (void)framePtr;
        (void)frameSize;
    }
    virtual void on_ZL_DCtx_decompressMultiTBuffer_end(
            ZL_DCtx* dctx,
            ZL_Report result)
    {
        (void)dctx;
        (void)result;
    }

    virtual void on_decompressChunk_start(ZL_DCtx* dctx, size_t chunkIndex)
    {
        (void)dctx;
        (void)chunkIndex;
    }
    virtual void on_decompressChunk_end(ZL_DCtx* dctx, ZL_Report result)
    {
        (void)dctx;
        (void)result;
    }

    virtual void on_ZL_Decoder_getCodecHeader(
            const ZL_Decoder* dictx,
            const void* trh,
            size_t trhSize)
    {
        (void)dictx;
        (void)trh;
        (void)trhSize;
    }

    virtual void on_codecDecode_start(
            ZL_Decoder* dictx,
            const ZL_Data* const* inStreams,
            size_t nbInStreams)
    {
        (void)dictx;
        (void)inStreams;
        (void)nbInStreams;
    }
    virtual void on_codecDecode_end(
            ZL_Decoder* dictx,
            const ZL_Data* const* outStreams,
            size_t nbOutStreams,
            ZL_Report result)
    {
        (void)dictx;
        (void)outStreams;
        (void)nbOutStreams;
        (void)result;
    }

   private:
    ZL_DecompressIntrospectionHooks rawHooks_{};
};

} // namespace openzl
