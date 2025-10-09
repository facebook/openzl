// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/tulip_v2/decode_tulip_v2.h"

#include <stdexcept>

#include "custom_transforms/thrift/kernels/decode_thrift_binding.h"

namespace zstrong::tulip_v2 {

ZL_Report registerCustomTransforms(
        ZL_DCtx* dctx,
        unsigned idRangeBegin,
        unsigned idRangeEnd)
{
    if (idRangeEnd - idRangeBegin < kNumCustomTransforms) {
        throw std::runtime_error("Not enough IDs");
    }

    // NOTE: These IDs must remain stable & in-sync with the compressor!
    ZL_RET_R_IF_ERR(ZL_ThriftKernel_registerDTransformMapI32Float(
            dctx, idRangeBegin + static_cast<unsigned>(Tag::FloatFeatures)));
    ZL_RET_R_IF_ERR(ZL_ThriftKernel_registerDTransformMapI32ArrayFloat(
            dctx,
            idRangeBegin + static_cast<unsigned>(Tag::FloatListFeatures)));
    ZL_RET_R_IF_ERR(ZL_ThriftKernel_registerDTransformMapI32ArrayI64(
            dctx, idRangeBegin + static_cast<unsigned>(Tag::IdListFeatures)));
    ZL_RET_R_IF_ERR(ZL_ThriftKernel_registerDTransformMapI32ArrayArrayI64(
            dctx,
            idRangeBegin + static_cast<unsigned>(Tag::IdListListFeatures)));
    ZL_RET_R_IF_ERR(ZL_ThriftKernel_registerDTransformMapI32MapI64Float(
            dctx,
            idRangeBegin + static_cast<unsigned>(Tag::IdScoreListFeatures)));

    return ZL_returnValue(idRangeBegin + kNumCustomTransforms);
}
} // namespace zstrong::tulip_v2
