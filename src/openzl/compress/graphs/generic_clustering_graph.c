// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/graphs/generic_clustering_graph.h"
#include "openzl/codecs/zl_clustering.h"
#include "openzl/codecs/zl_concat.h"
#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/allocation.h"
#include "openzl/common/limits.h"
#include "openzl/common/map.h"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_errors.h"

#define ZL_NUMBER_ELT_WIDTHS 4

typedef struct {
    ZL_Type type;
    size_t eltWidth;
} TypeWidth;

typedef struct {
    TypeWidth typeWidth;
    int tag;
} Tag;

ZL_FORCE_INLINE size_t TypeToSuccessorMap_hash(TypeWidth const* key)
{
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    XXH3_64bits_update(&state, &key->type, sizeof(int));
    XXH3_64bits_update(&state, &key->eltWidth, sizeof(size_t));
    XXH64_hash_t result = XXH3_64bits_digest(&state);
    return result;
}

ZL_FORCE_INLINE bool TypeToSuccessorMap_eq(
        TypeWidth const* lhs,
        TypeWidth const* rhs)
{
    return lhs->eltWidth == rhs->eltWidth && lhs->type == rhs->type;
}

ZL_FORCE_INLINE size_t TagToClusterMap_hash(Tag const* key)
{
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    XXH3_64bits_update(&state, &key->typeWidth.type, sizeof(int));
    XXH3_64bits_update(&state, &key->typeWidth.eltWidth, sizeof(size_t));
    XXH3_64bits_update(&state, &key->tag, sizeof(int));
    XXH64_hash_t result = XXH3_64bits_digest(&state);
    return result;
}

ZL_FORCE_INLINE bool TagToClusterMap_eq(Tag const* lhs, Tag const* rhs)
{
    return TypeToSuccessorMap_eq(&lhs->typeWidth, &rhs->typeWidth)
            && lhs->tag == rhs->tag;
}

ZL_DECLARE_CUSTOM_MAP_TYPE(
        TypeToSuccessorMap,
        TypeWidth,
        ZL_ClusteringConfig_TypeSuccessor);
ZL_DECLARE_CUSTOM_MAP_TYPE(TagToClusterMap, Tag, size_t);

/**
 * @brief Validates all clustering codec indices in a configuration
 *
 * @param errCtx Error context for reporting errors
 * @param config The clustering configuration to validate
 * @param nbClusteringCodecs The number of available clustering codecs
 * @return ZL_Report Success if all indices are valid, error otherwise
 */
static ZL_Report validateClusteringCodecIndices(
        ZL_ErrorContext* errCtx,
        const ZL_ClusteringConfig* config,
        size_t nbClusteringCodecs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);

    for (size_t i = 0; i < config->nbClusters; i++) {
        ZL_ERR_IF(
                config->clusters[i].typeSuccessor.clusteringCodecIdx
                        >= nbClusteringCodecs,
                parameter_invalid,
                "Cluster %zu has invalid clusteringCodecIdx %zu (max allowed: %zu)",
                i,
                config->clusters[i].typeSuccessor.clusteringCodecIdx,
                nbClusteringCodecs - 1);
    }

    for (size_t i = 0; i < config->nbTypeDefaults; i++) {
        ZL_ERR_IF(
                config->typeDefaults[i].clusteringCodecIdx
                        >= nbClusteringCodecs,
                parameter_invalid,
                "TypeDefault %zu has invalid clusteringCodecIdx %zu (max allowed: %zu)",
                i,
                config->typeDefaults[i].clusteringCodecIdx,
                nbClusteringCodecs - 1);
    }

    return ZL_returnSuccess();
}

static void* graph_arenaCalloc(void* opaque, size_t size)
{
    return ZL_Graph_getScratchSpace((ZL_Graph*)opaque, size);
}

static A1C_Arena graph_wrapArena(ZL_Graph* graph)
{
    A1C_Arena arena;
    arena.calloc = graph_arenaCalloc;
    arena.opaque = graph;
    return arena;
}

