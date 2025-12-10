// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/graph_registry.h" // InternalGraphDesc, GR_standardGraphs declarations
#include "openzl/codecs/bitpack/encode_bitpack_binding.h" // SI_selector_bitpack
#include "openzl/codecs/encoder_registry.h" // ER_standardNodes, CNode definitions
#include "openzl/codecs/entropy/encode_entropy_binding.h" // EI_fseDynamicGraph, EI_huffmanDynamicGraph, EI_entropyDynamicGraph
#include "openzl/codecs/lz/encode_lz_binding.h" // EI_fieldLzDynGraph, EI_fieldLzLiteralsDynGraph, SI_fieldLzLiteralsChannelSelector
#include "openzl/codecs/parse_int/encode_parse_int_binding.h" // MIGRAPH_TRY_PARSE_INT
#include "openzl/common/assertion.h" // ZL_ASSERT_* macros for runtime checks
#include "openzl/common/logging.h"   // STR_REPLACE_NULL, logging utilities
#include "openzl/compress/compress_types.h" // Compression-related type definitions
#include "openzl/compress/dyngraph_interface.h" // ZL_Graph definition and graph context functions
#include "openzl/compress/graphs/generic_clustering_graph.h" // MIGRAPH_CLUSTERING
#include "openzl/compress/graphs/sddl/simple_data_description_language.h" // ZL_SDDL_dynGraph
#include "openzl/compress/graphs/sddl2/sddl2.h" // SDDL2_parse
#include "openzl/compress/graphs/split_graph.h" // ZL_splitFnGraph
#include "openzl/compress/implicit_conversion.h" // ICONV_isCompatible for type checking
#include "openzl/compress/private_nodes.h" // ZL_PrivateStandardGraphID_end, private node ID definitions
#include "openzl/compress/segmenters/segmenter_numeric.h" // SEGM_numeric_desc
#include "openzl/compress/selector.h" // SelectorCtx, ZL_SelectorFn, SelCtx_* functions
#include "openzl/compress/selectors/selector_compress.h" // SI_selector_compress, SI_selector_compress_* functions
#include "openzl/compress/selectors/selector_constant.h" // SI_selector_constant
#include "openzl/compress/selectors/selector_genericLZ.h" // SI_selector_genericLZ
#include "openzl/compress/selectors/selector_numeric.h"   // SI_selector_numeric
#include "openzl/compress/selectors/selector_store.h" // SI_selector_store, MIGRAPH_STORE
#include "openzl/shared/utils.h"                      // ZL_ARRAY_SIZE macro
#include "openzl/zl_data.h"   // ZL_Type definitions and data structures
#include "openzl/zl_errors.h" // ZL_TRY_LET_T, ZL_RET_R_IF_* error handling macros
#include "openzl/zl_graph_api.h"    // ZL_Graph_*, ZL_Edge_* API functions
#include "openzl/zl_localParams.h"  // ZL_LocalParams structure and functions
#include "openzl/zl_opaque_types.h" // Opaque type definitions used by the API

#define _1_SUCCESSOR(s) (const ZL_GraphID[]){ { s } }, 1
#define _2_SUCCESSORS(s1, s2) (const ZL_GraphID[]){ { s1 }, { s2 } }, 2

// Pay attention to match the following conditions:
// - intype => input type of the Graph, hence of the Head Node
// - minv => highest minimum version of Head Node and all Successor Graphs
// - maxv => lowest maximum version of Head Node and all Successor Graphs
// These values must be manually determined and provided,
// they can't be extracted from ER_standardNodes nor GR_standardGraphs,
// because these sources are not considered "constant" by the C standard.
// For the same reason, they can't be checked at compile using static_assert(),
// but are checked at runtime (in debug mode) with GR_validate().
#define REGISTER_STATIC_GRAPH(id, _gname, intype, hnid, dstlist) \
    [id] = {                                                                 \
        .type = GR_dynamicGraph,                                             \
        .gdi  = {                                                            \
            .migd = {                                                        \
                .name             = _gname,                                  \
                .graph_f          = GR_staticGraphWrapper,                   \
                .inputTypeMasks   = (const ZL_Type[]){ intype },             \
                .nbInputs         = 1,                                       \
                .customNodes      = (const ZL_NodeID[]){ { hnid } },         \
                .nbCustomNodes    = 1,                                       \
                .customGraphs     = dstlist,                                 \
            },                                                               \
            .baseGraphID = ZL_GRAPH_ILLEGAL,                                 \
        },                                                                   \
    }

