// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "tests/zstrong/test_zstrong_fixture.h"

#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_reflection.h"
#include "openzl/zl_selector.h"

namespace zstrong {
namespace tests {

ZStrongTest::~ZStrongTest()
{
    ZL_Compressor_free(cgraph_);
    ZL_CCtx_free(cctx_);
    ZL_DCtx_free(dctx_);
    cgraph_                 = nullptr;
    cctx_                   = nullptr;
    dctx_                   = nullptr;
    inType_                 = std::nullopt;
    vsfFieldSizesInstructs_ = {};
}

void ZStrongTest::reset()
{
    ZL_Compressor_free(cgraph_);
    cgraph_ = ZL_Compressor_create();
    ZL_REQUIRE_NN(cgraph_);

    ZL_DCtx_free(dctx_);
    dctx_ = ZL_DCtx_create();

    inType_                 = std::nullopt;
    vsfFieldSizesInstructs_ = {};

    // Default the format version to the max format verison.
    // Set it in the cgraph because these parameters have lower priority.
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph_,
            ZL_CParam_formatVersion,
            (int)formatVersion_.value_or(ZL_MAX_FORMAT_VERSION)));
}

ZL_GraphID ZStrongTest::declareGraph(ZL_NodeID node, ZL_GraphID graph)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(cgraph_, node, graph);
}

ZL_GraphID ZStrongTest::declareGraph(
        ZL_NodeID node,
        std::vector<ZL_GraphID> const& graphs)
{
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph_, node, graphs.data(), graphs.size());
}

ZL_GraphID ZStrongTest::declareGraph(ZL_NodeID node)
{
    size_t const nbOutputs = ZL_Compressor_Node_getNumOutcomes(cgraph_, node);
    std::vector<ZL_GraphID> graphs;
    graphs.reserve(nbOutputs);
    for (size_t i = 0; i < nbOutputs; ++i) {
        auto const outType =
                ZL_Compressor_Node_getOutputType(cgraph_, node, (int)i);
        graphs.push_back(store(outType));
    }
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph_, node, graphs.data(), graphs.size());
}

ZL_GraphID ZStrongTest::declareSelectorGraph(
        ZL_SelectorFn selectorFunc,
        const std::vector<ZL_GraphID>& graphs)
{
    EXPECT_GE((size_t)graphs.size(), (size_t)1);
    ZL_Type inStreamType = (ZL_Type)0;
    for (const auto& graph : graphs) {
        ZL_Type currInStreamType =
                ZL_Compressor_Graph_getInput0Mask(cgraph_, graph);
        inStreamType = (ZL_Type)(inStreamType | currInStreamType);
    }

    ZL_SelectorDesc selector_desc = {
        .selector_f     = selectorFunc,
        .inStreamType   = inStreamType,
        .customGraphs   = graphs.data(),
        .nbCustomGraphs = graphs.size(),
    };

    return ZL_Compressor_registerSelectorGraph(cgraph_, &selector_desc);
}

ZL_NodeID ZStrongTest::registerCustomTransform(
        const ZL_TypedEncoderDesc& compress,
        const ZL_TypedDecoderDesc& decompress)
{
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(dctx_, &decompress));
    return ZL_Compressor_registerTypedEncoder(cgraph_, &compress);
}

ZL_NodeID ZStrongTest::registerCustomTransform(
        ZL_SplitEncoderDesc const& compress,
        ZL_SplitDecoderDesc const& decompress)
{
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerSplitDecoder(dctx_, &decompress));
    return ZL_Compressor_registerSplitEncoder(cgraph_, &compress);
}

ZL_NodeID ZStrongTest::registerCustomTransform(
        ZL_PipeEncoderDesc const& compress,
        ZL_PipeDecoderDesc const& decompress)
{
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerPipeDecoder(dctx_, &decompress));
    return ZL_Compressor_registerPipeEncoder(cgraph_, &compress);
}

ZL_NodeID ZStrongTest::createParameterizedNode(
        ZL_NodeID node,
        const ZL_LocalParams& localParams)
{
    return ZL_Compressor_cloneNode(cgraph_, node, &localParams);
}