static ZL_RESULT_OF(ZL_ClusteringConfig)
        graph_getClusteringConfig(ZL_Graph* graph)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_RefParam serializedConfig =
            ZL_Graph_getLocalRefParam(graph, ZL_GENERIC_CLUSTERING_CONFIG_ID);
    const uint8_t* config = serializedConfig.paramRef;
    size_t configSize     = serializedConfig.paramSize;

    // TODO: provide a default config when config parameter is not passed to
    // graph
    ZL_RET_T_IF_NULL(ZL_ClusteringConfig, graphParameter_invalid, config);

    A1C_Arena arena = graph_wrapArena(graph);
    return ZL_Clustering_deserializeClusteringConfig(
            ZL_ERR_CTX_PTR, config, configSize, &arena);
}

static ZL_Report cbor_serializeTypeSuccessor(
        ZL_ErrorContext* errCtx,
        A1C_Item* parent,
        A1C_Arena* arena,
        ZL_ClusteringConfig_TypeSuccessor* typeSuccesor)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    A1C_MapBuilder typeSuccessorMapBuilder =
            A1C_Item_map_builder(parent, 4, arena);
    {
        A1C_MAP_TRY_ADD_R(pair, typeSuccessorMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "type");
        A1C_Item_int64(&pair->val, (A1C_Int64)typeSuccesor->type);
    }
    {
        A1C_MAP_TRY_ADD_R(pair, typeSuccessorMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "eltWidth");
        A1C_Item_int64(&pair->val, (A1C_Int64)typeSuccesor->eltWidth);
    }
    {
        A1C_MAP_TRY_ADD_R(pair, typeSuccessorMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "successorIdx");
        A1C_Item_int64(&pair->val, (A1C_Int64)typeSuccesor->successorIdx);
    }
    {
        A1C_MAP_TRY_ADD_R(pair, typeSuccessorMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "clusteringCodecIdx");
        A1C_Item_int64(&pair->val, (A1C_Int64)typeSuccesor->clusteringCodecIdx);
    }
    return ZL_returnSuccess();
}

// Serialization is expected to be called at graph declaration time
ZL_Report ZL_Clustering_serializeClusteringConfig(
        ZL_ErrorContext* errCtx,
        uint8_t** dst,
        size_t* dstSize,
        const ZL_ClusteringConfig* config,
        A1C_Arena* arena)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    A1C_Item* root = A1C_Item_root(arena);
    ZL_RET_R_IF_NULL(allocation, root);
    A1C_MapBuilder rootMapBuilder = A1C_Item_map_builder(root, 2, arena);
    {
        A1C_MAP_TRY_ADD_R(pair, rootMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "clusters");
        A1C_Item* clusters =
                A1C_Item_array(&pair->val, config->nbClusters, arena);
        ZL_RET_R_IF_NULL(allocation, clusters);
        for (size_t i = 0; i < config->nbClusters; i++) {
            A1C_MapBuilder clustersMapBuilder =
                    A1C_Item_map_builder(&clusters[i], 2, arena);
            {
                A1C_MAP_TRY_ADD_R(p, clustersMapBuilder);
                A1C_Item_string_refCStr(&p->key, "typeSuccessor");
                ZL_RET_R_IF_ERR(cbor_serializeTypeSuccessor(
                        errCtx,
                        &p->val,
                        arena,
                        &config->clusters[i].typeSuccessor));
            }
            {
                A1C_MAP_TRY_ADD_R(p, clustersMapBuilder);
                A1C_Item_string_refCStr(&p->key, "memberTags");
                A1C_Item* memberTags = A1C_Item_array(
                        &p->val, config->clusters[i].nbMemberTags, arena);
                ZL_RET_R_IF_NULL(allocation, memberTags);
                for (size_t j = 0; j < config->clusters[i].nbMemberTags; j++) {
                    A1C_Item_int64(
                            &memberTags[j],
                            (A1C_Int64)config->clusters[i].memberTags[j]);
                }
            }
        }
    }
    {
        A1C_MAP_TRY_ADD_R(pair, rootMapBuilder);
        A1C_Item_string_refCStr(&pair->key, "typeDefaults");
        A1C_Item* typeDefaults =
                A1C_Item_array(&pair->val, config->nbTypeDefaults, arena);
        ZL_RET_R_IF_NULL(allocation, typeDefaults);
        for (size_t i = 0; i < config->nbTypeDefaults; i++) {
            ZL_RET_R_IF_ERR(cbor_serializeTypeSuccessor(
                    errCtx, &typeDefaults[i], arena, &config->typeDefaults[i]));
        }
    }
    *dstSize = A1C_Item_encodedSize(root);
    *dst     = arena->calloc(arena->opaque, *dstSize);
    ZL_RET_R_IF_NULL(allocation, *dst);
    A1C_Error error;
    size_t res = A1C_Item_encode(root, *dst, *dstSize, &error);
    if (res == 0) {
        ZL_RET_R_WRAP_ERR(A1C_Error_convert(NULL, error));
    }
    ZL_RET_R_IF_NE(allocation, res, *dstSize);
    return ZL_returnSuccess();
}