#define REGISTER_SELECTOR(id, _sname, _selectF, _intypes) \
    [id] = {                                                           \
        .type = GR_dynamicGraph,                                       \
        .gdi  = {                                                      \
            .migd = {                                                  \
                .name             = _sname,                            \
                .graph_f          = GR_selectorWrapper,                \
                .inputTypeMasks   = (const ZL_Type[]) { _intypes },    \
                .nbInputs         = 1,                                 \
            },                                                         \
            .privateParam = &(GR_SelectorFunction){ _selectF},         \
            .baseGraphID = ZL_GRAPH_ILLEGAL,                           \
        },                                                             \
    }

#define REGISTER_DYNAMIC_GRAPH(id, _gname, _intype, _graph_f) \
    [id] = {                                                                \
        .type = GR_dynamicGraph,                                            \
        .gdi  = {                                                           \
            .migd = {                                                       \
                .name             = (_gname),                               \
                .graph_f          = (_graph_f),                             \
                .inputTypeMasks   = (const ZL_Type[]){ _intype },           \
                .nbInputs         = 1,                                      \
            },                                                              \
            .baseGraphID = ZL_GRAPH_ILLEGAL,                                \
        },                                                                  \
    }

#define REGISTER_MIGRAPH(id, _gdesc) \
    [id] = {                                  \
        .type = GR_dynamicGraph,              \
        .gdi  = {                             \
            .migd = _gdesc,                   \
            .baseGraphID = ZL_GRAPH_ILLEGAL,  \
        },                                    \
    }

#define REGISTER_SEGMENTER(id, _sdesc) \
    [id] = {                                 \
        .type = GR_segmenter,                \
        .gdi  = {                            \
            .segDesc = _sdesc,               \
            .baseGraphID = ZL_GRAPH_ILLEGAL, \
        },                                   \
    }

#define REGISTER_SPECIAL(id, _name, _type) \
    [id] = {                                                              \
        .type = _type,                                                    \
        .gdi  = {                                                         \
            .migd = {                                                     \
                .name             = _name,                                \
                .inputTypeMasks   = (const ZL_Type[]){ ZL_Type_serial },  \
                .nbInputs         = 1,                                    \
            },                                                            \
            .baseGraphID = ZL_GRAPH_ILLEGAL,                              \
        },                                                                \
    }

