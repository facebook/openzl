// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "openzl/common/wire_format.h"
#include "openzl/decompress/decode_frameheader.h"
#include "openzl/dict/bundle.h"
#include "openzl/dict/dict.h"
#include "openzl/dict/dict_constants.h"
#include "openzl/shared/xxhash.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_dictloader.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_materializer.h"
#include "openzl/zl_version.h"

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/CParam.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/Input.hpp"
#include "openzl/cpp/poly/Span.hpp"

using namespace ::testing;

namespace openzl {
namespace {

constexpr ZL_Type kSerialTypes[1]  = { ZL_Type_serial };
constexpr ZL_NodeID kIllegalNodeID = ZL_NODE_ILLEGAL;

using DCtxPtr = std::unique_ptr<ZL_DCtx, decltype(&ZL_DCtx_free)>;
using FrameInfoPtr =
        std::unique_ptr<ZL_FrameInfo, decltype(&ZL_FrameInfo_free)>;
using LoaderPtr = std::unique_ptr<
        ZL_FatBundleDictLoader,
        decltype(&ZL_FatBundleDictLoader_free)>;

struct DictCodecSpec {
    ZL_IDType codecId;
    ZL_DictID dictId;
    uint8_t bias;
};

ZL_DictID makeDictID(uint8_t seed)
{
    ZL_DictID dictID = {};
    for (size_t i = 0; i < sizeof(dictID.id.bytes); ++i) {
        dictID.id.bytes[i] = static_cast<uint8_t>(seed + i);
    }
    return dictID;
}

std::vector<uint8_t> buildPackedDict(
        ZL_DictID dictID,
        ZL_IDType codecId,
        const std::vector<uint8_t>& content)
{
    std::vector<uint8_t> packed(ZL_DICT_HEADER_SIZE + content.size(), 0);
    ZL_Report report = Dict_pack(
            packed.data(),
            packed.size(),
            dictID,
            codecId,
            trt_custom,
            content.data(),
            content.size());
    EXPECT_FALSE(ZL_isError(report));
    if (ZL_isError(report)) {
        return {};
    }
    packed.resize(ZL_validResult(report));
    return packed;
}

std::vector<uint8_t> packFatBundle(
        const std::vector<std::vector<uint8_t>>& packedDicts)
{
    std::vector<const void*> dictPtrs;
    std::vector<size_t> dictSizes;
    size_t totalBytes = 0;
    dictPtrs.reserve(packedDicts.size());
    dictSizes.reserve(packedDicts.size());

    for (const auto& packedDict : packedDicts) {
        dictPtrs.push_back(packedDict.data());
        dictSizes.push_back(packedDict.size());
        totalBytes += packedDict.size();
    }

    size_t const bundleCapacity = ZL_BUNDLE_HEADER_SIZE
            + packedDicts.size() * ZL_UNIQUE_ID_SIZE + totalBytes;
    std::vector<uint8_t> fatBundle(bundleCapacity, 0);
    ZL_Report report = ZL_DictBundle_packFatBundle(
            fatBundle.data(),
            fatBundle.size(),
            packedDicts.empty() ? nullptr : dictPtrs.data(),
            packedDicts.empty() ? nullptr : dictSizes.data(),
            packedDicts.size());
    EXPECT_FALSE(ZL_isError(report));
    if (ZL_isError(report)) {
        return {};
    }
    fatBundle.resize(ZL_validResult(report));
    return fatBundle;
}

ZL_RESULT_OF(ZL_VoidPtr)
copyMaterialize(ZL_Materializer* matCtx, const void* src, size_t srcSize)
        ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, nullptr);
    void* copy = ZL_Materializer_allocate(matCtx, srcSize);
    ZL_ERR_IF_NULL(copy, allocation);
    std::memcpy(copy, src, srcSize);
    return ZL_WRAP_VALUE(copy);
}

ZL_MaterializerDesc2 const kCopyDictMaterializer = {
    .materializeFn   = copyMaterialize,
    .dematerializeFn = ZL_NOOP_DEMATERIALIZE,
};

