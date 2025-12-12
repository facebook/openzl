// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <initializer_list>

#include "openzl/openzl.hpp"
#include "openzl/zl_compressor.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_errors.h"
#include "tests/utils.h"

using namespace ::testing;

namespace openzl::tests {
namespace {
template <typename T, void (*FreeFn)(T*)>
struct StaticFunctionDeleter {
    void operator()(T* cgraph)
    {
        FreeFn(cgraph);
    }
};

using UniqueCGraph = std::unique_ptr<
        ZL_Compressor,
        StaticFunctionDeleter<ZL_Compressor, ZL_Compressor_free>>;
using UniqueCCtx =
        std::unique_ptr<ZL_CCtx, StaticFunctionDeleter<ZL_CCtx, ZL_CCtx_free>>;
using UniqueDCtx =
        std::unique_ptr<ZL_DCtx, StaticFunctionDeleter<ZL_DCtx, ZL_DCtx_free>>;

size_t sum(const size_t s[], size_t n)
{
    size_t total = 0;
    for (size_t u = 0; u < n; u++)
        total += s[u];
    return total;
}

// Note : kernels are as lean as possible
static void splitN(
        void* dst[],
        const size_t dstSizes[],
        size_t nbDsts,
        const void* src,
        size_t srcSize)
{
    ZL_REQUIRE(sum(dstSizes, nbDsts) == srcSize);
    (void)srcSize;
    size_t spos = 0;
    for (size_t u = 0; u < nbDsts; u++) {
        memcpy(dst[u], (const char*)src + spos, dstSizes[u]);
        spos += dstSizes[u];
    }
    ZL_REQUIRE(spos == srcSize);
}

// This transform split input in an arbitrary way
// (currently 4 segments of different sizes).
// The exact way it splits doesn't matter,
// what matters is that it respects the contract of the decoder side.
// The one "singleton" stream tells the decoder the order in which to
// concatenate the input streams.
static ZL_Report split4_encoder(ZL_Encoder* eic, const ZL_Input* in) noexcept
{
    ZL_REQUIRE(eic != nullptr);
    ZL_REQUIRE(in != nullptr);
    ZL_REQUIRE(ZL_Input_type(in) == ZL_Type_serial);
    const void* const src = ZL_Input_ptr(in);
    size_t const srcSize  = ZL_Input_numElts(in);

    // Just split arbitrarily into 4 parts of unequal size
    size_t const nbOuts = 4;
    size_t const s1     = srcSize / 3;
    size_t const s2     = srcSize / 4;
    size_t const s3     = srcSize / 5;
    size_t const s4     = srcSize - (s1 + s2 + s3);

    ZL_Output* const out0 = ZL_Encoder_createTypedStream(eic, 0, 4, 1);
    ZL_RET_R_IF_NULL(allocation, out0);

    ZL_Output* const out1 = ZL_Encoder_createTypedStream(eic, 1, s1, 1);
    ZL_RET_R_IF_NULL(allocation, out1);

    ZL_Output* const out2 = ZL_Encoder_createTypedStream(eic, 1, s2, 1);
    ZL_RET_R_IF_NULL(allocation, out2);

    ZL_Output* const out3 = ZL_Encoder_createTypedStream(eic, 1, s3, 1);
    ZL_RET_R_IF_NULL(allocation, out3);

    ZL_Output* const out4 = ZL_Encoder_createTypedStream(eic, 1, s4, 1);
    ZL_RET_R_IF_NULL(allocation, out4);

    void* dstArray[] = {
        ZL_Output_ptr(out2),
        ZL_Output_ptr(out1),
        ZL_Output_ptr(out4),
        ZL_Output_ptr(out3),
    };

    const size_t dstSizes[] = { s2, s1, s4, s3 };

    splitN(dstArray, dstSizes, nbOuts, src, srcSize);

    // Write the order into the singleton stream
    const uint8_t dstOrders[] = { 1, 0, 3, 2 };
    memcpy(ZL_Output_ptr(out0), dstOrders, 4);

    ZL_RET_R_IF_ERR(ZL_Output_commit(out0, 4));
    ZL_RET_R_IF_ERR(ZL_Output_commit(out1, s1));
    ZL_RET_R_IF_ERR(ZL_Output_commit(out2, s2));
    ZL_RET_R_IF_ERR(ZL_Output_commit(out3, s3));
    ZL_RET_R_IF_ERR(ZL_Output_commit(out4, s4));

    return ZL_returnSuccess();
}

// raw transform, minimalist interface
// @return: size written into dst (necessarily <= dstCapacity)
// requirement : sum(srcSizes[]) <= dstCapacity
static size_t concatenate(
        void* dst,
        size_t dstCapacity,
        const void* srcs[],
        size_t srcSizes[],
        size_t nbSrcs)
{
    if (nbSrcs)
        ZL_REQUIRE(srcSizes != nullptr && srcs != nullptr);
    ZL_REQUIRE(dstCapacity <= sum(srcSizes, nbSrcs));
    if (dstCapacity)
        ZL_REQUIRE(dst != nullptr);
    size_t pos = 0;
    for (size_t n = 0; n < nbSrcs; n++) {
        memcpy((char*)dst + pos, srcs[n], srcSizes[n]);
        pos += srcSizes[n];
    }
    ZL_REQUIRE(pos <= dstCapacity);
    return pos;
}

// decoder interface, respecting the Zstrong transform contract
// Concats all the VOsrcs in the order described by the single
// O1src.
static ZL_Report concat_decoder(
        ZL_Decoder* eictx,
        const ZL_Input* O1srcs[],
        size_t nbO1Srcs,
        const ZL_Input* VOsrcs[],
        size_t nbVOSrcs) noexcept
{
    ZL_REQUIRE(nbO1Srcs == 1);
    ZL_REQUIRE(VOsrcs != nullptr);
    for (size_t n = 0; n < nbVOSrcs; n++)
        ZL_REQUIRE(VOsrcs[n] != nullptr);
    for (size_t n = 0; n < nbVOSrcs; n++)
        ZL_REQUIRE(ZL_Input_type(VOsrcs[n]) == ZL_Type_serial);

    ZL_REQUIRE(ZL_Input_type(O1srcs[0]) == ZL_Type_numeric);
    ZL_REQUIRE_EQ(ZL_Input_numElts(O1srcs[0]), nbVOSrcs);
    ZL_REQUIRE_EQ(ZL_Input_eltWidth(O1srcs[0]), 1);
    uint8_t const* ordering = (uint8_t const*)ZL_Input_ptr(O1srcs[0]);

#define NB_SRCS_MAX 4
    ZL_REQUIRE(nbVOSrcs <= NB_SRCS_MAX);
    size_t srcSizes[NB_SRCS_MAX];
    for (size_t n = 0; n < nbVOSrcs; n++)
        srcSizes[n] = ZL_Input_numElts(VOsrcs[ordering[n]]);

    const void* srcPtrs[NB_SRCS_MAX];
    for (size_t n = 0; n < nbVOSrcs; n++)
        srcPtrs[n] = ZL_Input_ptr(VOsrcs[ordering[n]]);

    size_t const dstSize = sum(srcSizes, nbVOSrcs);

    ZL_Output* const out = ZL_Decoder_create1OutStream(eictx, dstSize, 1);
    ZL_RET_R_IF_NULL(allocation, out);

    size_t const r = concatenate(
            ZL_Output_ptr(out), dstSize, srcPtrs, srcSizes, nbVOSrcs);
    ZL_REQUIRE(r == dstSize);
    (void)r;

    ZL_RET_R_IF_ERR(ZL_Output_commit(out, dstSize));

    return ZL_returnSuccess();
}

class TestCustomTransform : public Test {
   public:
    void SetUp() override
    {
        cgraph_.reset(ZL_Compressor_create());
        dctx_.reset(ZL_DCtx_create());
    }