// clang-format off
const InternalGraphDesc GR_standardGraphs[ZL_PrivateStandardGraphID_end] = {
    // note: serial_store is effectively a special action
    REGISTER_SPECIAL(ZL_PrivateStandardGraphID_serial_store, "!zl.private.serial_store", GR_store ),

    // Public graphs
    REGISTER_MIGRAPH(ZL_StandardGraphID_store, MIGRAPH_STORE),
    REGISTER_DYNAMIC_GRAPH(ZL_StandardGraphID_fse, "!zl.fse", ZL_Type_serial, EI_fseDynamicGraph),
    REGISTER_DYNAMIC_GRAPH(ZL_StandardGraphID_huffman, "!zl.huffman", ZL_Type_serial | ZL_Type_struct | ZL_Type_numeric, EI_huffmanDynamicGraph),
    REGISTER_DYNAMIC_GRAPH(ZL_StandardGraphID_entropy, "!zl.entropy", ZL_Type_serial | ZL_Type_struct | ZL_Type_numeric, EI_entropyDynamicGraph),
    REGISTER_SELECTOR(ZL_StandardGraphID_constant, "!zl.constant", SI_selector_constant, ZL_Type_serial | ZL_Type_struct),
    REGISTER_STATIC_GRAPH(ZL_StandardGraphID_zstd, "!zl.zstd", ZL_Type_serial, ZL_PrivateStandardNodeID_zstd, _1_SUCCESSOR(ZL_PrivateStandardGraphID_serial_store) ),
    REGISTER_SELECTOR(ZL_StandardGraphID_bitpack, "!zl.bitpack", SI_selector_bitpack, ZL_Type_serial | ZL_Type_numeric),
    REGISTER_STATIC_GRAPH(ZL_StandardGraphID_flatpack, "!zl.flatpack", ZL_Type_serial, ZL_PrivateStandardNodeID_flatpack, _2_SUCCESSORS(ZL_PrivateStandardGraphID_serial_store, ZL_PrivateStandardGraphID_serial_store) ),
    REGISTER_DYNAMIC_GRAPH(ZL_StandardGraphID_field_lz, "!zl.field_lz", ZL_Type_struct | ZL_Type_numeric, EI_fieldLzDynGraph),
    REGISTER_MIGRAPH(ZL_StandardGraphID_compress_generic, MIGRAPH_COMPRESS),
    REGISTER_SELECTOR(ZL_StandardGraphID_select_generic_lz_backend, "!zl.select_generic_lz_backend", SI_selector_genericLZ, ZL_Type_serial),
    REGISTER_SEGMENTER(ZL_StandardGraphID_segment_numeric, SEGM_NUMERIC_DESC),
    REGISTER_SELECTOR(ZL_StandardGraphID_select_numeric, "!zl.select_numeric", SI_selector_numeric, ZL_Type_numeric),
    REGISTER_MIGRAPH(ZL_StandardGraphID_clustering, MIGRAPH_CLUSTERING),
    REGISTER_DYNAMIC_GRAPH(ZL_StandardGraphID_simple_data_description_language, "!zl.sddl", ZL_Type_serial, ZL_SDDL_dynGraph),
    REGISTER_DYNAMIC_GRAPH(ZL_StandardGraphID_simple_data_description_language_v2, "!zl.sddl2", ZL_Type_serial, SDDL2_parse),
    REGISTER_MIGRAPH(ZL_StandardGraphID_try_parse_int, MIGRAPH_TRY_PARSE_INT),
    REGISTER_STATIC_GRAPH(ZL_StandardGraphID_lz4, "!zl.lz4", ZL_Type_serial, ZL_PrivateStandardNodeID_lz4, _1_SUCCESSOR(ZL_PrivateStandardGraphID_serial_store)),

    // Private graphs
    REGISTER_SELECTOR(ZL_PrivateStandardGraphID_store1, "!zl.private.store1", SI_selector_store, ZL_Type_any),
    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_string_store, "!zl.private.string_store", ZL_Type_string, ZL_StandardNodeID_separate_string_components, _2_SUCCESSORS(ZL_PrivateStandardGraphID_serial_store, ZL_PrivateStandardGraphID_serial_store) ),

    REGISTER_SELECTOR(ZL_PrivateStandardGraphID_compress1, "!zl.private.compress2", SI_selector_compress, ZL_Type_any),
    REGISTER_SELECTOR(ZL_PrivateStandardGraphID_serial_compress, "!zl.private.serial_compress", SI_selector_compress_serial, ZL_Type_serial),
    REGISTER_SELECTOR(ZL_PrivateStandardGraphID_struct_compress, "!zl.private.struct_compress", SI_selector_compress_struct, ZL_Type_struct),
    REGISTER_SELECTOR(ZL_PrivateStandardGraphID_numeric_compress, "!zl.private.numeric_compress", SI_selector_compress_numeric, ZL_Type_numeric),
    REGISTER_SELECTOR(ZL_PrivateStandardGraphID_string_compress, "!zl.private.string_compress", SI_selector_compress_string, ZL_Type_string),
    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_string_separate_compress, "!zl.private.string_separate_compress", ZL_Type_string, ZL_StandardNodeID_separate_string_components, _2_SUCCESSORS(ZL_PrivateStandardGraphID_serial_compress, ZL_PrivateStandardGraphID_numeric_compress) ),

    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_bitpack_serial, "!zl.private.bitpack_serial", ZL_Type_serial, ZL_PrivateStandardNodeID_bitpack_serial, _1_SUCCESSOR(ZL_PrivateStandardGraphID_serial_store) ),
    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_bitpack_int, "!zl.private.bitpack_int", ZL_Type_numeric, ZL_PrivateStandardNodeID_bitpack_int, _1_SUCCESSOR(ZL_PrivateStandardGraphID_serial_store) ),

    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_constant_serial, "!zl.private.constant_serial", ZL_Type_serial, ZL_PrivateStandardNodeID_constant_serial, _1_SUCCESSOR(ZL_PrivateStandardGraphID_serial_store) ),
    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_constant_fixed, "!zl.private.constant_fixed", ZL_Type_struct, ZL_PrivateStandardNodeID_constant_fixed, _1_SUCCESSOR(ZL_PrivateStandardGraphID_serial_store) ),

    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_fse_ncount, "!zl.private.fse_ncount", ZL_Type_numeric, ZL_PrivateStandardNodeID_fse_ncount, _1_SUCCESSOR(ZL_PrivateStandardGraphID_serial_store) ),

    REGISTER_DYNAMIC_GRAPH(ZL_PrivateStandardGraphID_field_lz_literals, "!zl.private.field_lz_literals", ZL_Type_struct, EI_fieldLzLiteralsDynGraph),
    REGISTER_SELECTOR(ZL_PrivateStandardGraphID_field_lz_literals_channel, "!zl.private.field_lz_literals_channel", SI_fieldLzLiteralsChannelSelector, ZL_Type_serial),

    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_delta_huffman_internal, "!zl.private.delta_huffman_internal", ZL_Type_numeric, ZL_StandardNodeID_delta_int, _1_SUCCESSOR(ZL_StandardGraphID_huffman) ),
    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_delta_flatpack_internal, "!zl.private.flatpack_internal", ZL_Type_numeric, ZL_StandardNodeID_delta_int, _1_SUCCESSOR(ZL_StandardGraphID_flatpack) ),
    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_delta_zstd_internal, "!zl.private.zstd_internal", ZL_Type_numeric, ZL_StandardNodeID_delta_int, _1_SUCCESSOR(ZL_StandardGraphID_zstd) ),

    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_delta_huffman, "!zl.private.delta_huffman", ZL_Type_serial, ZL_StandardNodeID_convert_serial_to_num8, _1_SUCCESSOR(ZL_PrivateStandardGraphID_delta_huffman_internal) ),
    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_delta_flatpack, "!zl.private.delta_flatpack", ZL_Type_serial, ZL_StandardNodeID_convert_serial_to_num8, _1_SUCCESSOR(ZL_PrivateStandardGraphID_delta_flatpack_internal) ),
    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_delta_zstd, "!zl.private.delta_zstd", ZL_Type_serial, ZL_StandardNodeID_convert_serial_to_num8, _1_SUCCESSOR(ZL_PrivateStandardGraphID_delta_zstd_internal) ),

    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_delta_field_lz, "!zl.private.delta_field_lz", ZL_Type_numeric, ZL_StandardNodeID_delta_int, _1_SUCCESSOR(ZL_StandardGraphID_field_lz) ),
    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_range_pack, "!zl.private.range_pack", ZL_Type_numeric, ZL_StandardNodeID_range_pack, _1_SUCCESSOR(ZL_StandardGraphID_field_lz) ),
    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_range_pack_zstd, "!zl.private.range_pack_zstd", ZL_Type_numeric, ZL_StandardNodeID_range_pack, _1_SUCCESSOR(ZL_StandardGraphID_zstd) ),
    REGISTER_STATIC_GRAPH(ZL_PrivateStandardGraphID_tokenize_delta_field_lz, "!zl.private.tokenize_delta_field_lz", ZL_Type_numeric, ZL_PrivateStandardNodeID_tokenize_sorted, _2_SUCCESSORS(ZL_PrivateStandardGraphID_delta_field_lz, ZL_StandardGraphID_field_lz) ),

    REGISTER_DYNAMIC_GRAPH(ZL_PrivateStandardGraphID_split_serial, "!zl.private.split_serial", ZL_Type_serial, ZL_splitFnGraph),
    REGISTER_DYNAMIC_GRAPH(ZL_PrivateStandardGraphID_split_struct, "!zl.private.split_struct", ZL_Type_struct, ZL_splitFnGraph),
    REGISTER_DYNAMIC_GRAPH(ZL_PrivateStandardGraphID_split_numeric, "!zl.private.split_numeric", ZL_Type_numeric, ZL_splitFnGraph),
    REGISTER_DYNAMIC_GRAPH(ZL_PrivateStandardGraphID_split_string, "!zl.private.split_string", ZL_Type_string, ZL_splitFnGraph),

    REGISTER_MIGRAPH(ZL_PrivateStandardGraphID_n_to_n, MIGRAPH_N_TO_N),
};
// clang-format on

