// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/zl_compressor_serialization.h"

#include "openzl/zl_compressor.h"
#include "openzl/zl_reflection.h"

#include "openzl/shared/a1cbor.h"
#include "openzl/shared/string_view.h"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/allocation.h"
#include "openzl/common/assertion.h"
#include "openzl/common/limits.h"
#include "openzl/common/map.h"
#include "openzl/common/operation_context.h"
#include "openzl/common/vector.h"

#include "openzl/compress/localparams.h"

////////////////////////////////////////
// Misc Utilities
////////////////////////////////////////

static ZL_RESULT_OF(StringView)
        mk_sv_n(Arena* const arena, const char* str, const size_t len)
{
    ZL_RESULT_DECLARE_SCOPE(StringView, NULL);
    char* buf = ALLOC_Arena_malloc(arena, len + 1);
    ZL_ERR_IF_NULL(buf, allocation);
    memcpy(buf, str, len);
    buf[len] = '\0';
    return ZL_WRAP_VALUE(StringView_init(buf, len));
}

static ZL_RESULT_OF(StringView) mk_sv(Arena* const arena, const char* str)
{
    const size_t len = strlen(str);
    return mk_sv_n(arena, str, len);
}

static ZL_RESULT_OF(StringView)
        mk_sv_strip_name_fragment(Arena* const arena, StringView sv)
{
    size_t len = sv.size;
    {
        // Strip off hash fragment, if present
        const char* fragment = memchr(sv.data, '#', len);
        if (fragment != NULL) {
            ZL_ASSERT_GE(fragment, sv.data);
            ZL_ASSERT_LT(fragment, sv.data + sv.size);
            len = (size_t)(fragment - sv.data);
        }
    }
    return mk_sv_n(arena, sv.data, len);
}

static void assert_sv_nullterm(const StringView sv)
{
    if (sv.data == NULL) {
        ZL_ASSERT_EQ(sv.size, 0);
    } else {
        ZL_ASSERT_EQ(sv.data[sv.size], '\0');
    }
}

static void write_graph_type(A1C_Item* item, ZL_GraphType type)
{
    switch (type) {
        case ZL_GraphType_standard:
            A1C_Item_string_refCStr(item, "standard");
            return;
        case ZL_GraphType_static:
            A1C_Item_string_refCStr(item, "static");
            return;
        case ZL_GraphType_selector:
            A1C_Item_string_refCStr(item, "selector");
            return;
        case ZL_GraphType_function:
            A1C_Item_string_refCStr(item, "dynamic");
            return;
        case ZL_GraphType_multiInput:
            A1C_Item_string_refCStr(item, "multi_input");
            return;
        case ZL_GraphType_parameterized:
            A1C_Item_string_refCStr(item, "parameterized");
            return;
        case ZL_GraphType_segmenter:
            A1C_Item_string_refCStr(item, "segmenter");
            return;
        default:
            ZL_ASSERT_FAIL("Illegal graph type.");
            A1C_Item_string_refCStr(item, "unknown");
            return;
    }
}

ZL_RESULT_DECLARE_TYPE(ZL_GraphType);

static ZL_RESULT_OF(ZL_GraphType) read_graph_type(const A1C_Item* item)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphType, NULL);
    A1C_TRY_EXTRACT_STRING(str, item);
    const StringView val = StringView_initFromA1C(str);
    if (StringView_eqCStr(&val, "standard")) {
        return ZL_RESULT_WRAP_VALUE(ZL_GraphType, ZL_GraphType_standard);
    }
    if (StringView_eqCStr(&val, "static")) {
        return ZL_RESULT_WRAP_VALUE(ZL_GraphType, ZL_GraphType_static);
    }
    if (StringView_eqCStr(&val, "selector")) {
        return ZL_RESULT_WRAP_VALUE(ZL_GraphType, ZL_GraphType_selector);
    }
    if (StringView_eqCStr(&val, "dynamic")) {
        return ZL_RESULT_WRAP_VALUE(ZL_GraphType, ZL_GraphType_function);
    }
    if (StringView_eqCStr(&val, "multi_input")) {
        return ZL_RESULT_WRAP_VALUE(ZL_GraphType, ZL_GraphType_multiInput);
    }
    if (StringView_eqCStr(&val, "parameterized")) {
        return ZL_RESULT_WRAP_VALUE(ZL_GraphType, ZL_GraphType_parameterized);
    }
    if (StringView_eqCStr(&val, "unknown")) {
        ZL_ERR(GENERIC, "Serializer emitted 'unknown' graph type!");
    }
    ZL_ERR(GENERIC, "Unknown graph type '%.*s'!", val.size, val.data);
}

////////////////////////////////////////
// Internal Param Set Representation
////////////////////////////////////////

typedef struct {
    int paramId;
    int value;
} CompressorSerializer_IntParam;

typedef struct {
    int paramId;
    StringView value;
    // TODO: Handle recording the value as an out-of-line reference.
} CompressorSerializer_BlobParam;

DECLARE_VECTOR_TYPE(CompressorSerializer_IntParam)
DECLARE_VECTOR_TYPE(CompressorSerializer_BlobParam)

typedef struct {
    VECTOR(CompressorSerializer_IntParam) int_params;
    VECTOR(CompressorSerializer_BlobParam) blob_params;
} CompressorSerializer_ParamSet;

ZL_RESULT_DECLARE_TYPE(CompressorSerializer_ParamSet);

static void CompressorSerializer_ParamSet_destroy(
        CompressorSerializer_ParamSet* ps)
{
    if (ps == NULL) {
        return;
    }
    VECTOR_DESTROY(ps->int_params);
    VECTOR_DESTROY(ps->blob_params);
}

static void CompressorSerializer_ParamSet_init(
        CompressorSerializer_ParamSet* ps)
{
    memset(ps, 0, sizeof(*ps));
    VECTOR_INIT(
            ps->int_params, ZL_COMPRESSOR_SERIALIZATION_PARAM_SET_PARAM_LIMIT);
    VECTOR_INIT(
            ps->blob_params, ZL_COMPRESSOR_SERIALIZATION_PARAM_SET_PARAM_LIMIT);
}

static ZL_Report CompressorSerializer_ParamSet_build_inner(
        ZL_OperationContext* const opCtx,
        CompressorSerializer_ParamSet* ps,
        const ZL_LocalParams* const lp)
{
    ZL_RESULT_DECLARE_SCOPE(size_t, opCtx);
    ZL_ERR_IF_NULL(lp, GENERIC);

    {
        // Deduplicate and sort the param list.
        int prevParamIdPlusOne = INT_MIN;
        while (1) {
            const ZL_IntParam* curParam = NULL;
            int curParamId              = INT_MAX;
            for (size_t i = 0; i < lp->intParams.nbIntParams; i++) {
                const ZL_IntParam* const p = &lp->intParams.intParams[i];
                if (p->paramId >= prevParamIdPlusOne
                    && p->paramId <= curParamId) {
                    if (p->paramId < curParamId || curParam == NULL) {
                        curParam = p;
                    }
                    curParamId = p->paramId;
                }
            }

            if (curParam != NULL) {
                const CompressorSerializer_IntParam ip =
                        (CompressorSerializer_IntParam){
                            .paramId = curParam->paramId,
                            .value   = curParam->paramValue,
                        };
                ZL_ERR_IF(!VECTOR_PUSHBACK(ps->int_params, ip), allocation);
            }

            if (curParamId == INT_MAX) {
                break;
            }
            prevParamIdPlusOne = curParamId + 1;
        }
    }

    {
        // Deduplicate and sort the param list.
        int prevParamIdPlusOne = INT_MIN;
        while (1) {
            const ZL_CopyParam* curParam = NULL;
            int curParamId               = INT_MAX;
            for (size_t i = 0; i < lp->copyParams.nbCopyParams; i++) {
                const ZL_CopyParam* const p = &lp->copyParams.copyParams[i];
                if (p->paramId >= prevParamIdPlusOne
                    && p->paramId <= curParamId) {
                    if (p->paramId < curParamId || curParam == NULL) {
                        curParam = p;
                    }
                    curParamId = p->paramId;
                }
            }

            if (curParam != NULL) {
                const CompressorSerializer_BlobParam bp =
                        (CompressorSerializer_BlobParam){
                            .paramId = curParam->paramId,
                            .value   = StringView_init(
                                    curParam->paramPtr, curParam->paramSize),
                        };
                ZL_ERR_IF(!VECTOR_PUSHBACK(ps->blob_params, bp), allocation);
            }

            if (curParamId == INT_MAX) {
                break;
            }
            prevParamIdPlusOne = curParamId + 1;
        }
    }

    return ZL_returnSuccess();
}

static ZL_RESULT_OF(CompressorSerializer_ParamSet)
        CompressorSerializer_ParamSet_build(
                ZL_OperationContext* const opCtx,
                const ZL_LocalParams* const lp)
{
    ZL_RESULT_DECLARE_SCOPE(CompressorSerializer_ParamSet, opCtx);
    CompressorSerializer_ParamSet ps;
    CompressorSerializer_ParamSet_init(&ps);

    ZL_Report result =
            CompressorSerializer_ParamSet_build_inner(opCtx, &ps, lp);
    if (ZL_RES_isError(result)) {
        CompressorSerializer_ParamSet_destroy(&ps);
        ZL_ERR_IF_ERR(result);
    }

    return ZL_WRAP_VALUE(ps);
}

ZL_DECLARE_PREDEF_MAP_TYPE(
        CompressorSerializer_ParamSetMap,
        StringView,
        CompressorSerializer_ParamSet);

ZL_DECLARE_PREDEF_MAP_TYPE(
        CompressorSerializer_ParamSetCanonicalizationMap,
        ZL_LocalParams,
        StringView);

////////////////////////////////////////
// Intermediate Node Representation
////////////////////////////////////////

typedef struct {
    StringView node_name;
    StringView base_node_name;
    StringView param_set_name;
} CompressorSerializer_Node;

////////////////////////////////////////
// Intermediate Graph Representation
////////////////////////////////////////

DECLARE_VECTOR_TYPE(StringView)

typedef struct {
    StringView graph_name;

    ZL_GraphType graph_type;

    // For static graphs, the name of the head node codec.
    // For function-based graphs, the name of the base on which this is a
    // modification.
    StringView base_name;

    VECTOR(StringView) successor_nodes;
    VECTOR(StringView) successor_graphs;

    StringView param_set_name;
} CompressorSerializer_Graph;

static void CompressorSerializer_Graph_destroy(CompressorSerializer_Graph* g)
{
    VECTOR_DESTROY(g->successor_nodes);
    VECTOR_DESTROY(g->successor_graphs);
}

static ZL_Report CompressorSerializer_Graph_init(CompressorSerializer_Graph* g)
{
    VECTOR_INIT(
            g->successor_nodes,
            ZL_COMPRESSOR_SERIALIZATION_GRAPH_CUSTOM_NODE_LIMIT);
    VECTOR_INIT(
            g->successor_graphs,
            ZL_COMPRESSOR_SERIALIZATION_GRAPH_CUSTOM_GRAPH_LIMIT);
    return ZL_returnSuccess();
}

ZL_DECLARE_PREDEF_MAP_TYPE(
        CompressorSerializer_NodeMap,
        StringView,
        CompressorSerializer_Node);

ZL_DECLARE_PREDEF_MAP_TYPE(
        CompressorSerializer_GraphMap,
        StringView,
        CompressorSerializer_Graph);

////////////////////////////////////////
// ZL_CompressorSerializer
////////////////////////////////////////

DECLARE_VECTOR_TYPE(ZL_IntParam)

// typedef'ed in the header
struct ZL_CompressorSerializer_s {
    // Owns all memory (other than the ZL_CompressorSerializer itself).
    Arena* arena;

    ZL_OperationContext opCtx;

    // Intermediate data structures in which we accumulate / preprocess the
    // necessary info before transforming it into the `A1C_Item` tree and
    // finally the serialized CBOR.
    CompressorSerializer_ParamSetCanonicalizationMap param_names;
    CompressorSerializer_ParamSetMap params;
    CompressorSerializer_NodeMap nodes;
    CompressorSerializer_GraphMap graphs;

    VECTOR(ZL_IntParam) global_params;

    // CBOR Root
    A1C_Arena a1c_arena;
    A1C_Item* root;

    // First-level nodes in the serialized tree.
    A1C_Item* params_root;
    A1C_Item* nodes_root;
    A1C_Item* graphs_root;
};

static void ZL_CompressorSerializer_destroy(
        ZL_CompressorSerializer* const state)
{
    if (state == NULL) {
        return;
    }
    VECTOR_DESTROY(state->global_params);
    CompressorSerializer_GraphMap_IterMut graph_it =
            CompressorSerializer_GraphMap_iterMut(&state->graphs);
    CompressorSerializer_GraphMap_Entry* graph_entry;
    while ((graph_entry = CompressorSerializer_GraphMap_IterMut_next(&graph_it))
           != NULL) {
        CompressorSerializer_Graph_destroy(&graph_entry->val);
    }
    CompressorSerializer_GraphMap_destroy(&state->graphs);
    CompressorSerializer_NodeMap_destroy(&state->nodes);
    CompressorSerializer_ParamSetMap_IterMut param_set_it =
            CompressorSerializer_ParamSetMap_iterMut(&state->params);
    CompressorSerializer_ParamSetMap_Entry* param_set_entry;
    while ((param_set_entry = CompressorSerializer_ParamSetMap_IterMut_next(
                    &param_set_it))
           != NULL) {
        CompressorSerializer_ParamSet_destroy(&param_set_entry->val);
    }
    CompressorSerializer_ParamSetMap_destroy(&state->params);
    CompressorSerializer_ParamSetCanonicalizationMap_destroy(
            &state->param_names);
    ZL_OC_destroy(&state->opCtx);
    ALLOC_Arena_freeArena(state->arena);

    memset(state, 0, sizeof(*state));
}

