// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "VersionTestInterfaceABI.h"

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "openzl/codecs/encoder_registry.h"
#include "openzl/common/debug.h"
#include "openzl/common/errors_internal.h"
#include "openzl/compress/cgraph.h"
#include "openzl/compress/graph_registry.h"
#include "openzl/compress/implicit_conversion.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_reflection.h"
#include "tests/constants.h"
#include "tests/datagen/test_registry/CustomNodes.h"

namespace {
using namespace openzl::tests::datagen::test_registry;

struct CGraphDeleter {
    void operator()(ZL_Compressor* cgraph) const
    {
        ZL_Compressor_free(cgraph);
    }
};

struct CCtxDeleter {
    void operator()(ZL_CCtx* cctx) const
    {
        ZL_CCtx_free(cctx);
    }
};

struct DCtxDeleter {
    void operator()(ZL_DCtx* cctx) const
    {
        ZL_DCtx_free(cctx);
    }
};

using CGraphPtr = std::unique_ptr<ZL_Compressor, CGraphDeleter>;
using CCtxPtr   = std::unique_ptr<ZL_CCtx, CCtxDeleter>;
using DCtxPtr   = std::unique_ptr<ZL_DCtx, DCtxDeleter>;

ZL_NodeID vtiNodeIDToZStrongNodeID(ZL_Compressor* cgraph, int nodeID)
{
    if (nodeID >= 0) {
        return ZL_NodeID{ (ZL_IDType)nodeID };
    } else {
        auto it = getCustomNodes().find(static_cast<TransformID>(-nodeID));
        if (it != getCustomNodes().end()) {
            return it->second.registerEncoder(cgraph);
        } else {
            throw std::runtime_error("Bad NodeID");
        }
    }
}

std::vector<openzl::tests::datagen::FixedWidthData> genCustomTestDataForNode(
        int nodeID)
{
    if (nodeID >= 0) {
        return {};
    } else {
        auto it = getCustomNodes().find(static_cast<TransformID>(-nodeID));
        if (it != getCustomNodes().end()) {
            if (it->second.dataProducer != nullptr) {
                // generate 10 samples
                std::vector<openzl::tests::datagen::FixedWidthData> samples;
                for (size_t i = 0; i < 10; ++i) {
                    samples.emplace_back((*it->second.dataProducer)(
                            "VTI:Node:FixedWidthData"));
                }
                return samples;
            } else {
                return {};
            }
        } else {
            throw std::runtime_error("Bad NodeID");
        }
    }
}

std::vector<ZL_NodeID> getAllNodeIDs(ZL_Compressor* cgraph)
{
    std::vector<int> vtiNodeIDs(VersionTestInterface_getNbNodeIDs());
    VersionTestInterface_getAllNodeIDs(
            vtiNodeIDs.data(), nullptr, vtiNodeIDs.size());
    std::vector<ZL_NodeID> nodeIDs;
    nodeIDs.reserve(vtiNodeIDs.size());
    for (auto const vtiNodeID : vtiNodeIDs) {
        nodeIDs.push_back(vtiNodeIDToZStrongNodeID(cgraph, vtiNodeID));
    }
    return nodeIDs;
}

ZL_GraphID vtiGraphIDToZStrongGraphID(ZL_Compressor* cgraph, int graphID)
{
    if (graphID >= 0) {
        return ZL_GraphID{ (ZL_IDType)graphID };
    } else {
        auto it = getCustomGraphs().find(static_cast<TransformID>(-graphID));
        if (it != getCustomGraphs().end()) {
            return it->second.registerEncoder(cgraph);
        } else {
            throw std::runtime_error("Bad GraphID");
        }
    }
}

std::vector<openzl::tests::datagen::FixedWidthData> genCustomTestDataForGraph(
        int graphID)
{
    if (graphID >= 0) {
        return {};
    } else {
        auto it = getCustomGraphs().find(static_cast<TransformID>(-graphID));
        if (it != getCustomGraphs().end()) {
            if (it->second.dataProducer != nullptr) {
                // generate 10 samples
                std::vector<openzl::tests::datagen::FixedWidthData> samples;
                for (size_t i = 0; i < 10; ++i) {
                    samples.emplace_back((*it->second.dataProducer)(
                            "VTI:Graph:FixedWidthData"));
                }
                return samples;
            }
        } else {
            throw std::runtime_error("Bad GraphID");
        }
    }
    return {};
}

} // namespace