int GR_isStandardGraph(ZL_GraphID gid)
{
    return gid.gid < ZL_PrivateStandardGraphID_end;
}

static ZL_Report GR_validateStaticGraph(ZL_IDType sgid)
{
    ZL_ASSERT_LT(sgid, ZL_PrivateStandardGraphID_end);
    ZL_FunctionGraphDesc const migd = GR_standardGraphs[sgid].gdi.migd;
    ZL_ASSERT_EQ(migd.nbCustomNodes, 1);
    const CNode* const cnode = &ER_standardNodes[migd.customNodes[0].nid];
    ZL_ASSERT_NN(cnode);
    ZL_ASSERT_EQ(cnode->nodetype, node_internalTransform);
    const ZL_MIGraphDesc* const mitgd = &cnode->transformDesc.publicDesc.gd;
    ZL_ASSERT_EQ(mitgd->nbVOs, 0);

    const char* const gname            = STR_REPLACE_NULL(migd.name);
    const ZL_GraphID* const successors = migd.customGraphs;
    size_t const nbSuccessors          = migd.nbCustomGraphs;

    // Check compatibility with Head Node
    size_t const nbOutputs = mitgd->nbSOs;
    ZL_RET_R_IF_NE(
            logicError,
            mitgd->nbInputs,
            1,
            "Node %s has too many inputs",
            gname);
    ZL_RET_R_IF_NE(
            logicError,
            migd.inputTypeMasks[0],
            mitgd->inputTypes[0],
            "Incorrect input type for Graph %s",
            gname);

    // Ensure that Successors are valid
    ZL_RET_R_IF_NE(
            logicError,
            nbOutputs,
            nbSuccessors,
            "incorrect nb of successors for graph %s",
            gname);

    for (size_t n = 0; n < nbSuccessors; n++) {
        ZL_RET_R_IF_NOT(
                logicError,
                GR_isStandardGraph(successors[n]),
                "all successors of Graph %s must be standard Graphs",
                gname);
        const ZL_FunctionGraphDesc* succDesc =
                &GR_standardGraphs[successors[n].gid].gdi.migd;

        ZL_RET_R_IF_NE(
                logicError,
                succDesc->nbInputs,
                1,
                "Successor graph must take exactly one input");
        // check type mismatch
        ZL_Type const origType = mitgd->soTypes[n];
        ZL_Type const dstTypes = succDesc->inputTypeMasks[0];
        ZL_RET_R_IF_NOT(
                logicError,
                ICONV_isCompatible(origType, dstTypes),
                "one of the successors of graph %s requires an incompatible stream type (orig:%x != %x:dst)",
                gname,
                origType,
                dstTypes);
    }

    return ZL_returnSuccess();
}

