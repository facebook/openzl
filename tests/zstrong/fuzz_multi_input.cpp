// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/zl_concat.h"
#include "openzl/codecs/zl_field_lz.h"
#include "openzl/compress/graphs/generic_clustering_graph.h"
#include "openzl/compress/selectors/ml/gbt.h"
#include "openzl/compress/selectors/ml/ml_selector_graph.h"
#include "tests/fuzz_utils.h"
#include "tests/ml_selector_utils.h"
#include "tests/zstrong/test_multi_input_fixture.h"

namespace openzl {
namespace tests {
namespace {

std::vector<uint32_t> getSegments(
        datagen::DataGen& dg,
        size_t const srcSize,
        size_t maxSegments = 512)
{
    size_t const numSegments = dg.usize_range("num_segments", 0, maxSegments);
    std::vector<uint32_t> segmentSizes;
    size_t totalSize = 0;
    segmentSizes.reserve(numSegments);
    for (size_t i = 0; i < numSegments; ++i) {
        size_t const segmentSize =
                dg.usize_range("segment_size", 0, srcSize - totalSize);
        segmentSizes.push_back(segmentSize);
        totalSize += segmentSize;
    }
    if (totalSize < srcSize) {
        segmentSizes.push_back(srcSize - totalSize);
    }
    return segmentSizes;
}

FUZZ_F(MultiInputTest, FuzzConcatRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    reset();
    setLargeCompressBound(2);
    auto concat = dg.choices(
            "concat",
            std::vector<ZL_NodeID>{ ZL_NODE_CONCAT_SERIAL,
                                    ZL_NODE_CONCAT_NUMERIC,
                                    ZL_NODE_CONCAT_STRUCT,
                                    ZL_NODE_CONCAT_STRING });
    const ZL_GraphID successors[2] = { ZL_GRAPH_STORE, ZL_GRAPH_STORE };
    auto graph                     = ZL_Compressor_registerStaticGraph_fromNode(
            cgraph_, concat, successors, 2);
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, graph));
    size_t numInputs = dg.usize_range("num_inputs", 1, 512);
    std::vector<std::unique_ptr<ZL_TypedRef, ZS2_TypedRef_Deleter>> inputs;
    std::vector<TypedInputDesc> inputDescs;
    inputs.reserve(numInputs);
    inputDescs.reserve(numInputs);
    for (size_t i = 0; i < numInputs; ++i) {
        ZL_Type type = dg.choices(
                "type",
                std::vector<ZL_Type>{ ZL_Type_serial,
                                      ZL_Type_struct,
                                      ZL_Type_numeric,
                                      ZL_Type_string });
        std::string input;
        size_t eltWidth = 1;
        std::vector<uint32_t> strLens;
        switch (type) {
            case ZL_Type_serial:
                break;
            case ZL_Type_struct:
                eltWidth = dg.choices(
                        "elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
                break;
            case ZL_Type_numeric:
                eltWidth = dg.choices(
                        "elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
                break;
            case ZL_Type_string:
                break;
        }
        input = dg.randStringWithQuantizedLength("input_str", eltWidth);
        if (type == ZL_Type_string) {
            strLens = getSegments(dg, input.size(), false);
        }
        inputDescs.emplace_back(
                std::move(input), type, eltWidth, std::move(strLens));
        inputs.emplace_back(getTypedInput(inputDescs[i]));
    }

    bool typesAreCompat = true;
    size_t eltWidth     = inputDescs[0].eltWidth;
    for (const auto& inputDesc : inputDescs) {
        if (eltWidth != inputDesc.eltWidth) {
            typesAreCompat = false;
            continue;
        }
        switch (concat.nid) {
            case ZL_NODE_CONCAT_SERIAL.nid:
                if (inputDesc.type == ZL_Type_string) {
                    typesAreCompat = false;
                    continue;
                }
                break;
            case ZL_NODE_CONCAT_STRUCT.nid:
                if (inputDesc.type != ZL_Type_struct
                    && inputDesc.type != ZL_Type_numeric) {
                    typesAreCompat = false;
                    continue;
                }
                break;
            case ZL_NODE_CONCAT_NUMERIC.nid:
                if (inputDesc.type != ZL_Type_numeric) {
                    typesAreCompat = false;
                    continue;
                }
                break;
            case ZL_NODE_CONCAT_STRING.nid:
                if (inputDesc.type != ZL_Type_string) {
                    typesAreCompat = false;
                    continue;
                }
                break;
        }
    }
    if (typesAreCompat) {
        testRoundTripMI(inputs, inputDescs);
    } else {
        testRoundTripMICompressionMayFail(inputs, inputDescs);
    }
}

FUZZ_F(MultiInputTest, FuzzClusterRoundTrip)
{
    datagen::DataGen dg = fromFDP(f);
    reset();
    setLargeCompressBound(2);
    const ZL_GraphID successors[3] = { ZL_GRAPH_STORE,
                                       ZL_GRAPH_ZSTD,
                                       ZL_GRAPH_COMPRESS_GENERIC };
    const ZL_NodeID nodeIDs[3]     = { ZL_NODE_CONCAT_SERIAL,
                                       ZL_NODE_CONCAT_NUMERIC,
                                       ZL_NODE_CONCAT_STRING };

    size_t numInputs = dg.usize_range("num_inputs", 1, 512);
    std::vector<std::unique_ptr<ZL_TypedRef, ZS2_TypedRef_Deleter>> inputs;
    std::vector<TypedInputDesc> inputDescs;
    inputs.reserve(numInputs);
    inputDescs.reserve(numInputs);
    for (size_t i = 0; i < numInputs; ++i) {
        ZL_Type type = dg.choices(
                "type",
                std::vector<ZL_Type>{
                        ZL_Type_serial, ZL_Type_numeric, ZL_Type_string });
        std::string input;
        size_t eltWidth = 1;
        std::vector<uint32_t> strLens;
        switch (type) {
            case ZL_Type_serial:
                break;
            case ZL_Type_numeric:
                eltWidth = 8;
                break;
            case ZL_Type_string:
                break;
            default:
                throw std::runtime_error("Unsupported type");
        }
        input = dg.randStringWithQuantizedLength("input_str", eltWidth);
        if (type == ZL_Type_string) {
            strLens = getSegments(dg, input.size(), false);
        }
        inputDescs.emplace_back(
                std::move(input), type, eltWidth, std::move(strLens));
        inputs.emplace_back(getTypedInput(inputDescs[i]));
        int metadata = dg.usize_range("num_inputs", 1, 1024);
        if (ZL_isError(ZL_Input_setIntMetadata(inputs[i].get(), 0, metadata))) {
            throw std::runtime_error("Failed to set metadata");
        }
    }

    ZL_ClusteringConfig config;
    config.nbTypeDefaults = 3;
    // TODO: Let the fuzzer generate default successors
    ZL_ClusteringConfig_TypeSuccessor defaultSuccs[3];
    defaultSuccs[0]     = { .type               = ZL_Type_serial,
                            .eltWidth           = 1,
                            .successorIdx       = 1,
                            .clusteringCodecIdx = 0 };
    defaultSuccs[1]     = { .type               = ZL_Type_numeric,
                            .eltWidth           = 8,
                            .successorIdx       = 1,
                            .clusteringCodecIdx = 1 };
    defaultSuccs[2]     = { .type               = ZL_Type_string,
                            .eltWidth           = 0,
                            .successorIdx       = 2,
                            .clusteringCodecIdx = 2 };
    config.typeDefaults = defaultSuccs;
    config.nbClusters   = dg.usize_range("num_inputs", 1, 512);
    config.clusters     = (ZL_ClusteringConfig_Cluster*)malloc(
            sizeof(ZL_ClusteringConfig_Cluster) * config.nbClusters);
    for (size_t i = 0; i < config.nbClusters; i++) {
        // Member tag
        auto nbMemberTags               = dg.usize_range("members", 1, 10);
        config.clusters[i].nbMemberTags = nbMemberTags;
        config.clusters[i].memberTags =
                (int*)malloc(sizeof(int) * nbMemberTags);
        for (size_t j = 0; j < nbMemberTags; j++) {
            config.clusters[i].memberTags[j] = dg.usize_range("tags", 1, 1024);
        }
        // Type successor
        // TODO: Allow the fuzzer to generate out of range successor indices
        config.clusters[i].typeSuccessor.successorIdx =
                dg.usize_range("num_inputs", 0, 2);
        config.clusters[i].typeSuccessor.type = dg.choices(
                "type",
                std::vector<ZL_Type>{
                        ZL_Type_serial, ZL_Type_numeric, ZL_Type_string });
        switch (config.clusters[i].typeSuccessor.type) {
            case ZL_Type_serial:
                config.clusters[i].typeSuccessor.eltWidth = 1;
                break;
            case ZL_Type_numeric:
                config.clusters[i].typeSuccessor.eltWidth = 8;
                break;
            case ZL_Type_string:
                config.clusters[i].typeSuccessor.eltWidth = 0;
                break;
            default:
                throw std::runtime_error("Unsupported type");
        }
        // Clustering codec
        // TODO: Allow the fuzzer to generate out of range codec indices
        config.clusters[i].typeSuccessor.clusteringCodecIdx =
                dg.usize_range("cluster_codec_idx", 0, 2);
    }

    auto graph = ZL_Clustering_registerGraphWithCustomClusteringCodecs(
            cgraph_, &config, successors, 3, nodeIDs, 3);
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph_, graph));