extern "C" unsigned VersionTestInterface_getZStrongVersion(int versionType)
{
    using VT = openzl::detail::VersionType;
    switch (static_cast<VT>(versionType)) {
        case VT::MAJOR:
            return ZL_LIBRARY_VERSION_MAJOR;
        case VT::MINOR:
            return ZL_LIBRARY_VERSION_MINOR;
        case VT::PATCH:
            return ZL_LIBRARY_VERSION_PATCH;
        case VT::MIN_FORMAT:
            return ZL_MIN_FORMAT_VERSION;
        case VT::MAX_FORMAT:
            return ZL_MAX_FORMAT_VERSION;
        default:
            return 0;
    }
}

#define VTI_FORWARD_IF_ERROR(ret) \
    do {                          \
        if (ZL_isError(ret)) {    \
            return size_t(-1);    \
        }                         \
    } while (0)

#define VTI_RETURN_REPORT(fn)       \
    do {                            \
        ZL_Report const ret = (fn); \
        if (ZL_isError(ret)) {      \
            return size_t(-1);      \
        }                           \
        return ZL_validResult(ret); \
    } while (0)

extern "C" size_t VersionTestInterface_getNbNodeIDs()
{
    return ER_getNbStandardNodes() + getCustomNodes().size();
}

extern "C" void VersionTestInterface_getAllNodeIDs(
        int* nodeIDs,
        int* transformIDs,
        size_t nodesCapacity)
{
    auto cgraph = CGraphPtr(ZL_Compressor_create());
    std::vector<ZL_NodeID> nodes(nodesCapacity);
    size_t nbNodes = ER_getNbStandardNodes();
    ER_getAllStandardNodeIDs(nodes.data(), nbNodes);
    for (size_t i = 0; i < nbNodes; ++i) {
        nodeIDs[i] = (int)nodes[i].nid;
        ZL_REQUIRE_GE(nodeIDs[i], 0);
    }
    for (auto const& [transformID, _] : getCustomNodes()) {
        nodeIDs[nbNodes++] = -static_cast<int>(transformID);
    }
    ZL_ASSERT_EQ(nbNodes, VersionTestInterface_getNbNodeIDs());
    if (transformIDs != nullptr) {
        for (size_t i = 0; i < nbNodes; ++i) {
            auto const zstrongNodeID =
                    vtiNodeIDToZStrongNodeID(cgraph.get(), nodeIDs[i]);
            transformIDs[i] =
                    ZL_Compressor_Node_getCodecID(cgraph.get(), zstrongNodeID);
        }
    }
}

extern "C" size_t VersionTestInterface_getNbGraphIDs()
{
    return GR_getNbStandardGraphs() + getCustomGraphs().size();
}

extern "C" void VersionTestInterface_getAllGraphIDs(
        int* graphs,
        size_t graphsCapacity)
{
    auto cgraph = CGraphPtr(ZL_Compressor_create());
    std::vector<ZL_GraphID> graphIDs(graphsCapacity);
    GR_getAllStandardGraphIDs(graphIDs.data(), graphsCapacity);
    size_t nbGraphs = GR_getNbStandardGraphs();
    for (size_t i = 0; i < nbGraphs; ++i) {
        graphs[i] = (int)graphIDs[i].gid;
        ZL_REQUIRE_GE(graphs[i], 0);
    }
    for (auto const& [transformID, _] : getCustomGraphs()) {
        graphs[nbGraphs++] = -static_cast<int>(transformID);
    }
    ZL_ASSERT_EQ(nbGraphs, VersionTestInterface_getNbGraphIDs());
}

