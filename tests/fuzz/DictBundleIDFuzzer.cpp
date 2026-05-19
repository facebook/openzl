// (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "openzl/codecs/encoder_registry.h"
#include "openzl/common/errors_internal.h"
#include "openzl/compress/graph_registry.h"
#include "openzl/compress/implicit_conversion.h"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/dict/bundle.h"
#include "openzl/dict/dict.h"
#include "openzl/dict/dict_constants.h"
#include "openzl/zl_common_types.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_dictloader.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_materializer.h"
#include "openzl/zl_reflection.h"
#include "openzl/zl_unique_id.h"
#include "openzl/zl_version.h"
#include "tests/constants.h"
#include "tests/datagen/DataGen.h"
#include "tests/fuzz_utils.h"
#include "tests/registry/OpenZLComponents.h"

namespace openzl::tests {
namespace {

// ---------------------------------------------------------------------------
// Graph-building helpers (adapted from fuzz_graph.cpp)
// ---------------------------------------------------------------------------

std::vector<ZL_NodeID> getAllNodes(uint32_t formatVersion)
{
    std::vector<ZL_NodeID> nodes(ER_getNbStandardNodes());
    ER_getAllStandardNodeIDs(nodes.data(), nodes.size());

    auto cgraph = ZL_Compressor_create();
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, formatVersion));
    auto end = std::remove_if(
            nodes.begin(), nodes.end(), [cgraph](ZL_NodeID node) {
                if (ZL_Compressor_Node_getNumInputs(cgraph, node) != 1) {
                    return true;
                }
                size_t const nbSuccessors =
                        ZL_Compressor_Node_getNumOutcomes(cgraph, node);
                std::vector<ZL_GraphID> dsts(nbSuccessors, ZL_GRAPH_STORE);
                auto graph = ZL_Compressor_registerStaticGraph_fromNode(
                        cgraph, node, dsts.data(), dsts.size());
                return !ZL_GraphID_isValid(graph);
            });
    ZL_Compressor_free(cgraph);

    nodes.resize(end - nodes.begin());
    return nodes;
}

std::vector<ZL_GraphID> getAllGraphs(uint32_t formatVersion)
{
    std::vector<ZL_GraphID> graphs(GR_getNbStandardGraphs());
    GR_getAllStandardGraphIDs(graphs.data(), graphs.size());

    auto cgraph = ZL_Compressor_create();
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, formatVersion));
    auto end = std::remove_if(
            graphs.begin(), graphs.end(), [cgraph](ZL_GraphID graph) {
                if (ZL_Compressor_Graph_getNumInputs(cgraph, graph) != 1) {
                    return true;
                }
                return !ZL_GraphID_isValid(graph);
            });
    ZL_Compressor_free(cgraph);

    graphs.resize(end - graphs.begin());
    return graphs;
}

template <typename F>
size_t findFirstAfter(size_t start, size_t size, F const& fn)
{
    size_t idx = start;
    do {
        if (fn(idx))
            return idx;
        idx = (idx + 1) % size;
    } while (idx != start);
    throw std::runtime_error("No idx is true!");
}

ZL_GraphID buildStoreGraph(ZL_Compressor* cgraph, ZL_Type inType)
{
    if (inType == ZL_Type_string) {
        return ZL_Compressor_registerStaticGraph_fromNode(
                cgraph,
                ZL_NODE_SEPARATE_STRING_COMPONENTS,
                ZL_GRAPHLIST(ZL_GRAPH_STORE, ZL_GRAPH_STORE));
    }
    return ZL_GRAPH_STORE;
}

// ---------------------------------------------------------------------------
// Dict helpers (adapted from DictIntegrationTest.cpp, using XOR)
// ---------------------------------------------------------------------------

constexpr ZL_Type kSerialTypes[1] = { ZL_Type_serial };

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