ZL_Report passthroughTypedEncoder(ZL_Encoder* enc, const ZL_Input* in)
        ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(enc);
    ZL_REQUIRE_NN(in);
    ZL_REQUIRE_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_REQUIRE_NULL(ZL_Encoder_getMaterializedDict(enc));

    size_t const size = ZL_Input_numElts(in);
    ZL_Output* out    = ZL_Encoder_createTypedStream(enc, 0, size, 1);
    ZL_ERR_IF_NULL(out, GENERIC);
    std::memcpy(ZL_Output_ptr(out), ZL_Input_ptr(in), size);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, size));
    return ZL_returnValue(1);
}

ZL_Report passthroughTypedDecoder(ZL_Decoder* dec, const ZL_Input* inputs[])
        ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dec);
    const ZL_Input* input = inputs[0];
    ZL_REQUIRE_NN(input);
    ZL_REQUIRE_NULL(ZL_Decoder_getMaterializedDict(dec));

    size_t const size = ZL_Input_numElts(input);
    ZL_Output* out    = ZL_Decoder_create1OutStream(dec, size, 1);
    ZL_ERR_IF_NULL(out, GENERIC);
    std::memcpy(ZL_Output_ptr(out), ZL_Input_ptr(input), size);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, size));
    return ZL_returnValue(1);
}

ZL_Report passthroughMIEncoder(
        ZL_Encoder* enc,
        const ZL_Input* inputs[],
        size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(enc);
    ZL_ERR_IF_NE(nbInputs, 1, GENERIC);
    const ZL_Input* input = inputs[0];
    ZL_ERR_IF_NULL(input, GENERIC);
    ZL_ERR_IF_NE(ZL_Input_type(input), ZL_Type_serial, GENERIC);

    size_t const size = ZL_Input_numElts(input);
    ZL_Output* out    = ZL_Encoder_createTypedStream(enc, 0, size, 1);
    ZL_ERR_IF_NULL(out, GENERIC);
    std::memcpy(ZL_Output_ptr(out), ZL_Input_ptr(input), size);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, size));
    return ZL_returnSuccess();
}

ZL_Report encodeWithDict(
        ZL_Encoder* enc,
        const ZL_Input* inputs[],
        size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(enc);
    ZL_ERR_IF_NE(nbInputs, 1, GENERIC);
    const ZL_Input* input = inputs[0];
    ZL_ERR_IF_NULL(input, GENERIC);
    ZL_ERR_IF_NE(ZL_Input_type(input), ZL_Type_serial, GENERIC);

    const uint8_t* materializedDict =
            static_cast<const uint8_t*>(ZL_Encoder_getMaterializedDict(enc));
    ZL_ERR_IF_NULL(
            materializedDict,
            GENERIC,
            "Expected getMaterializedDict to return a materialized dict");

    size_t const size = ZL_Input_numElts(input);
    ZL_Output* out    = ZL_Encoder_createTypedStream(enc, 0, size, 1);
    ZL_ERR_IF_NULL(out, GENERIC);

    const uint8_t* src = static_cast<const uint8_t*>(ZL_Input_ptr(input));
    uint8_t* dst       = static_cast<uint8_t*>(ZL_Output_ptr(out));
    for (size_t i = 0; i < size; ++i) {
        dst[i] = static_cast<uint8_t>(src[i] + materializedDict[0]);
    }
    ZL_ERR_IF_ERR(ZL_Output_commit(out, size));
    return ZL_returnSuccess();
}

ZL_Report encodeChecksMaterializedDict(
        ZL_Encoder* enc,
        const ZL_Input* inputs[],
        size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(enc);
    const uint8_t* expected = static_cast<const uint8_t*>(
            ZL_Encoder_getLocalParam(enc, 1).paramRef);
    ZL_ERR_IF_NULL(expected, GENERIC);

    const uint8_t* materializedDict =
            static_cast<const uint8_t*>(ZL_Encoder_getMaterializedDict(enc));
    ZL_ERR_IF_NULL(materializedDict, GENERIC);
    ZL_ERR_IF_NE(
            *materializedDict, *expected, GENERIC, "Dict content mismatch");

    return passthroughMIEncoder(enc, inputs, nbInputs);
}