    void roundTripTest(std::string_view data, ZL_GraphID graph)
    {
        printf("\nroundTripTest \n");
        ASSERT_FALSE(ZL_isError(ZL_Compressor_setParameter(
                cgraph_.get(),
                ZL_CParam_formatVersion,
                ZL_MAX_FORMAT_VERSION)));
        ASSERT_FALSE(ZL_isError(
                ZL_Compressor_selectStartingGraphID(cgraph_.get(), graph)));
        std::string compressed;
        compressed.resize(ZL_compressBound(data.size()));
        printf("compressing %zu bytes into buffer of size %zu \n",
               data.size(),
               compressed.size());
        fflush(NULL);
        auto const cSize = ZL_compress_usingCompressor(
                compressed.data(),
                compressed.size(),
                data.data(),
                data.size(),
                cgraph_.get());
        ASSERT_FALSE(ZL_isError(cSize));
        compressed.resize(ZL_validResult(cSize));
        std::string decompressed;
        decompressed.resize(data.size());
        printf("decompressing %zu compressed bytes into buffer of size %zu \n",
               ZL_validResult(cSize),
               decompressed.size());
        fflush(NULL);
        auto const dSize = ZL_DCtx_decompress(
                dctx_.get(),
                decompressed.data(),
                decompressed.size(),
                compressed.data(),
                compressed.size());
        ASSERT_FALSE(ZL_isError(dSize));

        ASSERT_EQ(ZL_validResult(dSize), decompressed.size());
        ASSERT_TRUE(data == decompressed);
    }