extern "C" size_t VersionTestInterface_compressBound(size_t srcSize)
{
    return 2 * ZL_compressBound(srcSize);
}

extern "C" bool VersionTestInterface_isError(size_t ret)
{
    return ret == size_t(-1);
}

static ZL_SetStringLensInstructions justOneField(
        ZL_SetStringLensState* state,
        const ZL_Input* in)
{
    assert(state != NULL);
    assert(in != NULL);
    assert(ZL_Input_type(in) == ZL_Type_serial);
    size_t const inSize          = ZL_Input_contentSize(in);
    uint32_t* const oneFieldSize = (uint32_t*)ZL_SetStringLensState_malloc(
            state, sizeof(*oneFieldSize));
    ZL_REQUIRE_NN(oneFieldSize);
    assert(inSize < (size_t)(uint32_t)(-1));
    oneFieldSize[0]                         = (uint32_t)inSize;
    ZL_SetStringLensInstructions const sfsi = { oneFieldSize, 1 };
    return sfsi;
}

static ZL_GraphID
convertFromSerial(ZL_Compressor* cgraph, ZL_GraphID graphID, unsigned eltWidth)
{
    ZL_ASSERT(ZL_GraphID_isValid(graphID));
    ZL_Type type = ZL_Compressor_Graph_getInput0Mask(cgraph, graphID);
    // Deal with situations where Input Node supports multiple stream types.
    if (type & ZL_Type_serial)
        type = ZL_Type_serial;
    if (type & ZL_Type_struct)
        type = ZL_Type_struct;
    if (type & ZL_Type_numeric)
        type = ZL_Type_numeric;
    if (type & ZL_Type_string)
        type = ZL_Type_string;
    switch (type) {
        case ZL_Type_serial:
            (void)eltWidth;
            return graphID;
        case ZL_Type_struct: {
            ZL_IntParam param = {
                .paramId    = ZL_trlip_tokenSize,
                .paramValue = (int)eltWidth,
            };
            ZL_LocalParams const params = {
                .intParams = { .intParams = &param, .nbIntParams = 1 },
            };
            const ZL_ParameterizedNodeDesc pndesc = {
                .node        = ZL_NODE_CONVERT_SERIAL_TO_TOKENX,
                .localParams = &params,
            };
            ZL_NodeID const convert =
                    ZL_Compressor_registerParameterizedNode(cgraph, &pndesc);
            return ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, convert, graphID);
        }
        case ZL_Type_numeric: {
            auto const convert = [eltWidth]() {
                switch (eltWidth) {
                    default:
                        return ZL_NodeID{ 0 };
                    case 1:
                        return ZL_NODE_INTERPRET_AS_LE8;
                    case 2:
                        return ZL_NODE_INTERPRET_AS_LE16;
                    case 4:
                        return ZL_NODE_INTERPRET_AS_LE32;
                    case 8:
                        return ZL_NODE_INTERPRET_AS_LE64;
                }
            }();
            if (convert.nid == 0)
                return ZL_GraphID{ 0 };
            return ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, convert, graphID);
        }
        case ZL_Type_string: {
            ZL_NodeID const convert =
                    ZL_Compressor_registerConvertSerialToStringNode(
                            cgraph, justOneField, NULL);
            return ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, convert, graphID);
        }
        default:
            ZL_REQUIRE_FAIL("Bad type: %d", type);
    }
}

