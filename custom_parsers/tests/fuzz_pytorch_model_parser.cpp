// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "security/lionhead/utils/lib_ftest/ftest.h"

#include "custom_parsers/pytorch_model_parser.h"
#include "openzl/common/errors_internal.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_version.h"

FUZZ(PytorchModelParserTest, FuzzCompress)
{
    const auto formatVersion =
            f.i32_range("format_version", 16, ZL_MAX_FORMAT_VERSION);
    auto data = f.all_remaining_bytes();

    ZL_Compressor* cgraph = ZL_Compressor_create();
    ZL_REQUIRE_SUCCESS(
            ZL_Compressor_setParameter(cgraph, ZL_CParam_compressionLevel, 1));
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, formatVersion));
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(
            cgraph, ZL_createGraph_pytorchModelCompressor(cgraph)));

    ZL_CCtx* cctx = ZL_CCtx_create();
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx, cgraph));

    std::string compressed(data.size() * 2 + 1000, '\0');
    const auto cSize = ZL_CCtx_compress(
            cctx,
            compressed.data(),
            compressed.size(),
            data.data(),
            data.size());
    if (!ZL_isError(cSize)) {
        ZL_DCtx* dctx = ZL_DCtx_create();
        std::vector<uint8_t> roundTripped(data.size());
        const auto dSize = ZL_DCtx_decompress(
                dctx,
                roundTripped.data(),
                roundTripped.size(),
                compressed.data(),
                ZL_validResult(cSize));
        ZL_REQUIRE_SUCCESS(dSize);
        ZL_REQUIRE_EQ(data.size(), ZL_validResult(dSize));
        ZL_REQUIRE(data == roundTripped);
        ZL_DCtx_free(dctx);
    }
    ZL_CCtx_free(cctx);
    ZL_Compressor_free(cgraph);
}