    void roundTripTest(std::string_view data, ZL_NodeID node, int eltWidth = 0)
    {
        auto const graph = buildTrivialGraph(node, eltWidth);
        roundTripTest(data, graph);
    }

    ZL_GraphID buildTrivialGraph(
            ZL_NodeID node,
            int eltWidth         = 0,
            ZL_Type inStreamType = ZL_Type_serial)
    {
        auto const graph = tests::buildTrivialGraph(cgraph_.get(), node);
        return addConversionToGraph(
                cgraph_.get(), graph, inStreamType, eltWidth);
    }

    ZL_GraphID declareGraph(
            ZL_NodeID node,
            std::initializer_list<ZL_GraphID> dsts)
    {
        return ZL_Compressor_registerStaticGraph_fromNode(
                cgraph_.get(), node, dsts.begin(), dsts.size());
    }

    ZL_NodeID registerTransform(
            ZL_PipeEncoderDesc cdesc,
            ZL_PipeDecoderDesc ddesc)
    {
        ZL_REQUIRE_SUCCESS(ZL_DCtx_registerPipeDecoder(dctx_.get(), &ddesc));
        return ZL_Compressor_registerPipeEncoder(cgraph_.get(), &cdesc);
    }

    ZL_NodeID registerTransform(
            ZL_SplitEncoderDesc cdesc,
            ZL_SplitDecoderDesc ddesc)
    {
        ZL_REQUIRE_SUCCESS(ZL_DCtx_registerSplitDecoder(dctx_.get(), &ddesc));
        return ZL_Compressor_registerSplitEncoder(cgraph_.get(), &cdesc);
    }

    ZL_NodeID registerTransform(
            ZL_TypedEncoderDesc cdesc,
            ZL_TypedDecoderDesc ddesc)
    {
        ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(dctx_.get(), &ddesc));
        return ZL_Compressor_registerTypedEncoder(cgraph_.get(), &cdesc);
    }

    ZL_NodeID registerTransform(ZL_VOEncoderDesc cdesc, ZL_VODecoderDesc ddesc)
    {
        ZL_REQUIRE_SUCCESS(ZL_DCtx_registerVODecoder(dctx_.get(), &ddesc));
        return ZL_Compressor_registerVOEncoder(cgraph_.get(), &cdesc);
    }