static ZL_Report cbor_deserializeTypeSuccessor(
        ZL_ErrorContext* errCtx,
        const A1C_Map* typeSuccessorMap,
        ZL_ClusteringConfig_TypeSuccessor* typeSuccessor)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    A1C_TRY_EXTRACT_R_INT64(type, A1C_Map_get_cstr(typeSuccessorMap, "type"));
    typeSuccessor->type = (ZL_Type)type;
    A1C_TRY_EXTRACT_R_INT64(
            eltWidth, A1C_Map_get_cstr(typeSuccessorMap, "eltWidth"));
    typeSuccessor->eltWidth = (size_t)eltWidth;
    A1C_TRY_EXTRACT_R_INT64(
            successorIdx, A1C_Map_get_cstr(typeSuccessorMap, "successorIdx"));
    typeSuccessor->successorIdx = (size_t)successorIdx;
    A1C_TRY_EXTRACT_R_INT64(
            clusteringCodecIdx,
            A1C_Map_get_cstr(typeSuccessorMap, "clusteringCodecIdx"));
    typeSuccessor->clusteringCodecIdx = (size_t)clusteringCodecIdx;
    return ZL_returnSuccess();
}

// Deserialization is expected to be called at compression time typically
ZL_RESULT_OF(ZL_ClusteringConfig)
ZL_Clustering_deserializeClusteringConfig(
        ZL_ErrorContext* errCtx,
        const uint8_t* config,
        size_t configSize,
        A1C_Arena* arena)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    ZL_ClusteringConfig dst;
    A1C_Decoder decoder;
    A1C_DecoderConfig decoderConfig =
            (A1C_DecoderConfig){ .maxDepth            = 0,
                                 .limitBytes          = 0,
                                 .referenceSource     = true,
                                 .rejectUnknownSimple = true };
    A1C_Decoder_init(&decoder, *arena, decoderConfig);
    const A1C_Item* root = A1C_Decoder_decode(&decoder, config, configSize);
    A1C_TRY_EXTRACT_T_MAP(ZL_ClusteringConfig, rootMap, root);

    A1C_TRY_EXTRACT_T_ARRAY(
            ZL_ClusteringConfig,
            clustersItem,
            A1C_Map_get_cstr(&rootMap, "clusters"));
    dst.nbClusters = clustersItem.size;
    ZL_RET_T_IF_GT(
            ZL_ClusteringConfig,
            node_invalid_input,
            dst.nbClusters,
            (size_t)ZL_runtimeNodeInputLimit(ZL_MAX_FORMAT_VERSION));
    dst.clusters = arena->calloc(
            arena->opaque,
            dst.nbClusters * sizeof(ZL_ClusteringConfig_Cluster));
    ZL_RET_T_IF_NULL(ZL_ClusteringConfig, allocation, dst.clusters);
    for (size_t i = 0; i < dst.nbClusters; i++) {
        const A1C_Item* clusterItem = A1C_Array_get(&clustersItem, i);
        A1C_TRY_EXTRACT_T_MAP(ZL_ClusteringConfig, clusterMap, clusterItem);
        A1C_TRY_EXTRACT_T_MAP(
                ZL_ClusteringConfig,
                typeSuccessorMap,
                A1C_Map_get_cstr(&clusterMap, "typeSuccessor"));
        ZL_RET_T_IF_ERR(
                ZL_ClusteringConfig,
                cbor_deserializeTypeSuccessor(
                        errCtx,
                        &typeSuccessorMap,
                        &dst.clusters[i].typeSuccessor));
        A1C_TRY_EXTRACT_T_ARRAY(
                ZL_ClusteringConfig,
                memberTagsArray,
                A1C_Map_get_cstr(&clusterMap, "memberTags"));
        dst.clusters[i].nbMemberTags = memberTagsArray.size;
        dst.clusters[i].memberTags   = arena->calloc(
                arena->opaque, dst.clusters[i].nbMemberTags * sizeof(int));
        ZL_RET_T_IF_NULL(
                ZL_ClusteringConfig, allocation, dst.clusters[i].memberTags);
        for (size_t j = 0; j < dst.clusters[i].nbMemberTags; j++) {
            A1C_TRY_EXTRACT_T_INT64(
                    ZL_ClusteringConfig,
                    memberTag,
                    A1C_Array_get(&memberTagsArray, j));
            ZL_RET_T_IF_GT(
                    ZL_ClusteringConfig,
                    nodeParameter_invalidValue,
                    memberTag,
                    INT32_MAX);
            dst.clusters[i].memberTags[j] = (int)memberTag;
        }
    }
    A1C_TRY_EXTRACT_T_ARRAY(
            ZL_ClusteringConfig,
            typeDefaultsItem,
            A1C_Map_get_cstr(&rootMap, "typeDefaults"));
    dst.nbTypeDefaults = typeDefaultsItem.size;
    dst.typeDefaults   = arena->calloc(
            arena->opaque,
            dst.nbTypeDefaults * sizeof(ZL_ClusteringConfig_TypeSuccessor));
    ZL_RET_T_IF_NULL(ZL_ClusteringConfig, allocation, dst.typeDefaults);
    for (size_t i = 0; i < dst.nbTypeDefaults; i++) {
        const A1C_Item* typeDefaultItem = A1C_Array_get(&typeDefaultsItem, i);
        A1C_TRY_EXTRACT_T_MAP(
                ZL_ClusteringConfig, typeDefaultMap, typeDefaultItem);
        ZL_RET_T_IF_ERR(
                ZL_ClusteringConfig,
                cbor_deserializeTypeSuccessor(
                        errCtx, &typeDefaultMap, &dst.typeDefaults[i]));
    }

    return ZL_RESULT_WRAP_VALUE(ZL_ClusteringConfig, dst);
}