static ZL_Report ZL_CompressorSerializer_init(
        ZL_CompressorSerializer* const state)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    ZL_ERR_IF_NULL(state, GENERIC);
    memset(state, 0, sizeof(*state));
    state->arena = ALLOC_HeapArena_create();
    if (state->arena == NULL) {
        ZL_CompressorSerializer_destroy(state);
        ZL_ERR_IF_NULL(state->arena, allocation);
    }

    ZL_OC_init(&state->opCtx);

    state->param_names =
            CompressorSerializer_ParamSetCanonicalizationMap_create(
                    ZL_COMPRESSOR_SERIALIZATION_PARAM_SET_LIMIT);
    state->params = CompressorSerializer_ParamSetMap_create(
            ZL_COMPRESSOR_SERIALIZATION_PARAM_SET_LIMIT);
    state->nodes = CompressorSerializer_NodeMap_create(
            ZL_COMPRESSOR_SERIALIZATION_NODE_COUNT_LIMIT);
    state->graphs =
            CompressorSerializer_GraphMap_create(ZL_ENCODER_GRAPH_LIMIT);

    VECTOR_INIT(
            state->global_params,
            ZL_COMPRESSOR_SERIALIZATION_PARAM_SET_PARAM_LIMIT);

    state->a1c_arena = A1C_Arena_wrap(state->arena);
    state->root      = A1C_Item_root(&state->a1c_arena);
    if (state->root == NULL) {
        ZL_CompressorSerializer_destroy(state);
        ZL_ERR_IF_NULL(state->root, allocation);
    }
    return ZL_returnSuccess();
}

ZL_CompressorSerializer* ZL_CompressorSerializer_create(void)
{
    ZL_CompressorSerializer* const state =
            malloc(sizeof(ZL_CompressorSerializer));
    if (state == NULL) {
        return NULL;
    }
    if (ZL_RES_isError(ZL_CompressorSerializer_init(state))) {
        return NULL;
    }
    return state;
}

void ZL_CompressorSerializer_free(ZL_CompressorSerializer* const state)
{
    ZL_CompressorSerializer_destroy(state);
    free(state);
}

ZL_CONST_FN
ZL_OperationContext* ZL_CompressorSerializer_getOperationContext(
        ZL_CompressorSerializer* ctx)
{
    if (ctx == NULL) {
        return NULL;
    }
    return &ctx->opCtx;
}

/**
 * Resolves (admittedly extremely unlikely) hash collisions with other param
 * sets.
 */
static ZL_RESULT_OF(StringView) ZL_CompressorSerializer_nameParamSet(
        ZL_CompressorSerializer* const state,
        const ZL_LocalParams* const local_params)
{
    ZL_RESULT_DECLARE_SCOPE(StringView, state);
    size_t hash  = ZL_LocalParams_hash(local_params);
    int disambig = 0;
    while (true) {
        char buf[16 + 1 + 9 + 1];
        memset(buf, 0, sizeof(buf));
        int len;
        if (disambig == 0) {
            len = snprintf(
                    buf,
                    sizeof(buf),
                    "%0*llx",
                    (int)sizeof(hash) * 2,
                    (unsigned long long)hash);
        } else {
            len = snprintf(
                    buf,
                    sizeof(buf),
                    "%0*llx_%d",
                    (int)sizeof(hash) * 2,
                    (unsigned long long)hash,
                    disambig);
        }
        ZL_ERR_IF_LT(len, 0, GENERIC);
        ZL_ERR_IF_GT(len, (int)sizeof(buf), allocation);
        StringView tmp_hash_sv = StringView_init(buf, (size_t)len);

        const CompressorSerializer_ParamSetMap_Entry* const entry =
                CompressorSerializer_ParamSetMap_find(
                        &state->params, &tmp_hash_sv);
        if (entry == NULL) {
            // We found an unused name.
            ZL_TRY_LET_CONST(
                    StringView, sv, mk_sv_n(state->arena, buf, (size_t)len));
            return ZL_WRAP_VALUE(sv);
        }
        disambig++;
    }
}

static ZL_RESULT_OF(StringView) ZL_CompressorSerializer_recordParamSet(
        ZL_CompressorSerializer* const state,
        const ZL_LocalParams* const local_params)
{
    ZL_RESULT_DECLARE_SCOPE(StringView, state);

    {
        // Check if it's already been stored.
        const CompressorSerializer_ParamSetCanonicalizationMap_Entry* const
                entry = CompressorSerializer_ParamSetCanonicalizationMap_find(
                        &state->param_names, local_params);
        if (entry != NULL) {
            ZL_ASSERT(ZL_LocalParams_eq(local_params, &entry->key));
            return ZL_WRAP_VALUE(entry->val);
        }
    }

    ZL_TRY_LET_CONST(
            StringView,
            param_set_name,
            ZL_CompressorSerializer_nameParamSet(state, local_params));

    const ZL_RESULT_OF(CompressorSerializer_ParamSet) params_res =
            CompressorSerializer_ParamSet_build(&state->opCtx, local_params);
    ZL_ERR_IF_ERR(params_res);

    {
        // Record association from the name we've picked to the param set.
        const CompressorSerializer_ParamSetMap_Entry params_entry =
                (CompressorSerializer_ParamSetMap_Entry){
                    .key = param_set_name,
                    .val = ZL_RES_value(params_res),
                };

        const CompressorSerializer_ParamSetMap_Insert params_insert =
                CompressorSerializer_ParamSetMap_insert(
                        &state->params, &params_entry);
        ZL_ERR_IF(params_insert.badAlloc, allocation);
        ZL_ERR_IF(!params_insert.inserted, GENERIC);
    }

    {
        // Record disambiguation entry mapping the local params to the name
        // we've assigned to them.
        const CompressorSerializer_ParamSetCanonicalizationMap_Entry
                canonicalization_entry =
                        (CompressorSerializer_ParamSetCanonicalizationMap_Entry){
                            .key = *local_params,
                            .val = param_set_name,
                        };

        const CompressorSerializer_ParamSetCanonicalizationMap_Insert
                canonicalization_insert =
                        CompressorSerializer_ParamSetCanonicalizationMap_insert(
                                &state->param_names, &canonicalization_entry);
        ZL_ERR_IF(canonicalization_insert.badAlloc, allocation);
        ZL_ERR_IF(!canonicalization_insert.inserted, GENERIC);
    }

    return ZL_WRAP_VALUE(param_set_name);
}

static ZL_Report CompressorSerializer_serializeGraph_cb(
        void* const opaque,
        const ZL_Compressor* const c,
        const ZL_GraphID gid)
{
    ZL_CompressorSerializer* const state = (ZL_CompressorSerializer*)opaque;
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    ZL_GraphType graph_type = ZL_Compressor_getGraphType(c, gid);
    if (graph_type == ZL_GraphType_segmenter) {
        // Check the base graph Id's graph type
        const ZL_GraphID base_gid = ZL_Compressor_Graph_getBaseGraphID(c, gid);
        if (base_gid.gid != ZL_GRAPH_ILLEGAL.gid) {
            // This is actually a parameterized graph, not a segmenter.
            graph_type = ZL_GraphType_parameterized;
        }
    }
    switch (graph_type) {
        case ZL_GraphType_standard:
        case ZL_GraphType_selector:
        case ZL_GraphType_function:
        case ZL_GraphType_multiInput:
        case ZL_GraphType_segmenter:
            // These types of graphs are non-serializable!

            // TODO: require that they have explicit names?
            return ZL_returnSuccess();
        case ZL_GraphType_static:
        case ZL_GraphType_parameterized:
            // These are serializable. We can proceed.
            break;
    }

    const char* const name = ZL_Compressor_Graph_getName(c, gid);
    ZL_ERR_IF_NULL(name, GENERIC, "Unnamed graph %d!", gid.gid);
    ZL_TRY_LET_CONST(StringView, name_sv, mk_sv(state->arena, name));

    CompressorSerializer_Graph* info;
    {
        CompressorSerializer_GraphMap_Entry entry;
        entry.key = name_sv;
        CompressorSerializer_GraphMap_Insert insert =
                CompressorSerializer_GraphMap_insert(&state->graphs, &entry);
        ZL_ERR_IF(
                insert.badAlloc,
                allocation,
                "Failed to insert entry into graph map!");
        ZL_ERR_IF(
                !insert.inserted,
                GENERIC,
                "Failed to insert entry into graph map!");
        info = &insert.ptr->val;
    }

    ZL_ERR_IF_ERR(CompressorSerializer_Graph_init(info));
    info->graph_name = name_sv;

    bool write_params = true;
    const ZL_LocalParams local_params =
            ZL_Compressor_Graph_getLocalParams(c, gid);

    info->graph_type = graph_type;
    switch (info->graph_type) {
        case ZL_GraphType_static: {
            const ZL_NodeID head_nid = ZL_Compressor_Graph_getHeadNode(c, gid);
            ZL_ERR_IF_EQ(head_nid.nid, ZL_NODE_ILLEGAL.nid, corruption);
            const char* const head_name =
                    ZL_Compressor_Node_getName(c, head_nid);
            ZL_ERR_IF_NULL(head_name, corruption);
            ZL_TRY_LET_CONST(
                    StringView, head_name_sv, mk_sv(state->arena, head_name));
            info->base_name = head_name_sv;

            {
                // Check that the graph's local params are identical to the
                // head node's params, which should always be the case, because
                // they are actually the same thing in Zstrong core.
                const ZL_LocalParams head_node_local_params =
                        ZL_Compressor_Node_getLocalParams(c, head_nid);
                if (ZL_LocalParams_eq(&local_params, &head_node_local_params)) {
                    write_params = false;
                }
            }

            const ZL_GraphIDList gids =
                    ZL_Compressor_Graph_getSuccessors(c, gid);
            for (size_t i = 0; i < gids.nbGraphIDs; i++) {
                ZL_GraphID successor_gid = gids.graphids[i];
                const char* const successor_name =
                        ZL_Compressor_Graph_getName(c, successor_gid);
                ZL_ERR_IF_NULL(
                        successor_name,
                        GENERIC,
                        "Unnamed successor graph %d to graph '%s'!",
                        successor_gid.gid,
                        info->graph_name.data);
                const StringView successor_name_sv =
                        StringView_initFromCStr(successor_name);
                ZL_ERR_IF(
                        !VECTOR_PUSHBACK(
                                info->successor_graphs, successor_name_sv),
                        allocation);
            }
            break;
        }
        case ZL_GraphType_parameterized: {
            const ZL_GraphID base_gid =
                    ZL_Compressor_Graph_getBaseGraphID(c, gid);
            ZL_ERR_IF_EQ(base_gid.gid, ZL_GRAPH_ILLEGAL.gid, corruption);
            const char* base_graph_name =
                    ZL_Compressor_Graph_getName(c, base_gid);
            ZL_ERR_IF_NULL(base_graph_name, corruption);
            ZL_TRY_LET_CONST(
                    StringView,
                    base_graph_name_sv,
                    mk_sv(state->arena, base_graph_name));
            info->base_name = base_graph_name_sv;

            {
                // Validate that this graph and the graph it's based on have
                // the same non-serializable params.
                const ZL_LocalParams base_graph_local_params =
                        ZL_Compressor_Graph_getLocalParams(c, base_gid);
                ZL_ERR_IF(
                        !ZL_LocalRefParams_eq(
                                &local_params.refParams,
                                &base_graph_local_params.refParams),
                        graph_nonserializable,
                        "Graph '%.*s' has different refParams than the graph "
                        "from which it's built, '%.*s'. Because refParams are "
                        "non-serializable, changes to them compared to the "
                        "base graph makes this graph unserializable.",
                        name_sv.size,
                        name_sv.data,
                        base_graph_name_sv.size,
                        base_graph_name_sv.data);
                // TODO: maybe just drop this graph from serialization in this
                // case and require that it has an explicit name (i.e., make it
                // the user's problem)?
            }

            const ZL_GraphIDList gids =
                    ZL_Compressor_Graph_getCustomGraphs(c, gid);
            for (size_t i = 0; i < gids.nbGraphIDs; i++) {
                ZL_GraphID successor_gid = gids.graphids[i];
                const char* const successor_name =
                        ZL_Compressor_Graph_getName(c, successor_gid);
                ZL_ERR_IF_NULL(
                        successor_name,
                        GENERIC,
                        "Unnamed custom graph %d in graph '%s'!",
                        successor_gid.gid,
                        info->graph_name.data);
                const StringView successor_name_sv =
                        StringView_initFromCStr(successor_name);
                ZL_ERR_IF(
                        !VECTOR_PUSHBACK(
                                info->successor_graphs, successor_name_sv),
                        allocation);
            }
            const ZL_NodeIDList nids =
                    ZL_Compressor_Graph_getCustomNodes(c, gid);
            for (size_t i = 0; i < nids.nbNodeIDs; i++) {
                const ZL_NodeID successor_nid = nids.nodeids[i];
                const char* const successor_name =
                        ZL_Compressor_Node_getName(c, successor_nid);
                ZL_ERR_IF_NULL(
                        successor_name,
                        GENERIC,
                        "Unnamed custom node %d in graph '%s'!",
                        successor_nid.nid,
                        info->graph_name.data);
                const StringView successor_name_sv =
                        StringView_initFromCStr(successor_name);
                ZL_ERR_IF(
                        !VECTOR_PUSHBACK(
                                info->successor_nodes, successor_name_sv),
                        allocation);
            }
            break;
        }
        case ZL_GraphType_standard:
        case ZL_GraphType_selector:
        case ZL_GraphType_function:
        case ZL_GraphType_multiInput:
        case ZL_GraphType_segmenter:
            ZL_ERR(logicError, "Should already have bailed!");
        default:
            ZL_ERR(GENERIC,
                   "Invalid graph type for graph \"%s\"!",
                   info->graph_name);
    }

    if (write_params) {
        ZL_TRY_LET_CONST(
                StringView,
                param_set_name,
                ZL_CompressorSerializer_recordParamSet(state, &local_params));
        info->param_set_name = param_set_name;
    } else {
        info->param_set_name = StringView_init(NULL, 0);
    }

    return ZL_returnSuccess();
}