/* =============================================== */
/*   =====   Wrappers   =====   */
/* =============================================== */

// Checks that Static Graphs have their versioning correctly set
// Any error detected must be fixed, and the code compiled again
void GR_validate(void)
{
    for (ZL_IDType sgid = 0; sgid < ZL_PrivateStandardGraphID_end; sgid++) {
        if ((GR_standardGraphs[sgid].type == GR_dynamicGraph)
            && GR_standardGraphs[sgid].gdi.migd.graph_f
                    == GR_staticGraphWrapper) {
            // Require is allowed here because it is a static check that must
            // always pass, otherwise Zstrong is completely broken.
            ZL_REQUIRE_SUCCESS(
                    GR_validateStaticGraph(sgid),
                    "Static check that is guaranteed to pass if the code is correct");
        }
    }
}

// Version for Static Graphs starting with a TypedTransform
ZL_Report
GR_staticGraphWrapper(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs)
{
    ZL_ASSERT_NN(gctx);
    ZL_ASSERT_NN(inputs);
    ZL_NodeIDList const headNode_l = ZL_Graph_getCustomNodes(gctx);
    ZL_ASSERT_EQ(headNode_l.nbNodeIDs, 1);
    ZL_NodeID const headNode      = headNode_l.nodeids[0];
    const ZL_LocalParams* lparams = GCTX_getAllLocalParams(gctx);
    if (lparams->intParams.nbIntParams == 0
        && lparams->copyParams.nbCopyParams == 0
        && lparams->refParams.nbRefParams == 0) {
        // no local parameter passed
        lparams = NULL;
    }
    ZL_TRY_LET_T(
            ZL_EdgeList,
            outputList,
            ZL_Edge_runMultiInputNode_withParams(
                    inputs, nbInputs, headNode, lparams));
    size_t const nbOutputs    = outputList.nbEdges;
    ZL_GraphIDList const gidl = ZL_Graph_getCustomGraphs(gctx);
    // Note: this version only supports TypedTransform as HeadNode
    ZL_ASSERT_EQ(gidl.nbGraphIDs, nbOutputs);
    for (size_t n = 0; n < nbOutputs; n++) {
        ZL_RET_R_IF_ERR(
                ZL_Edge_setDestination(outputList.edges[n], gidl.graphids[n]));
    }
    return ZL_returnSuccess();
}