/**
 * @brief Get the default successor for a given type and eltWidth.
 *
 * @return The defaultsuccessor for type matching the @p type and @p
 * eltWidth in @p config for inputs with tag not specified in the config.
 */
static ZL_RESULT_OF(ZL_GraphID) getDefaultSuccessor(
        ZL_ErrorContext* errCtx,
        TypeToSuccessorMap_Key typeWidth,
        const TypeToSuccessorMap* typeToSuccessorIdxDefaultsMap,
        const ZL_GraphIDList* successors)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, errCtx);
    // Use the configured type default if present
    const TypeToSuccessorMap_Entry* entry =
            TypeToSuccessorMap_find(typeToSuccessorIdxDefaultsMap, &typeWidth);

    if (entry != NULL) {
        ZL_ERR_IF_GE(
                entry->val.successorIdx,
                successors->nbGraphIDs,
                node_invalid_input,
                "Successor index out of range for uncofigured tag");
        return ZL_RESULT_WRAP_VALUE(
                ZL_GraphID, successors->graphids[entry->val.successorIdx]);
    }

    return ZL_RESULT_WRAP_VALUE(ZL_GraphID, ZL_GRAPH_COMPRESS_GENERIC);
}
/** @brief Get the default clustering codec for a given type and eltWidth.
 *
 * @return The default clustering codec for type matching the @p type and @p
 * eltWidth in @p config for inputs with tag not specified in the config.
 */

static ZL_RESULT_OF(ZL_NodeID) getDefaultClusteringCodec(
        ZL_ErrorContext* errCtx,
        TypeWidth typeWidth,
        const TypeToSuccessorMap* defaultSuccessors,
        const ZL_NodeIDList* nodes)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_NodeID, errCtx);
    const TypeToSuccessorMap_Entry* entry =
            TypeToSuccessorMap_find(defaultSuccessors, &typeWidth);
    if (entry == NULL) {
        ZL_NodeID clusteringCodec;
        switch (typeWidth.type) {
            case ZL_Type_serial:
                clusteringCodec = ZL_NODE_CONCAT_SERIAL;
                break;
            case ZL_Type_numeric:
                clusteringCodec = ZL_NODE_CONCAT_NUMERIC;
                break;
            case ZL_Type_struct:
                clusteringCodec = ZL_NODE_CONCAT_STRUCT;
                break;
            case ZL_Type_string:
                clusteringCodec = ZL_NODE_CONCAT_STRING;
                break;
            default:
                ZL_RET_T_ERR(
                        ZL_NodeID,
                        node_invalid_input,
                        "Invalid type for uncofigured tag");
        }
        return ZL_RESULT_WRAP_VALUE(ZL_NodeID, clusteringCodec);
    } else {
        ZL_ERR_IF_GE(
                entry->val.clusteringCodecIdx,
                nodes->nbNodeIDs,
                node_invalid_input,
                "Cluster codec index out of range for uncofigured tag");
        return ZL_RESULT_WRAP_VALUE(
                ZL_NodeID, nodes->nodeids[entry->val.clusteringCodecIdx]);
    }
}