static ZL_Report CompressorSerializer_serializeNode_cb(
        void* const opaque,
        const ZL_Compressor* const c,
        const ZL_NodeID nid)
{
    ZL_CompressorSerializer* const state = (ZL_CompressorSerializer*)opaque;
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    CompressorSerializer_Node info = { 0 };

    const ZL_NodeID base_nid = ZL_Compressor_Node_getBaseNodeID(c, nid);
    {
        const char* name = ZL_Compressor_Node_getName(c, nid);

        {
            ZL_ERR_IF_NULL(name, GENERIC, "Unnamed node %d!", nid.nid);
            ZL_TRY_LET_CONST(StringView, name_sv, mk_sv(state->arena, name));
            info.node_name = name_sv;
        }

        if (base_nid.nid != ZL_NODE_ILLEGAL.nid) {
            const char* base_name = ZL_Compressor_Node_getName(c, base_nid);
            ZL_ERR_IF_NULL(
                    base_name, GENERIC, "Unnamed base node %d!", base_nid.nid);
            ZL_TRY_LET_CONST(
                    StringView, base_name_sv, mk_sv(state->arena, base_name));
            info.base_node_name = base_name_sv;
        } else {
            ZL_ERR_IF_NN(
                    strstr(name, "#"),
                    graph_nonserializable,
                    "Non-serializable node '%s' (a node with no base node) "
                    "does not have an explicit name! In order for a compressor "
                    "to be round-trippable, non-serializable nodes must be "
                    "pre-registered under the same name that they had on the "
                    "compressor that was serialized. But this non-serializable "
                    "node has an unstable name.",
                    name);
            // Node can't be serialized (it's a custom node that isn't a
            // serializable modification of an existing node). We just expect
            // the same node to be registered to the same name in the
            // compressor we eventually materialize into.
            return ZL_returnSuccess();
        }
    }

    const ZL_LocalParams lp = ZL_Compressor_Node_getLocalParams(c, nid);

    // Validate that base node has same non-serializable local params!
    if (base_nid.nid != ZL_NODE_ILLEGAL.nid) {
        const ZL_LocalParams base_lp =
                ZL_Compressor_Node_getLocalParams(c, base_nid);
        ZL_ERR_IF(
                !ZL_LocalRefParams_eq(&lp.refParams, &base_lp.refParams),
                graph_nonserializable,
                "Copied node '%s' has different ZL_LocalRefParam than the "
                "base node '%s' from which it was constructed. "
                "ZL_LocalRefParam can't be transported through a serialized "
                "graph and must be set up on the pre-registered nodes.",
                info.node_name.data,
                info.base_node_name.data);
    }

    ZL_TRY_LET_CONST(
            StringView,
            param_set_name,
            ZL_CompressorSerializer_recordParamSet(state, &lp));
    info.param_set_name = param_set_name;

    {
        const CompressorSerializer_NodeMap_Entry entry =
                (CompressorSerializer_NodeMap_Entry){
                    .key = info.node_name,
                    .val = info,
                };
        const CompressorSerializer_NodeMap_Insert insert_result =
                CompressorSerializer_NodeMap_insert(&state->nodes, &entry);
        ZL_ERR_IF(
                insert_result.badAlloc,
                allocation,
                "Failed to insert entry into node map!");
        ZL_ERR_IF(
                !insert_result.inserted,
                GENERIC,
                "Failed to insert entry into node map!");
    }
    return ZL_returnSuccess();
}

static ZL_Report CompressorSerializer_serializeCParam_cb(
        void* const opaque,
        const ZL_CParam key,
        const int val)
{
    ZL_CompressorSerializer* const state = (ZL_CompressorSerializer*)opaque;
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    const ZL_IntParam param = (ZL_IntParam){
        .paramId    = (int)key,
        .paramValue = val,
    };
    ZL_ERR_IF(!VECTOR_PUSHBACK(state->global_params, param), allocation);
    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorSerializer_encodeGlobalParams(
        ZL_CompressorSerializer* const state,
        A1C_Item* const global_params_item)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    const ZL_LocalParams local_params = (ZL_LocalParams){
        .intParams =
                (ZL_LocalIntParams){
                        .intParams   = VECTOR_DATA(state->global_params),
                        .nbIntParams = VECTOR_SIZE(state->global_params),
                },
        .copyParams =
                (ZL_LocalCopyParams){
                        .copyParams   = NULL,
                        .nbCopyParams = 0,
                },
        .refParams =
                (ZL_LocalRefParams){
                        .refParams   = NULL,
                        .nbRefParams = 0,
                },
    };
    ZL_TRY_LET_CONST(
            StringView,
            global_param_set_name,
            ZL_CompressorSerializer_recordParamSet(state, &local_params));
    A1C_Item_string_refStringView(global_params_item, global_param_set_name);

    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorSerializer_encodeParamSet(
        ZL_CompressorSerializer* const state,
        const CompressorSerializer_ParamSetMap_Entry* const entry,
        A1C_Pair* const param_set_pair)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    const CompressorSerializer_ParamSet* const ps = &entry->val;

    A1C_Item* const param_set_key_item = &param_set_pair->key;
    A1C_Item* const param_set_val_item = &param_set_pair->val;
    A1C_Item_string_refStringView(param_set_key_item, entry->key);
    A1C_Pair* const param_set_val_pairs =
            A1C_Item_map(param_set_val_item, 2, &state->a1c_arena);
    ZL_ERR_IF_NULL(param_set_val_pairs, allocation);

    A1C_Item* const int_params_key_item  = &param_set_val_pairs[0].key;
    A1C_Item* const int_params_val_item  = &param_set_val_pairs[0].val;
    A1C_Item* const blob_params_key_item = &param_set_val_pairs[1].key;
    A1C_Item* const blob_params_val_item = &param_set_val_pairs[1].val;

    A1C_Item_string_refCStr(int_params_key_item, "ints");
    A1C_Item_string_refCStr(blob_params_key_item, "blobs");

    const size_t int_param_count  = VECTOR_SIZE(ps->int_params);
    const size_t blob_param_count = VECTOR_SIZE(ps->blob_params);

    A1C_Pair* const int_param_pairs = A1C_Item_map(
            int_params_val_item, int_param_count, &state->a1c_arena);
    ZL_ERR_IF_NULL(int_param_pairs, allocation);

    A1C_Pair* const blob_param_pairs = A1C_Item_map(
            blob_params_val_item, blob_param_count, &state->a1c_arena);
    ZL_ERR_IF_NULL(blob_param_pairs, allocation);

    for (size_t i = 0; i < int_param_count; i++) {
        A1C_Item* const param_key_item = &int_param_pairs[i].key;
        A1C_Item* const param_val_item = &int_param_pairs[i].val;

        const CompressorSerializer_IntParam* const ip =
                &VECTOR_AT(ps->int_params, i);

        A1C_Item_int64(param_key_item, ip->paramId);
        A1C_Item_int64(param_val_item, ip->value);
    }

    for (size_t i = 0; i < blob_param_count; i++) {
        A1C_Item* const param_key_item = &blob_param_pairs[i].key;
        A1C_Item* const param_val_item = &blob_param_pairs[i].val;

        const CompressorSerializer_BlobParam* const bp =
                &VECTOR_AT(ps->blob_params, i);

        A1C_Item_int64(param_key_item, bp->paramId);
        A1C_Item_bytes_ref(
                param_val_item, (const uint8_t*)bp->value.data, bp->value.size);
    }

    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorSerializer_encodeParams(
        ZL_CompressorSerializer* const state)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    const size_t param_set_count =
            CompressorSerializer_ParamSetMap_size(&state->params);
    A1C_Pair* const param_set_pairs = A1C_Item_map(
            state->params_root, param_set_count, &state->a1c_arena);
    ZL_ERR_IF_NULL(param_set_pairs, allocation);
    size_t param_set_idx = 0;
    CompressorSerializer_ParamSetMap_Iter it =
            CompressorSerializer_ParamSetMap_iter(&state->params);
    const CompressorSerializer_ParamSetMap_Entry* entry;
    while ((entry = CompressorSerializer_ParamSetMap_Iter_next(&it)) != NULL) {
        ZL_ERR_IF_GE(param_set_idx, param_set_count, logicError);

        ZL_ERR_IF_ERR(ZL_CompressorSerializer_encodeParamSet(
                state, entry, &param_set_pairs[param_set_idx]));

        param_set_idx++;
    }
    ZL_ERR_IF_NE(param_set_idx, param_set_count, logicError);

    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorSerializer_encodeNode(
        ZL_CompressorSerializer* const state,
        const CompressorSerializer_NodeMap_Entry* const entry,
        A1C_Pair* const node_map_pair)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    const CompressorSerializer_Node* info = &entry->val;
    A1C_Item* const node_key_item         = &node_map_pair->key;
    A1C_Item* const node_val_item         = &node_map_pair->val;

    A1C_Item_string_refStringView(node_key_item, entry->key);
    const A1C_MapBuilder node_builder =
            A1C_Item_map_builder(node_val_item, 2, &state->a1c_arena);

    {
        A1C_MAP_TRY_ADD(pair, node_builder);
        A1C_Item_string_refCStr(&pair->key, "base");
        A1C_Item_string_refStringView(&pair->val, info->base_node_name);
    }

    {
        A1C_MAP_TRY_ADD(pair, node_builder);
        A1C_Item_string_refCStr(&pair->key, "params");
        A1C_Item_string_refStringView(&pair->val, info->param_set_name);
    }

    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorSerializer_encodeNodes(
        ZL_CompressorSerializer* const state)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    const size_t nodes_count = CompressorSerializer_NodeMap_size(&state->nodes);
    const A1C_MapBuilder node_map_builder = A1C_Item_map_builder(
            state->nodes_root, nodes_count, &state->a1c_arena);

    CompressorSerializer_NodeMap_Iter it =
            CompressorSerializer_NodeMap_iter(&state->nodes);
    const CompressorSerializer_NodeMap_Entry* entry;
    while ((entry = CompressorSerializer_NodeMap_Iter_next(&it)) != NULL) {
        A1C_MAP_TRY_ADD(pair, node_map_builder);

        ZL_ERR_IF_ERR(ZL_CompressorSerializer_encodeNode(state, entry, pair));
    }
    ZL_ERR_IF_NE(state->nodes_root->map.size, nodes_count, logicError);
    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorSerializer_encodeGraph(
        ZL_CompressorSerializer* const state,
        const CompressorSerializer_GraphMap_Entry* const entry,
        A1C_Pair* const graph_map_pair)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    const CompressorSerializer_Graph* const info = &entry->val;
    A1C_Item* const key                          = &graph_map_pair->key;
    A1C_Item* const val                          = &graph_map_pair->val;

    size_t num_pairs = 2; // type + params
    switch (info->graph_type) {
        case ZL_GraphType_standard:
            break;
        case ZL_GraphType_static:
            num_pairs += 2; // codec + successors
            break;
        case ZL_GraphType_parameterized:
            num_pairs += 3; // base + graphs + nodes
            break;
        case ZL_GraphType_selector:
        case ZL_GraphType_function:
        case ZL_GraphType_multiInput:
        case ZL_GraphType_segmenter:
        default:
            ZL_ERR(GENERIC,
                   "Invalid graph type for graph \"%s\"!",
                   info->graph_name);
    }

    A1C_Item_string_refStringView(key, entry->key);
    const A1C_MapBuilder builder =
            A1C_Item_map_builder(val, num_pairs, &state->a1c_arena);

    {
        A1C_MAP_TRY_ADD(pair, builder);
        A1C_Item_string_refCStr(&pair->key, "type");
        write_graph_type(&pair->val, info->graph_type);
    }

    switch (info->graph_type) {
        case ZL_GraphType_standard:
            break;
        case ZL_GraphType_static: {
            {
                A1C_MAP_TRY_ADD(pair, builder);
                A1C_Item_string_refCStr(&pair->key, "node");
                A1C_Item_string_refStringView(&pair->val, info->base_name);
            }
            {
                A1C_MAP_TRY_ADD(pair, builder);
                A1C_Item_string_refCStr(&pair->key, "successors");
                const size_t num_successors =
                        VECTOR_SIZE(info->successor_graphs);
                A1C_Item* const successor_items = A1C_Item_array(
                        &pair->val, num_successors, &state->a1c_arena);
                ZL_ERR_IF_NULL(successor_items, allocation);
                for (size_t i = 0; i < num_successors; i++) {
                    const StringView successor_name_sv =
                            VECTOR_AT(info->successor_graphs, i);
                    A1C_Item_string_refStringView(
                            &successor_items[i], successor_name_sv);
                }
            }
            break;
        }
        case ZL_GraphType_parameterized: {
            {
                A1C_MAP_TRY_ADD(pair, builder);
                A1C_Item_string_refCStr(&pair->key, "base");
                A1C_Item_string_refStringView(&pair->val, info->base_name);
            }
            {
                A1C_MAP_TRY_ADD(pair, builder);
                A1C_Item_string_refCStr(&pair->key, "graphs");
                const size_t num_graphs = VECTOR_SIZE(info->successor_graphs);
                A1C_Item* const graph_items = A1C_Item_array(
                        &pair->val, num_graphs, &state->a1c_arena);
                ZL_ERR_IF_NULL(graph_items, allocation);
                for (size_t i = 0; i < num_graphs; i++) {
                    const StringView graph_name_sv =
                            VECTOR_AT(info->successor_graphs, i);
                    A1C_Item_string_refStringView(
                            &graph_items[i], graph_name_sv);
                }
            }
            {
                A1C_MAP_TRY_ADD(pair, builder);
                A1C_Item_string_refCStr(&pair->key, "nodes");
                const size_t num_nodes     = VECTOR_SIZE(info->successor_nodes);
                A1C_Item* const node_items = A1C_Item_array(
                        &pair->val, num_nodes, &state->a1c_arena);
                ZL_ERR_IF_NULL(node_items, allocation);
                for (size_t i = 0; i < num_nodes; i++) {
                    const StringView node_name_sv =
                            VECTOR_AT(info->successor_nodes, i);
                    A1C_Item_string_refStringView(&node_items[i], node_name_sv);
                }
            }
            break;
        }
        case ZL_GraphType_selector:
        case ZL_GraphType_function:
        case ZL_GraphType_multiInput:
        case ZL_GraphType_segmenter: {
            ZL_ERR(logicError,
                   "Somehow got so confused that we are trying to encode \"%s\", which is not a serializable graph type!",
                   info->graph_name);
        }
        default:
            ZL_ERR(GENERIC,
                   "Invalid graph type for graph \"%s\"!",
                   info->graph_name);
    }

    {
        A1C_MAP_TRY_ADD(pair, builder);
        A1C_Item_string_refCStr(&pair->key, "params");
        if (info->param_set_name.data != NULL) {
            A1C_Item_string_refStringView(&pair->val, info->param_set_name);
        } else {
            A1C_Item_null(&pair->val);
        }
    }

    ZL_ERR_IF_NE(val->map.size, num_pairs, logicError);

    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorSerializer_encodeGraphs(
        ZL_CompressorSerializer* const state)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    const size_t graphs_count =
            CompressorSerializer_GraphMap_size(&state->graphs);
    const A1C_MapBuilder graph_map_builder = A1C_Item_map_builder(
            state->graphs_root, graphs_count, &state->a1c_arena);

    CompressorSerializer_GraphMap_Iter it =
            CompressorSerializer_GraphMap_iter(&state->graphs);
    const CompressorSerializer_GraphMap_Entry* entry;
    while ((entry = CompressorSerializer_GraphMap_Iter_next(&it)) != NULL) {
        A1C_MAP_TRY_ADD(pair, graph_map_builder);
        ZL_ERR_IF_ERR(ZL_CompressorSerializer_encodeGraph(state, entry, pair));
    }
    ZL_ERR_IF_NE(state->graphs_root->map.size, graphs_count, logicError);
    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorSerializer_setStartingGraph(
        ZL_CompressorSerializer* const state,
        const ZL_Compressor* const compressor,
        A1C_Item* starting_graph_item)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    ZL_GraphID starting_graph_id;
    if (ZL_Compressor_getStartingGraphID(compressor, &starting_graph_id)) {
        const char* starting_graph_name =
                ZL_Compressor_Graph_getName(compressor, starting_graph_id);
        ZL_ERR_IF_NULL(
                starting_graph_name,
                graph_invalid,
                "Couldn't retrieve name for starting graph ID");
        ZL_TRY_LET_CONST(
                StringView, sv, mk_sv(state->arena, starting_graph_name));
        A1C_Item_string_refStringView(starting_graph_item, sv);
    }
    return ZL_returnSuccess();
}