static size_t compressWithGraphID(
        ZL_CCtx* cctx,
        ZL_Compressor* cgraph,
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize,
        unsigned eltWidth,
        unsigned formatVersion,
        ZL_GraphID graphID)
{
    // TODO(terrelln): Once non-serialized inputs are supported remove the
    // conversion transforms.
    graphID = convertFromSerial(cgraph, graphID, eltWidth);
    if (graphID.gid == 0)
        return size_t(-1);
    VTI_FORWARD_IF_ERROR(ZL_CCtx_setParameter(
            cctx, ZL_CParam_formatVersion, (int)formatVersion));
    // disable automatic store for small data to preserve wanted behavior
    VTI_FORWARD_IF_ERROR(
            ZL_CCtx_setParameter(cctx, ZL_CParam_minStreamSize, -1));
    // disable anti-inflation guard to preserve wanted behavior
    VTI_FORWARD_IF_ERROR(ZL_CCtx_setParameter(
            cctx, ZL_CParam_storeOnExpansion, ZL_TernaryParam_disable));
    VTI_FORWARD_IF_ERROR(ZL_Compressor_selectStartingGraphID(cgraph, graphID));
    VTI_FORWARD_IF_ERROR(ZL_CCtx_refCompressor(cctx, cgraph));
    VTI_RETURN_REPORT(ZL_CCtx_compress(cctx, dst, dstCapacity, src, srcSize));
}

extern "C" size_t VersionTestInterface_compressWithNodeID(
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize,
        unsigned eltWidth,
        unsigned formatVersion,
        int node)
{
    auto cctx   = CCtxPtr(ZL_CCtx_create());
    auto cgraph = CGraphPtr(ZL_Compressor_create());
    if (cctx == nullptr || cgraph == nullptr)
        return size_t(-1);
    auto const nodeID = vtiNodeIDToZStrongNodeID(cgraph.get(), node);
    size_t const nbOutcomes =
            ZL_Compressor_Node_getNumOutcomes(cgraph.get(), nodeID);
    std::vector<ZL_GraphID> outputs(nbOutcomes, ZL_GRAPH_STORE);
    ZL_GraphID const graphID = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph.get(), nodeID, outputs.data(), outputs.size());
    return compressWithGraphID(
            cctx.get(),
            cgraph.get(),
            dst,
            dstCapacity,
            src,
            srcSize,
            eltWidth,
            formatVersion,
            graphID);
}

extern "C" size_t VersionTestInterface_compressWithGraphID(
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize,
        unsigned eltWidth,
        unsigned formatVersion,
        int graph)
{
    auto cctx   = CCtxPtr(ZL_CCtx_create());
    auto cgraph = CGraphPtr(ZL_Compressor_create());
    if (cctx == nullptr || cgraph == nullptr)
        return size_t(-1);
    auto const graphID = vtiGraphIDToZStrongGraphID(cgraph.get(), graph);
    return compressWithGraphID(
            cctx.get(),
            cgraph.get(),
            dst,
            dstCapacity,
            src,
            srcSize,
            eltWidth,
            formatVersion,
            graphID);
}

using NodeDef         = std::pair<ZL_NodeID, std::vector<unsigned>>;
using AllowedNodesMap = std::unordered_map<unsigned, std::vector<NodeDef>>;

static ZL_GraphID store(ZL_Compressor* cgraph, ZL_Type inType)
{
    if (inType == ZL_Type_string) {
        std::array<ZL_GraphID, 2> store = { ZL_GRAPH_STORE, ZL_GRAPH_STORE };
        return ZL_Compressor_registerStaticGraph_fromNode(
                cgraph,
                ZL_NODE_SEPARATE_STRING_COMPONENTS,
                store.data(),
                store.size());
    }
    return ZL_GRAPH_STORE;
}