ZL_Report xorWithDict(
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
    ZL_ERR_IF_NULL(materializedDict, GENERIC);

    size_t const size = ZL_Input_numElts(input);
    ZL_Output* out    = ZL_Encoder_createTypedStream(enc, 0, size, 1);
    ZL_ERR_IF_NULL(out, GENERIC);

    const uint8_t* src = static_cast<const uint8_t*>(ZL_Input_ptr(input));
    uint8_t* dst       = static_cast<uint8_t*>(ZL_Output_ptr(out));
    for (size_t i = 0; i < size; ++i) {
        dst[i] = src[i] ^ materializedDict[0];
    }
    ZL_ERR_IF_ERR(ZL_Output_commit(out, size));
    return ZL_returnSuccess();
}

ZL_Report xorWithDictDecoder(
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
        dst[i] = src[i] ^ materializedDict[0];
    }
    ZL_ERR_IF_ERR(ZL_Output_commit(out, size));
    return ZL_returnSuccess();
}

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
    ZL_REQUIRE_SUCCESS(report);
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
    ZL_REQUIRE_SUCCESS(report);
    fatBundle.resize(ZL_validResult(report));
    return fatBundle;
}

// ---------------------------------------------------------------------------
// DictNodeFactory + buildGraph
// ---------------------------------------------------------------------------

// Tracks dict node instances created during graph building.
// Each time the base dict node is selected, a parameterized clone with a
// unique dict ID is registered so that different positions in the graph
// reference different bundle indices.
struct DictNodeFactory {
    datagen::DataGen* dg;
    ZL_Compressor* cgraph;
    ZL_NodeID baseNode;
    ZL_IDType codecId;
    std::vector<std::vector<uint8_t>> packedDicts;

    ZL_NodeID createInstance()
    {
        auto id = dg->randStringWithLength("dict_id", 32);
        ZL_DictID dictID;
        ZL_UniqueID_read(&dictID.id, id.c_str());
        if (!ZL_UniqueID_isValid(&dictID.id)) {
            dictID.id.bytes[0] = 1; // make sure it's a valid ID
        }
        uint8_t const dictContent = 0xAB ^ dictID.id.bytes[0];

        ZL_ParameterizedNodeDesc desc = {
            .name        = nullptr,
            .node        = baseNode,
            .localParams = nullptr,
            .dictID      = dictID,
        };
        ZL_NodeID const node =
                ZL_Compressor_registerParameterizedNode(cgraph, &desc);
        if (node.nid == ZL_NODE_ILLEGAL.nid) {
            throw std::runtime_error("Failed to register dict node");
        }

        packedDicts.push_back(
                buildPackedDict(dictID, codecId, { dictContent }));

        return node;
    }
};

ZL_GraphID buildGraph(
        datagen::DataGen& dg,
        ZL_Compressor* cgraph,
        size_t* nodesInGraph,
        std::vector<ZL_NodeID> const& nodes,
        std::vector<ZL_GraphID> const& graphs,
        ZL_Type inType,
        size_t maxDepth,
        DictNodeFactory* factory)
{
    if (*nodesInGraph > kMaxNodesInGraph || maxDepth == 0) {
        return buildStoreGraph(cgraph, inType);
    }

    ++*nodesInGraph;

    bool const stop = dg.coin("use_store", 0.1);
    if (stop) {
        return buildStoreGraph(cgraph, inType);
    }

    bool const useGraph = dg.boolean("use_graph");
    if (useGraph) {
        size_t graphIdx = dg.usize_range("graph_index", 0, graphs.size() - 1);
        graphIdx = findFirstAfter(graphIdx, graphs.size(), [&](size_t idx) {
            auto const graphType =
                    ZL_Compressor_Graph_getInput0Mask(cgraph, graphs[idx]);
            return ICONV_isCompatible(inType, graphType);
        });
        return graphs[graphIdx];
    }

    size_t nodeIdx = dg.usize_range("node_index", 0, nodes.size() - 1);
    nodeIdx        = findFirstAfter(nodeIdx, nodes.size(), [&](size_t idx) {
        auto const nodeType =
                ZL_Compressor_Node_getInput0Type(cgraph, nodes[idx]);
        return ICONV_isCompatible(inType, nodeType);
    });
    auto node      = nodes[nodeIdx];

    // When the base dict node is selected, mint a fresh encoder instance
    // with a unique codec ID and dict ID so each position in the graph
    // references a different dict index in the bundle.
    if (factory != nullptr && node.nid == factory->baseNode.nid) {
        node = factory->createInstance();
    }

    size_t const nbSuccessors = ZL_Compressor_Node_getNumOutcomes(cgraph, node);
    std::vector<ZL_GraphID> successors(nbSuccessors);
    for (size_t i = 0; i < successors.size(); ++i) {
        auto const outType = ZL_Compressor_Node_getOutputType(cgraph, node, i);
        successors[i]      = buildGraph(
                dg,
                cgraph,
                nodesInGraph,
                nodes,
                graphs,
                outType,
                maxDepth - 1,
                factory);
    }

    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, node, successors.data(), successors.size());
}