typedef struct {
    ZL_CompressorSerializer* const state;

    uint8_t* const buf;
    size_t pos;
    const size_t cap;
} ZL_CompressorSerializer_EncodingState;

static size_t serialize_encoder_write_cb(
        void* const opaque,
        const uint8_t* const data,
        const size_t size)
{
    ZL_CompressorSerializer_EncodingState* const encoding_state =
            (ZL_CompressorSerializer_EncodingState*)opaque;
    uint8_t* const cur = encoding_state->buf + encoding_state->pos;
    if (encoding_state->pos + size <= encoding_state->cap) {
        memcpy(cur, data, size);
    }
    encoding_state->pos += size;
    return size;
}

static ZL_Report ZL_CompressorSerializer_encodeInner(
        ZL_CompressorSerializer* const state,
        const A1C_Item* const root,
        void** const dst,
        size_t* const dstSize,
        size_t (*const sizeFunc)(const A1C_Item*),
        bool (*const encodeFunc)(A1C_Encoder*, const A1C_Item*),
        const bool null_term)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    ZL_ERR_IF_NULL(state, parameter_invalid);
    ZL_ERR_IF_NULL(root, parameter_invalid);
    ZL_ERR_IF_NULL(dst, parameter_invalid);
    ZL_ERR_IF_NULL(dstSize, parameter_invalid);
    ZL_ERR_IF_NULL(sizeFunc, parameter_invalid);
    ZL_ERR_IF_NULL(encodeFunc, parameter_invalid);

    const size_t encoded_size = sizeFunc(root);
    const size_t alloc_size   = encoded_size + null_term;

    uint8_t* buf;
    bool owns_buffer;
    if (*dst != NULL && *dstSize >= alloc_size) {
        owns_buffer = false;
        buf         = *dst;
    } else {
        owns_buffer = true;
        buf         = ALLOC_Arena_malloc(state->arena, alloc_size);
    }
    ZL_ERR_IF_NULL(buf, allocation);
    ZL_CompressorSerializer_EncodingState encoding_state =
            (ZL_CompressorSerializer_EncodingState){
                .state = state,
                .buf   = buf,
                .pos   = 0,
                .cap   = encoded_size,
            };

    {
        A1C_Encoder encoder;
        A1C_Encoder_init(&encoder, serialize_encoder_write_cb, &encoding_state);

        if (!encodeFunc(&encoder, root)) {
            if (owns_buffer) {
                ALLOC_Arena_free(state->arena, buf);
            }
            return ZL_WRAP_ERROR(A1C_Error_convert(
                    ZL_ERR_CTX_PTR, A1C_Encoder_getError(&encoder)));
        }
    }

    ZL_ERR_IF_NE(
            encoding_state.pos,
            encoded_size,
            GENERIC,
            "Serialized size (%lu) didn't end up being the size we expected (%lu).",
            encoding_state.pos,
            encoded_size);

    if (null_term) {
        encoding_state.buf[encoded_size] = '\0';
    }

    *dst     = encoding_state.buf;
    *dstSize = encoded_size;

    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorSerializer_encode(
        ZL_CompressorSerializer* const state,
        const A1C_Item* const root,
        void** const dst,
        size_t* const dstSize)
{
    return ZL_CompressorSerializer_encodeInner(
            state,
            root,
            dst,
            dstSize,
            A1C_Item_encodedSize,
            A1C_Encoder_encode,
            false);
}

static ZL_Report ZL_CompressorSerializer_encodeToJson(
        ZL_CompressorSerializer* const state,
        const A1C_Item* const root,
        void** const dst,
        size_t* const dstSize)
{
    return ZL_CompressorSerializer_encodeInner(
            state,
            root,
            dst,
            dstSize,
            A1C_Item_jsonSize,
            A1C_Encoder_json,
            true);
}

static ZL_Report ZL_CompressorSerializer_serializeInner(
        ZL_CompressorSerializer* const state,
        const ZL_Compressor* const c,
        void** const dst,
        size_t* const dstSize,
        ZL_Report (*const encoderFunc)(
                ZL_CompressorSerializer*,
                const A1C_Item*,
                void**,
                size_t*))
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    if (state != NULL) {
        ZL_OC_startOperation(&state->opCtx, ZL_Operation_serializeCompressor);
    }

    ZL_ERR_IF_NULL(state, parameter_invalid);

    // Extract info about components from compressor

    // Nodes
    ZL_ERR_IF_ERR(ZL_Compressor_forEachNode(
            c, CompressorSerializer_serializeNode_cb, state));

    // Graphs
    ZL_ERR_IF_ERR(ZL_Compressor_forEachGraph(
            c, CompressorSerializer_serializeGraph_cb, state));

    // Global Params
    ZL_ERR_IF_ERR(ZL_Compressor_forEachParam(
            c, CompressorSerializer_serializeCParam_cb, state));

    // Set up A1C_Item tree

    // Set up first-level nodes
    A1C_Item* version;
    A1C_Item* starting_graph;
    A1C_Item* global_params;
    {
        const A1C_MapBuilder root_map_builder =
                A1C_Item_map_builder(state->root, 6, &state->a1c_arena);
        {
            A1C_MAP_TRY_ADD(pair, root_map_builder);
            A1C_Item_string_refCStr(&pair->key, "version");
            version = &pair->val;
        }
        {
            A1C_MAP_TRY_ADD(pair, root_map_builder);
            A1C_Item_string_refCStr(&pair->key, "params");
            state->params_root = &pair->val;
        }
        {
            A1C_MAP_TRY_ADD(pair, root_map_builder);
            A1C_Item_string_refCStr(&pair->key, "nodes");
            state->nodes_root = &pair->val;
        }
        {
            A1C_MAP_TRY_ADD(pair, root_map_builder);
            A1C_Item_string_refCStr(&pair->key, "graphs");
            state->graphs_root = &pair->val;
        }
        {
            A1C_MAP_TRY_ADD(pair, root_map_builder);
            A1C_Item_string_refCStr(&pair->key, "start");
            starting_graph = &pair->val;
        }
        {
            A1C_MAP_TRY_ADD(pair, root_map_builder);
            A1C_Item_string_refCStr(&pair->key, "global_params");
            global_params = &pair->val;
        }
    }

    // Write Version
    A1C_Item_int64(version, ZL_LIBRARY_VERSION_NUMBER);

    // Write Global Params
    ZL_ERR_IF_ERR(
            ZL_CompressorSerializer_encodeGlobalParams(state, global_params));

    // Write Params
    ZL_ERR_IF_ERR(ZL_CompressorSerializer_encodeParams(state));

    // Write Nodes
    ZL_ERR_IF_ERR(ZL_CompressorSerializer_encodeNodes(state));

    // Write Graphs
    ZL_ERR_IF_ERR(ZL_CompressorSerializer_encodeGraphs(state));

    // Write Starting Graph
    ZL_ERR_IF_ERR(
            ZL_CompressorSerializer_setStartingGraph(state, c, starting_graph));

    // Encode A1C_Item Tree
    ZL_ERR_IF_NULL(encoderFunc, parameter_invalid);
    return encoderFunc(state, state->root, dst, dstSize);
}

ZL_Report ZL_CompressorSerializer_serialize(
        ZL_CompressorSerializer* const state,
        const ZL_Compressor* const c,
        void** const dst,
        size_t* const dstSize)
{
    return ZL_CompressorSerializer_serializeInner(
            state, c, dst, dstSize, ZL_CompressorSerializer_encode);
}

ZL_Report ZL_CompressorSerializer_serializeToJson(
        ZL_CompressorSerializer* const state,
        const ZL_Compressor* const c,
        void** const dst,
        size_t* const dstSize)
{
    return ZL_CompressorSerializer_serializeInner(
            state, c, dst, dstSize, ZL_CompressorSerializer_encodeToJson);
}

ZL_Report ZL_CompressorSerializer_convertToJson(
        ZL_CompressorSerializer* const state,
        void** const dst,
        size_t* const dstSize,
        const void* const src,
        size_t const srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    if (state != NULL) {
        ZL_OC_startOperation(&state->opCtx, ZL_Operation_serializeCompressor);
    }

    ZL_ERR_IF_NULL(state, parameter_invalid);

    return A1C_convert_cbor_to_json(
            ZL_ERR_CTX_PTR,
            state->arena,
            dst,
            dstSize,
            StringView_init(src, srcSize));
}

const char* ZL_CompressorSerializer_getErrorContextString(
        const ZL_CompressorSerializer* const state,
        const ZL_Report result)
{
    return ZL_CompressorSerializer_getErrorContextString_fromError(
            state, ZL_RES_error(result));
}

const char* ZL_CompressorSerializer_getErrorContextString_fromError(
        const ZL_CompressorSerializer* const state,
        const ZL_Error error)
{
    if (state == NULL) {
        return NULL;
    }
    if (!ZL_E_isError(error)) {
        return NULL;
    }
    return ZL_OC_getErrorContextString(&state->opCtx, error);
}

////////////////////////////////////////
// ZL_CompressorDeserializer
////////////////////////////////////////

ZL_DECLARE_PREDEF_MAP_TYPE(
        CompressorDeserializer_NameMap,
        StringView,
        StringView);

ZL_DECLARE_PREDEF_MAP_TYPE(
        CompressorDeserializer_ParamMap,
        StringView,
        ZL_LocalParams);

// typedef'ed in the header
struct ZL_CompressorDeserializer_s {
    // May be NULL!
    const ZL_Compressor* const_compressor;
    // May be NULL!
    ZL_Compressor* mut_compressor;

    Arena* arena; // Owns all working memory.

    ZL_OperationContext opCtx;

    // CBOR Root
    A1C_Arena a1c_arena;
    const A1C_Item* root;

    const A1C_Map* params;
    const A1C_Map* nodes;
    const A1C_Map* graphs;

    // Stores the stack of item indices that we have deferred processing while
    // we DFS down into setting up their prerequisites.
    VECTOR(size_t) pending;

    // Maps names in the serialized graph to the (possibly different) names
    // the corresponding components in the materialized graph have been
    // assigned.
    CompressorDeserializer_NameMap node_names;
    CompressorDeserializer_NameMap graph_names;

    CompressorDeserializer_ParamMap cached_params;
};

static void ZL_CompressorDeserializer_destroy(
        ZL_CompressorDeserializer* const state)
{
    if (state == NULL) {
        return;
    }

    CompressorDeserializer_ParamMap_destroy(&state->cached_params);

    CompressorDeserializer_NameMap_destroy(&state->graph_names);
    CompressorDeserializer_NameMap_destroy(&state->node_names);

    VECTOR_DESTROY(state->pending);

    ZL_OC_destroy(&state->opCtx);

    ALLOC_Arena_freeArena(state->arena);
    memset(state, 0, sizeof(*state));
}

static ZL_Report ZL_CompressorDeserializer_init(
        ZL_CompressorDeserializer* const state)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_NULL(state, GENERIC);

    memset(state, 0, sizeof(*state));
    state->arena = ALLOC_HeapArena_create();
    ZL_ERR_IF_NULL(state->arena, allocation);
    state->a1c_arena = A1C_Arena_wrap(state->arena);

    ZL_OC_init(&state->opCtx);
    ZL_RESULT_UPDATE_SCOPE_CONTEXT(state);

    VECTOR_INIT(state->pending, ZL_ENCODER_GRAPH_LIMIT);

    state->node_names =
            CompressorDeserializer_NameMap_create(ZL_ENCODER_GRAPH_LIMIT);
    state->graph_names =
            CompressorDeserializer_NameMap_create(ZL_ENCODER_GRAPH_LIMIT);

    state->cached_params =
            CompressorDeserializer_ParamMap_create(ZL_ENCODER_GRAPH_LIMIT);

    return ZL_returnSuccess();
}

ZL_CompressorDeserializer* ZL_CompressorDeserializer_create(void)
{
    ZL_CompressorDeserializer* const state =
            malloc(sizeof(ZL_CompressorDeserializer));
    if (state == NULL) {
        return NULL;
    }
    if (ZL_RES_isError(ZL_CompressorDeserializer_init(state))) {
        return NULL;
    }
    return state;
}

void ZL_CompressorDeserializer_free(ZL_CompressorDeserializer* const state)
{
    ZL_CompressorDeserializer_destroy(state);
    free(state);
}

ZL_CONST_FN
ZL_OperationContext* ZL_CompressorDeserializer_getOperationContext(
        ZL_CompressorDeserializer* ctx)
{
    if (ctx == NULL) {
        return NULL;
    }
    return &ctx->opCtx;
}

ZL_RESULT_DECLARE_TYPE(ZL_LocalParams);