/*
 * Does type validation, ensuring the streams in the same cluster have the same
 * types.
 */
static ZL_Report validateClusteredConfig(
        ZL_Graph* graph,
        const ZL_ClusteringConfig* config,
        const ZL_GraphIDList* succList)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    /* Check successor index is not out of range for clusters */
    for (size_t i = 0; i < config->nbClusters; i++) {
        ZL_RET_R_IF_GE(
                graphParameter_invalid,
                config->clusters[i].typeSuccessor.successorIdx,
                succList->nbGraphIDs);
    }
    /* Check successor index is not out of range for defaultSuccessors */
    for (size_t i = 0; i < config->nbTypeDefaults; i++) {
        ZL_RET_R_IF_GE(
                graphParameter_invalid,
                config->typeDefaults[i].successorIdx,
                succList->nbGraphIDs);
    }
    return ZL_returnSuccess();
}

typedef struct {
    size_t nbEdges;
    ZL_GraphID successor;
    ZL_NodeID node;
} ClusterInfo;

static ZL_Report sendClustersToSuccessors(
        ZL_ErrorContext* errCtx,
        ZL_Edge*** clusters,
        const ClusterInfo* clusterInfos,
        size_t nbClusters)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    /*
     * Do concatenation of clusters
     */
    for (size_t i = 0; i < nbClusters; i++) {
        ZL_Edge** cluster     = clusters[i];
        size_t nbEdges        = clusterInfos[i].nbEdges;
        ZL_GraphID successor  = clusterInfos[i].successor;
        ZL_NodeID clusterNode = clusterInfos[i].node;
        if (nbEdges == 0) {
            continue;
        }
        if (nbEdges == 1) {
            // Directly send edge to successor if there is only a single edge in
            // the cluster.
            ZL_RET_R_IF_ERR(ZL_Edge_setDestination(cluster[0], successor));
            continue;
        }

        ZL_TRY_LET_T(
                ZL_EdgeList,
                clustered,
                ZL_Edge_runMultiInputNode(cluster, nbEdges, clusterNode));

        // TODO: T230569108: Make clustering codec output handling more generic
        size_t clusteredOutIdx = 0;
        if (clustered.nbEdges == 2) {
            // The first edge of concat is numeric of the size of each input
            // stream
            // TODO: These numeric streams can be concat together across all
            // clusters. It is worth checking if this is worthwhile at the stage
            // of benchmarking the testing corpus
            ZL_RET_R_IF_ERR(ZL_Edge_setDestination(
                    clustered.edges[0], ZL_GRAPH_FIELD_LZ));
            clusteredOutIdx = 1;
        }

        // The second edge goes to the custom successor
        ZL_RET_R_IF_ERR(ZL_Edge_setDestination(
                clustered.edges[clusteredOutIdx], successor));
    }
    return ZL_returnSuccess();
}

ZL_RESULT_DECLARE_TYPE(Tag);

static ZL_RESULT_OF(Tag) getTagForEdge(const ZL_Edge* edge)
{
    const ZL_Input* input = ZL_Edge_getData(edge);
    ZL_IntMetadata metadata =
            ZL_Input_getIntMetadata(input, ZL_CLUSTERING_TAG_METADATA_ID);
    ZL_RET_T_IF(Tag, node_invalid_input, !metadata.isPresent);
    Tag tag = { .tag       = metadata.mValue,
                .typeWidth = { .eltWidth = ZL_Input_eltWidth(input),
                               .type     = ZL_Input_type(input) } };
    return ZL_RESULT_WRAP_VALUE(Tag, tag);
}

/**
 * Populates clusterInfos and tagToCluster map for unconfigured tags. Clusters
 * edges with the same tag together.
 *
 * @return the total number of clusterInfos that are populated after this call.
 */