// ---------------------------------------------------------------------------
// This fuzzer exercises a compression roundtrip to ensure the optional v25+
// bundle ID does not cause frame corruption. In particular, we:
// 1. Register a base dict-backed MI encoder node used as a sentinel.
// 2. Build a random graph; each time the sentinel is selected, a fresh
//    encoder with a unique codec ID and dict ID is registered, so
//    different positions in the graph reference different bundle indices.
// 3. Pack all generated dicts into a fat bundle with a fuzzed bundle ID.
// 4. Compress and decompress, ensuring no corruption.
// ---------------------------------------------------------------------------

FUZZ(DictBundleIDFuzzer, FuzzDictBundleIDRoundTrip)
{
    datagen::DataGen dg   = fromFDP(f);
    ZL_Compressor* cgraph = ZL_Compressor_create();
    ASSERT_NE(cgraph, nullptr);

    // --- Register a base dict node (sentinel for graph building) ---
    constexpr ZL_IDType kDictCodecId = 9000;

    ZL_MIEncoderDesc baseEncoderDesc = {
        .gd =
                {
                        .CTid       = kDictCodecId,
                        .inputTypes = kSerialTypes,
                        .nbInputs   = 1,
                        .soTypes    = kSerialTypes,
                        .nbSOs      = 1,
                },
        .transform_f = xorWithDict,
        .name        = "fuzz_dict_xor",
        .dictMat     = kCopyDictMaterializer,
    };
    ZL_RESULT_OF(ZL_NodeID)
    baseNodeResult = ZL_Compressor_registerMIEncoder2(cgraph, &baseEncoderDesc);
    ASSERT_FALSE(ZL_RES_isError(baseNodeResult));
    ZL_NodeID const baseDictNode = ZL_RES_value(baseNodeResult);

    // --- Graph setup ---
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_permissiveCompression, ZL_TernaryParam_enable));
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));

    auto nodes = getAllNodes(ZL_MAX_FORMAT_VERSION);
    // Add the sentinel a few times twice to increase selection probability
    // against the larger set of standard nodes.
    for (size_t i = 0; i < nodes.size() / 2; ++i) {
        nodes.push_back(baseDictNode);
    }
    auto graphs = getAllGraphs(ZL_MAX_FORMAT_VERSION);

    // The factory parameterizes the base node with a fresh dict ID each
    // time buildGraph picks it. The base dict is included so it is
    // exercised when the sentinel appears exactly once.
    DictNodeFactory factory = {
        .dg          = &dg,
        .cgraph      = cgraph,
        .baseNode    = baseDictNode,
        .codecId     = kDictCodecId,
        .packedDicts = {},
    };

    size_t nodesInGraph    = 0;
    ZL_GraphID const graph = buildGraph(
            dg,
            cgraph,
            &nodesInGraph,
            nodes,
            graphs,
            ZL_Type_serial,
            kMaxGraphDepth,
            &factory);

    // --- Build fat bundle from all dicts created during graph building ---
    auto fatBundle = packFatBundle(factory.packedDicts);

    // Generate a fuzzer-determined 32-byte bundle ID and patch it into the
    // fat bundle before loading. The compressor will then naturally write
    // this ID into the frame header, so no post-compression patching is
    // needed.
    ZL_UniqueID fuzzedBundleID;
    for (size_t i = 0; i < ZL_UNIQUE_ID_SIZE; ++i) {
        fuzzedBundleID.bytes[i] =
                static_cast<uint8_t>(dg.u32_range("bundle_id_byte", 0, 255));
    }
    // CDictMgr_setBundleID rejects all-zero IDs; skip this iteration.
    if (!ZL_UniqueID_isValid(&fuzzedBundleID)) {
        ZL_Compressor_free(cgraph);
        return;
    }
    // Fat bundle layout: 4-byte magic + 32-byte bundleID + ...
    ASSERT_GE(fatBundle.size(), 4 + ZL_UNIQUE_ID_SIZE);
    std::memcpy(&fatBundle[4], fuzzedBundleID.bytes, ZL_UNIQUE_ID_SIZE);

    ZL_REQUIRE_SUCCESS(ZL_Compressor_loadDictBundle(
            cgraph, fatBundle.data(), fatBundle.size()));

    // --- Compress ---
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph, graph));
    std::string input = dg.randString("input_str");

    size_t constexpr kMaxCompressedSize = kDefaultMaxInputLength * 10;
    std::string compressed(kMaxCompressedSize, '\0');

    auto const cSize = ZL_compress_usingCompressor(
            compressed.data(),
            compressed.size(),
            input.data(),
            input.size(),
            cgraph);
    ASSERT_FALSE(ZL_isError(cSize));
    compressed.resize(ZL_validResult(cSize));

    // --- Decompress ---
    // Always use DCtx+loader because the compressor writes a bundleID into
    // the frame header whenever a fat bundle is loaded, even if no codec
    // in the selected graph uses a dict.
    ZL_FatBundleDictLoader* loader = ZL_FatBundleDictLoader_create();
    ASSERT_NE(loader, nullptr);
    ZL_DictLoader* dictLoader = ZL_FatBundleDictLoader_getDictLoader(loader);
    ASSERT_NE(dictLoader, nullptr);
    ZL_REQUIRE_SUCCESS(ZL_DictLoader_registerMaterializer(
            dictLoader, kDictCodecId, &kCopyDictMaterializer));
    ZL_REQUIRE_SUCCESS(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBundle.data(), fatBundle.size()));

    DCtx dctx;
    for (const auto& component : getAllOpenZLComponents()) {
        component->registerComponent(dctx);
    }

    // Register custom dict decoder
    ZL_MIDecoderDesc decoderDesc = {
        .gd =
                {
                        .CTid       = kDictCodecId,
                        .inputTypes = kSerialTypes,
                        .nbInputs   = 1,
                        .soTypes    = kSerialTypes,
                        .nbSOs      = 1,
                },
        .transform_f = xorWithDictDecoder,
    };
    ZL_REQUIRE_SUCCESS(ZL_DCtx_registerMIDecoder(dctx.get(), &decoderDesc));

    ZL_DCtx_refDictLoader(dctx.get(), dictLoader);

    std::string output(input.size(), '\0');
    auto const dSize = ZL_DCtx_decompress(
            dctx.get(),
            output.data(),
            output.size(),
            compressed.data(),
            compressed.size());
    ZL_REQUIRE_SUCCESS(dSize);
    ASSERT_EQ(ZL_validResult(dSize), input.size());
    ASSERT_TRUE(output == input);

    ZL_FatBundleDictLoader_free(loader);

    ZL_Compressor_free(cgraph);
}

} // namespace
} // namespace openzl::tests