static ZL_RESULT_OF(ZL_LocalParams) ZL_CompressorDeserializer_LocalParams_build(
        ZL_CompressorDeserializer* const state,
        const A1C_Map* const map)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_LocalParams, state);
    size_t int_param_count                    = 0;
    ZL_IntParam* int_params                   = NULL;
    const A1C_Item* const int_params_map_item = A1C_Map_get_cstr(map, "ints");
    if (int_params_map_item != NULL) {
        A1C_TRY_EXTRACT_MAP(int_params_map, int_params_map_item);

        int_param_count = int_params_map.size;
        int_params      = ALLOC_Arena_malloc(
                state->arena, int_param_count * sizeof(ZL_IntParam));
        ZL_ERR_IF_NULL(int_params, allocation);

        for (size_t i = 0; i < int_param_count; i++) {
            A1C_TRY_EXTRACT_INT64(int_param_key, &int_params_map.items[i].key);
            A1C_TRY_EXTRACT_INT64(int_param_val, &int_params_map.items[i].val);

            ZL_ERR_IF_GT(int_param_key, INT_MAX, nodeParameter_invalidValue);
            ZL_ERR_IF_LT(int_param_key, INT_MIN, nodeParameter_invalidValue);

            ZL_ERR_IF_GT(int_param_val, INT_MAX, nodeParameter_invalid);
            ZL_ERR_IF_LT(int_param_val, INT_MIN, nodeParameter_invalid);

            int_params[i].paramId    = (int)int_param_key;
            int_params[i].paramValue = (int)int_param_val;
        }
    }

    size_t blob_param_count                    = 0;
    ZL_CopyParam* blob_params                  = NULL;
    const A1C_Item* const blob_params_map_item = A1C_Map_get_cstr(map, "blobs");
    if (blob_params_map_item != NULL) {
        A1C_TRY_EXTRACT_MAP(blob_params_map, blob_params_map_item);

        blob_param_count = blob_params_map.size;
        blob_params      = ALLOC_Arena_malloc(
                state->arena, blob_param_count * sizeof(ZL_CopyParam));
        ZL_ERR_IF_NULL(blob_params, allocation);

        for (size_t i = 0; i < blob_param_count; i++) {
            A1C_TRY_EXTRACT_INT64(
                    blob_param_key, &blob_params_map.items[i].key);
            A1C_TRY_EXTRACT_BYTES(
                    blob_param_val, &blob_params_map.items[i].val);

            ZL_ERR_IF_GT(blob_param_key, INT_MAX, nodeParameter_invalidValue);
            ZL_ERR_IF_LT(blob_param_key, INT_MIN, nodeParameter_invalidValue);

            blob_params[i].paramId   = (int)blob_param_key;
            blob_params[i].paramPtr  = blob_param_val.data;
            blob_params[i].paramSize = blob_param_val.size;
        }
    }

    const ZL_LocalParams local_params = (ZL_LocalParams){
        .intParams =
                (ZL_LocalIntParams){
                        .intParams   = int_params,
                        .nbIntParams = int_param_count,
                },
        .copyParams =
                (ZL_LocalCopyParams){
                        .copyParams   = blob_params,
                        .nbCopyParams = blob_param_count,
                },
        .refParams =
                (ZL_LocalRefParams){
                        .refParams   = NULL,
                        .nbRefParams = 0,
                },
    };

    return ZL_WRAP_VALUE(local_params);
}

/**
 * Memoized (caching) function to materialize LocalParams by name.
 *
 * @note This does not transport / set up the non-serialized params, i.e.,
 *       `refParams`. You must set those up from the base node.
 */
static ZL_RESULT_OF(ZL_LocalParams)
        ZL_CompressorDeserializer_LocalParams_lookup(
                ZL_CompressorDeserializer* const state,
                const A1C_Item* const param_set_name_item)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_LocalParams, state);

    A1C_TRY_EXTRACT_STRING(param_set_name_str, param_set_name_item);
    const StringView param_set_name_sv =
            StringView_initFromA1C(param_set_name_str);

    {
        // If we've already materialized these params, just return that.
        const CompressorDeserializer_ParamMap_Entry* const entry =
                CompressorDeserializer_ParamMap_find(
                        &state->cached_params, &param_set_name_sv);
        if (entry != NULL) {
            const ZL_LocalParams cached_local_params = entry->val;
            return ZL_RESULT_WRAP_VALUE(ZL_LocalParams, cached_local_params);
        }
    }

    A1C_TRY_EXTRACT_MAP(
            param_set_map, A1C_Map_get(state->params, param_set_name_item));

    ZL_TRY_LET_CONST(
            ZL_LocalParams,
            local_params,
            ZL_CompressorDeserializer_LocalParams_build(state, &param_set_map));

    const CompressorDeserializer_ParamMap_Entry param_cache_entry =
            (CompressorDeserializer_ParamMap_Entry){
                .key = param_set_name_sv,
                .val = local_params,
            };
    const CompressorDeserializer_ParamMap_Insert param_cache_insert =
            CompressorDeserializer_ParamMap_insert(
                    &state->cached_params, &param_cache_entry);
    ZL_ERR_IF(param_cache_insert.badAlloc, allocation);
    ZL_ERR_IF(!param_cache_insert.inserted, logicError);

    return ZL_WRAP_VALUE(local_params);
}

typedef enum {
    ZL_CompressorDeserializer_ParamResolution_Absent,
    ZL_CompressorDeserializer_ParamResolution_Present,
} ZL_CompressorDeserializer_ParamResolution;

/**
 * Resolves an optional "params" field in the provided map to a ZL_LocalParams
 * object. The params field can be:
 *
 * - Absent: Resolves to no local params.
 * - Null: Resolves to no local params.
 * - A Map: The map is interpreted as an immediate/literal param set and is
 *   parsed and written to @p lp.
 * - A String: The string is assumed to identify a param set in the params map
 *   and is looked up there.
 *
 * @param resolution[out] Optional pointer to variable in which to report
 *                        whether a parameter set was resolved or whether it
 *                        wholly inherited from the base.
 *
 * @returns 0 if it successfully resolves to no params, 1 if params were
 *          successfully found and written, and an error otherwise.
 */
static ZL_RESULT_OF(ZL_LocalParams)
        ZL_CompressorDeserializer_LocalParams_resolve(
                ZL_CompressorDeserializer* const state,
                const A1C_Item* const params_value_item,
                const ZL_LocalParams* const base,
                ZL_CompressorDeserializer_ParamResolution* resolution)
{
    ZL_ASSERT_NN(state);
    ZL_RESULT_DECLARE_SCOPE(ZL_LocalParams, state);

    ZL_CompressorDeserializer_ParamResolution _dummy;
    if (resolution == NULL) {
        resolution = &_dummy;
    }

    ZL_LocalParams result = (ZL_LocalParams){ 0 };
    if (base != NULL) {
        result = *base;
    }

    if (params_value_item == NULL) {
        // No params described. Do nothing.
        *resolution = ZL_CompressorDeserializer_ParamResolution_Absent;
    } else if (params_value_item->type == A1C_ItemType_null) {
        // No params described. Do nothing.
        *resolution = ZL_CompressorDeserializer_ParamResolution_Absent;
    } else if (params_value_item->type == A1C_ItemType_map) {
        *resolution = ZL_CompressorDeserializer_ParamResolution_Present;
        ZL_TRY_LET_CONST(
                ZL_LocalParams,
                materialized,
                ZL_CompressorDeserializer_LocalParams_build(
                        state, &params_value_item->map));
        result.intParams  = materialized.intParams;
        result.copyParams = materialized.copyParams;
    } else if (params_value_item->type == A1C_ItemType_string) {
        *resolution = ZL_CompressorDeserializer_ParamResolution_Present;
        ZL_TRY_LET_CONST(
                ZL_LocalParams,
                retrieved,
                ZL_CompressorDeserializer_LocalParams_lookup(
                        state, params_value_item));
        result.intParams  = retrieved.intParams;
        result.copyParams = retrieved.copyParams;
    } else {
        ZL_ERR(corruption, "'params' field has unsupported type.");
    }
    return ZL_WRAP_VALUE(result);
}

static ZL_Report ZL_CompressorDeserializer_enqueuePending(
        ZL_CompressorDeserializer* const state,
        const A1C_Map* const map,
        const A1C_Item* const value_item)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    const A1C_Pair* const pair =
            (const A1C_Pair*)(const void*)((const char*)value_item
                                           - offsetof(A1C_Pair, val));
    const ptrdiff_t idx = pair - map->items;
    ZL_ASSERT_GE(idx, 0);
    ZL_ASSERT_LT(idx, (ptrdiff_t)map->size);
    ZL_ERR_IF(!VECTOR_PUSHBACK(state->pending, idx), allocation);
    return ZL_returnSuccess();
}

/**
 * Tries to find the materialized component named @p name.
 *
 * Possible outcomes:
 *
 * 1. The component already exists (whether because it was pre-registered or
 *    we've already set it up). No further setup required. Sets
 *    @p resolved_id to the component's ID.
 * 2. The component doesn't exist, but we found instructions for how to make it.
 *    Sets @p resolved_setup_item to point to the value in the serialized setup
 *    instructions map, @p setup_map, which describes this component.
 * 3. The component doesn't exist and we don't know how to make it.
 *    Returns an error.
 *
 * Additionally mutates the @p name_map in order to record the result of this
 * lookup.
 */
static ZL_Report ZL_CompressorDeserializer_findIfNeedsSetup_generic(
        ZL_CompressorDeserializer* const state,
        const StringView name,
        const A1C_Item** const resolved_setup_item,
        void* const resolved_id,
        bool (*find_existing_in_compressor)(
                const ZL_Compressor* compressor,
                const StringView name,
                void* resolved_id),
        const A1C_Map* setup_map,
        CompressorDeserializer_NameMap* const name_map)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    // Generic strategy to try to find a graph component.
    //
    // We have to check three different places:
    //
    // 1. The mapping we're keeping during deserialization for components that
    //    we've already set up or found in the compressor on which we're
    //    operating. This maps the name of the component in the serialized
    //    representation to the actual name the component has in the
    //    compressor.
    // 2. The components described in the serialized representation. If we have
    //    a description for the component here but we don't have a name mapping
    //    it means we haven't created this component yet.
    // 3. The component that already exist in the materialized compressor. If
    //    we haven't yet found a mapping for a name and that component isn't
    //    described in the serialized setup descriptions, we check if it exists
    //    in the compressor already as a pre-registered/pre-created component.
    //
    // Pseudocode:
    //   ```
    //   if name in state.name_map:
    //     resolved_name = state.name_map[name]
    //     if resolved_name is NULL:
    //       throw corruption! # can't look up a component while building it
    //     else resolved_name is not NULL:
    //       component_id = compressor.components[resolved_name]
    //       if component_id doesn't exist:
    //         throw logic_error!
    //       else component_id exists:
    //         return component_id!
    //   else name not in state.name_map:
    //     setup_info = serialized.components[name]
    //     if setup_info exists:
    //       return setup_info!
    //     else no ser_entry:
    //       component_id = compressor.components[name]
    //       if component_id doesn't exist:
    //         throw corruption!
    //       else component_id exists:
    //         state.name_map[name] = name
    //         return component_id!
    //   ```
    const ZL_Compressor* const compressor = state->const_compressor;
    const CompressorDeserializer_NameMap_Entry* const name_mapping_entry =
            CompressorDeserializer_NameMap_find(name_map, &name);
    if (name_mapping_entry != NULL) {
        const StringView orig_name_sv = name_mapping_entry->key;
        const StringView new_name_sv  = name_mapping_entry->val;
        ZL_ERR_IF_NULL(
                new_name_sv.data,
                corruption,
                "Compressor component '%.*s' has a dependency cycle.",
                name.size,
                name.data);
        assert_sv_nullterm(new_name_sv);
        ZL_ERR_IF(
                !find_existing_in_compressor(
                        compressor, new_name_sv, resolved_id),
                logicError,
                "Name map mapping exists pointing '%s' to '%s' exists but "
                "materialized component '%s' doesn't exist!?",
                orig_name_sv.data,
                new_name_sv.data,
                new_name_sv.data);
        return ZL_returnSuccess();
    } else {
        A1C_Item node_key_item;
        A1C_Item_string_refStringView(&node_key_item, name);
        const A1C_Item* const setup_item =
                A1C_Map_get(setup_map, &node_key_item);
        if (setup_item != NULL) {
            if (resolved_setup_item != NULL) {
                *resolved_setup_item = setup_item;
            }
            return ZL_returnSuccess();
        } else {
            // Get a null-terminated version of the name so we can pass it
            // into the reflection API, which doesn't take a size. :/
            ZL_TRY_LET_CONST(
                    StringView,
                    name_term,
                    mk_sv_n(state->arena, name.data, name.size));
            // TODO: require that this component is an anchor component?
            ZL_ERR_IF(
                    !find_existing_in_compressor(
                            compressor, name_term, resolved_id),
                    corruption,
                    "Serialized compressor has a dependency on graph component "
                    "'%s' but there is no component by that name, neither in "
                    "the serialized compressor nor pre-registered in the "
                    "provided compressor.",
                    name_term.data);
            const CompressorDeserializer_NameMap_Entry
                    name_mapping_circular_entry =
                            (CompressorDeserializer_NameMap_Entry){
                                .key = name_term,
                                .val = name_term,
                            };
            const CompressorDeserializer_NameMap_Insert
                    name_mapping_circular_insert =
                            CompressorDeserializer_NameMap_insert(
                                    name_map, &name_mapping_circular_entry);
            ZL_ERR_IF(name_mapping_circular_insert.badAlloc, allocation);
            ZL_ERR_IF(!name_mapping_circular_insert.inserted, corruption);

            return ZL_returnSuccess();
        }
    }
}

static bool ZL_CompressorDeserializer_findIfNeedsSetup_lookupNodeHelper(
        const ZL_Compressor* const compressor,
        const StringView name,
        void* const resolved_id)
{
    assert_sv_nullterm(name);
    const ZL_NodeID nid = ZL_Compressor_getNode(compressor, name.data);
    if (nid.nid == ZL_NODE_ILLEGAL.nid) {
        return false;
    }
    if (resolved_id != NULL) {
        memcpy(resolved_id, &nid, sizeof(nid));
    }
    return true;
}

static bool ZL_CompressorDeserializer_findIfNeedsSetup_lookupGraphHelper(
        const ZL_Compressor* const compressor,
        const StringView name,
        void* const resolved_id)
{
    assert_sv_nullterm(name);
    const ZL_GraphID gid = ZL_Compressor_getGraph(compressor, name.data);
    if (gid.gid == ZL_GRAPH_ILLEGAL.gid) {
        return false;
    }
    if (resolved_id != NULL) {
        memcpy(resolved_id, &gid, sizeof(gid));
    }
    return true;
}

/**
 * See docs for @ref ZL_CompressorDeserializer_findIfNeedsSetup_generic.
 */
static ZL_Report ZL_CompressorDeserializer_findIfNodeNeedsSetup(
        ZL_CompressorDeserializer* const state,
        const StringView node_name,
        const A1C_Item** const resolved_setup_item,
        ZL_NodeID* const resolved_node_id)
{
    return ZL_CompressorDeserializer_findIfNeedsSetup_generic(
            state,
            node_name,
            resolved_setup_item,
            resolved_node_id,
            ZL_CompressorDeserializer_findIfNeedsSetup_lookupNodeHelper,
            state->nodes,
            &state->node_names);
}