ZL_GraphID ZStrongTest::convertSerializedToType(
        ZL_Type type,
        size_t eltWidth,
        ZL_GraphID graph)
{
    if (type & ZL_Type_serial) {
        return graph;
    } else if (type & ZL_Type_numeric) {
        ZL_NodeID convert;
        switch (eltWidth) {
            case 1:
                convert = ZL_NODE_INTERPRET_AS_LE8;
                break;
            case 2:
                convert = ZL_NODE_INTERPRET_AS_LE16;
                break;
            case 4:
                convert = ZL_NODE_INTERPRET_AS_LE32;
                break;
            case 8:
                convert = ZL_NODE_INTERPRET_AS_LE64;
                break;
            default:
                ZL_REQUIRE_FAIL("Bad integer width!");
        }
        return declareGraph(convert, graph);
    } else if (type & ZL_Type_struct) {
        ZL_IntParam param = {
            .paramId    = ZL_trlip_tokenSize,
            .paramValue = (int)(unsigned)eltWidth,
        };
        ZL_LocalParams const params = {
            .intParams = { .intParams = &param, .nbIntParams = 1 },
        };
        ZL_NodeID const convert = ZL_Compressor_cloneNode(
                cgraph_, ZL_NODE_CONVERT_SERIAL_TO_TOKENX, &params);
        return declareGraph(convert, graph);
    } else if (type & ZL_Type_string) {
        ZL_SetStringLensParserFn parser =
                [](ZL_SetStringLensState* state,
                   const ZL_Input* in) -> ZL_SetStringLensInstructions {
            (void)in;
            ZL_ASSERT_NN(state);
            const ZL_SetStringLensInstructions* fieldSizes =
                    (const ZL_SetStringLensInstructions*)
                            ZL_SetStringLensState_getOpaquePtr(state);
            return *fieldSizes;
        };
        ZL_ASSERT_NN(vsfFieldSizesInstructs_.stringLens);
        ZL_NodeID const convert =
                ZL_Compressor_registerConvertSerialToStringNode(
                        cgraph_, parser, &vsfFieldSizesInstructs_);
        return declareGraph(convert, graph);
    } else {
        ZL_REQUIRE_FAIL("Bad stream type");
    }
}

void ZStrongTest::setStreamInType(ZL_Type inType)
{
    inType_ = inType;
}

ZL_GraphID ZStrongTest::store(ZL_Type type)
{
    if (type == ZL_Type_string) {
        return declareGraph(ZL_NODE_SEPARATE_STRING_COMPONENTS);
    } else {
        return ZL_GRAPH_STORE;
    }
}

ZL_Compressor* ZStrongTest::finalizeGraph(ZL_GraphID graph, size_t inEltWidth)
{
    auto const inType = inType_ == std::nullopt
            ? ZL_Compressor_Graph_getInput0Mask(cgraph_, graph)
            : inType_.value();
    eltWidth_         = inEltWidth;
    graph             = convertSerializedToType(inType, inEltWidth, graph);
    ZL_REQUIRE(
            !ZL_isError(ZL_Compressor_selectStartingGraphID(cgraph_, graph)));
    return cgraph_;
}

void ZStrongTest::setVsfFieldSizes(std::vector<uint32_t> fieldSizes)
{
    fieldSizes_ = std::move(fieldSizes);
    if (fieldSizes_.empty()) {
        fieldSizes_.reserve(1); // force an allocation so data() is non-null
    }
    vsfFieldSizesInstructs_ = { fieldSizes_.data(), fieldSizes_.size() };
}

void ZStrongTest::setParameter(ZL_CParam param, int value)
{
    ZL_REQUIRE(!ZL_isError(ZL_Compressor_setParameter(cgraph_, param, value)));
}

void ZStrongTest::setLevels(int compressionLevel, int decompressionLevel)
{
    setParameter(ZL_CParam_compressionLevel, compressionLevel);
    setParameter(ZL_CParam_decompressionLevel, decompressionLevel);
}

size_t ZStrongTest::compressBounds(std::string_view data)
{
    return ZL_compressBound(compressBoundFactor_ * data.size());
}

void ZStrongTest::setLargeCompressBound(size_t factor)
{
    compressBoundFactor_ = factor;
}

std::pair<ZL_Report, std::optional<std::string>> ZStrongTest::compress(
        std::string_view data)
{
    // Compress
    std::string compressed(compressBounds(data), 0);
    if (cctx_ != nullptr) {
        ZL_CCtx_free(cctx_);
        cctx_ = nullptr;
    }
    cctx_ = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx_);
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
    ZL_Report const csize = ZL_CCtx_compress(
            cctx_,
            compressed.data(),
            compressed.size(),
            data.data(),
            data.size());
    if (ZL_isError(csize)) {
        return { csize, std::nullopt };
    }
    compressed.resize(ZL_validResult(csize));
    return { csize, std::move(compressed) };
}