    bool configIsValid = true;
    // Config is valid if
    // 1. all inputs types have a default successor if a default
    // is necessary. We always provide a default successor in this config.
    // 2. successor indices are in range. This guaranteed by the ranges chosen
    // 3. Type defaults have unique types. This is guaranteed since we use a
    // static list of defualts
    // 4. a successor chosen for a cluster is incompatible with the type of the
    // cluster.
    // 5. Each cluster has unique tags
    // 6. For each type, there is no two clusters with a shared tag
    // 7. The cluster codec chosen matches the type of the cluster
    std::unordered_map<std::pair<ZL_Type, size_t>, std::unordered_set<int>>
            typeToTags;
    for (size_t i = 0; i < config.nbClusters; i++) {
        // Check the cluster codec type matches the cluster type
        switch (config.clusters[i].typeSuccessor.type) {
            case ZL_Type_serial:
                if (config.clusters[i].typeSuccessor.clusteringCodecIdx != 0) {
                    configIsValid = false;
                }
                break;
            case ZL_Type_numeric:
                if (config.clusters[i].typeSuccessor.clusteringCodecIdx != 1) {
                    configIsValid = false;
                }
                break;
            case ZL_Type_string:
                if (config.clusters[i].typeSuccessor.clusteringCodecIdx != 2) {
                    configIsValid = false;
                }
                break;
            default:
                // This cannot happen since we choose from these 3 types
                throw std::runtime_error("Unsupported type");
        }

        // Zstd is not valid with string type
        if (config.clusters[i].typeSuccessor.successorIdx == 1) {
            if (config.clusters[i].typeSuccessor.type == ZL_Type_string) {
                configIsValid = false;
                break;
            }
        }
        // A cluster cannot have a duplicate tag
        std::unordered_set<int> uniqueTags;
        for (size_t j = 0; j < config.clusters[i].nbMemberTags; j++) {
            uniqueTags.insert(config.clusters[i].memberTags[j]);
            std::pair<ZL_Type, size_t> type = std::make_pair(
                    config.clusters[i].typeSuccessor.type,
                    config.clusters[i].typeSuccessor.eltWidth);
            if (typeToTags[type].contains(config.clusters[i].memberTags[j])) {
                configIsValid = false;
                break;
            } else {
                typeToTags[type].insert(config.clusters[i].memberTags[j]);
            }
        }

        if (uniqueTags.size() != config.clusters[i].nbMemberTags) {
            configIsValid = false;
        }
        if (!configIsValid) {
            break;
        }
    }
    if (configIsValid) {
        testRoundTripMI(inputs, inputDescs);
    } else {
        testRoundTripMICompressionMayFail(inputs, inputDescs);
    }
    // Free memory
    for (size_t i = 0; i < config.nbClusters; i++) {
        free(config.clusters[i].memberTags);
    }
    free(config.clusters);
}

class MLSelectorMultiInputTest : public MultiInputTest {
   public:
    void SetUp() override
    {
        mlSelectorConfig_ = { .model = ZL_GBT, .runtimeConfig = &gbtModel };
    }