/**
 * See docs for @ref ZL_CompressorDeserializer_findIfNeedsSetup_generic.
 */
static ZL_Report ZL_CompressorDeserializer_findIfGraphNeedsSetup(
        ZL_CompressorDeserializer* const state,
        const StringView graph_name,
        const A1C_Item** const resolved_setup_item,
        ZL_GraphID* const resolved_graph_id)
{
    return ZL_CompressorDeserializer_findIfNeedsSetup_generic(
            state,
            graph_name,
            resolved_setup_item,
            resolved_graph_id,
            ZL_CompressorDeserializer_findIfNeedsSetup_lookupGraphHelper,
            state->graphs,
            &state->graph_names);
}

/**
 * Checks whether any of the @p prerequisite_graphs this graph depends on
 * haven't been set up yet. If so, it enqueues this graph and the unmet
 * dependencies into the pending queue.
 *
 * Increments @p cumulative_unsatisfied_prerequisites for each unmet dep.
 *
 * @returns an error if the unmet dependency has no setup instructions and
 *          therefore can't be satisfied.
 */
static ZL_Report ZL_CompressorDeserializer_checkPrerequisiteGraphs(
        ZL_CompressorDeserializer* const state,
        const A1C_Array* const prerequisite_graphs,
        const A1C_Item* const this_item,
        size_t* cumulative_unsatisfied_prerequisites)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    const size_t num_prerequisites = prerequisite_graphs->size;
    for (size_t i = 0; i < num_prerequisites; i++) {
        A1C_TRY_EXTRACT_STRING(prereq_name_str, &prerequisite_graphs->items[i]);
        const A1C_Item* setup_item = NULL;
        ZL_ERR_IF_ERR(ZL_CompressorDeserializer_findIfGraphNeedsSetup(
                state,
                StringView_initFromA1C(prereq_name_str),
                &setup_item,
                NULL));
        if (setup_item != NULL) {
            if (*cumulative_unsatisfied_prerequisites == 0) {
                ZL_ERR_IF_ERR(ZL_CompressorDeserializer_enqueuePending(
                        state, state->graphs, this_item));
            }
            ZL_ERR_IF_ERR(ZL_CompressorDeserializer_enqueuePending(
                    state, state->graphs, setup_item));
            (*cumulative_unsatisfied_prerequisites)++;
        }
    }
    return ZL_returnSuccess();
}

typedef enum {
    CompressorDeserializer_ComponentProcessingState_firstVisit,
    CompressorDeserializer_ComponentProcessingState_secondVisit,
    CompressorDeserializer_ComponentProcessingState_done,
} CompressorDeserializer_ComponentProcessingState;

static ZL_Report ZL_CompressorDeserializer_tryBuildNode(
        ZL_CompressorDeserializer* const state,
        const A1C_Pair* const pair)
{
    ZL_ASSERT_NN(state);
    ZL_Compressor* const compressor = state->mut_compressor;
    ZL_ASSERT_NN(compressor);
    ZL_ASSERT_NN(pair);

    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    const A1C_Item* const key = &pair->key;
    const A1C_Item* const val = &pair->val;
    A1C_TRY_EXTRACT_STRING(key_str, key);
    A1C_TRY_EXTRACT_MAP(val_map, val);

    const StringView ser_name_unterm = StringView_initFromA1C(key_str);

    CompressorDeserializer_ComponentProcessingState processing_state;
    {
        // Write a placeholder entry into the name resolution map. This
        // allows us to detect cycles or duplicates.
        const CompressorDeserializer_NameMap_Entry name_mapping_entry =
                (CompressorDeserializer_NameMap_Entry){
                    .key = ser_name_unterm, .val = StringView_init(NULL, 0)
                };
        const CompressorDeserializer_NameMap_Insert name_mapping_insert =
                CompressorDeserializer_NameMap_insert(
                        &state->node_names, &name_mapping_entry);
        ZL_ERR_IF(name_mapping_insert.badAlloc, allocation);
        if (name_mapping_insert.ptr->val.data != NULL) {
            // Already set up. Skip.
            return ZL_returnSuccess();
        }
        if (name_mapping_insert.inserted) {
            processing_state =
                    CompressorDeserializer_ComponentProcessingState_firstVisit;
        } else {
            processing_state =
                    CompressorDeserializer_ComponentProcessingState_secondVisit;
        }
    }

    const A1C_Item* base_node_setup_item = NULL;
    ZL_NodeID base_nid                   = ZL_NODE_ILLEGAL;

    {
        A1C_TRY_EXTRACT_STRING(
                base_name_str, A1C_Map_get_cstr(&val_map, "base"));
        const StringView base_name = StringView_initFromA1C(base_name_str);

        ZL_ERR_IF_ERR(ZL_CompressorDeserializer_findIfNodeNeedsSetup(
                state, base_name, &base_node_setup_item, &base_nid));

        if (base_node_setup_item != NULL) {
            ZL_ASSERT_EQ(
                    processing_state,
                    CompressorDeserializer_ComponentProcessingState_firstVisit);
            ZL_ERR_IF_ERR(ZL_CompressorDeserializer_enqueuePending(
                    state, state->nodes, val));
            ZL_ERR_IF_ERR(ZL_CompressorDeserializer_enqueuePending(
                    state, state->nodes, base_node_setup_item));
            return ZL_returnSuccess();
        }

        ZL_ASSERT_NE(base_nid.nid, ZL_NODE_ILLEGAL.nid);
    }

    const ZL_LocalParams base_local_params =
            ZL_Compressor_Node_getLocalParams(compressor, base_nid);
    ZL_TRY_LET_CONST(
            ZL_LocalParams,
            local_params,
            ZL_CompressorDeserializer_LocalParams_resolve(
                    state,
                    A1C_Map_get_cstr(&val_map, "params"),
                    &base_local_params,
                    NULL));

    const ZL_NodeID node_id =
            ZL_Compressor_cloneNode(compressor, base_nid, &local_params);
    ZL_ERR_IF_EQ(node_id.nid, ZL_NODE_ILLEGAL.nid, corruption);

    const char* new_name = ZL_Compressor_Node_getName(compressor, node_id);
    ZL_ERR_IF_NULL(new_name, corruption);
    ZL_TRY_LET_CONST(StringView, new_name_sv, mk_sv(state->arena, new_name));

    CompressorDeserializer_NameMap_Entry* name_mapping_entry =
            CompressorDeserializer_NameMap_findMut(
                    &state->node_names, &ser_name_unterm);
    ZL_ERR_IF_NULL(name_mapping_entry, logicError);
    ZL_ASSERT_NULL(name_mapping_entry->val.data);
    name_mapping_entry->val = new_name_sv;

    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorDeserializer_tryBuildGraph(
        ZL_CompressorDeserializer* const state,
        const A1C_Pair* const pair)
{
    ZL_ASSERT_NN(state);
    ZL_Compressor* const compressor = state->mut_compressor;
    ZL_ASSERT_NN(compressor);
    ZL_ASSERT_NN(pair);

    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    CompressorDeserializer_ComponentProcessingState processing_state;
    const A1C_Item* const key = &pair->key;
    const A1C_Item* const val = &pair->val;
    A1C_TRY_EXTRACT_STRING(key_str, key);
    const StringView ser_name_unterm = StringView_initFromA1C(key_str);
    A1C_TRY_EXTRACT_MAP(val_map, val);

    {
        // Write a placeholder entry into the name resolution map. This
        // allows us to detect cycles or duplicates.
        const CompressorDeserializer_NameMap_Entry name_mapping_entry =
                (CompressorDeserializer_NameMap_Entry){
                    .key = ser_name_unterm, .val = StringView_init(NULL, 0)
                };
        const CompressorDeserializer_NameMap_Insert name_mapping_insert =
                CompressorDeserializer_NameMap_insert(
                        &state->graph_names, &name_mapping_entry);
        ZL_ERR_IF(name_mapping_insert.badAlloc, allocation);
        if (name_mapping_insert.ptr->val.data != NULL) {
            // Already set up. Skip.
            return ZL_returnSuccess();
        }
        if (name_mapping_insert.inserted) {
            processing_state =
                    CompressorDeserializer_ComponentProcessingState_firstVisit;
        } else {
            processing_state =
                    CompressorDeserializer_ComponentProcessingState_secondVisit;
        }
    }

    const A1C_Item* const type_item = A1C_Map_get_cstr(&val_map, "type");
    ZL_ERR_IF_NULL(type_item, corruption);
    ZL_TRY_LET_CONST(ZL_GraphType, graph_type, read_graph_type(type_item));

    ZL_TRY_LET_CONST(
            StringView,
            new_graph_name_base,
            mk_sv_strip_name_fragment(state->arena, ser_name_unterm));

    const char* new_name = NULL;
    switch (graph_type) {
        case ZL_GraphType_static: {
            A1C_TRY_EXTRACT_ARRAY(
                    successors_array, A1C_Map_get_cstr(&val_map, "successors"));
            const size_t num_successors = successors_array.size;
            {
                // Check all of the successor graphs are available, or if not,
                // bail so we can set them up.
                size_t unsatisfied_prerequisites = 0;
                ZL_ERR_IF_ERR(ZL_CompressorDeserializer_checkPrerequisiteGraphs(
                        state,
                        &successors_array,
                        val,
                        &unsatisfied_prerequisites));
                if (unsatisfied_prerequisites != 0) {
                    // Abort processing this graph for now, so we can go set up
                    // its dependencies first. We'll return later.
                    ZL_ERR_IF_EQ(
                            processing_state,
                            CompressorDeserializer_ComponentProcessingState_secondVisit,
                            GENERIC);
                    return ZL_returnSuccess();
                }
            }

            ZL_NodeID head_nid = ZL_NODE_ILLEGAL;
            {
                A1C_TRY_EXTRACT_STRING(
                        head_node_name, A1C_Map_get_cstr(&val_map, "node"));

                const A1C_Item* setup_item = NULL;
                ZL_ERR_IF_ERR(ZL_CompressorDeserializer_findIfNodeNeedsSetup(
                        state,
                        StringView_initFromA1C(head_node_name),
                        &setup_item,
                        &head_nid));
                ZL_ERR_IF_NN(
                        setup_item,
                        corruption,
                        "Can't find head node '%.*s' to build static graph '%.*s'.",
                        head_node_name.size,
                        head_node_name.data,
                        ser_name_unterm.size,
                        ser_name_unterm.data);
                ZL_ASSERT_NE(head_nid.nid, ZL_NODE_ILLEGAL.nid);
            }

            const ZL_LocalParams head_node_local_params =
                    ZL_Compressor_Node_getLocalParams(compressor, head_nid);
            ZL_TRY_LET_CONST(
                    ZL_LocalParams,
                    local_params,
                    ZL_CompressorDeserializer_LocalParams_resolve(
                            state,
                            A1C_Map_get_cstr(&val_map, "params"),
                            &head_node_local_params,
                            NULL));

            ZL_GraphID* const successor_gids = ALLOC_Arena_malloc(
                    state->arena, num_successors * sizeof(ZL_GraphID));
            ZL_ERR_IF_NULL(successor_gids, allocation);

            for (size_t i = 0; i < num_successors; i++) {
                A1C_TRY_EXTRACT_STRING(
                        successor_name_str, &successors_array.items[i]);

                const A1C_Item* successor_setup_item = NULL;
                ZL_GraphID successor_gid             = ZL_GRAPH_ILLEGAL;

                ZL_ERR_IF_ERR(ZL_CompressorDeserializer_findIfGraphNeedsSetup(
                        state,
                        StringView_initFromA1C(successor_name_str),
                        &successor_setup_item,
                        &successor_gid));
                ZL_ERR_IF_NN(
                        successor_setup_item,
                        corruption,
                        "Can't find successor graph '%.*s' to build "
                        "static graph '%.*s'.",
                        successor_name_str.size,
                        successor_name_str.data,
                        ser_name_unterm.size,
                        ser_name_unterm.data);
                ZL_ASSERT_NE(successor_gid.gid, ZL_GRAPH_ILLEGAL.gid);

                successor_gids[i] = successor_gid;
            }

            const ZL_LocalParams* lpOptPtr      = &local_params;
            const ZL_StaticGraphDesc graph_desc = (ZL_StaticGraphDesc){
                .name           = new_graph_name_base.data,
                .headNodeid     = head_nid,
                .successor_gids = successor_gids,
                .nbGids         = num_successors,
                .localParams    = lpOptPtr,
            };
            const ZL_GraphID gid =
                    ZL_Compressor_registerStaticGraph(compressor, &graph_desc);
            ZL_ERR_IF_EQ(gid.gid, ZL_GRAPH_ILLEGAL.gid, corruption);
            new_name = ZL_Compressor_Graph_getName(compressor, gid);
            ZL_ERR_IF_NULL(new_name, GENERIC);

            ALLOC_Arena_free(state->arena, successor_gids);

            break;
        }
        case ZL_GraphType_parameterized: {
            A1C_TRY_EXTRACT_ARRAY(
                    graphs_array, A1C_Map_get_cstr(&val_map, "graphs"));
            const size_t num_graphs                     = graphs_array.size;
            size_t cumulative_unsatisfied_prerequisites = 0;

            // Check if any of the custom graphs aren't ready.
            ZL_ERR_IF_ERR(ZL_CompressorDeserializer_checkPrerequisiteGraphs(
                    state,
                    &graphs_array,
                    val,
                    &cumulative_unsatisfied_prerequisites));

            A1C_Item* const base_item = A1C_Map_get_cstr(&val_map, "base");
            ZL_ERR_IF_NULL(base_item, corruption);
            {
                const A1C_Array tmp_base_array =
                        (A1C_Array){ .items = base_item, .size = 1 };
                // Check that the base graph is ready.
                ZL_ERR_IF_ERR(ZL_CompressorDeserializer_checkPrerequisiteGraphs(
                        state,
                        &tmp_base_array,
                        val,
                        &cumulative_unsatisfied_prerequisites));
            }

            if (cumulative_unsatisfied_prerequisites != 0) {
                ZL_ERR_IF_EQ(
                        processing_state,
                        CompressorDeserializer_ComponentProcessingState_secondVisit,
                        GENERIC);
                // Abort processing this graph for now, so we can go set up its
                // dependencies first. We'll return later.
                return ZL_returnSuccess();
            }

            ZL_GraphID base_gid;
            {
                A1C_TRY_EXTRACT_STRING(base_name, base_item);
                const A1C_Item* setup_item = NULL;
                ZL_ERR_IF_ERR(ZL_CompressorDeserializer_findIfGraphNeedsSetup(
                        state,
                        StringView_initFromA1C(base_name),
                        &setup_item,
                        &base_gid));
                ZL_ERR_IF_NN(setup_item, logicError);
                ZL_ASSERT_NE(base_gid.gid, ZL_GRAPH_ILLEGAL.gid);
            }

            const ZL_LocalParams base_graph_local_params =
                    ZL_Compressor_Graph_getLocalParams(compressor, base_gid);
            ZL_TRY_LET_CONST(
                    ZL_LocalParams,
                    local_params,
                    ZL_CompressorDeserializer_LocalParams_resolve(
                            state,
                            A1C_Map_get_cstr(&val_map, "params"),
                            &base_graph_local_params,
                            NULL));

            A1C_TRY_EXTRACT_ARRAY(
                    nodes_array, A1C_Map_get_cstr(&val_map, "nodes"));
            size_t const num_nodes = nodes_array.size;
            ZL_NodeID* nodes       = ALLOC_Arena_malloc(
                    state->arena, num_nodes * sizeof(ZL_NodeID));
            ZL_ERR_IF_NULL(nodes, allocation);
            for (size_t i = 0; i < num_nodes; i++) {
                A1C_TRY_EXTRACT_STRING(node_name_str, &nodes_array.items[i]);

                const A1C_Item* setup_item = NULL;
                ZL_NodeID nid              = ZL_NODE_ILLEGAL;

                ZL_ERR_IF_ERR(ZL_CompressorDeserializer_findIfNodeNeedsSetup(
                        state,
                        StringView_initFromA1C(node_name_str),
                        &setup_item,
                        &nid));
                ZL_ERR_IF_NN(
                        setup_item,
                        corruption,
                        "Can't find node '%.*s' to build parameterized graph '%.*s'.",
                        node_name_str.size,
                        node_name_str.data,
                        ser_name_unterm.size,
                        ser_name_unterm.data);
                ZL_ASSERT_NE(nid.nid, ZL_NODE_ILLEGAL.nid);

                nodes[i] = nid;
            }

            ZL_GraphID* graphs = ALLOC_Arena_malloc(
                    state->arena, num_graphs * sizeof(ZL_GraphID));
            ZL_ERR_IF_NULL(graphs, allocation);
            for (size_t i = 0; i < num_graphs; i++) {
                A1C_TRY_EXTRACT_STRING(graph_name_str, &graphs_array.items[i]);

                const A1C_Item* setup_item = NULL;
                ZL_GraphID gid             = ZL_GRAPH_ILLEGAL;

                ZL_ERR_IF_ERR(ZL_CompressorDeserializer_findIfGraphNeedsSetup(
                        state,
                        StringView_initFromA1C(graph_name_str),
                        &setup_item,
                        &gid));
                ZL_ERR_IF_NN(
                        setup_item,
                        corruption,
                        "Can't find graph '%.*s' to build parameterized graph '%.*s'.",
                        graph_name_str.size,
                        graph_name_str.data,
                        ser_name_unterm.size,
                        ser_name_unterm.data);
                ZL_ASSERT_NE(gid.gid, ZL_GRAPH_ILLEGAL.gid);

                graphs[i] = gid;
            }

            const ZL_ParameterizedGraphDesc graph_desc =
                    (ZL_ParameterizedGraphDesc){
                        .name           = new_graph_name_base.data,
                        .graph          = base_gid,
                        .customGraphs   = graphs,
                        .nbCustomGraphs = num_graphs,
                        .customNodes    = nodes,
                        .nbCustomNodes  = num_nodes,
                        .localParams    = &local_params,
                    };
            const ZL_GraphID gid = ZL_Compressor_registerParameterizedGraph(
                    compressor, &graph_desc);
            ZL_ERR_IF_EQ(gid.gid, ZL_GRAPH_ILLEGAL.gid, corruption);
            new_name = ZL_Compressor_Graph_getName(compressor, gid);
            ZL_ERR_IF_NULL(new_name, GENERIC);

            ALLOC_Arena_free(state->arena, nodes);

            break;
        }
        case ZL_GraphType_standard:
            ZL_ERR(corruption,
                   "Serialized graph component '%.*s' can't have type "
                   "ZL_GraphType_standard!",
                   ser_name_unterm.size,
                   ser_name_unterm.data);
        case ZL_GraphType_selector:
            ZL_ERR(corruption,
                   "Serialized graph component '%.*s' can't have type "
                   "ZL_GraphType_selector!",
                   ser_name_unterm.size,
                   ser_name_unterm.data);
        case ZL_GraphType_function:
            ZL_ERR(corruption,
                   "Serialized graph component '%.*s' can't have type "
                   "ZL_GraphType_function!",
                   ser_name_unterm.size,
                   ser_name_unterm.data);
        case ZL_GraphType_multiInput:
            ZL_ERR(corruption,
                   "Serialized graph component '%.*s' can't have type "
                   "ZL_GraphType_multiInput!",
                   ser_name_unterm.size,
                   ser_name_unterm.data);
        case ZL_GraphType_segmenter:
            ZL_ERR(corruption,
                   "Serialized graph component '%.*s' can't have type "
                   "ZL_GraphType_segmenter!",
                   ser_name_unterm.size,
                   ser_name_unterm.data);
        default:
            ZL_ERR(logicError,
                   "Illegal graph type on graph '%.*s'!",
                   ser_name_unterm.size,
                   ser_name_unterm.data);
    }

    ZL_ERR_IF_NULL(new_name, corruption);
    ZL_TRY_LET_CONST(StringView, new_name_sv, mk_sv(state->arena, new_name));

    CompressorDeserializer_NameMap_Entry* name_mapping_entry =
            CompressorDeserializer_NameMap_findMut(
                    &state->graph_names, &ser_name_unterm);
    ZL_ERR_IF_NULL(name_mapping_entry, logicError);
    ZL_ASSERT_NULL(name_mapping_entry->val.data);
    name_mapping_entry->val = new_name_sv;

    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorDeserializer_getDeps_addNodeRef(
        ZL_CompressorDeserializer* const state,
        const StringView name,
        bool missing)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    // May be NULL!
    const ZL_Compressor* const compressor = state->const_compressor;
    const CompressorDeserializer_NameMap_Entry name_map_entry =
            (CompressorDeserializer_NameMap_Entry){
                .key = name,
                .val = missing ? StringView_init(NULL, 0) : name,
            };
    const CompressorDeserializer_NameMap_Insert name_map_insert =
            CompressorDeserializer_NameMap_insert(
                    &state->node_names, &name_map_entry);
    ZL_ERR_IF(name_map_insert.badAlloc, allocation);
    if (!name_map_insert.inserted) {
        if (!missing) {
            name_map_insert.ptr->val = name;
        }
    } else if (missing && compressor != NULL) {
        // Only try resolving explicit names
        const char* fragment = memchr(name.data, '#', name.size);
        if (fragment == NULL) {
            ZL_TRY_LET_CONST(
                    StringView,
                    name_term,
                    mk_sv_n(state->arena, name.data, name.size));
            const ZL_NodeID nid =
                    ZL_Compressor_getNode(compressor, name_term.data);
            if (nid.nid != ZL_NODE_ILLEGAL.nid) {
                name_map_insert.ptr->val =
                        StringView_initFromCStr("__already_in_the_compressor");
            }
        }
    }
    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorDeserializer_getDeps_addGraphRef(
        ZL_CompressorDeserializer* const state,
        const StringView name,
        bool missing)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    // May be NULL!
    const ZL_Compressor* const compressor = state->const_compressor;
    const CompressorDeserializer_NameMap_Entry name_map_entry =
            (CompressorDeserializer_NameMap_Entry){
                .key = name,
                .val = missing ? StringView_init(NULL, 0) : name,
            };
    const CompressorDeserializer_NameMap_Insert name_map_insert =
            CompressorDeserializer_NameMap_insert(
                    &state->graph_names, &name_map_entry);
    ZL_ERR_IF(name_map_insert.badAlloc, allocation);
    if (!name_map_insert.inserted) {
        if (!missing) {
            name_map_insert.ptr->val = name;
        }
    } else if (missing && compressor != NULL) {
        // Only try resolving explicit names
        const char* fragment = memchr(name.data, '#', name.size);
        if (fragment == NULL) {
            ZL_TRY_LET_CONST(
                    StringView,
                    name_term,
                    mk_sv_n(state->arena, name.data, name.size));
            const ZL_GraphID gid =
                    ZL_Compressor_getGraph(compressor, name_term.data);
            if (gid.gid != ZL_GRAPH_ILLEGAL.gid) {
                name_map_insert.ptr->val =
                        StringView_initFromCStr("__already_in_the_compressor");
            }
        }
    }
    return ZL_returnSuccess();
}

/**
 * Used to power @ref ZL_CompressorDeserializer_getDependencies().
 *
 * Fills the node_names map with entries, where for each node mentioned in the
 * serialized compressor, it adds an entry at that name. When the serialized
 * compressor describes how to make that node, it sets the value to a non-NULL
 * string. When it doesn't, it leaves the value NULL.
 */
static ZL_Report ZL_CompressorDeserializer_getDeps_visitNode(
        ZL_CompressorDeserializer* const state,
        const A1C_Pair* const pair)
{
    ZL_ASSERT_NN(state);
    ZL_ASSERT_NN(pair);

    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    const A1C_Item* const key = &pair->key;
    const A1C_Item* const val = &pair->val;
    A1C_TRY_EXTRACT_STRING(key_str, key);
    const StringView name = StringView_initFromA1C(key_str);
    A1C_TRY_EXTRACT_MAP(val_map, val);

    ZL_ERR_IF_ERR(
            ZL_CompressorDeserializer_getDeps_addNodeRef(state, name, false));

    A1C_TRY_EXTRACT_STRING(base_name_str, A1C_Map_get_cstr(&val_map, "base"));
    const StringView base_name = StringView_initFromA1C(base_name_str);

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_getDeps_addNodeRef(
            state, base_name, true));

    return ZL_returnSuccess();
}