// Version for Static Graphs starting with a VO Transform
// Note: there is probably a way to merge both Static Graph wrappers;
//       unclear if it's worth it though:
//       it might be more readable to keep them separated.
ZL_Report GR_VOGraphWrapper(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs)
{
    ZL_ASSERT_NN(gctx);
    ZL_ASSERT_EQ(nbInputs, 1);
    ZL_ASSERT_NN(inputs);
    ZL_NodeIDList const headNode_l = ZL_Graph_getCustomNodes(gctx);
    ZL_ASSERT_EQ(headNode_l.nbNodeIDs, 1);
    ZL_NodeID const headNode      = headNode_l.nodeids[0];
    const ZL_LocalParams* lparams = GCTX_getAllLocalParams(gctx);
    if (lparams->intParams.nbIntParams == 0
        && lparams->copyParams.nbCopyParams == 0
        && lparams->refParams.nbRefParams == 0) {
        // no local parameter passed
        lparams = NULL;
    }
    ZL_TRY_LET_T(
            ZL_EdgeList,
            outputList,
            ZL_Edge_runMultiInputNode_withParams(
                    inputs, nbInputs, headNode, lparams));
    size_t const nbOutputs           = outputList.nbEdges;
    ZL_GraphIDList const outcomeList = ZL_Graph_getCustomGraphs(gctx);

    const void* const pp = GCTX_getPrivateParam(gctx);
    ZL_ASSERT_NN(pp);
    unsigned const nbSingletons = ((const unsigned*)pp)[0];
    ZL_RET_R_IF_LT(
            nodeExecution_invalidOutputs,
            nbOutputs,
            (size_t)nbSingletons,
            "the head VO Node has not generated enough outputs according to its definition ");

    // Register and check that all singular outputs receive one successor.
    // This test relies on a property of the Engine which presents all Singular
    // outputs first, followed by the Variable outputs.
    for (size_t n = 0; n < nbOutputs; n++) {
        ZL_Edge* const sctx       = outputList.edges[n];
        ZL_IDType const outcomeID = StreamCtx_getOutcomeID(sctx);
        if (n < nbSingletons) {
            /* Singular output */
            ZL_RET_R_IF_NE(
                    nodeExecution_invalidOutputs,
                    outcomeID,
                    n,
                    "a Singular output has not received a Successor");
        } else {
            /* Variable outputs */
            ZL_RET_R_IF_LT(
                    nodeExecution_invalidOutputs,
                    outcomeID,
                    nbSingletons,
                    "overloading Singular output");
            ZL_RET_R_IF_GE(
                    nodeExecution_invalidOutputs,
                    outcomeID,
                    outcomeList.nbGraphIDs,
                    "Variable Output ID is out of range");
        }
        // assign successor
        ZL_GraphID const next_gid = outcomeList.graphids[outcomeID];
        ZL_RET_R_IF_ERR(ZL_Edge_setDestination(sctx, next_gid));
    }

    return ZL_returnSuccess();
}