ZL_Report decodeWithDict(
        ZL_Decoder* dec,
        const ZL_Input* compressedStreams[],
        size_t nbCompressedStreams,
        const ZL_Input*[],
        size_t) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dec);
    ZL_ERR_IF_NE(nbCompressedStreams, 1, GENERIC);
    const ZL_Input* input = compressedStreams[0];
    ZL_ERR_IF_NULL(input, GENERIC);

    const uint8_t* materializedDict =
            static_cast<const uint8_t*>(ZL_Decoder_getMaterializedDict(dec));
    ZL_ERR_IF_NULL(materializedDict, GENERIC);

    size_t const size = ZL_Input_numElts(input);
    ZL_Output* out    = ZL_Decoder_create1OutStream(dec, size, 1);
    ZL_ERR_IF_NULL(out, GENERIC);

    const uint8_t* src = static_cast<const uint8_t*>(ZL_Input_ptr(input));
    uint8_t* dst       = static_cast<uint8_t*>(ZL_Output_ptr(out));
    for (size_t i = 0; i < size; ++i) {
        dst[i] = static_cast<uint8_t>(src[i] - materializedDict[0]);
    }
    ZL_ERR_IF_ERR(ZL_Output_commit(out, size));
    return ZL_returnSuccess();
}

class DictIntegrationTest : public Test {
   protected:
    void SetUp() override
    {
        nextCodecId_ = 5000;
    }

    DictCodecSpec makeDictSpec(uint8_t seed, uint8_t bias)
    {
        return DictCodecSpec{
            .codecId = nextCodecId_++,
            .dictId  = makeDictID(seed),
            .bias    = bias,
        };
    }

    std::vector<uint8_t> buildBundleForSpecs(
            const std::vector<DictCodecSpec>& specs) const
    {
        std::vector<std::vector<uint8_t>> packedDicts;
        packedDicts.reserve(specs.size());
        for (const DictCodecSpec& spec : specs) {
            packedDicts.push_back(
                    buildPackedDict(spec.dictId, spec.codecId, { spec.bias }));
        }
        return packFatBundle(packedDicts);
    }

    void loadBundleForSpecs(const std::vector<DictCodecSpec>& specs)
    {
        fatBundle_ = buildBundleForSpecs(specs);
        ASSERT_FALSE(ZL_isError(ZL_Compressor_loadDictBundle(
                compressor_.get(), fatBundle_.data(), fatBundle_.size())));
    }

    ZL_NodeID registerTypedNoDictNode(const char* name = "typed_no_dict")
    {
        ZL_TypedGraphDesc graphDesc = {
            .CTid           = nextCodecId_++,
            .inStreamType   = ZL_Type_serial,
            .outStreamTypes = kSerialTypes,
            .nbOutStreams   = 1,
        };
        ZL_TypedEncoderDesc encoderDesc = {
            .gd          = graphDesc,
            .transform_f = passthroughTypedEncoder,
            .name        = name,
        };
        ZL_NodeID node = ZL_Compressor_registerTypedEncoder(
                compressor_.get(), &encoderDesc);
        EXPECT_NE(node.nid, kIllegalNodeID.nid);
        return node;
    }

    ZL_NodeID registerDictNode(
            const DictCodecSpec& spec,
            ZL_MIEncoderFn transformFn        = encodeWithDict,
            const ZL_LocalParams* localParams = nullptr,
            const char* name                  = "dict_node")
    {
        ZL_MIEncoderDesc encoderDesc = {
            .gd =
                    {
                            .CTid       = spec.codecId,
                            .inputTypes = kSerialTypes,
                            .nbInputs   = 1,
                            .soTypes    = kSerialTypes,
                            .nbSOs      = 1,
                    },
            .transform_f = transformFn,
            .name        = name,
            .dictMat     = kCopyDictMaterializer,
            .dictID      = spec.dictId,
        };
        if (localParams != nullptr) {
            encoderDesc.localParams = *localParams;
        }
        ZL_RESULT_OF(ZL_NodeID)
        nodeResult = ZL_Compressor_registerMIEncoder2(
                compressor_.get(), &encoderDesc);
        EXPECT_FALSE(ZL_RES_isError(nodeResult));
        return ZL_RES_isError(nodeResult) ? kIllegalNodeID
                                          : ZL_RES_value(nodeResult);
    }

