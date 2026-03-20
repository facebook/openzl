// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/DecompressIntrospectionHooks.hpp"

namespace openzl {

DecompressIntrospectionHooks::DecompressIntrospectionHooks()
{
    rawHooks_.opaque = this;

    rawHooks_.on_ZL_DCtx_decompressMultiTBuffer_start =
            [](void* this_ptr,
               ZL_DCtx* dctx,
               size_t nbOutputs,
               const void* framePtr,
               size_t frameSize) noexcept {
                ((DecompressIntrospectionHooks*)this_ptr)
                        ->on_ZL_DCtx_decompressMultiTBuffer_start(
                                dctx, nbOutputs, framePtr, frameSize);
            };
    rawHooks_.on_ZL_DCtx_decompressMultiTBuffer_end =
            [](void* this_ptr, ZL_DCtx* dctx, ZL_Report result) noexcept {
                ((DecompressIntrospectionHooks*)this_ptr)
                        ->on_ZL_DCtx_decompressMultiTBuffer_end(dctx, result);
            };

    rawHooks_.on_decompressChunk_start =
            [](void* this_ptr, ZL_DCtx* dctx, size_t chunkIndex) noexcept {
                ((DecompressIntrospectionHooks*)this_ptr)
                        ->on_decompressChunk_start(dctx, chunkIndex);
            };
    rawHooks_.on_decompressChunk_end =
            [](void* this_ptr, ZL_DCtx* dctx, ZL_Report result) noexcept {
                ((DecompressIntrospectionHooks*)this_ptr)
                        ->on_decompressChunk_end(dctx, result);
            };

    rawHooks_.on_ZL_Decoder_getCodecHeader = [](void* this_ptr,
                                                const ZL_Decoder* dictx,
                                                const void* trh,
                                                size_t trhSize) noexcept {
        ((DecompressIntrospectionHooks*)this_ptr)
                ->on_ZL_Decoder_getCodecHeader(dictx, trh, trhSize);
    };

    rawHooks_.on_codecDecode_start = [](void* this_ptr,
                                        ZL_Decoder* dictx,
                                        const ZL_Data* const* inStreams,
                                        size_t nbInStreams) noexcept {
        ((DecompressIntrospectionHooks*)this_ptr)
                ->on_codecDecode_start(dictx, inStreams, nbInStreams);
    };
    rawHooks_.on_codecDecode_end = [](void* this_ptr,
                                      ZL_Decoder* dictx,
                                      const ZL_Data* const* outStreams,
                                      size_t nbOutStreams,
                                      ZL_Report result) noexcept {
        ((DecompressIntrospectionHooks*)this_ptr)
                ->on_codecDecode_end(dictx, outStreams, nbOutStreams, result);
    };
}

} // namespace openzl
