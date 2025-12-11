// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <functional>

#include "custom_transforms/thrift/kernels/decode_thrift_binding.h"
#include "custom_transforms/thrift/kernels/encode_thrift_binding.h"
#include "custom_transforms/thrift/kernels/tests/thrift_kernel_test_utils.h"
#include "custom_transforms/thrift/tests/util.h"
#include "custom_transforms/thrift/thrift_parsers.h"
#include "custom_transforms/tulip_v2/encode_tulip_v2.h"
#include "custom_transforms/tulip_v2/tests/tulip_v2_data_utils.h"
#include "openzl/codecs/splitByStruct/encode_splitByStruct_binding.h" // ZL_createNode_splitByStruct
#include "openzl/codecs/tokenize/encode_tokenize_binding.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "tests/datagen/random_producer/PRNGWrapper.h"
#include "tests/datagen/structures/FSENCountProducer.h"
#include "tests/datagen/test_registry/CustomNodes.h"

using apache::thrift::BinarySerializer;
using apache::thrift::CompactSerializer;

namespace openzl::tests::datagen::test_registry {
namespace {

ZL_NodeID createSplitByStructNode(ZL_Compressor* cgraph)
{
    std::array<size_t, 3> fieldSizes = { 1, 1, 2 };
    return ZL_createNode_splitByStruct(
            cgraph, fieldSizes.data(), fieldSizes.size());
}

template <ZL_Type type>
ZL_NodeID createSplitNNode(ZL_Compressor* cgraph)
{
    auto parser = [](ZL_SplitState* state, ZL_Input const* in) {
        ZL_SplitInstructions instructions = { nullptr, 0 };
        size_t constexpr kNumSegments     = 10;
        auto* segmentSizes                = (size_t*)ZL_SplitState_malloc(
                state, kNumSegments * sizeof(size_t));
        if (segmentSizes == nullptr) {
            return instructions;
        };
        instructions = { segmentSizes, kNumSegments };

        size_t remaining = ZL_Input_numElts(in);
        for (size_t i = 0; i < kNumSegments; ++i) {
            segmentSizes[i] = std::min(
                    remaining, std::max(remaining / kNumSegments, size_t(100)));
            remaining -= segmentSizes[i];
        }
        if (remaining > 0)
            segmentSizes[kNumSegments - 1] = 0;

        return instructions;
    };
    return ZL_Compressor_registerSplitNode_withParser(
            cgraph, type, parser, nullptr);
}

ZL_NodeID createCustomTokenizeNode(ZL_Compressor* cg)
{
    auto tokenize = [](ZL_CustomTokenizeState* ctx,
                       ZL_Input const* input) -> ZL_Report {
        if (ZL_Input_eltWidth(input) != 4) {
            ZL_RET_R_ERR(node_invalid_input);
        }

        std::unordered_map<uint32_t, uint32_t> valueToIndex;
        uint32_t const* src  = (uint32_t const*)ZL_Input_ptr(input);
        size_t const srcSize = ZL_Input_numElts(input);

        uint32_t* indices =
                (uint32_t*)ZL_CustomTokenizeState_createIndexOutput(ctx, 4);
        if (indices == nullptr) {
            ZL_RET_R_ERR(allocation);
        }

        for (size_t i = 0; i < srcSize; ++i) {
            auto [it, _] = valueToIndex.emplace(src[i], valueToIndex.size());
            indices[i]   = it->second;
        }

        uint32_t* alphabet =
                (uint32_t*)ZL_CustomTokenizeState_createAlphabetOutput(
                        ctx, valueToIndex.size());
        if (alphabet == nullptr) {
            ZL_RET_R_ERR(allocation);
        }

        for (auto [value, index] : valueToIndex) {
            assert(index < valueToIndex.size());
            alphabet[index] = value;
        }

        return ZL_returnSuccess();
    };
    return ZS2_createNode_customTokenize(cg, ZL_Type_struct, tokenize, nullptr);
}

ZL_NodeID createDispatchNByTagNode(ZL_Compressor* cgraph)
{
    auto parser = [](ZL_DispatchState* state, ZL_Input const* in) {
        size_t constexpr kNumSegments        = 10;
        unsigned constexpr kNumTags          = 3;
        ZL_DispatchInstructions instructions = { nullptr, nullptr, 0, 0 };
        auto* segmentSizes                   = (size_t*)ZL_DispatchState_malloc(
                state, kNumSegments * sizeof(size_t));
        auto* tags = (unsigned*)ZL_DispatchState_malloc(
                state, kNumSegments * sizeof(unsigned));
        if (segmentSizes == nullptr) {
            return instructions;
        };
        instructions = { segmentSizes, tags, kNumSegments, kNumTags };

        static_assert(kNumSegments >= 10, "");
        tags[0] = 0;
        tags[1] = 0;
        tags[2] = 2;
        tags[3] = 1;
        tags[4] = 2;
        tags[5] = 0;
        tags[6] = 1;
        tags[7] = 1;
        tags[8] = 2;
        tags[9] = 2;

        size_t remaining = ZL_Input_numElts(in);
        for (size_t i = 0; i < kNumSegments; ++i) {
            segmentSizes[i] = std::min(
                    remaining, std::max(remaining / kNumSegments, size_t(100)));
            remaining -= segmentSizes[i];
            if (i >= 10) {
                tags[i] = unsigned(i) % kNumTags;
            }
        }
        if (remaining > 0)
            segmentSizes[kNumSegments - 1] += remaining;

        return instructions;
    };
    return ZL_Compressor_registerDispatchNode(cgraph, parser, nullptr);
}

ZL_IDType zstrongTransformID(TransformID id)
{
    return static_cast<ZL_IDType>(-static_cast<int>(id));
}

void registerCustomTransform(
        std::unordered_map<TransformID, CustomNode>& customNodes,
        TransformID transformID,
        ZL_NodeID (*registerCTransform)(ZL_Compressor*, ZL_IDType),
        ZL_Report (*registerDTransform)(ZL_DCtx*, ZL_IDType),
        std::unique_ptr<tests::datagen::FixedWidthDataProducer> dataProd = {})
{
    auto const id   = zstrongTransformID(transformID);
    CustomNode node = { [=](ZL_Compressor* cgraph) {
                           return registerCTransform(cgraph, id);
                       },
                        [=](ZL_DCtx* dctx) {
                            ZL_REQUIRE_SUCCESS(registerDTransform(dctx, id));
                        },
                        std::move(dataProd) };
    auto [it, inserted] = customNodes.emplace(transformID, std::move(node));
    ZL_REQUIRE(inserted);
}

void registerCustomParser(
        std::unordered_map<TransformID, CustomNode>& customNodes,
        TransformID transformID,
        std::function<ZL_NodeID(ZL_Compressor*)> registerNode,
        std::unique_ptr<tests::datagen::FixedWidthDataProducer> dataProd = {})
{
    auto const [it, inserted] = customNodes.emplace(
            transformID,
            CustomNode{ std::move(registerNode),
                        std::nullopt,
                        std::move(dataProd) });
    ZL_REQUIRE(inserted);
}

void registerCustomData(
        std::unordered_map<TransformID, CustomNode>& customNodes,
        TransformID transformID,
        ZL_NodeID node,
        std::unique_ptr<tests::datagen::FixedWidthDataProducer> dataProd = {})
{
    auto const [it, inserted] = customNodes.emplace(
            transformID,
            CustomNode{ [node](ZL_Compressor*) { return node; },
                        std::nullopt,
                        std::move(dataProd) });
    ZL_REQUIRE(inserted);
}

// Register additional instances of the Thrift custom node which all share the
// same decoder, but use configs with different min format versions.
void registerAdditionalThriftNodes(
        std::unordered_map<TransformID, CustomNode>& customNodes,
        std::shared_ptr<RandWrapper> rw,
        TransformID tidCompact,
        TransformID tidBinary,
        int minFormatVersion)
{
    const ZL_IDType commonIdCompact =
            zstrongTransformID(TransformID::ThriftCompact);
    const ZL_IDType commonIdBinary =
            zstrongTransformID(TransformID::ThriftBinary);

    registerCustomParser(
            customNodes,
            tidCompact,
            [=](ZL_Compressor* cgraph) {
                ZL_NodeID node = ::zstrong::thrift::registerCompactTransform(
                        cgraph, commonIdCompact);
                return ::zstrong::thrift::cloneThriftNodeWithLocalParams(
                        cgraph,
                        node,
                        ::openzl::thrift::tests::buildValidEncoderConfig(
                                minFormatVersion));
            },

            std::make_unique<
                    ::openzl::thrift::tests::ConfigurableThriftProducer<
                            CompactSerializer>>(rw));

    registerCustomParser(
            customNodes,
            tidBinary,
            [=](ZL_Compressor* cgraph) {
                ZL_NodeID node = ::zstrong::thrift::registerBinaryTransform(
                        cgraph, commonIdBinary);
                return ::zstrong::thrift::cloneThriftNodeWithLocalParams(
                        cgraph,
                        node,
                        ::openzl::thrift::tests::buildValidEncoderConfig(
                                minFormatVersion));
            },
            std::make_unique<
                    ::openzl::thrift::tests::ConfigurableThriftProducer<
                            BinarySerializer>>(rw));
}

std::unordered_map<TransformID, CustomNode> makeCustomNodes()
{
    // Register custom nodes that are packaged with Zstrong here.
    // These could be packaged custom transforms, or nodes which
    // require some additional configuration to work.
    // NOTE: There must be at most 1 custom transform per node.
    // The custom transform must use zstrongTransformID(id) as its
    // custom transform ID, if any.

    std::unordered_map<TransformID, CustomNode> customNodes;
    auto rw = std::make_shared<PRNGWrapper>(std::make_shared<std::mt19937>());

#define ZS2_REGISTER_THRIFT_KERNEL(kernel)                                     \
    registerCustomTransform(                                                   \
            customNodes,                                                       \
            TransformID::ThriftKernel##kernel,                                 \
            ZS2_ThriftKernel_registerCTransform##kernel,                       \
            ZS2_ThriftKernel_registerDTransform##kernel,                       \
            std::make_unique<::openzl::thrift::tests::ThriftProducer<kernel>>( \
                    rw))

    using MapI32Float      = std::map<int32_t, float>;
    using MapI32ArrayFloat = std::map<int32_t, std::vector<float>>;
    using MapI32ArrayI64   = std::map<int32_t, std::vector<int64_t>>;
    using MapI32ArrayArrayI64 =
            std::map<int32_t, std::vector<std::vector<int64_t>>>;
    using MapI32MapI64Float = std::map<int32_t, std::map<int64_t, float>>;
    using ArrayI64          = std::vector<int64_t>;
    using ArrayI32          = std::vector<int32_t>;
    using ArrayFloat        = std::vector<float>;

    ZS2_REGISTER_THRIFT_KERNEL(MapI32Float);
    ZS2_REGISTER_THRIFT_KERNEL(MapI32ArrayFloat);
    ZS2_REGISTER_THRIFT_KERNEL(MapI32ArrayI64);
    ZS2_REGISTER_THRIFT_KERNEL(MapI32ArrayArrayI64);
    ZS2_REGISTER_THRIFT_KERNEL(MapI32MapI64Float);
    ZS2_REGISTER_THRIFT_KERNEL(ArrayI64);
    ZS2_REGISTER_THRIFT_KERNEL(ArrayI32);
    ZS2_REGISTER_THRIFT_KERNEL(ArrayFloat);

#undef ZS2_REGISTER_THRIFT_KERNEL

    registerCustomParser(
            customNodes,
            TransformID::TulipV2,
            ::zstrong::tulip_v2::createTulipV2Node,
            std::make_unique<::openzl::tulip_v2::tests::TulipV2Producer>(rw));

    registerCustomParser(
            customNodes, TransformID::SplitByStruct, createSplitByStructNode);

    registerCustomParser(
            customNodes, TransformID::SplitN, createSplitNNode<ZL_Type_serial>);
    registerCustomParser(
            customNodes,
            TransformID::SplitNStruct,
            createSplitNNode<ZL_Type_struct>);
    registerCustomParser(
            customNodes,
            TransformID::SplitNNumeric,
            createSplitNNode<ZL_Type_numeric>);

    registerCustomParser(
            customNodes, TransformID::DispatchNByTag, createDispatchNByTagNode);

    registerCustomParser(
            customNodes, TransformID::Bitunpack7, [](ZL_Compressor* cgraph) {
                return ZL_Compressor_registerBitunpackNode(cgraph, 7);
            });

    registerCustomParser(
            customNodes, TransformID::Bitunpack64, [](ZL_Compressor* cgraph) {
                return ZL_Compressor_registerBitunpackNode(cgraph, 64);
            });

    registerCustomTransform(
            customNodes,
            TransformID::ThriftCompact,
            [](ZL_Compressor* cgraph, ZL_IDType id) {
                ZL_NodeID node =
                        ::zstrong::thrift::registerCompactTransform(cgraph, id);
                return ::zstrong::thrift::cloneThriftNodeWithLocalParams(
                        cgraph,
                        node,
                        ::openzl::thrift::tests::buildValidEncoderConfig(
                                ::zstrong::thrift::kMinFormatVersionEncode));
            },
            ::zstrong::thrift::registerCompactTransform,
            std::make_unique<
                    ::openzl::thrift::tests::ConfigurableThriftProducer<
                            CompactSerializer>>(rw));

    registerCustomTransform(
            customNodes,
            TransformID::ThriftBinary,
            [](ZL_Compressor* cgraph, ZL_IDType id) {
                ZL_NodeID node =
                        ::zstrong::thrift::registerBinaryTransform(cgraph, id);
                return ::zstrong::thrift::cloneThriftNodeWithLocalParams(
                        cgraph,
                        node,
                        ::openzl::thrift::tests::buildValidEncoderConfig(
                                ::zstrong::thrift::kMinFormatVersionEncode));
            },
            ::zstrong::thrift::registerBinaryTransform,
            std::make_unique<
                    ::openzl::thrift::tests::ConfigurableThriftProducer<
                            BinarySerializer>>(rw));

    // We want to test the Thrift custom node with the highest format version
    // shared between dev and release. A higher format version means more
    // coverage for more features. The highest shared version is guaranteed to
    // be one of the two below.
    registerAdditionalThriftNodes(
            customNodes,
            rw,
            TransformID::ThriftCompactPrevFormatVersion,
            TransformID::ThriftBinaryPrevFormatVersion,
            ZL_MAX_FORMAT_VERSION - 1);
    registerAdditionalThriftNodes(
            customNodes,
            rw,
            TransformID::ThriftCompactMaxFormatVersion,
            TransformID::ThriftBinaryMaxFormatVersion,
            ZL_MAX_FORMAT_VERSION);

    registerCustomParser(
            customNodes, TransformID::CustomTokenize, createCustomTokenizeNode);

    registerCustomData(
            customNodes,
            TransformID::FSENCount,
            { ZL_PrivateStandardNodeID_fse_ncount },
            std::make_unique<tests::datagen::FSENCountProducer>(rw));

    for (auto const& [key, _] : customNodes) {
        if (static_cast<int>(key) <= 0) {
            throw std::runtime_error(
                    "All custom nodes must have positive keys!");
        }
    }

    return customNodes;
}

void registerCustomGraph(
        std::unordered_map<TransformID, CustomGraph>& customGraphs,
        TransformID transformID,
        std::function<ZL_GraphID(ZL_Compressor*)> registerGraph,
        std::unique_ptr<tests::datagen::FixedWidthDataProducer> dataProd = {})
{
    auto const [it, inserted] = customGraphs.emplace(
            transformID,
            CustomGraph{ std::move(registerGraph),
                         std::nullopt,
                         std::move(dataProd) });
    ZL_REQUIRE(inserted);
}

std::unordered_map<TransformID, CustomGraph> makeCustomGraphs()
{
    std::unordered_map<TransformID, CustomGraph> customGraphs;

    registerCustomGraph(
            customGraphs,
            TransformID::TransposeSplit,
            [](ZL_Compressor* cgraph) {
                return ZL_Compressor_registerTransposeSplitGraph(
                        cgraph, ZL_GRAPH_STORE);
            });

    registerCustomGraph(
            customGraphs,
            TransformID::FieldLz,
            ZL_Compressor_registerFieldLZGraph);

    return customGraphs;
}

} // namespace

std::unordered_map<TransformID, CustomNode> const& getCustomNodes()
{
    static auto* customNodesPtr =
            new std::unordered_map<TransformID, CustomNode>(makeCustomNodes());
    return *customNodesPtr;
}

std::unordered_map<TransformID, CustomGraph> const& getCustomGraphs()
{
    static auto* customGraphsPtr =
            new std::unordered_map<TransformID, CustomGraph>(
                    makeCustomGraphs());
    return *customGraphsPtr;
}

} // namespace openzl::tests::datagen::test_registry