    void registerTypedNoDictDecoder(ZL_DCtx* dctx, ZL_IDType codecId) const
    {
        ZL_TypedGraphDesc graphDesc = {
            .CTid           = codecId,
            .inStreamType   = ZL_Type_serial,
            .outStreamTypes = kSerialTypes,
            .nbOutStreams   = 1,
        };
        ZL_TypedDecoderDesc decoderDesc = {
            .gd          = graphDesc,
            .transform_f = passthroughTypedDecoder,
        };
        ASSERT_FALSE(
                ZL_isError(ZL_DCtx_registerTypedDecoder(dctx, &decoderDesc)));
    }

    void registerDictDecoder(ZL_DCtx* dctx, const ZL_IDType ctid) const
    {
        ZL_MIDecoderDesc decoderDesc = {
            .gd =
                    {
                            .CTid       = ctid,
                            .inputTypes = kSerialTypes,
                            .nbInputs   = 1,
                            .soTypes    = kSerialTypes,
                            .nbSOs      = 1,
                    },
            .transform_f = decodeWithDict,
        };
        ASSERT_FALSE(ZL_isError(ZL_DCtx_registerMIDecoder(dctx, &decoderDesc)));
    }

    ZL_GraphID buildSingleNodeGraph(ZL_NodeID node)
    {
        ZL_GraphID graphId =
                compressor_.buildStaticGraph(node, { ZL_GRAPH_STORE });
        EXPECT_NE(graphId.gid, ZL_GRAPH_ILLEGAL.gid);
        return graphId;
    }

    ZL_GraphID buildPipelineGraph(std::initializer_list<ZL_NodeID> nodes)
    {
        std::vector<ZL_NodeID> pipeline(nodes);
        ZL_GraphID graphId =
                ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
                        compressor_.get(),
                        pipeline.data(),
                        pipeline.size(),
                        ZL_GRAPH_STORE);
        EXPECT_TRUE(ZL_GraphID_isValid(graphId));
        return graphId;
    }

    void selectGraph(ZL_GraphID graphId)
    {
        ASSERT_FALSE(ZL_isError(ZL_Compressor_selectStartingGraphID(
                compressor_.get(), graphId)));
    }

    std::vector<char> compress(
            const std::string& input,
            uint32_t formatVersion = ZL_MAX_FORMAT_VERSION)
    {
        cctx_.refCompressor(compressor_);
        cctx_.setParameter(CParam::FormatVersion, formatVersion);
        std::vector<char> compressed(ZL_COMPRESSBOUND(input.size()), 0);
        size_t const compressedSize = cctx_.compressSerial(
                poly::span<char>(compressed.data(), compressed.size()), input);
        compressed.resize(compressedSize);
        return compressed;
    }

    ZL_Report compressExpectingReport(
            const std::string& input,
            std::vector<char>& output,
            uint32_t formatVersion)
    {
        cctx_.refCompressor(compressor_);
        cctx_.setParameter(CParam::FormatVersion, formatVersion);
        output.assign(ZL_COMPRESSBOUND(input.size()), 0);
        auto typedInput = Input::refSerial(input);
        return ZL_CCtx_compressTypedRef(
                cctx_.get(), output.data(), output.size(), typedInput.get());
    }

    std::string decompress(
            const std::vector<char>& compressed,
            size_t expectedSize,
            ZL_DCtx* dctx) const
    {
        std::string output(expectedSize, '\0');
        ZL_Report report = ZL_DCtx_decompress(
                dctx,
                output.data(),
                output.size(),
                compressed.data(),
                compressed.size());
        EXPECT_FALSE(ZL_isError(report));
        if (!ZL_isError(report)) {
            output.resize(ZL_validResult(report));
        }
        return output;
    }

    DCtxPtr createDCtx() const
    {
        return DCtxPtr(ZL_DCtx_create(), &ZL_DCtx_free);
    }

    LoaderPtr createLoader(
            const std::vector<DictCodecSpec>& specs,
            const std::vector<uint8_t>& fatBundle) const
    {
        LoaderPtr loader(
                ZL_FatBundleDictLoader_create(), &ZL_FatBundleDictLoader_free);
        EXPECT_NE(loader.get(), nullptr);
        if (loader == nullptr) {
            return loader;
        }

        ZL_DictLoader* dictLoader =
                ZL_FatBundleDictLoader_getDictLoader(loader.get());
        EXPECT_NE(dictLoader, nullptr);
        if (dictLoader == nullptr) {
            return loader;
        }

        for (const DictCodecSpec& spec : specs) {
            EXPECT_FALSE(ZL_isError(ZL_DictLoader_registerMaterializer(
                    dictLoader, spec.codecId, &kCopyDictMaterializer)));
        }

        EXPECT_FALSE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
                loader.get(), fatBundle.data(), fatBundle.size())));
        return loader;
    }

    FrameInfoPtr createFrameInfo(const std::vector<char>& compressed) const
    {
        FrameInfoPtr frameInfo(
                ZL_FrameInfo_create(compressed.data(), compressed.size()),
                &ZL_FrameInfo_free);
        EXPECT_NE(frameInfo.get(), nullptr);
        return frameInfo;
    }

    void corruptBundleID(std::vector<char>& compressed) const
    {
        FrameInfoPtr frameInfo = createFrameInfo(compressed);
        ASSERT_NE(frameInfo.get(), nullptr);
        ASSERT_NE(ZL_FrameInfo_getBundleID(frameInfo.get()), nullptr);
        size_t const frameHeaderSize =
                FrameInfo_frameHeaderSize(frameInfo.get());
        ASSERT_GE(frameHeaderSize, sizeof(ZL_BundleID));
        compressed[frameHeaderSize - sizeof(ZL_BundleID)] ^= 1;
        // Recompute the frame header checksum (last byte) so the
        // decoder reaches the dict lookup rather than failing on
        // a generic header corruption check.
        if (FrameInfo_hasCompressedChecksum(frameInfo.get())) {
            uint64_t const fhchk =
                    XXH3_64bits(compressed.data(), frameHeaderSize - 1);
            compressed[frameHeaderSize - 1] = static_cast<char>(fhchk & 255);
        }
    }

    Compressor compressor_;
    CCtx cctx_;
    ZL_IDType nextCodecId_{ 0 };
    std::vector<uint8_t> fatBundle_;
};