std::pair<ZL_Report, std::optional<std::string>> ZStrongTest::compressMI(
        std::vector<std::unique_ptr<ZL_TypedRef, ZL_TypedRef_Deleter>>& inputs)
{
    size_t compressBound = 0;
    std::vector<const ZL_TypedRef*> constInputs;
    for (auto& input : inputs) {
        compressBound += ZL_compressBound(
                (ZL_Input_contentSize(input.get())
                 + ZL_Input_numElts(input.get()) * 4)
                * compressBoundFactor_);
        constInputs.push_back(input.get());
    }
    std::string compressed(compressBound, 0);
    if (cctx_ != nullptr) {
        ZL_CCtx_free(cctx_);
        cctx_ = nullptr;
    }
    cctx_ = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx_);
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));

    ZL_Report const csize = ZL_CCtx_compressMultiTypedRef(
            cctx_,
            compressed.data(),
            compressed.size(),
            constInputs.data(),
            inputs.size());
    if (ZL_isError(csize)) {
        return { csize, std::nullopt };
    }
    compressed.resize(ZL_validResult(csize));
    return { csize, std::move(compressed) };
}

std::pair<ZL_Report, std::vector<ZL_TypedBuffer*>> ZStrongTest::decompressMI(
        std::string_view data)
{
    std::vector<ZL_TypedBuffer*> uncompressed;
    ZL_FrameInfo* fi = ZL_FrameInfo_create(data.data(), data.size());
    ZL_REQUIRE_NN(fi);
    ZL_Report res = ZL_FrameInfo_getNumOutputs(fi);
    if (ZL_isError(res)) {
        ZL_FrameInfo_free(fi);
        return { res, uncompressed };
    }
    size_t nbOutputs = ZL_validResult(res);
    std::vector<ZL_Type> outputTypes;
    outputTypes.resize(nbOutputs);
    for (size_t n = 0; n < nbOutputs; n++) {
        outputTypes[n] =
                (ZL_Type)ZL_validResult(ZL_FrameInfo_getOutputType(fi, (int)n));
    }

    std::vector<size_t> outputSizes;
    outputSizes.resize(nbOutputs);
    for (size_t n = 0; n < nbOutputs; n++) {
        outputSizes[n] = (ZL_Type)ZL_validResult(
                ZL_FrameInfo_getDecompressedSize(fi, (int)n));
    }
    ZL_FrameInfo_free(fi);

    for (size_t i = 0; i < nbOutputs; ++i) {
        uncompressed.emplace_back(ZL_TypedBuffer_create());
    }

    ZL_Report const nbDecompressed = ZL_DCtx_decompressMultiTBuffer(
            dctx_,
            uncompressed.data(),
            uncompressed.size(),
            data.data(),
            data.size());
    if (ZL_isError(nbDecompressed)) {
        return { nbDecompressed, uncompressed };
    }
    // Check the types and sizes of the decompressed outputs match FI as
    // well as type specfic information such as string lens
    ZL_ASSERT_EQ(ZL_validResult(nbDecompressed), nbOutputs);
    for (size_t n = 0; n < nbOutputs; n++) {
        ZL_ASSERT_EQ(
                (int)ZL_TypedBuffer_byteSize(uncompressed[n]),
                (int)outputSizes[n]);
        ZL_ASSERT_EQ(
                (int)ZL_TypedBuffer_type(uncompressed[n]), (int)outputTypes[n]);
        if (ZL_TypedBuffer_type(uncompressed[n]) == ZL_Type_string) {
            EXPECT_TRUE(ZL_TypedBuffer_rStringLens(uncompressed[n]));
        }
    }
    return { nbDecompressed, std::move(uncompressed) };
}

std::pair<ZL_Report, std::optional<std::string>> ZStrongTest::decompress(
        std::string_view data)
{
    ZL_Report expectedDsize = ZL_getDecompressedSize(data.data(), data.size());
    if (ZL_isError(expectedDsize)) {
        return { expectedDsize, std::nullopt };
    }
    std::string decompressed(ZL_validResult(expectedDsize), 0);
    ZL_Report const dsize = ZL_DCtx_decompress(
            dctx_,
            decompressed.data(),
            decompressed.size(),
            data.data(),
            data.size());
    if (ZL_isError(dsize)) {
        return { dsize, std::nullopt };
    }
    decompressed.resize(ZL_validResult(dsize));
    return { dsize, std::move(decompressed) };
}