/**
 * Used to power @ref ZL_CompressorDeserializer_getDependencies().
 *
 * Fills the graph_names map with entries, where for each graph mentioned in
 * the serialized compressor, it adds an entry at that name. When the
 * serialized compressor describes how to make that graph, it sets the value to
 * a non-NULL string. When it doesn't, it leaves the value NULL. This also adds
 * adds refs to the node_names map for nodes referenced by graphs this visits.
 */
static ZL_Report ZL_CompressorDeserializer_getDeps_visitGraph(
        ZL_CompressorDeserializer* const state,
        const A1C_Pair* const pair)
{
    ZL_ASSERT_NN(state);
    ZL_ASSERT_NN(pair);

    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    const A1C_Item* const key = &pair->key;
    const A1C_Item* const val = &pair->val;
    A1C_TRY_EXTRACT_STRING(key_str, key);
    const StringView name = StringView_initFromA1C(key_str);
    A1C_TRY_EXTRACT_MAP(val_map, val);

    ZL_ERR_IF_ERR(
            ZL_CompressorDeserializer_getDeps_addGraphRef(state, name, false));

    const A1C_Item* const type_item = A1C_Map_get_cstr(&val_map, "type");
    ZL_ERR_IF_NULL(type_item, corruption);
    ZL_TRY_LET_CONST(ZL_GraphType, graph_type, read_graph_type(type_item));

    switch (graph_type) {
        case ZL_GraphType_static: {
            A1C_TRY_EXTRACT_ARRAY(
                    successors_array, A1C_Map_get_cstr(&val_map, "successors"));
            const size_t num_successors = successors_array.size;
            for (size_t i = 0; i < num_successors; i++) {
                A1C_TRY_EXTRACT_STRING(
                        successor_name_str, &successors_array.items[i]);
                const StringView successor_name_sv =
                        StringView_initFromA1C(successor_name_str);
                ZL_ERR_IF_ERR(ZL_CompressorDeserializer_getDeps_addGraphRef(
                        state, successor_name_sv, true));
            }

            {
                A1C_TRY_EXTRACT_STRING(
                        head_node_name, A1C_Map_get_cstr(&val_map, "node"));
                const StringView head_node_name_sv =
                        StringView_initFromA1C(head_node_name);
                ZL_ERR_IF_ERR(ZL_CompressorDeserializer_getDeps_addNodeRef(
                        state, head_node_name_sv, true));
            }
            break;
        }
        case ZL_GraphType_parameterized: {
            {
                A1C_TRY_EXTRACT_STRING(
                        base_name_str, A1C_Map_get_cstr(&val_map, "base"));
                const StringView base_name_sv =
                        StringView_initFromA1C(base_name_str);
                ZL_ERR_IF_ERR(ZL_CompressorDeserializer_getDeps_addGraphRef(
                        state, base_name_sv, true));
            }

            A1C_TRY_EXTRACT_ARRAY(
                    graphs_array, A1C_Map_get_cstr(&val_map, "graphs"));
            const size_t num_graphs = graphs_array.size;
            for (size_t i = 0; i < num_graphs; i++) {
                A1C_TRY_EXTRACT_STRING(graph_name_str, &graphs_array.items[i]);
                const StringView graph_name_sv =
                        StringView_initFromA1C(graph_name_str);
                ZL_ERR_IF_ERR(ZL_CompressorDeserializer_getDeps_addGraphRef(
                        state, graph_name_sv, true));
            }

            A1C_TRY_EXTRACT_ARRAY(
                    nodes_array, A1C_Map_get_cstr(&val_map, "nodes"));
            size_t const num_nodes = nodes_array.size;
            for (size_t i = 0; i < num_nodes; i++) {
                A1C_TRY_EXTRACT_STRING(node_name_str, &nodes_array.items[i]);
                const StringView node_name_sv =
                        StringView_initFromA1C(node_name_str);
                ZL_ERR_IF_ERR(ZL_CompressorDeserializer_getDeps_addNodeRef(
                        state, node_name_sv, true));
            }

            break;
        }
        case ZL_GraphType_standard:
            ZL_ERR(corruption,
                   "Serialized graph component '%.*s' can't have type "
                   "ZL_GraphType_standard!",
                   name.size,
                   name.data);
        case ZL_GraphType_selector:
            ZL_ERR(corruption,
                   "Serialized graph component '%.*s' can't have type "
                   "ZL_GraphType_selector!",
                   name.size,
                   name.data);
        case ZL_GraphType_function:
            ZL_ERR(corruption,
                   "Serialized graph component '%.*s' can't have type "
                   "ZL_GraphType_function!",
                   name.size,
                   name.data);
        case ZL_GraphType_multiInput:
            ZL_ERR(corruption,
                   "Serialized graph component '%.*s' can't have type "
                   "ZL_GraphType_multiInput!",
                   name.size,
                   name.data);
        case ZL_GraphType_segmenter:
            ZL_ERR(corruption,
                   "Serialized graph component '%.*s' can't have type "
                   "ZL_GraphType_segmenter!",
                   name.size,
                   name.data);
        default:
            ZL_ERR(logicError,
                   "Illegal graph type on graph '%.*s'!",
                   name.size,
                   name.data);
    }

    return ZL_returnSuccess();
}