TEST_F(DictIntegrationTest, FullRoundTrip)
{
    DictCodecSpec dictSpec = makeDictSpec(0xD1, 42);
    ZL_NodeID dictNode =
            registerDictNode(dictSpec, encodeWithDict, nullptr, "dict");
    ZL_NodeID storeNode = registerTypedNoDictNode("store");
    ZL_GraphID graphId  = buildPipelineGraph({ dictNode, storeNode });
    loadBundleForSpecs({ dictSpec });
    std::string input(77, '\0');
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<char>(i);
    }

    selectGraph(graphId);
    std::vector<char> compressed = compress(input);

    DCtxPtr dctx = createDCtx();
    ASSERT_NE(dctx.get(), nullptr);
    LoaderPtr loader = createLoader({ dictSpec }, fatBundle_);
    ASSERT_NE(loader.get(), nullptr);
    ZL_DCtx_refDictLoader(
            dctx.get(), ZL_FatBundleDictLoader_getDictLoader(loader.get()));
    registerDictDecoder(dctx.get(), dictSpec.codecId);
    registerTypedNoDictDecoder(dctx.get(), nextCodecId_ - 1);

    EXPECT_EQ(decompress(compressed, input.size(), dctx.get()), input);
}

TEST_F(DictIntegrationTest, PreV24FrameOmitsBundleIDEvenWhenBundleIsLoaded)
{
    DictCodecSpec unusedDict = makeDictSpec(11, 0x31);
    ZL_NodeID unusedDictNode = registerDictNode(
            unusedDict, encodeWithDict, nullptr, "unused_dict");
    ASSERT_TRUE(ZL_NodeID_isValid(unusedDictNode));
    loadBundleForSpecs({ unusedDict });

    ZL_NodeID node     = registerTypedNoDictNode();
    ZL_GraphID graphId = buildSingleNodeGraph(node);
    std::string input(77, '\0');
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<char>(i);
    }

    selectGraph(graphId);
    std::vector<char> compressed =
            compress(input, ZL_MATERIALIZED_DICT_VERSION_MIN - 1);

    ZL_Report formatVersion =
            ZL_getFormatVersionFromFrame(compressed.data(), compressed.size());
    ASSERT_FALSE(ZL_isError(formatVersion));
    EXPECT_EQ(
            ZL_validResult(formatVersion),
            ZL_MATERIALIZED_DICT_VERSION_MIN - 1);

    FrameInfoPtr frameInfo = createFrameInfo(compressed);
    ASSERT_NE(frameInfo.get(), nullptr);
    EXPECT_EQ(ZL_FrameInfo_getBundleID(frameInfo.get()), nullptr);

    DCtxPtr dctx = createDCtx();
    ASSERT_NE(dctx.get(), nullptr);
    registerTypedNoDictDecoder(dctx.get(), nextCodecId_ - 1);
    EXPECT_EQ(decompress(compressed, input.size(), dctx.get()), input);
}