static ZL_Report setClusterInfosUnconfigured_byTag(
        ZL_ErrorContext* errCtx,
        ClusterInfo* clusterInfos,
        size_t nbClusters,
        ZL_Edge* inputs[],
        size_t nbInputs,
        TagToClusterMap* tagToClusterMap,
        const TypeToSuccessorMap* defaultSuccessors,
        const ZL_GraphIDList* successors,
        const ZL_NodeIDList* nodes)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    size_t nbConfigured = nbClusters;

    for (size_t i = 0; i < nbInputs; i++) {
        ZL_TRY_LET_T(Tag, tag, getTagForEdge(inputs[i]));
        TagToClusterMap_Insert status = TagToClusterMap_insertVal(
                tagToClusterMap, (TagToClusterMap_Entry){ tag, nbClusters });
        ZL_RET_R_IF(allocation, status.badAlloc);

        size_t idx = status.ptr->val;
        // Skip if configured cluster
        if (idx < nbConfigured)
            continue;
        if (!status.inserted) {
            clusterInfos[idx].nbEdges++;
            continue;
        }
        // Create new cluster
        clusterInfos[idx].nbEdges = 1;
        ZL_TRY_SET_T(
                ZL_NodeID,
                clusterInfos[idx].node,
                getDefaultClusteringCodec(
                        errCtx, tag.typeWidth, defaultSuccessors, nodes));
        ZL_TRY_SET_T(
                ZL_GraphID,
                clusterInfos[idx].successor,
                getDefaultSuccessor(
                        errCtx, tag.typeWidth, defaultSuccessors, successors));
        nbClusters++;
    }

    return ZL_returnValue(nbClusters);
}

/**
 * Populates clusterInfos and tagToCluster map for configured tags.
 *
 * @return the total number of cluster infos that are populated after this call.
 */
static ZL_Report setClusterInfosConfigured(
        ZL_ErrorContext* errCtx,
        ClusterInfo* clusterInfos,
        ZL_Edge* inputs[],
        size_t nbInputs,
        ZL_ClusteringConfig* config,
        TagToClusterMap* tagToCluster,
        const ZL_GraphIDList* successors,
        const ZL_NodeIDList* nodes)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    // Populate the clusters that are present in the config
    for (size_t i = 0; i < config->nbClusters; i++) {
        ZL_ClusteringConfig_Cluster cluster = config->clusters[i];
        TypeWidth typeWidth                 = { cluster.typeSuccessor.type,
                                                cluster.typeSuccessor.eltWidth };

        for (size_t j = 0; j < cluster.nbMemberTags; j++) {
            Tag tag = { .tag = cluster.memberTags[j], .typeWidth = typeWidth };
            TagToClusterMap_Insert status = TagToClusterMap_insertVal(
                    tagToCluster, (TagToClusterMap_Entry){ tag, i });
            ZL_RET_R_IF(allocation, status.badAlloc);
            // Checks that for clusters of the same type, a tag does not appear
            // twice
            ZL_RET_R_IF(node_invalid_input, !status.inserted);
        }
        clusterInfos[i].nbEdges = 0;
        ZL_ERR_IF_GE(
                cluster.typeSuccessor.successorIdx,
                successors->nbGraphIDs,
                node_invalid_input,
                "Successor index out of range for cluster");
        ZL_ERR_IF_GE(
                cluster.typeSuccessor.clusteringCodecIdx,
                nodes->nbNodeIDs,
                node_invalid_input,
                "Cluster codec index out of range for cluster");
        clusterInfos[i].node =
                nodes->nodeids[cluster.typeSuccessor.clusteringCodecIdx];
        clusterInfos[i].successor =
                successors->graphids[cluster.typeSuccessor.successorIdx];
    }
    // Count the number of edges in each cluster
    for (size_t i = 0; i < nbInputs; i++) {
        ZL_TRY_LET_T(Tag, tag, getTagForEdge(inputs[i]));
        const TagToClusterMap_Entry* entry =
                TagToClusterMap_findVal(tagToCluster, tag);
        if (entry == NULL) {
            continue; // Unconfigured tag
        }
        clusterInfos[entry->val].nbEdges++;
    }
    return ZL_returnValue(config->nbClusters);
}

