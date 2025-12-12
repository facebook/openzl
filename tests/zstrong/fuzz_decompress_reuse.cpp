// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <memory>

#include "tests/datagen/DataGen.h"
#include "tests/fuzz_utils.h"

#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/logging.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_errors.h"

namespace {
constexpr std::array<ZL_Type, 1> kOutStreamTypes = { ZL_Type_serial };

constexpr ZL_TypedDecoderDesc kTransform = {
    .gd = {
        .CTid = 0,
        .inStreamType = ZL_Type_serial,
        .outStreamTypes = kOutStreamTypes.data(),
        .nbOutStreams = kOutStreamTypes.size(),
    },
    .transform_f = [](ZL_Decoder* dictx, ZL_Input const* ins[]) noexcept {
        ZL_Output* out = ZL_Decoder_create1OutStream(dictx, ZL_Input_numElts(ins[0]), 1);
        ZL_RET_R_IF_NULL(allocation, out);
        memcpy(ZL_Output_ptr(out), ZL_Input_ptr(ins[0]), ZL_Input_numElts(ins[0]));
        ZL_RET_R_IF_ERR(ZL_Output_commit(out, ZL_Input_numElts(ins[0])));
        return ZL_returnSuccess();
    },
};
} // namespace

FUZZ(DecompressTest, ReuseDCtx)
{
    openzl::tests::datagen::DataGen dg = openzl::tests::fromFDP(f);
    ZL_g_logLevel                      = ZL_LOG_LVL_ALWAYS;
    ZL_DCtx* const dctx                = ZL_DCtx_create();
    ZL_REQUIRE_NN(dctx);
    while (dg.has_more_data()) {
        std::string input      = dg.randString("input_data");
        const char* const data = input.c_str();
        size_t const size      = input.length();
        ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(dctx, &kTransform));
        ZL_Report const dstSizeR = ZL_getDecompressedSize(data, size);
        if (ZL_isError(dstSizeR))
            continue;
        size_t const dstSize     = ZL_validResult(dstSizeR);
        size_t const maxDstSize  = std::min<size_t>(10 << 20, size * 100);
        size_t const dstCapacity = std::min<size_t>(dstSize, maxDstSize);
        auto dst                 = std::make_unique<uint8_t[]>(dstCapacity);
        ZL_Report const reportHeaderSize = ZL_getHeaderSize(data, size);
        ZL_Report const report =
                ZL_DCtx_decompress(dctx, dst.get(), dstCapacity, data, size);
        if (ZL_isError(reportHeaderSize) && !ZL_isError(report)) {
            // We cannot succeed in decoding while failing in decoding header
            // for header size
            ZL_REQUIRE_SUCCESS(reportHeaderSize);
        }
        if (ZL_isError(report) && report._code == ZL_ErrorCode_logicError) {
            ZL_REQUIRE_SUCCESS(report);
        }
    }
    ZL_DCtx_free(dctx);
}