ZL_Report
GR_selectorWrapper(ZL_Graph* gctx, ZL_Edge* inputCtxs[], size_t nbInputs)
{
    ZL_ASSERT_NN(gctx);
    ZL_ASSERT_EQ(nbInputs, 1);
    ZL_Edge* const inputCtx             = inputCtxs[0];
    const GR_SelectorFunction* const pp = GCTX_getPrivateParam(gctx);
    ZL_ASSERT_NN(pp);
    ZL_SelectorFn selector_f = pp->selector_f;
    ZL_ASSERT_NN(selector_f);

    /* note: type control and conversion are provided before
     * reaching this function */

    const ZL_LocalParams* const lparams = &gctx->dgd->localParams;
    ZL_GraphIDList const gidl           = ZL_Graph_getCustomGraphs(gctx);
    ZL_Selector siState;
    ALLOC_ARENA_MALLOC_CHECKED(
            SelectorSuccessorParams, successorParams, 1, gctx->graphArena);
    successorParams->params = NULL; // init to NULL, will be set by selector if
                                    // there are params to be sent
    ZL_RET_R_IF_ERR(SelCtx_initSelectorCtx(
            &siState,
            gctx->cctx,
            gctx->graphArena,
            lparams,
            successorParams,
            ZL_Graph_getOpaquePtr(gctx)));
    ZL_ASSERT_NN(selector_f);
    ZL_GraphID selectedSuccessor = selector_f(
            &siState,
            ZL_Edge_getData(inputCtx),
            gidl.graphids,
            gidl.nbGraphIDs);

    ZL_RuntimeGraphParameters const rgp = { .localParams =
                                                    successorParams->params };
    ZL_RET_R_IF_ERR(ZL_Edge_setParameterizedDestination(
            inputCtxs, nbInputs, selectedSuccessor, &rgp));
    SelCtx_destroySelectorCtx(&siState);

    return ZL_returnSuccess();
}

/* =============================================== */
/*   =====   Accessors   =====   */
/* =============================================== */

size_t GR_getNbStandardGraphs(void)
{
    size_t nbGraphs = 0;
    for (size_t i = 0; i < ZL_ARRAY_SIZE(GR_standardGraphs); ++i) {
        /* note: does not count serial_store (special internal) */
        if (GR_standardGraphs[i].type == GR_dynamicGraph) {
            ++nbGraphs;
        }
    }
    return nbGraphs;
}

void GR_getAllStandardGraphIDs(ZL_GraphID* graphs, size_t graphsSize)
{
    ZL_ASSERT_GE(graphsSize, GR_getNbStandardGraphs());
    size_t nbGraphs = 0;
    for (ZL_IDType gid = 0;
         gid < ZL_ARRAY_SIZE(GR_standardGraphs) && nbGraphs < graphsSize;
         ++gid) {
        if (GR_standardGraphs[gid].type == GR_dynamicGraph) {
            graphs[nbGraphs++].gid = gid;
            continue;
        }
    }
}

ZL_Report GR_forEachStandardGraph(GR_StandardGraphsCallback cb, void* opaque)
{
    for (ZL_IDType gid = 0; gid < ZL_ARRAY_SIZE(GR_standardGraphs); ++gid) {
        if (GR_standardGraphs[gid].type != GR_illegal) {
            ZL_RET_R_IF_ERR(
                    cb(opaque, (ZL_GraphID){ gid }, &GR_standardGraphs[gid]));
        }
    }
    return ZL_returnSuccess();
}