/*
 * A graph that clusters inputs according to the specified config. This means
 * that inputs specified by the config to be part of the same cluster are sent
 * to the concat graph. Throws an error if the config is invalid, or any other
 * compression error happens.
 *
 * The inputs to the clustering graph are expected to
 * have metadata specifying their tags, and inputs with tags matching the
 * memberTags of the clusteringConfig are members of that cluster. A member tag
 * present in the config may or may not be present in the inputs. If the tag of
 * an input is not contained in any clusters, the input can be clustered in an
 * online fashion. The current behavior is that every such input is sent to its
 * own cluster, and sent to a default successor according to its type.
 */
static ZL_Report graph_compressClusteredImpl(
        ZL_Graph* graph,
        ZL_Edge* inputs[],
        size_t nbInputs,
        ZL_ClusteringConfig* config,
        TagToClusterMap* tagToClusterIdxMap,
        TypeToSuccessorMap* defaultSuccessors)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_GraphIDList succList            = ZL_Graph_getCustomGraphs(graph);
    ZL_NodeIDList clusteringCodecsList = ZL_Graph_getCustomNodes(graph);
    ZL_RET_R_IF_ERR(validateClusteredConfig(graph, config, &succList));

    // Initialize cluster infos
    size_t maxNbClusters      = nbInputs + config->nbClusters;
    ClusterInfo* clusterInfos = ZL_Graph_getScratchSpace(
            graph, maxNbClusters * sizeof(ClusterInfo));

    // Cluster the configured inputs and initialize nbClusters
    ZL_TRY_LET_R(
            nbClusters,
            setClusterInfosConfigured(
                    ZL_ERR_CTX_PTR,
                    clusterInfos,
                    inputs,
                    nbInputs,
                    config,
                    tagToClusterIdxMap,
                    &succList,
                    &clusteringCodecsList));

    // Initialize maps for type defaults
    for (size_t i = 0; i < config->nbTypeDefaults; i++) {
        ZL_ClusteringConfig_TypeSuccessor* typeSuccesor =
                &config->typeDefaults[i];
        TypeWidth typeWidth = { typeSuccesor->type, typeSuccesor->eltWidth };
        TypeToSuccessorMap_Insert status = TypeToSuccessorMap_insertVal(
                defaultSuccessors,
                (TypeToSuccessorMap_Entry){ typeWidth, *typeSuccesor });
        ZL_RET_R_IF(allocation, status.badAlloc);
        // Checks that there are no duplicates among type defualts
        ZL_RET_R_IF(node_invalid_input, !status.inserted);
    }

    // Cluster unconfigured inputs and update nbClusters
    ZL_TRY_SET_R(
            nbClusters,
            setClusterInfosUnconfigured_byTag(
                    ZL_ERR_CTX_PTR,
                    clusterInfos,
                    nbClusters,
                    inputs,
                    nbInputs,
                    tagToClusterIdxMap,
                    defaultSuccessors,
                    &succList,
                    &clusteringCodecsList));

    // Group edges present by cluster
    ZL_Edge*** clusteredEdges =
            ZL_Graph_getScratchSpace(graph, nbClusters * sizeof(ZL_Edge**));
    ZL_RET_R_IF_NULL(allocation, clusteredEdges);
    size_t* clusterSizes =
            ZL_Graph_getScratchSpace(graph, nbClusters * sizeof(size_t));
    memset(clusterSizes, 0, nbClusters * sizeof(size_t));

    for (size_t i = 0; i < nbClusters; i++) {
        clusteredEdges[i] = ZL_Graph_getScratchSpace(
                graph, clusterInfos[i].nbEdges * sizeof(ZL_Edge*));
        ZL_RET_R_IF_NULL(allocation, clusteredEdges[i]);
    }

    // Group edges by cluster
    for (size_t i = 0; i < nbInputs; i++) {
        ZL_TRY_LET_T(Tag, tag, getTagForEdge(inputs[i]));
        const TagToClusterMap_Entry* entry =
                TagToClusterMap_findVal(tagToClusterIdxMap, tag);
        ZL_RET_R_IF_NULL(GENERIC, entry);
        size_t idx = entry->val;
        ZL_RET_R_IF_GT(GENERIC, clusterSizes[idx], clusterInfos[idx].nbEdges);
        clusteredEdges[idx][clusterSizes[idx]++] = inputs[i];
    }

    // Send clustered edges to their successors
    ZL_RET_R_IF_ERR(sendClustersToSuccessors(
            ZL_ERR_CTX_PTR, clusteredEdges, clusterInfos, nbClusters));
    return ZL_returnSuccess();
}