typedef ZL_Report (*ZL_CompressorDeserializer_DFSFunc)(
        ZL_CompressorDeserializer* const state,
        const A1C_Pair* const pair);

/**
 * Run a semi-DFS traversal of the map, invoking @p func on each node.
 *
 * If a node isn't ready to be set up, it can push itself and then any items
 * that need to be visited first into the pending stack with
 * `ZL_CompressorDeserializer_enqueuePending`, and then just return. The
 * enqueued nodes will be visited first before returning to the original node
 * to try again to set it up. That second time should succeed.
 */
static ZL_Report ZL_CompressorDeserializer_dfs(
        ZL_CompressorDeserializer* const state,
        const A1C_Map* const map,
        const ZL_CompressorDeserializer_DFSFunc func)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);

    ZL_ASSERT_EQ(VECTOR_SIZE(state->pending), 0);

    const A1C_Pair* it        = map->items;
    const A1C_Pair* const end = it + map->size;
    while (1) {
        const A1C_Pair* cur;
        if (VECTOR_SIZE(state->pending) != 0) {
            const size_t idx =
                    VECTOR_AT(state->pending, VECTOR_SIZE(state->pending) - 1);
            VECTOR_POPBACK(state->pending);
            cur = &map->items[idx];
        } else {
            if (it == end) {
                break;
            }
            cur = it++;
        }
        A1C_TRY_EXTRACT_STRING(name, &cur->key);

        ZL_ERR_IF_ERR(
                func(state, cur),
                "Failed trying to build component '%.*s'.",
                name.size,
                name.data);
    }

    ZL_ASSERT_EQ(VECTOR_SIZE(state->pending), 0);

    return ZL_returnSuccess();
}

static ZL_RESULT_OF(ZL_CompressorDeserializer_Dependencies)
        ZL_CompressorDeserializer_getDeps_buildResult(
                ZL_CompressorDeserializer* const state)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_CompressorDeserializer_Dependencies, state);

    ZL_ERR_IF_NULL(state, GENERIC);

    size_t total_string_size = 0;
    size_t num_nodes         = 0;
    {
        CompressorDeserializer_NameMap_Iter it =
                CompressorDeserializer_NameMap_iter(&state->node_names);
        const CompressorDeserializer_NameMap_Entry* entry;
        while ((entry = CompressorDeserializer_NameMap_Iter_next(&it))
               != NULL) {
            const StringView* const name = &entry->key;
            const StringView* const val  = &entry->val;
            if (val->data == NULL) {
                num_nodes++;
                total_string_size += name->size + 1;
            }
        }
    }

    size_t num_graphs = 0;
    {
        CompressorDeserializer_NameMap_Iter it =
                CompressorDeserializer_NameMap_iter(&state->graph_names);
        const CompressorDeserializer_NameMap_Entry* entry;
        while ((entry = CompressorDeserializer_NameMap_Iter_next(&it))
               != NULL) {
            const StringView* const name = &entry->key;
            const StringView* const val  = &entry->val;
            if (val->data == NULL) {
                num_graphs++;
                total_string_size += name->size + 1;
            }
        }
    }

    const size_t total_alloc_size =
            (num_nodes + num_graphs) * sizeof(const char*) + total_string_size;
    char* buffer = ALLOC_Arena_malloc(state->arena, total_alloc_size);
    ZL_ERR_IF_NULL(buffer, allocation);

    const char** graph_names = (const char**)(void*)buffer;
    const char** node_names  = graph_names + num_graphs;
    char* str_buffer         = (char*)(void*)(node_names + num_nodes);

    {
        size_t idx = 0;
        CompressorDeserializer_NameMap_Iter it =
                CompressorDeserializer_NameMap_iter(&state->graph_names);
        const CompressorDeserializer_NameMap_Entry* entry;
        while ((entry = CompressorDeserializer_NameMap_Iter_next(&it))
               != NULL) {
            const StringView* const name = &entry->key;
            const StringView* const val  = &entry->val;
            if (val->data == NULL) {
                graph_names[idx++] = str_buffer;
                memcpy(str_buffer, name->data, name->size);
                str_buffer += name->size;
                (str_buffer++)[0] = '\0';
            }
        }
        ZL_ERR_IF_NE(idx, num_graphs, logicError);
    }

    {
        size_t idx = 0;
        CompressorDeserializer_NameMap_Iter it =
                CompressorDeserializer_NameMap_iter(&state->node_names);
        const CompressorDeserializer_NameMap_Entry* entry;
        while ((entry = CompressorDeserializer_NameMap_Iter_next(&it))
               != NULL) {
            const StringView* const name = &entry->key;
            const StringView* const val  = &entry->val;
            if (val->data == NULL) {
                node_names[idx++] = str_buffer;
                memcpy(str_buffer, name->data, name->size);
                str_buffer += name->size;
                (str_buffer++)[0] = '\0';
            }
        }
        ZL_ERR_IF_NE(idx, num_nodes, logicError);
    }

    ZL_CompressorDeserializer_Dependencies deps;
    deps.graph_names = graph_names;
    deps.num_graphs  = num_graphs;
    deps.node_names  = node_names;
    deps.num_nodes   = num_nodes;
    return ZL_WRAP_VALUE(deps);
}

static ZL_Report ZL_CompressorDeserializer_decode(
        ZL_CompressorDeserializer* const state,
        const StringView serialized)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    const A1C_DecoderConfig decoder_config =
            (A1C_DecoderConfig){ .maxDepth            = 0,
                                 .limitBytes          = 0,
                                 .referenceSource     = true,
                                 .rejectUnknownSimple = true };
    A1C_Decoder decoder;
    A1C_Decoder_init(&decoder, state->a1c_arena, decoder_config);

    state->root = A1C_Decoder_decode(
            &decoder, (const uint8_t*)serialized.data, serialized.size);
    if (state->root == NULL) {
        return ZL_WRAP_ERROR(A1C_Error_convert(
                ZL_ERR_CTX_PTR, A1C_Decoder_getError(&decoder)));
    }

    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorDeserializer_checkVersion(
        ZL_CompressorDeserializer* const state)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    /*
    A1C_TRY_EXTRACT_MAP(root_map, state->root);
    A1C_TRY_EXTRACT_INT64(version, A1C_Map_get_cstr(&root_map, "version"));
    ZL_ERR_IF_NE(
            version,
            ZL_LIBRARY_VERSION_NUMBER,
            formatVersion_unsupported,
            "The compressor serialization format is unstable: compressor "
            "deserialization currently only works with serialized compressors "
            "generated by the same library version. This serialized compressor "
            "was generated by library version v%u.%u.%u, but you're attempting
    to " "deserialize it with library version v%u.%u.%u.", version / 10000,
            (version / 100) % 100,
            version % 100,
            ZL_LIBRARY_VERSION_MAJOR,
            ZL_LIBRARY_VERSION_MINOR,
            ZL_LIBRARY_VERSION_PATCH);
            */
    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorDeserializer_setupParams(
        ZL_CompressorDeserializer* const state)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    A1C_TRY_EXTRACT_MAP(root_map, state->root);
    const A1C_Item* const params = A1C_Map_get_cstr(&root_map, "params");
    ZL_ERR_IF_NULL(params, corruption);
    ZL_ERR_IF_NE(params->type, A1C_ItemType_map, corruption);
    state->params = &params->map;
    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorDeserializer_setupNodes(
        ZL_CompressorDeserializer* const state,
        const ZL_CompressorDeserializer_DFSFunc func)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    A1C_TRY_EXTRACT_MAP(root_map, state->root);
    const A1C_Item* const nodes = A1C_Map_get_cstr(&root_map, "nodes");
    ZL_ERR_IF_NULL(nodes, corruption);
    ZL_ERR_IF_NE(nodes->type, A1C_ItemType_map, corruption);
    state->nodes = &nodes->map;

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_dfs(state, &nodes->map, func));
    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorDeserializer_setupGraphs(
        ZL_CompressorDeserializer* const state,
        const ZL_CompressorDeserializer_DFSFunc func)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    A1C_TRY_EXTRACT_MAP(root_map, state->root);
    const A1C_Item* const graphs = A1C_Map_get_cstr(&root_map, "graphs");
    ZL_ERR_IF_NULL(graphs, corruption);
    ZL_ERR_IF_NE(graphs->type, A1C_ItemType_map, corruption);
    state->graphs = &graphs->map;

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_dfs(state, &graphs->map, func));
    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorDeserializer_setStartingGraph(
        ZL_CompressorDeserializer* const state,
        ZL_Compressor* const compressor)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    A1C_TRY_EXTRACT_MAP(root_map, state->root);
    A1C_TRY_EXTRACT_STRING(
            starting_graph_name, A1C_Map_get_cstr(&root_map, "start"));
    const StringView starting_graph_name_sv =
            StringView_initFromA1C(starting_graph_name);

    const A1C_Item* starting_graph_setup = NULL;
    ZL_GraphID starting_graph_id         = ZL_GRAPH_ILLEGAL;

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_findIfGraphNeedsSetup(
            state,
            starting_graph_name_sv,
            &starting_graph_setup,
            &starting_graph_id));

    ZL_ERR_IF_NN(
            starting_graph_setup,
            corruption,
            "Starting graph '%.*s' apparently still needs setup.",
            starting_graph_name_sv.size,
            starting_graph_name_sv.data);
    ZL_ERR_IF_EQ(
            starting_graph_id.gid,
            ZL_GRAPH_ILLEGAL.gid,
            corruption,
            "Starting graph '%.*s' is illegal??",
            starting_graph_name_sv.size,
            starting_graph_name_sv.data);

    ZL_ERR_IF_ERR(
            ZL_Compressor_selectStartingGraphID(compressor, starting_graph_id));
    return ZL_returnSuccess();
}

static ZL_Report ZL_CompressorDeserializer_setGlobalParams(
        ZL_CompressorDeserializer* const state,
        ZL_Compressor* const compressor)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    A1C_TRY_EXTRACT_MAP(root_map, state->root);
    ZL_TRY_LET_CONST(
            ZL_LocalParams,
            local_params,
            ZL_CompressorDeserializer_LocalParams_resolve(
                    state,
                    A1C_Map_get_cstr(&root_map, "global_params"),
                    NULL,
                    NULL));
    ZL_ERR_IF_NE(
            local_params.copyParams.nbCopyParams,
            0,
            corruption,
            "Can't set global copyParams!");
    ZL_ERR_IF_NE(
            local_params.refParams.nbRefParams,
            0,
            corruption,
            "Can't set global refParams!");
    for (size_t i = 0; i < local_params.intParams.nbIntParams; i++) {
        const ZL_IntParam int_param = local_params.intParams.intParams[i];
        ZL_ERR_IF_ERR(ZL_Compressor_setParameter(
                compressor,
                (ZL_CParam)int_param.paramId,
                int_param.paramValue));
    }
    return ZL_returnSuccess();
}

ZL_Report ZL_CompressorDeserializer_deserialize(
        ZL_CompressorDeserializer* const state,
        ZL_Compressor* const compressor,
        const void* const serialized_ptr,
        const size_t serialized_size)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state);
    ZL_ERR_IF_NULL(state, GENERIC);

    state->mut_compressor   = compressor;
    state->const_compressor = compressor;

    ZL_OC_startOperation(&state->opCtx, ZL_Operation_deserializeCompressor);

    const StringView serialized =
            StringView_init(serialized_ptr, serialized_size);

    ZL_ERR_IF_NULL(compressor, GENERIC);
    ZL_ERR_IF_NULL(serialized.data, GENERIC);

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_decode(state, serialized));

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_checkVersion(state));

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_setupParams(state));

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_setupNodes(
            state, ZL_CompressorDeserializer_tryBuildNode));

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_setupGraphs(
            state, ZL_CompressorDeserializer_tryBuildGraph));

    ZL_ERR_IF_ERR(
            ZL_CompressorDeserializer_setStartingGraph(state, compressor));

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_setGlobalParams(state, compressor));

    return ZL_returnSuccess();
}

ZL_RESULT_OF(ZL_CompressorDeserializer_Dependencies)
ZL_CompressorDeserializer_getDependencies(
        ZL_CompressorDeserializer* const state,
        const ZL_Compressor* const compressor,
        const void* const serialized_ptr,
        const size_t serialized_size)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_CompressorDeserializer_Dependencies, NULL);
    ZL_ERR_IF_NULL(state, GENERIC);

    state->mut_compressor   = NULL;
    state->const_compressor = compressor;

    ZL_OC_startOperation(&state->opCtx, ZL_Operation_deserializeCompressor);
    ZL_RESULT_UPDATE_SCOPE_CONTEXT(state);

    const StringView serialized =
            StringView_init(serialized_ptr, serialized_size);

    ZL_ERR_IF_NULL(serialized.data, GENERIC);

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_decode(state, serialized));

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_checkVersion(state));

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_setupNodes(
            state, ZL_CompressorDeserializer_getDeps_visitNode));

    ZL_ERR_IF_ERR(ZL_CompressorDeserializer_setupGraphs(
            state, ZL_CompressorDeserializer_getDeps_visitGraph));

    return ZL_CompressorDeserializer_getDeps_buildResult(state);
}

const char* ZL_CompressorDeserializer_getErrorContextString(
        const ZL_CompressorDeserializer* const state,
        const ZL_Report result)
{
    return ZL_CompressorDeserializer_getErrorContextString_fromError(
            state, ZL_RES_error(result));
}

const char* ZL_CompressorDeserializer_getErrorContextString_fromError(
        const ZL_CompressorDeserializer* const state,
        const ZL_Error error)
{
    if (state == NULL) {
        return NULL;
    }
    if (!ZL_E_isError(error)) {
        return NULL;
    }
    return ZL_OC_getErrorContextString(&state->opCtx, error);
}