TEST_F(DictIntegrationTest, TwoDictNodesResolveDistinctMaterializedDicts)
{
    DictCodecSpec dictA = makeDictSpec(20, 0x0A);
    DictCodecSpec dictB = makeDictSpec(30, 0x13);
    ZL_NodeID nodeA =
            registerDictNode(dictA, encodeWithDict, nullptr, "dict_a");
    ZL_NodeID nodeB =
            registerDictNode(dictB, encodeWithDict, nullptr, "dict_b");
    ZL_GraphID graphId = buildPipelineGraph({ nodeA, nodeB });
    loadBundleForSpecs({ dictA, dictB });
    std::string input(77, '\0');
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<char>(i);
    }

    selectGraph(graphId);
    std::vector<char> compressed = compress(input);

    DCtxPtr dctx = createDCtx();
    ASSERT_NE(dctx.get(), nullptr);
    LoaderPtr loader = createLoader({ dictA, dictB }, fatBundle_);
    ASSERT_NE(loader.get(), nullptr);
    ZL_DCtx_refDictLoader(
            dctx.get(), ZL_FatBundleDictLoader_getDictLoader(loader.get()));
    registerDictDecoder(dctx.get(), dictA.codecId);
    registerDictDecoder(dctx.get(), dictB.codecId);

    EXPECT_EQ(decompress(compressed, input.size(), dctx.get()), input);
}

TEST_F(DictIntegrationTest, MissingDictLoaderFailsWithDictNoRecord)
{
    DictCodecSpec dictSpec = makeDictSpec(40, 0x07);
    ZL_NodeID node         = registerDictNode(dictSpec);
    ZL_GraphID graphId     = buildSingleNodeGraph(node);
    loadBundleForSpecs({ dictSpec });
    std::string input(77, '\0');
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<char>(i);
    }

    selectGraph(graphId);
    std::vector<char> compressed = compress(input);

    DCtxPtr dctx = createDCtx();
    ASSERT_NE(dctx.get(), nullptr);
    registerDictDecoder(dctx.get(), dictSpec.codecId);
    // no DCtx_refDictLoader() call

    std::string output(input.size(), '\0');
    ZL_Report report = ZL_DCtx_decompress(
            dctx.get(),
            output.data(),
            output.size(),
            compressed.data(),
            compressed.size());
    EXPECT_TRUE(ZL_isError(report));
    EXPECT_EQ(ZL_errorCode(report), ZL_ErrorCode_dictNoRecord);
}

TEST_F(DictIntegrationTest, MissingReferencedBundleFailsWithDictNoRecord)
{
    DictCodecSpec dictSpec = makeDictSpec(50, 0x19);
    ZL_NodeID node         = registerDictNode(dictSpec);
    ZL_GraphID graphId     = buildSingleNodeGraph(node);
    loadBundleForSpecs({ dictSpec });
    std::string input(77, '\0');
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<char>(i);
    }

    selectGraph(graphId);
    std::vector<char> compressed = compress(input);
    corruptBundleID(compressed);

    DCtxPtr dctx = createDCtx();
    ASSERT_NE(dctx.get(), nullptr);
    LoaderPtr loader = createLoader({ dictSpec }, fatBundle_);
    ASSERT_NE(loader.get(), nullptr);
    ZL_DCtx_refDictLoader(
            dctx.get(), ZL_FatBundleDictLoader_getDictLoader(loader.get()));
    registerDictDecoder(dctx.get(), dictSpec.codecId);

    std::string output(input.size(), '\0');
    ZL_Report report = ZL_DCtx_decompress(
            dctx.get(),
            output.data(),
            output.size(),
            compressed.data(),
            compressed.size());
    EXPECT_TRUE(ZL_isError(report));
    EXPECT_EQ(ZL_errorCode(report), ZL_ErrorCode_dictNoRecord);
}