    openzl::tests::SampleBinaryGBTModel sampleModel;
    GBTModel gbtModel = sampleModel.getModel();
    ZL_MLSelectorConfig mlSelectorConfig_{};
};
static ZL_GraphID declareGraphFromNode(
        ZL_Compressor* cgraph,
        ZL_NodeID node,
        std::initializer_list<ZL_GraphID> successors)
{
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, node, successors.begin(), successors.size());
}

FUZZ_F(MLSelectorMultiInputTest, FuzzMLSelectorRoundTrip)
{
    reset();
    setLargeCompressBound(2);

    const size_t numSucc = 2;
    auto fieldlz         = ZL_Compressor_registerFieldLZGraph(cgraph_);

    auto range_pack =
            declareGraphFromNode(cgraph_, ZL_NODE_RANGE_PACK, { fieldlz });
    auto range_pack_zstd = declareGraphFromNode(
            cgraph_, ZL_NODE_RANGE_PACK, { ZL_GRAPH_ZSTD });
    auto zig_zag =
            declareGraphFromNode(cgraph_, ZL_NODE_ZIGZAG, { ZL_GRAPH_ZSTD });
    auto delta_fieldlz =
            declareGraphFromNode(cgraph_, ZL_NODE_DELTA_INT, { fieldlz });
    auto tokenize_delta_fieldlz = ZL_Compressor_registerTokenizeGraph(
            cgraph_, ZL_Type_numeric, /* sort */ true, delta_fieldlz, fieldlz);

    std::vector<ZL_GraphID> possibleSuccessors{
        ZL_GRAPH_STORE, fieldlz,          range_pack,
        delta_fieldlz,  range_pack_zstd,  tokenize_delta_fieldlz,
        zig_zag,        ZL_GRAPH_NUMERIC, ZL_GRAPH_COMPRESS_GENERIC,
    };

    size_t firstIdx =
            f.usize_range("first_successor", 0, possibleSuccessors.size() - 1);
    size_t secondIdx =
            f.usize_range("second_successor", 0, possibleSuccessors.size() - 1);

    if (firstIdx == secondIdx) {
        secondIdx = (firstIdx + 1) % possibleSuccessors.size();
    }

    const ZL_GraphID successors[numSucc] = { possibleSuccessors[firstIdx],
                                             possibleSuccessors[secondIdx] };

    size_t eltWidth   = f.choices("elt_width", { 1, 2, 4, 8 });
    std::string input = gen_str(f, "input_str", InputLengthInBytes(eltWidth));
    std::vector<uint32_t> strLens;

    // Only have featureGen_integer right now, so only use ZL_Type_numeric
    // No multi input for ML Selector so only using vector of size one
    std::vector<TypedInputDesc> inputDescs = { TypedInputDesc(
            input, ZL_Type_numeric, eltWidth, {}) };
    std::vector<std::unique_ptr<ZL_TypedRef, ZS2_TypedRef_Deleter>> inputs;
    inputs.push_back(getTypedInput(inputDescs.back()));

    int metadata = f.usize_range("metadata_value", 1, 1024);
    if (ZL_isError(ZL_Input_setIntMetadata(inputs.back().get(), 0, metadata))) {
        throw std::runtime_error("Failed to set metadata");
    }

    auto graph = ZL_MLSelector_registerGraph(
            cgraph_, &mlSelectorConfig_, successors, numSucc);
    EXPECT_FALSE(ZL_RES_isError(graph));

    testRoundTripMI(inputs, inputDescs);
}

} // namespace
} // namespace tests
} // namespace openzl