ZL_Report
graph_compressClustered(ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_TRY_LET_T(ZL_ClusteringConfig, config, graph_getClusteringConfig(graph));

    size_t maxNbTags = nbInputs;
    for (size_t i = 0; i < config.nbClusters; i++) {
        maxNbTags += config.clusters[i].nbMemberTags;
    }
    TagToClusterMap tagToClusterIdxMap =
            TagToClusterMap_create((uint32_t)maxNbTags);
    TypeToSuccessorMap typeToSuccessorDefaultsMap = TypeToSuccessorMap_create(
            (uint32_t)config.nbTypeDefaults * ZL_NUMBER_ELT_WIDTHS);
    ZL_Report report = graph_compressClusteredImpl(
            graph,
            inputs,
            nbInputs,
            &config,
            &tagToClusterIdxMap,
            &typeToSuccessorDefaultsMap);
    TypeToSuccessorMap_destroy(&typeToSuccessorDefaultsMap);
    TagToClusterMap_destroy(&tagToClusterIdxMap);
    return report;
}

ZL_GraphID ZL_Clustering_registerGraph(
        ZL_Compressor* compressor,
        const ZL_ClusteringConfig* config,
        const ZL_GraphID* successors,
        size_t nbSuccessors)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(compressor);
    const ZL_NodeID clusteringCodecs[4] = { ZL_NODE_CONCAT_SERIAL,
                                            ZL_NODE_CONCAT_STRUCT,
                                            ZL_NODE_CONCAT_NUMERIC,
                                            ZL_NODE_CONCAT_STRING };
    return ZL_Clustering_registerGraphWithCustomClusteringCodecs(
            compressor, config, successors, nbSuccessors, clusteringCodecs, 4);
}

ZL_GraphID ZL_Clustering_registerGraphWithCustomClusteringCodecs(
        ZL_Compressor* compressor,
        const ZL_ClusteringConfig* config,
        const ZL_GraphID* successors,
        size_t nbSuccessors,
        const ZL_NodeID* clusteringCodecs,
        size_t nbClusteringCodecs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(compressor);

    ZL_Report report = validateClusteringCodecIndices(
            ZL_ERR_CTX_PTR, config, nbClusteringCodecs);
    if (ZL_isError(report)) {
        ZL_DLOG(ERROR,
                "Error validating clustering codec indices: %s",
                ZL_OC_getErrorContextString(
                        ZL_ERR_CTX_PTR->opCtx, ZL_RES_error(report)));
        return ZL_GRAPH_ILLEGAL;
    }

    uint8_t* dst       = NULL;
    size_t dstSize     = 0;
    Arena* arena       = ALLOC_HeapArena_create();
    A1C_Arena a1cArena = A1C_Arena_wrap(arena);
    ZL_Report r        = ZL_Clustering_serializeClusteringConfig(
            ZL_ERR_CTX_PTR, &dst, &dstSize, config, &a1cArena);
    if (ZL_isError(r)) {
        // Free cbor serialization memory usage
        ALLOC_Arena_freeArena(arena);
        return ZL_GRAPH_ILLEGAL;
    }
    ZL_CopyParam configParam = (ZL_CopyParam){
        .paramId   = ZL_GENERIC_CLUSTERING_CONFIG_ID,
        .paramPtr  = dst,
        .paramSize = dstSize,
    };
    ZL_LocalParams clusteringParams = (ZL_LocalParams){
        .copyParams = { .copyParams = &configParam, .nbCopyParams = 1 },
    };
    ZL_ParameterizedGraphDesc const clusteringGraphDesc = {
        .graph          = ZL_GRAPH_CLUSTERING,
        .customGraphs   = successors,
        .nbCustomGraphs = nbSuccessors,
        .customNodes    = clusteringCodecs,
        .nbCustomNodes  = nbClusteringCodecs,
        .localParams    = &clusteringParams,
    };
    const ZL_GraphID clusteringGraph = ZL_Compressor_registerParameterizedGraph(
            compressor, &clusteringGraphDesc);

    // Free cbor serialization memory usage
    ALLOC_Arena_freeArena(arena);
    return clusteringGraph;
}