TEST_F(DictIntegrationTest, EncoderCanReadMaterializedDictFromLoadedBundle)
{
    DictCodecSpec dictSpec    = makeDictSpec(120, 0x55);
    uint8_t expectedByte      = dictSpec.bias;
    ZL_RefParam expectedParam = {
        .paramId  = 1,
        .paramRef = &expectedByte,
    };
    ZL_LocalParams localParams = {
        .refParams =
                {
                        .refParams   = &expectedParam,
                        .nbRefParams = 1,
                },
    };

    ZL_NodeID dictNode = registerDictNode(
            dictSpec,
            encodeChecksMaterializedDict,
            &localParams,
            "checks_materialized_dict");
    ZL_NodeID noDictNode = registerTypedNoDictNode("typed_tail");
    ZL_GraphID graphId   = buildPipelineGraph({ noDictNode, dictNode });
    loadBundleForSpecs({ dictSpec });

    selectGraph(graphId);
    std::vector<char> compressed = compress("encoder-materialized-dict");
    EXPECT_FALSE(compressed.empty());
}

TEST_F(DictIntegrationTest, LegacyFormatFailsForDictBackedNode)
{
    DictCodecSpec dictSpec = makeDictSpec(130, 0x08);
    ZL_NodeID dictNode     = registerDictNode(
            dictSpec, passthroughMIEncoder, nullptr, "legacy_format_dict");
    ASSERT_NE(dictNode.nid, kIllegalNodeID.nid);
    loadBundleForSpecs({ dictSpec });
    ASSERT_FALSE(ZL_isError(ZL_Compressor_setParameter(
            compressor_.get(),
            ZL_CParam_formatVersion,
            ZL_MATERIALIZED_DICT_VERSION_MIN)));

    selectGraph(buildSingleNodeGraph(dictNode));

    std::vector<char> compressed;
    ZL_Report report = compressExpectingReport(
            "legacy-version-dict-check",
            compressed,
            ZL_MATERIALIZED_DICT_VERSION_MIN - 1);
    EXPECT_TRUE(ZL_isError(report));
    EXPECT_EQ(ZL_RES_code(report), ZL_ErrorCode_formatVersion_unsupported);
    ASSERT_TRUE(ZL_isError(report));

    std::string errorContext =
            ZL_CCtx_getErrorContextString(cctx_.get(), report);
    EXPECT_NE(
            errorContext.find("does not support dict-backed transforms"),
            std::string::npos);
    EXPECT_NE(errorContext.find("use format version >="), std::string::npos);
}

TEST_F(DictIntegrationTest,
       LegacyFormatSucceedsWhenDictNodeIsNotInSelectedGraph)
{
    DictCodecSpec dictSpec = makeDictSpec(140, 0x09);
    ZL_NodeID dictNode     = registerDictNode(
            dictSpec, passthroughMIEncoder, nullptr, "unused_dict_node");
    ASSERT_NE(dictNode.nid, kIllegalNodeID.nid);
    loadBundleForSpecs({ dictSpec });

    // Select a graph that does NOT include the dict-backed node.
    ZL_NodeID noDictNode = registerTypedNoDictNode("legacy_no_dict");
    selectGraph(buildSingleNodeGraph(noDictNode));

    std::string input = "legacy-unused-dict-node";
    std::vector<char> compressed =
            compress(input, ZL_MATERIALIZED_DICT_VERSION_MIN - 1);
    ASSERT_FALSE(compressed.empty());

    DCtxPtr dctx = createDCtx();
    ASSERT_NE(dctx.get(), nullptr);
    registerTypedNoDictDecoder(dctx.get(), nextCodecId_ - 1);
    EXPECT_EQ(decompress(compressed, input.size(), dctx.get()), input);
}

} // namespace
} // namespace openzl