    // Registers a simple VO transform with the given ID.
    // Input: ZL_Type_serial
    // Output 1 (fixed): ZL_Type_numeric
    // Output 2 (variable): ZL_Type_serial
    ZL_NodeID registerSimpleVOTransform(ZL_IDType id)
    {
        // Allocates all arrays for the VO transform graph description on the
        // heap, so that they go out of scope when this function returns.
        // This helps us catch places where ZStrong accidentally holds onto a
        // pointer in the graph description, instead of moving it to stable
        // memory.
        std::vector<ZL_Type> singletonTypes = { ZL_Type_numeric };
        std::vector<ZL_Type> voTypes        = { ZL_Type_serial };
        const ZL_VOGraphDesc split4_gd      = {
                 .CTid           = id,
                 .inStreamType   = ZL_Type_serial,
                 .singletonTypes = singletonTypes.data(),
                 .nbSingletons   = singletonTypes.size(),
                 .voTypes        = voTypes.data(),
                 .nbVOs          = voTypes.size(),
        };

        ZL_VOEncoderDesc const split4_CDesc = {
            .gd          = split4_gd,
            .transform_f = split4_encoder,
            .name        = "split4_encoder",
        };
        ZL_VODecoderDesc const concat_DDesc = {
            .gd          = split4_gd,
            .transform_f = concat_decoder,
            .name        = "concat_decoder",
        };

        return registerTransform(split4_CDesc, concat_DDesc);
    }

    UniqueCGraph cgraph_;
    UniqueDCtx dctx_;
};

TEST_F(TestCustomTransform, SimpleVOTransform)
{
    auto const node = registerSimpleVOTransform(0);
    roundTripTest(kEmptyTestInput, node);
    roundTripTest(kFooTestInput, node);
    roundTripTest(kLoremTestInput, node);
    roundTripTest(kAudioPCMS32LETestInput, node);
}

TEST_F(TestCustomTransform, TwoSimpleVOTransforms)
{
    auto const node0  = registerSimpleVOTransform(0);
    auto const node1  = registerSimpleVOTransform(1);
    auto const graph0 = buildTrivialGraph(node0);
    auto const graph1 = buildTrivialGraph(node1);
    auto const graph2 = declareGraph(node0, { graph0, graph1 });

    roundTripTest(kEmptyTestInput, graph2);
    roundTripTest(kFooTestInput, graph2);
    roundTripTest(kLoremTestInput, graph2);
    roundTripTest(kAudioPCMS32LETestInput, graph2);
}

TEST_F(TestCustomTransform, ZeroOutputTransform)
{
    using namespace openzl;
    class ZeroEncoder : public CustomEncoder {
       public:
        SimpleCodecDescription simpleCodecDescription() const override
        {
            return SimpleCodecDescription{
                .id          = 0,
                .inputType   = Type::Serial,
                .outputTypes = {},
            };
        }

        void encode(EncoderState& encoder) const override
        {
            const auto& input = encoder.inputs()[0];
            if (input.contentSize() != 1 || *(const uint8_t*)input.ptr() != 0) {
                throw std::runtime_error("Invalid input");
            }
        }

        ~ZeroEncoder() override = default;
    };

    class ZeroDecoder : public CustomDecoder {
       public:
        SimpleCodecDescription simpleCodecDescription() const override
        {
            return SimpleCodecDescription{
                .id          = 0,
                .inputType   = Type::Serial,
                .outputTypes = {},
            };
        }

        void decode(DecoderState& decoder) const override
        {
            auto out   = decoder.createOutput(0, 1, 1);
            uint8_t* p = (uint8_t*)out.ptr();
            p[0]       = 0;
            out.commit(1);
        }

        ~ZeroDecoder() override = default;
    };

    Compressor compressor;
    auto node =
            compressor.registerCustomEncoder(std::make_unique<ZeroEncoder>());
    auto graph = compressor.buildStaticGraph(node, {});
    compressor.selectStartingGraph(graph);
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    compressor.setParameter(CParam::MinStreamSize, -1);

    CCtx cctx;
    cctx.refCompressor(compressor);

    auto compressed = cctx.compressSerial({ "", 1 });

    DCtx dctx;
    dctx.registerCustomDecoder(std::make_unique<ZeroDecoder>());
    auto decompressed = dctx.decompressSerial(compressed);

    ASSERT_EQ(decompressed.size(), 1);
    ASSERT_EQ(decompressed[0], 0);
}

} // namespace
} // namespace openzl::tests