static ZL_GraphID buildGraph(
        ZL_Compressor* cgraph,
        std::string_view& entropy,
        size_t& nodesInGraph,
        std::vector<ZL_NodeID> const& nodes,
        ZL_Type inType,
        size_t nbVOsAllowed,
        size_t maxDepth,
        unsigned minVersion = ZL_MIN_FORMAT_VERSION,
        unsigned maxVersion = ZL_MAX_FORMAT_VERSION)
{
    ++nodesInGraph;
    if (nodesInGraph > openzl::tests::kMaxNodesInGraph || entropy.size() < 2) {
        return store(cgraph, inType);
    }
    unsigned const stopByte    = (unsigned)entropy[0];
    unsigned const entropyByte = (unsigned)entropy[1];
    entropy                    = entropy.substr(2);

    if (maxDepth == 0) {
        return store(cgraph, inType);
    }
    --maxDepth;

    auto const stop = (stopByte & 7) < 3;
    if (stop) {
        return store(cgraph, inType);
    }

    ZL_REQUIRE_LT(nodes.size(), 256);

    // Find the next compatible node
    size_t nodeIdx = entropyByte % nodes.size();
    for (;; nodeIdx = (nodeIdx + 1) % nodes.size()) {
        ZL_Type const nodeInType =
                ZL_Compressor_Node_getInput0Type(cgraph, nodes[nodeIdx]);
        size_t const nbVOs = ZL_Compressor_Node_getNumVariableOutcomes(
                cgraph, nodes[nodeIdx]);
        if (nbVOsAllowed == 0 && nbVOs > 0) {
            continue;
        }
        if (ZL_Compressor_Node_getMinVersion(cgraph, nodes[nodeIdx])
            > maxVersion) {
            continue;
        }
        if (ZL_Compressor_Node_getMaxVersion(cgraph, nodes[nodeIdx])
            < minVersion) {
            continue;
        }
        if (ICONV_isCompatible(inType, nodeInType)) {
            break;
        }
    }
    ZL_NodeID const node = nodes[nodeIdx];
    size_t const nbVOs =
            ZL_Compressor_Node_getNumVariableOutcomes(cgraph, node);
    if (nbVOs > 0) {
        ZL_ASSERT_GT(nbVOsAllowed, 0);
        --nbVOsAllowed;
    }
    maxVersion = std::min(
            maxVersion,
            ZL_Compressor_Node_getMaxVersion(cgraph, nodes[nodeIdx]));
    minVersion = std::max(
            minVersion,
            ZL_Compressor_Node_getMinVersion(cgraph, nodes[nodeIdx]));
    ZL_ASSERT_GE(minVersion, ZL_MIN_FORMAT_VERSION);
    ZL_ASSERT_LE(maxVersion, ZL_MAX_FORMAT_VERSION);
    ZL_ASSERT_GE(maxVersion, minVersion);

    std::vector<ZL_GraphID> outGraphs;
    outGraphs.resize(ZL_Compressor_Node_getNumOutcomes(cgraph, node));
    for (size_t i = 0; i < outGraphs.size(); ++i) {
        auto const outType = ZL_Compressor_Node_getOutputType(cgraph, node, i);
        outGraphs[i]       = buildGraph(
                cgraph,
                entropy,
                nodesInGraph,
                nodes,
                outType,
                nbVOsAllowed,
                maxDepth,
                minVersion,
                maxVersion);
    }

    ZL_GraphID const graph = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, node, outGraphs.data(), outGraphs.size());

    fprintf(stderr,
            "(nid = %u (name = %s), gid = %u (name = %s), tid = %d) -> {",
            node.nid,
            ZL_Compressor_Node_getName(cgraph, node),
            graph.gid,
            ZL_Compressor_Graph_getName(cgraph, graph),
            (int)ZL_Compressor_Node_getCodecID(cgraph, node));
    for (auto const g : outGraphs)
        fprintf(stderr, " %u", g.gid);
    fprintf(stderr, " }\n");

    return graph;
}

static ZL_GraphID createGraphFromEntropy(
        ZL_Compressor* cgraph,
        unsigned eltWidth,
        unsigned formatVersion,
        void const* entropyBuffer,
        size_t entropySize)
{
    (void)eltWidth;
    (void)formatVersion;
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_permissiveCompression, ZL_TernaryParam_enable));
    auto nodes = getAllNodeIDs(cgraph);
    std::string_view entropy{ (char const*)entropyBuffer, entropySize };
    size_t nodesInGraph    = 0;
    size_t const kMaxNbVOs = 2;
    size_t const kMaxDepth = 4;
    return buildGraph(
            cgraph,
            entropy,
            nodesInGraph,
            nodes,
            ZL_Type_serial,
            kMaxNbVOs,
            kMaxDepth);
}

