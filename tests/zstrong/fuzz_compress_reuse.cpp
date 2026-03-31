// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <memory>

#include "tests/datagen/DataGen.h"
#include "tests/fuzz_utils.h"

#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/logging.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_errors.h"

namespace {
constexpr std::array<ZL_Type, 1> kOutStreamTypes = { ZL_Type_serial };

constexpr ZL_TypedGraphDesc kGraphDesc = {
    .CTid           = 0,
    .inStreamType   = ZL_Type_serial,
    .outStreamTypes = kOutStreamTypes.data(),
    .nbOutStreams   = kOutStreamTypes.size(),
};

constexpr ZL_TypedEncoderDesc kCTransform = {
    .gd = kGraphDesc,
    .transform_f =
            [](ZL_Encoder* eictx, ZL_Input const* in) noexcept {
                ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
                ZL_Output* out = ZL_Encoder_createTypedStream(
                        eictx, 0, ZL_Input_numElts(in), 1);
                ZL_ERR_IF_NULL(out, allocation);
                memcpy(ZL_Output_ptr(out),
                       ZL_Input_ptr(in),
                       ZL_Input_numElts(in));
                ZL_ERR_IF_ERR(ZL_Output_commit(out, ZL_Input_numElts(in)));
                return ZL_returnSuccess();
            },
};

constexpr ZL_TypedDecoderDesc kDTransform = {
    .gd = kGraphDesc,
    .transform_f =
            [](ZL_Decoder* dictx, ZL_Input const* ins[]) noexcept {
                ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
                ZL_Output* out = ZL_Decoder_create1OutStream(
                        dictx, 0, ZL_Input_numElts(ins[0]));
                ZL_ERR_IF_NULL(out, allocation);
                memcpy(ZL_Output_ptr(out),
                       ZL_Input_ptr(ins[0]),
                       ZL_Input_numElts(ins[0]));
                ZL_ERR_IF_ERR(ZL_Output_commit(out, ZL_Input_numElts(ins[0])));
                return ZL_returnSuccess();
            },
};

void check_decompression(
        const void* cBuffer,
        size_t cSize,
        const void* origBuffer,
        size_t origSize)
{
    ZL_Report const dsR = ZL_getDecompressedSize(cBuffer, cSize);
    EXPECT_EQ(ZL_isError(dsR), 0);
    size_t const decSize = ZL_validResult(dsR);
    EXPECT_EQ(decSize, origSize);
    auto decompressed     = std::make_unique<uint8_t[]>(decSize);
    void* const decBuffer = decompressed.get();
    ZL_DCtx* dctx         = ZL_DCtx_create();
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(dctx, &kDTransform));
    ZL_Report const decR =
            ZL_DCtx_decompress(dctx, decBuffer, decSize, cBuffer, cSize);
    EXPECT_EQ(ZL_isError(decR), 0) << ZL_DCtx_getOperationContext(dctx);
    ZL_DCtx_free(dctx);
    size_t const finalDecSize = ZL_validResult(decR);
    EXPECT_EQ(finalDecSize, origSize);
    EXPECT_EQ(
            memcmp(origBuffer, decBuffer, origSize),
            0); // identical binary content
}

ZL_GraphID setFLZGraph(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
            cgraph,
            ZL_NODELIST(ZL_NODE_INTERPRET_AS_LE32, ZL_NODE_DELTA_INT),
            ZL_Compressor_registerFieldLZGraph(cgraph));
}

ZL_GraphID setZstdGraph(ZL_Compressor* cgraph)
{
    (void)cgraph;
    return ZL_GRAPH_ZSTD;
}

ZL_GraphID setCopyGraph(ZL_Compressor* cgraph)
{
    auto const node = ZL_Compressor_registerTypedEncoder(cgraph, &kCTransform);
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, node, ZL_GRAPH_STORE);
}

} // namespace

FUZZ(CompressTest, ReuseCCtx)
{
    openzl::tests::datagen::DataGen dg = openzl::tests::fromFDP(f);
    ZL_g_logLevel                      = ZL_LOG_LVL_ALWAYS;
    ZL_CCtx* const cctx                = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx);
    ZL_Compressor* const cgraph1 = ZL_Compressor_create();
    ZL_REQUIRE_NN(cgraph1);
    {
        ZL_GraphID const sgid = setFLZGraph(cgraph1);
        ZL_Report const gssr =
                ZL_Compressor_selectStartingGraphID(cgraph1, sgid);
        EXPECT_EQ(ZL_isError(gssr), 0) << "setting cgraph1 failed\n";
    }
    ZL_Compressor* const cgraph2 = ZL_Compressor_create();
    ZL_REQUIRE_NN(cgraph2);
    {
        ZL_GraphID const sgid = setZstdGraph(cgraph2);
        ZL_Report const gssr =
                ZL_Compressor_selectStartingGraphID(cgraph2, sgid);
        EXPECT_EQ(ZL_isError(gssr), 0) << "setting cgraph2 failed\n";
    }
    ZL_Compressor* const cgraph3 = ZL_Compressor_create();
    ZL_REQUIRE_NN(cgraph3);
    {
        ZL_GraphID const sgid = setCopyGraph(cgraph3);
        ZL_Report const gssr =
                ZL_Compressor_selectStartingGraphID(cgraph3, sgid);
        EXPECT_EQ(ZL_isError(gssr), 0) << "setting cgraph3 failed\n";
    }

    while (dg.has_more_data()) {
        std::string input        = dg.randString("input_data");
        const char* const data   = input.c_str();
        size_t const isize       = input.length();
        size_t const dstCapacity = ZL_compressBound(isize);
        auto dst                 = std::make_unique<uint8_t[]>(dstCapacity);
        const ZL_Compressor* const cgraph = dg.choices(
                "cgraph",
                std::vector<const ZL_Compressor*>{ cgraph1, cgraph2, cgraph3 });
        ZL_Report const cgr = ZL_CCtx_refCompressor(cctx, cgraph);
        EXPECT_EQ(ZL_isError(cgr), 0) << "CGraph reference failed\n";
        ZL_Report const creport =
                ZL_CCtx_compress(cctx, dst.get(), dstCapacity, data, isize);
        if (!ZL_isError(creport)) {
            // check that the produced frame is valid (can be decompressed,
            // and reproduces original data)
            size_t const cSize = ZL_validResult(creport);
            check_decompression(dst.get(), cSize, data, isize);
        }
        // If compression fails cleanly, that's fine too.
        // We just want to check for Sanitizer errors (like asan, ubsan, msan)
    }
    ZL_Compressor_free(cgraph1);
    ZL_Compressor_free(cgraph2);
    ZL_Compressor_free(cgraph3);
    ZL_CCtx_free(cctx);
}