std::pair<ZL_Report, std::optional<std::string>> ZStrongTest::compressTyped(
        ZL_TypedRef* typedRef)
{
    std::string compressed(
            ZL_compressBound(
                    ZL_Input_contentSize(typedRef) * compressBoundFactor_),
            0);
    if (cctx_ != nullptr) {
        ZL_CCtx_free(cctx_);
        cctx_ = nullptr;
    }
    cctx_ = ZL_CCtx_create();
    ZL_REQUIRE_NN(cctx_);
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(cctx_, cgraph_));
    ZL_Report const csize = ZL_CCtx_compressTypedRef(
            cctx_, compressed.data(), compressed.size(), typedRef);
    if (ZL_isError(csize)) {
        return { csize, std::nullopt };
    }
    compressed.resize(ZL_validResult(csize));
    return { csize, std::move(compressed) };
}

// In fuzzers, because we don't include ftest.h, gtest ASSERT_*
// macros don't correctly crash the fuzzer. So in fuzzing build
// modes, use ZL_REQUIRE*() instead.
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
#    define RT_ASSERT_SUCCESS(report) ZL_REQUIRE_SUCCESS(report)
#    define RT_ASSERT_EQ(lhs, rhs) ZL_REQUIRE_EQ((lhs), (rhs))
#    define RT_ASSERT_TRUE(cond) ZL_REQUIRE(cond)
#else
#    define RT_ASSERT_SUCCESS(report)                  \
        do {                                           \
            ASSERT_TRUE(!ZL_isError(report))           \
                    << ZL_E_str(ZL_RES_error(report)); \
        } while (0)
#    define RT_ASSERT_EQ(lhs, rhs) ASSERT_EQ((lhs), (rhs))
#    define RT_ASSERT_TRUE(cond) ASSERT_TRUE(cond)
#endif

void ZStrongTest::testRoundTripImpl(
        std::string_view data,
        bool compressionMayFail)
{
    data = data.substr(0, data.size() - (data.size() % eltWidth_));

    auto [csize, compressedOpt] = compress(data);
    if (compressionMayFail && ZL_isError(csize)) {
        return;
    }
    RT_ASSERT_SUCCESS(csize);
    auto compressed = compressedOpt.value();

    // Decompress
    auto [dsize, decompressedOpt] = decompress(compressed);
    RT_ASSERT_SUCCESS(dsize);
    auto decompressed = decompressedOpt.value();

    // Check data matches
    RT_ASSERT_EQ(data.size(), ZL_validResult(dsize));
    RT_ASSERT_TRUE(data == decompressed);
}

void ZStrongTest::assertEqual(
        const ZL_TypedBuffer* buffer,
        const TypedInputDesc& desc)
{
    /* Check type/ byte size matches original*/
    RT_ASSERT_EQ(ZL_TypedBuffer_byteSize(buffer), desc.data.size());

    RT_ASSERT_EQ((int)ZL_TypedBuffer_type(buffer), (int)desc.type);
    /* Check string lengths match original*/
    if (desc.type == ZL_Type_string) {
        RT_ASSERT_EQ(ZL_TypedBuffer_numElts(buffer), desc.strLens.size());
        if (desc.strLens.size()) {
            RT_ASSERT_EQ(
                    memcmp(desc.strLens.data(),
                           ZL_TypedBuffer_rStringLens(buffer),
                           desc.strLens.size()),
                    0);
        }
    }
    /* Check uncompressed contents match original */
    if (desc.data.size()) {
        RT_ASSERT_EQ(
                memcmp(desc.data.data(),
                       ZL_TypedBuffer_rPtr(buffer),
                       desc.data.size()),
                0);
    }
}

void ZStrongTest::testRoundTripMIImpl(
        std::vector<std::unique_ptr<ZL_TypedRef, ZL_TypedRef_Deleter>>& inputs,
        std::vector<TypedInputDesc>& inputDescs,
        bool compressionMayFail)
{
    auto [cSize, compressedOpt] = compressMI(inputs);
    if (compressionMayFail && ZL_isError(cSize)) {
        return;
    }
    RT_ASSERT_SUCCESS(cSize);
    auto compressed = compressedOpt.value();

    // Decompress
    auto [nbDecompressed, decompressedOpt] = decompressMI(compressed);
    RT_ASSERT_SUCCESS(nbDecompressed);

    for (size_t n = 0; n < ZL_validResult(nbDecompressed); ++n) {
        assertEqual(decompressedOpt[n], inputDescs[n]);
        /* Free decompressed outputs*/
        ZL_TypedBuffer_free(decompressedOpt[n]);
    }
}

} // namespace tests
} // namespace zstrong