extern "C" size_t VersionTestInterface_compressWithGraphFromEntropy(
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize,
        unsigned eltWidth,
        unsigned formatVersion,
        void const* entropy,
        size_t entropySize)
{
    // TODO(terrelln): Actually use the entropy
    (void)entropy;
    (void)entropySize;
    auto cctx   = CCtxPtr(ZL_CCtx_create());
    auto cgraph = CGraphPtr(ZL_Compressor_create());
    if (cctx == nullptr || cgraph == nullptr)
        return size_t(-1);
    ZL_GraphID const graphID = createGraphFromEntropy(
            cgraph.get(), eltWidth, formatVersion, entropy, entropySize);
    return compressWithGraphID(
            cctx.get(),
            cgraph.get(),
            dst,
            dstCapacity,
            src,
            srcSize,
            eltWidth,
            formatVersion,
            graphID);
}

extern "C" size_t VersionTestInterface_decompressedSize(
        void const* src,
        size_t srcSize)
{
    VTI_RETURN_REPORT(ZL_getDecompressedSize(src, srcSize));
}

extern "C" size_t VersionTestInterface_decompress(
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize)
{
    DCtxPtr dctx(ZL_DCtx_create());
    for (auto const& [_, customNode] : getCustomNodes()) {
        if (customNode.registerDecoder.has_value()) {
            customNode.registerDecoder.value()(dctx.get());
        }
    }
    const auto report =
            ZL_DCtx_decompress(dctx.get(), dst, dstCapacity, src, srcSize);
    if (ZL_isError(report)) {
        ZL_E_print(ZL_RES_error(report));
    }
    VTI_RETURN_REPORT(report);
}

static size_t fillCustomTestData(
        char** bufferPtr,
        size_t** eltWidthsPtr,
        size_t** sizesPtr,
        const std::vector<openzl::tests::datagen::FixedWidthData>& testData)
{
    if (testData.empty()) {
        *bufferPtr    = nullptr;
        *eltWidthsPtr = nullptr;
        *sizesPtr     = nullptr;
        return 0;
    }
    auto eltWidths   = std::make_unique<size_t[]>(testData.size());
    auto sizes       = std::make_unique<size_t[]>(testData.size());
    size_t totalSize = 0;
    for (size_t i = 0; i < testData.size(); ++i) {
        auto const& [data, eltWidth] = testData.at(i);
        ZL_ASSERT_NE(eltWidth, 0);
        ZL_ASSERT_EQ(data.size() % eltWidth, 0);
        eltWidths[i] = eltWidth;
        sizes[i]     = data.size();
        totalSize += sizes[i];
    }
    auto buffer   = std::make_unique<char[]>(totalSize);
    size_t offset = 0;
    for (auto const& [data, eltWidth] : testData) {
        if (data.size() > 0) {
            memcpy(buffer.get() + offset, data.data(), data.size());
            offset += data.size();
        }
    }
    *bufferPtr    = buffer.release();
    *eltWidthsPtr = eltWidths.release();
    *sizesPtr     = sizes.release();

    return testData.size();
}

extern "C" size_t VersionTestInterface_customNodeData(
        char** bufferPtr,
        size_t** eltWidthsPtr,
        size_t** sizesPtr,
        int node)
{
    const auto testData = genCustomTestDataForNode(node);
    return fillCustomTestData(bufferPtr, eltWidthsPtr, sizesPtr, testData);
}

extern "C" size_t VersionTestInterface_customGraphData(
        char** bufferPtr,
        size_t** eltWidthsPtr,
        size_t** sizesPtr,
        int graph)
{
    const auto testData = genCustomTestDataForGraph(graph);
    return fillCustomTestData(bufferPtr, eltWidthsPtr, sizesPtr, testData);
}
