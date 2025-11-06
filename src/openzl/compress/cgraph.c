// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/cgraph.h"
#include "openzl/common/allocation.h" // ZL_malloc, ZL_free
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h" // ZS2_RET_IF_ERR
#include "openzl/common/opaque.h"
#include "openzl/common/operation_context.h"
#include "openzl/compress/cctx.h" // CCTX_setOutBufferSizes
#include "openzl/compress/cnode.h"
#include "openzl/compress/enc_interface.h"  // ZL_Encoder definition
#include "openzl/compress/gcparams.h"       // GCParams
#include "openzl/compress/graph_registry.h" // GR_staticGraphWrapper
#include "openzl/compress/graphmgr.h"       // Graphs_manager
#include "openzl/compress/nodemgr.h"        // Nodes_manager
#include "openzl/zl_compress.h"             // ZL_Compressor*
#include "openzl/zl_compressor.h"           // ZS2_declare*_*
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_reflection.h"
#include "openzl/zl_segmenter.h"
#include "openzl/zl_selector.h"

// ******************************************************************
// CGraph
// ******************************************************************

struct ZL_Compressor_s {
    Nodes_manager nmgr;
    GraphsMgr* gm;
    ZL_GraphID starting_graph;
    GCParams gcparams;
    ZL_OperationContext opCtx; // for error logging
}; /* note typedef'd to ZL_Compressor in zs2_compress.h */

ZL_Compressor* ZL_Compressor_create(void)
{
    ZL_Compressor* const cgraph = ZL_calloc(sizeof(ZL_Compressor));
    if (cgraph == NULL)
        return NULL;
    if (ZL_isError(NM_init(&cgraph->nmgr, &cgraph->opCtx))) {
        ZL_Compressor_free(cgraph);
        return NULL;
    }
    cgraph->gm = GM_create(&cgraph->nmgr);
    if (cgraph->gm == NULL) {
        ZL_Compressor_free(cgraph);
        return NULL;
    }
    cgraph->starting_graph = (ZL_GraphID){ .gid = 0 }; // default

#if ZL_ENABLE_ASSERT
    // In debug mode, runtime check on the configuration of the Standard Graphs
    GR_validate();
#endif

    ZL_OC_init(&cgraph->opCtx);
    ZL_OC_startOperation(&cgraph->opCtx, ZL_Operation_createCGraph);
    return cgraph;
}

void ZL_Compressor_free(ZL_Compressor* cgraph)
{
    if (!cgraph)
        return;
    ZL_OC_destroy(&cgraph->opCtx);
    GM_free(cgraph->gm);
    NM_destroy(&cgraph->nmgr);
    ZL_free(cgraph);
}

ZL_Report
ZL_Compressor_setParameter(ZL_Compressor* cgraph, ZL_CParam gcparam, int value)
{
    ZL_ASSERT_NN(cgraph);
    return GCParams_setParameter(&cgraph->gcparams, gcparam, value);
}

int ZL_Compressor_getParameter(const ZL_Compressor* cgraph, ZL_CParam gcparam)
{
    ZL_ASSERT_NN(cgraph);
    return GCParams_getParameter(&cgraph->gcparams, gcparam);
}

const GCParams* CGRAPH_getGCParams(const ZL_Compressor* cgraph)
{
    ZL_ASSERT_NN(cgraph);
    return &cgraph->gcparams;
}

// ******************************************************************
// CGraph creation
// ******************************************************************

ZL_Report ZL_Compressor_initUsingGraphFn(ZL_Compressor* cgraph, ZL_GraphFn f)
{
    ZL_GraphID const graphHead = f(cgraph);
    return ZL_Compressor_selectStartingGraphID(cgraph, graphHead);
}

ZL_Report ZL_Compressor_selectStartingGraphID(
        ZL_Compressor* cgraph,
        ZL_GraphID gid)
{
    ZL_ASSERT_NN(cgraph);
    if (CGRAPH_checkGraphIDExists(cgraph, gid)) {
        ZL_DLOG(FRAME,
                "ZL_Compressor_selectStartingGraphID '%s' (%u)",
                ZL_Compressor_Graph_getName(cgraph, gid),
                gid.gid);
    }
    ZL_RET_R_IF_ERR(ZL_Compressor_validate(cgraph, gid));
    cgraph->starting_graph = gid;
    return ZL_returnSuccess();
}

// ******************************************************************
// Node registration API
// ******************************************************************

int ZL_NodeID_isValid(ZL_NodeID nodeid)
{
    return nodeid.nid != ZL_NODE_ILLEGAL.nid;
}

static ZL_Report
CGraph_pipeAdaptor(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbInputs)
{
    ZL_DLOG(BLOCK, "CGraph_pipeAdaptor");
    ZL_ASSERT_NN(ins);
    ZL_ASSERT_EQ(nbInputs, 1);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    const void* const src = ZL_Input_ptr(in);
    size_t const srcSize  = ZL_Input_numElts(in);

    const ZL_PipeEncoderDesc* const pipeDesc = ENC_getPrivateParam(eictx);
    ZL_ASSERT_NN(pipeDesc);
    size_t const outCapacity = (pipeDesc->dstBound_f == NULL)
            ? srcSize
            : pipeDesc->dstBound_f(src, srcSize);

    ZL_ASSERT_NN(eictx);
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eictx, 0, outCapacity, 1);
    ZL_RET_R_IF_NULL(allocation, out);

    ZL_RET_R_IF_NULL(customNode_definitionInvalid, pipeDesc->transform_f);
    size_t const dstSize = pipeDesc->transform_f(
            ZL_Output_ptr(out), outCapacity, src, srcSize);

    ZL_RET_R_IF_GT(transform_executionFailure, dstSize, outCapacity);
    ZL_RET_R_IF_ERR(ZL_Output_commit(out, dstSize));

    return ZL_returnValue(1);
}

ZL_NodeID ZL_Compressor_registerPipeEncoder(
        ZL_Compressor* cgraph,
        const ZL_PipeEncoderDesc* cptd)
{
    ZL_DLOG(BLOCK, "ZL_Compressor_registerPipeEncoder");

    ZL_MIGraphDesc const gd = {
        .CTid       = cptd->CTid,
        .inputTypes = (const ZL_Type[]){ ZL_Type_serial },
        .nbInputs   = 1,
        .soTypes    = (const ZL_Type[]){ ZL_Type_serial },
        .nbSOs      = 1,
    };
    ZL_MIEncoderDesc const ttd = {
        .gd          = gd,
        .transform_f = CGraph_pipeAdaptor,
        .name        = cptd->name,
    };
    InternalTransform_Desc const itd = {
        .publicDesc   = ttd,
        .privateParam = cptd,
        .ppSize       = sizeof(*cptd),
    };
    ZL_ASSERT_NN(cgraph);
    ZL_RESULT_OF(ZL_NodeID)
    nodeidResult = NM_registerCustomTransform(&cgraph->nmgr, &itd);
    if (ZL_RES_isError(nodeidResult)) {
        return ZL_NODE_ILLEGAL;
    }
    return ZL_RES_value(nodeidResult);
}

typedef struct {
    ZL_SplitEncoderFn transform_f;
    size_t nbOuts;
} SplitAdaptor_param;

static ZL_Report
CGraph_splitAdaptor(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbInputs)
{
    ZL_DLOG(BLOCK, "CGraph_splitAdaptor");
    ZL_ASSERT_NN(ins);
    ZL_ASSERT_EQ(nbInputs, 1);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    const void* const src = ZL_Input_ptr(in);
    size_t const srcSize  = ZL_Input_numElts(in);

    const SplitAdaptor_param* const splitDesc = ENC_getPrivateParam(eictx);
    ZL_ASSERT_NN(splitDesc);

    size_t const nbDsts = splitDesc->nbOuts;
    size_t* const dstSizes =
            ZL_Encoder_getScratchSpace(eictx, nbDsts * sizeof(*dstSizes));
    ZL_RET_R_IF_NULL(allocation, dstSizes);
    ZL_Report const r = splitDesc->transform_f(eictx, dstSizes, src, srcSize);
    ZL_RET_R_IF_ERR(r);
    ZL_ASSERT_EQ(
            nbDsts, ZL_validResult(r)); // create as many outputs as pledged

    ZL_ASSERT_NN(eictx);
    ZL_RET_R_IF_ERR(CCTX_setOutBufferSizes(
            eictx->cctx, eictx->rtnodeid, dstSizes, nbDsts));

    return r;
}

#define ZL_SPLIT_TRANSFORM_OUT_STREAM_LIMIT 32

ZL_NodeID ZL_Compressor_registerSplitEncoder(
        ZL_Compressor* cgraph,
        const ZL_SplitEncoderDesc* cstd)
{
    ZL_DLOG(BLOCK, "ZL_Compressor_registerSplitEncoder");

    if (cstd->nbOutputStreams > ZL_SPLIT_TRANSFORM_OUT_STREAM_LIMIT) {
        ZL_DLOG(ERROR,
                "Too many outputs for split transform: %zu",
                cstd->nbOutputStreams);
        return ZL_NODE_ILLEGAL;
    }

    ZL_Type outSTs[ZL_SPLIT_TRANSFORM_OUT_STREAM_LIMIT];
    for (size_t n = 0; n < ZL_SPLIT_TRANSFORM_OUT_STREAM_LIMIT; n++)
        outSTs[n] = ZL_Type_serial;

    SplitAdaptor_param const sap = {
        .nbOuts      = cstd->nbOutputStreams,
        .transform_f = cstd->transform_f,
    };

    ZL_MIGraphDesc const graphDesc = {
        .CTid       = cstd->CTid,
        .inputTypes = (const ZL_Type[]){ ZL_Type_serial },
        .nbInputs   = 1,
        .nbSOs      = cstd->nbOutputStreams,
        .soTypes    = outSTs,
    };

    ZL_MIEncoderDesc const votd = {
        .gd          = graphDesc,
        .localParams = cstd->localParams,
        .transform_f = CGraph_splitAdaptor,
        .name        = cstd->name,
    };

    InternalTransform_Desc const itd = {
        .publicDesc   = votd,
        .privateParam = &sap,
        .ppSize       = sizeof(sap),
    };
    ZL_ASSERT_NN(cgraph);
    ZL_RESULT_OF(ZL_NodeID)
    nodeidResult = NM_registerCustomTransform(&cgraph->nmgr, &itd);
    if (ZL_RES_isError(nodeidResult)) {
        return ZL_NODE_ILLEGAL;
    }
    return ZL_RES_value(nodeidResult);
}

typedef struct {
    ZL_VOEncoderFn transform_f;
} VOAdaptor_desc;

static ZL_Report
CGraph_VOAdaptor(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbInputs)
{
    ZL_DLOG(BLOCK, "CGraph_VOAdaptor");
    ZL_ASSERT_NN(ins);
    ZL_ASSERT_EQ(nbInputs, 1);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);

    const VOAdaptor_desc* const voDesc = ENC_getPrivateParam(eictx);
    ZL_ASSERT_NN(voDesc);
    return voDesc->transform_f(eictx, in);
}

ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_registerTypedEncoder2(
        ZL_Compressor* compressor,
        const ZL_TypedEncoderDesc* desc)
{
    ZL_DLOG(BLOCK, "ZL_Compressor_registerTypedEncoder");

    ZL_MIGraphDesc const migd = {
        .CTid       = desc->gd.CTid,
        .inputTypes = (const ZL_Type[]){ desc->gd.inStreamType },
        .nbInputs   = 1,
        .soTypes    = desc->gd.outStreamTypes,
        .nbSOs      = desc->gd.nbOutStreams
    };

    ZL_MIEncoderDesc const votd = {
        .gd          = migd,
        .localParams = desc->localParams,
        .transform_f = CGraph_VOAdaptor,
        .name        = desc->name,
        .opaque      = desc->opaque,
    };

    VOAdaptor_desc const voad = {
        .transform_f = desc->transform_f,
    };

    InternalTransform_Desc const itd = {
        .publicDesc   = votd,
        .privateParam = &voad,
        .ppSize       = sizeof(voad),
    };
    ZL_ASSERT_NN(compressor);
    // WARNING: Must not fail before this line otherwise opaque will be leaked
    return NM_registerCustomTransform(&compressor->nmgr, &itd);
}

ZL_NodeID ZL_Compressor_registerTypedEncoder(
        ZL_Compressor* compressor,
        const ZL_TypedEncoderDesc* desc)
{
    ZL_RESULT_OF(ZL_NodeID)
    nodeidResult = ZL_Compressor_registerTypedEncoder2(compressor, desc);
    if (ZL_RES_isError(nodeidResult)) {
        return ZL_NODE_ILLEGAL;
    }
    return ZL_RES_value(nodeidResult);
}

ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_registerVOEncoder2(
        ZL_Compressor* compressor,
        const ZL_VOEncoderDesc* desc)
{
    ZL_DLOG(BLOCK, "ZL_Compressor_registerVOEncoder");

    ZL_MIGraphDesc const migd = {
        .CTid       = desc->gd.CTid,
        .inputTypes = (const ZL_Type[]){ desc->gd.inStreamType },
        .nbInputs   = 1,
        .soTypes    = desc->gd.singletonTypes,
        .nbSOs      = desc->gd.nbSingletons,
        .voTypes    = desc->gd.voTypes,
        .nbVOs      = desc->gd.nbVOs,
    };

    ZL_MIEncoderDesc const mitd = {
        .gd          = migd,
        .localParams = desc->localParams,
        .transform_f = CGraph_VOAdaptor,
        .name        = desc->name,
        .opaque      = desc->opaque,
    };

    VOAdaptor_desc const voad = {
        .transform_f = desc->transform_f,
    };

    InternalTransform_Desc const itd = {
        .publicDesc   = mitd,
        .privateParam = &voad,
        .ppSize       = sizeof(voad),
    };
    ZL_ASSERT_NN(compressor);
    // WARNING: Must not fail before this line otherwise opaque will be leaked
    return NM_registerCustomTransform(&compressor->nmgr, &itd);
}

ZL_NodeID ZL_Compressor_registerVOEncoder(
        ZL_Compressor* compressor,
        const ZL_VOEncoderDesc* desc)
{
    ZL_RESULT_OF(ZL_NodeID)
    nodeidResult = ZL_Compressor_registerVOEncoder2(compressor, desc);
    if (ZL_RES_isError(nodeidResult)) {
        return ZL_NODE_ILLEGAL;
    }
    return ZL_RES_value(nodeidResult);
}

ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_parameterizeNode(
        ZL_Compressor* compressor,
        ZL_NodeID node,
        const ZL_NodeParameters* params)
{
    ZL_DLOG(BLOCK, "ZL_Compressor_parameterizeNode");
    ZL_ASSERT_NN(compressor);
    ZL_ASSERT_NN(params);
    ZL_ParameterizedNodeDesc desc = {
        .name        = params->name,
        .node        = node,
        .localParams = params->localParams,
    };

    return NM_parameterizeNode(&compressor->nmgr, &desc);
}

ZL_NodeID ZL_Compressor_registerParameterizedNode(
        ZL_Compressor* compressor,
        const ZL_ParameterizedNodeDesc* desc)
{
    ZL_ASSERT_NN(desc);
    ZL_NodeParameters params = {
        .name        = desc->name,
        .localParams = desc->localParams,
    };
    ZL_RESULT_OF(ZL_NodeID)
    nodeidResult =
            ZL_Compressor_parameterizeNode(compressor, desc->node, &params);
    if (ZL_RES_isError(nodeidResult)) {
        return ZL_NODE_ILLEGAL;
    }
    return ZL_RES_value(nodeidResult);
}

ZL_NodeID ZL_Compressor_cloneNode(
        ZL_Compressor* cgraph,
        ZL_NodeID nodeid,
        const ZL_LocalParams* localParams)
{
    ZL_ParameterizedNodeDesc desc = {
        .name        = NULL,
        .node        = nodeid,
        .localParams = localParams,
    };
    return ZL_Compressor_registerParameterizedNode(cgraph, &desc);
}

ZL_NodeID CGraph_registerStandardVOTransform(
        ZL_Compressor* cgraph,
        const ZL_VOEncoderDesc* votd,
        unsigned minFormatVersion,
        unsigned maxFormatVersion)
{
    ZL_DLOG(BLOCK, "CGraph_registerStandardVOTransform");

    ZL_MIGraphDesc const migd = {
        .CTid       = votd->gd.CTid,
        .inputTypes = (const ZL_Type[]){ votd->gd.inStreamType },
        .nbInputs   = 1,
        .soTypes    = votd->gd.singletonTypes,
        .nbSOs      = votd->gd.nbSingletons,
        .voTypes    = votd->gd.voTypes,
        .nbVOs      = votd->gd.nbVOs,
    };

    ZL_MIEncoderDesc const mitd = {
        .gd          = migd,
        .localParams = votd->localParams,
        .transform_f = CGraph_VOAdaptor,
        .name        = votd->name,
        .opaque      = votd->opaque,
    };

    VOAdaptor_desc const voad = {
        .transform_f = votd->transform_f,
    };

    InternalTransform_Desc const itd = {
        .publicDesc   = mitd,
        .privateParam = &voad,
        .ppSize       = sizeof(voad),
    };
    ZL_ASSERT_NN(cgraph);
    // WARNING: Must not fail before this line otherwise opaque will be leaked
    ZL_RESULT_OF(ZL_NodeID)
    nodeidResult = NM_registerStandardTransform(
            &cgraph->nmgr, &itd, minFormatVersion, maxFormatVersion);
    if (ZL_RES_isError(nodeidResult)) {
        return ZL_NODE_ILLEGAL;
    }
    return ZL_RES_value(nodeidResult);
}

ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_registerMIEncoder2(
        ZL_Compressor* compressor,
        const ZL_MIEncoderDesc* desc)
{
    ZL_DLOG(BLOCK, "ZL_Compressor_registerMultiInputEncoder");

    InternalTransform_Desc const itd = {
        .publicDesc = *desc,
    };
    ZL_ASSERT_NN(compressor);
    // WARNING: Must not fail before this line otherwise opaque will be leaked
    return NM_registerCustomTransform(&compressor->nmgr, &itd);
}

ZL_NodeID ZL_Compressor_registerMIEncoder(
        ZL_Compressor* compressor,
        const ZL_MIEncoderDesc* desc)
{
    ZL_RESULT_OF(ZL_NodeID)
    nodeidResult = ZL_Compressor_registerMIEncoder2(compressor, desc);
    if (ZL_RES_isError(nodeidResult)) {
        return ZL_NODE_ILLEGAL;
    }
    return ZL_RES_value(nodeidResult);
}

ZL_NodeID CGraph_registerStandardMITransform(
        ZL_Compressor* cgraph,
        const ZL_MIEncoderDesc* mitd,
        unsigned minFormatVersion,
        unsigned maxFormatVersion)
{
    ZL_DLOG(BLOCK, "CGraph_registerStandardMITransform");

    InternalTransform_Desc const itd = {
        .publicDesc = *mitd,
    };
    ZL_ASSERT_NN(cgraph);
    // WARNING: Must not fail before this line otherwise opaque will be leaked
    ZL_RESULT_OF(ZL_NodeID)
    nodeidResult = NM_registerStandardTransform(
            &cgraph->nmgr, &itd, minFormatVersion, maxFormatVersion);
    if (ZL_RES_isError(nodeidResult)) {
        return ZL_NODE_ILLEGAL;
    }
    return ZL_RES_value(nodeidResult);
}

// ******************************************************************
// Selector registration API
// ******************************************************************

enum { cgraph_gpid_simpleToTypes_selector = 33011 };

static ZL_GraphID CGraph_simpleToTyped_selector(
        const ZL_Selector* selCtx,
        const ZL_Input* in,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_ASSERT_NN(selCtx);
    ZL_CopyParam const gp = ZL_Selector_getLocalCopyParam(
            selCtx, cgraph_gpid_simpleToTypes_selector);
    ZL_ASSERT_EQ(gp.paramId, cgraph_gpid_simpleToTypes_selector);
    ZL_ASSERT_EQ(gp.paramSize, sizeof(ZL_SerialSelectorDesc));
    const ZL_SerialSelectorDesc* const selectorDesc = gp.paramPtr;

    return selectorDesc->selector_f(
            ZL_Input_ptr(in),
            ZL_Input_numElts(in),
            customGraphs,
            nbCustomGraphs);
}

ZL_GraphID ZL_Compressor_registerSerialSelectorGraph(
        ZL_Compressor* cgraph,
        const ZL_SerialSelectorDesc* csd)
{
    ZL_DLOG(BLOCK, "ZL_Compressor_registerSerialSelectorGraph");

    ZL_CopyParam const gp = {
        .paramId   = cgraph_gpid_simpleToTypes_selector,
        .paramPtr  = csd,
        .paramSize = sizeof(*csd),
    };

    ZL_LocalParams const lp = {
        .copyParams = { &gp, 1 },
    };

    ZL_SelectorDesc const tselDesc = {
        .selector_f     = CGraph_simpleToTyped_selector,
        .inStreamType   = ZL_Type_serial,
        .customGraphs   = csd->customGraphs,
        .nbCustomGraphs = csd->nbCustomGraphs,
        .localParams    = lp,
        .name           = csd->name,
    };

    return ZL_Compressor_registerSelectorGraph(cgraph, &tselDesc);
}

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_registerSelectorGraph2(
        ZL_Compressor* compressor,
        const ZL_SelectorDesc* desc)
{
    ZL_DLOG(BLOCK,
            "ZL_Compressor_registerSelectorGraph2 (%zu candidate successors)",
            desc->nbCustomGraphs);
    ZL_ASSERT_NN(compressor);
    ZL_ASSERT_NN(desc);

    // WARNING: Must not fail before this line otherwise opaque will be leaked
    return GM_registerTypedSelectorGraph(compressor->gm, desc);
}

ZL_GraphID ZL_Compressor_registerSelectorGraph(
        ZL_Compressor* compressor,
        const ZL_SelectorDesc* desc)
{
    ZL_RESULT_OF(ZL_GraphID)
    graphidResult = ZL_Compressor_registerSelectorGraph2(compressor, desc);
    if (ZL_RES_isError(graphidResult)) {
        return ZL_GRAPH_ILLEGAL;
    } else {
        return ZL_RES_value(graphidResult);
    }
}

// ******************************************************************
// Static Graphs registration API
// ******************************************************************

bool CGRAPH_checkGraphIDExists(const ZL_Compressor* cgraph, ZL_GraphID graphid)
{
    ZL_DLOG(SEQ, "CGRAPH_checkGraphIDExists (gid=%u)", graphid.gid);
    return GM_isValidGraphID(cgraph->gm, graphid);
}

// Registers Static Graphs
ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildStaticGraph(
        ZL_Compressor* compressor,
        ZL_NodeID headNode,
        const ZL_GraphID* successorGraphs,
        size_t numSuccessorGraphs,
        const ZL_StaticGraphParameters* params)
{
    ZL_DLOG(BLOCK,
            "ZL_Compressor_buildStaticGraph %s (%zu successors)",
            params ? STR_REPLACE_NULL(params->name) : "NULL",
            numSuccessorGraphs);
    ZL_ASSERT_NN(compressor);
    ZL_StaticGraphDesc desc = {
        .headNodeid     = headNode,
        .successor_gids = successorGraphs,
        .nbGids         = numSuccessorGraphs,
    };
    if (params != NULL) {
        desc.name        = params->name;
        desc.localParams = params->localParams;
    }
    return GM_registerStaticGraph(compressor->gm, &desc);
}

ZL_GraphID ZL_Compressor_registerStaticGraph(
        ZL_Compressor* compressor,
        const ZL_StaticGraphDesc* desc)
{
    ZL_ASSERT_NN(desc);
    ZL_StaticGraphParameters params = {
        .name        = desc->name,
        .localParams = desc->localParams,
    };
    ZL_RESULT_OF(ZL_GraphID)
    graphidResult = ZL_Compressor_buildStaticGraph(
            compressor,
            desc->headNodeid,
            desc->successor_gids,
            desc->nbGids,
            &params);
    if (ZL_RES_isError(graphidResult)) {
        return ZL_GRAPH_ILLEGAL;
    } else {
        return ZL_RES_value(graphidResult);
    }
}

ZL_GraphID ZL_Compressor_registerStaticGraph_fromNode(
        ZL_Compressor* cgraph,
        ZL_NodeID nodeid,
        const ZL_GraphID* dst_gids,
        size_t nbGids)
{
    ZL_DLOG(BLOCK,
            "ZL_Compressor_registerStaticGraph_fromNode '%s' (%zu successors)",
            CNODE_getName(CGRAPH_getCNode(cgraph, nodeid)),
            nbGids);
    ZL_ASSERT_NN(cgraph);

    if (!ZL_NodeID_isValid(nodeid)) {
        return ZL_GRAPH_ILLEGAL;
    }

    // Try to give the graph a meaningful name.
    char tmp_name_buf[ZL_NAME_MAX_LEN + 1];
    const char* head_node_name = ZL_Compressor_Node_getName(cgraph, nodeid);
    if (head_node_name != NULL) {
        strncpy(tmp_name_buf, head_node_name, sizeof(tmp_name_buf));
        tmp_name_buf[ZL_NAME_MAX_LEN] = '\0';
        char* hash                    = strstr(tmp_name_buf, "#");
        if (hash != NULL) {
            *hash = '\0';
        }
    } else {
        tmp_name_buf[0] = '\0';
    }
    const ZL_StaticGraphDesc gDesc = {
        .name           = tmp_name_buf,
        .headNodeid     = nodeid,
        .successor_gids = dst_gids,
        .nbGids         = nbGids,
    };
    return ZL_Compressor_registerStaticGraph(cgraph, &gDesc);
}

ZL_GraphID ZL_Compressor_registerStaticGraph_fromNode1o(
        ZL_Compressor* cgraph,
        ZL_NodeID nodeid,
        ZL_GraphID successor_graph)
{
    ZL_DLOG(BLOCK,
            "ZL_Compressor_registerStaticGraph_fromNode1o (nid=%u, successor_gid=%u)",
            nodeid.nid,
            successor_graph.gid);
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, nodeid, &successor_graph, 1);
}

ZL_GraphID ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
        ZL_Compressor* cgraph,
        const ZL_NodeID* nodes,
        size_t nbVnids,
        ZL_GraphID dst_graphID)
{
    ZL_ASSERT_NN(cgraph);
    ZL_GraphID successor = dst_graphID;
    ZL_ASSERT_NN(nodes);
    for (size_t n = 0; n < nbVnids; n++) {
        successor = ZL_Compressor_registerStaticGraph_fromNode1o(
                cgraph, nodes[nbVnids - 1 - n], successor);
    }
    return successor;
}

// ******************************************************************
// Dynamic Graphs registration API
// ******************************************************************

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_registerFunctionGraph2(
        ZL_Compressor* compressor,
        const ZL_FunctionGraphDesc* desc)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor);
    ZL_ASSERT_NN(desc);
    ZL_DLOG(BLOCK,
            "ZL_Compressor_registerFunctionGraph '%s'",
            STR_REPLACE_NULL(desc->name));
    ZL_ASSERT_NN(cgraph);
    if (desc->validate_f && !desc->validate_f(compressor, desc)) {
        ZL_OpaquePtr_free(desc->opaque);
        ZL_ERR(graph_invalid, "Validation failed");
    }

    // WARNING: Failures before this line must free migd->opaque
    return GM_registerMultiInputGraph(compressor->gm, desc);
}

ZL_GraphID ZL_Compressor_registerFunctionGraph(
        ZL_Compressor* compressor,
        const ZL_FunctionGraphDesc* desc)
{
    ZL_RESULT_OF(ZL_GraphID)
    graphidResult = ZL_Compressor_registerFunctionGraph2(compressor, desc);
    if (ZL_RES_isError(graphidResult)) {
        return ZL_GRAPH_ILLEGAL;
    } else {
        return ZL_RES_value(graphidResult);
    }
}

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_parameterizeGraph(
        ZL_Compressor* compressor,
        ZL_GraphID graph,
        const ZL_GraphParameters* params)
{
    ZL_ParameterizedGraphDesc desc = {
        .name           = params->name,
        .graph          = graph,
        .customGraphs   = params->customGraphs,
        .nbCustomGraphs = params->nbCustomGraphs,
        .customNodes    = params->customNodes,
        .nbCustomNodes  = params->nbCustomNodes,
        .localParams    = params->localParams,
    };
    return GM_registerParameterizedGraph(compressor->gm, &desc);
}

ZL_Report ZL_Compressor_overrideGraphParams(
        ZL_Compressor* compressor,
        ZL_GraphID graph,
        const ZL_GraphParameters* gp)
{
    ZL_RESULT_DECLARE_SCOPE(size_t, compressor);
    ZL_ERR_IF_NOT(
            CGRAPH_checkGraphIDExists(compressor, graph),
            graph_invalid,
            "Graph must be registered in compressor");

    ZL_ERR_IF_ERR(GM_overrideGraphParams(compressor->gm, graph, gp));
    return ZL_returnSuccess();
}

ZL_GraphID ZL_Compressor_registerParameterizedGraph(
        ZL_Compressor* compressor,
        const ZL_ParameterizedGraphDesc* desc)
{
    ZL_GraphParameters params = {
        .name           = desc->name,
        .customGraphs   = desc->customGraphs,
        .nbCustomGraphs = desc->nbCustomGraphs,
        .customNodes    = desc->customNodes,
        .nbCustomNodes  = desc->nbCustomNodes,
        .localParams    = desc->localParams,
    };
    ZL_RESULT_OF(ZL_GraphID)
    graphidResult =
            ZL_Compressor_parameterizeGraph(compressor, desc->graph, &params);
    if (ZL_RES_isError(graphidResult)) {
        return ZL_GRAPH_ILLEGAL;
    } else {
        return ZL_RES_value(graphidResult);
    }
}

// ******************************************************************
// Segmenter registration API
// ******************************************************************

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_registerSegmenter2(
        ZL_Compressor* compressor,
        const ZL_SegmenterDesc* desc)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor);
    ZL_ASSERT_NN(desc);
    ZL_DLOG(BLOCK,
            "ZL_Compressor_registerSegmenter2 '%s' (%zu inputs)",
            STR_REPLACE_NULL(desc->name),
            desc->numInputs);
    ZL_ASSERT_NN(compressor);

    // WARNING: Failures before this line must free migd->opaque
    return GM_registerSegmenter(compressor->gm, desc);
}

ZL_GraphID ZL_Compressor_registerSegmenter(
        ZL_Compressor* compressor,
        const ZL_SegmenterDesc* desc)
{
    ZL_RESULT_OF(ZL_GraphID)
    graphidResult = ZL_Compressor_registerSegmenter2(compressor, desc);
    if (ZL_RES_isError(graphidResult)) {
        return ZL_GRAPH_ILLEGAL;
    } else {
        return ZL_RES_value(graphidResult);
    }
}

// ******************************************************************
// Public Accessors
// ******************************************************************

int ZL_GraphID_isValid(ZL_GraphID graphid)
{
    return graphid.gid != ZL_GRAPH_ILLEGAL.gid;
}

ZL_GraphID ZL_Compressor_getGraph(
        const ZL_Compressor* compressor,
        const char* graph)
{
    return GM_getGraphByName(compressor->gm, graph);
}

ZL_NodeID ZL_Compressor_getNode(
        const ZL_Compressor* compressor,
        const char* node)
{
    return NM_getNodeByName(&compressor->nmgr, node);
}

// ******************************************************************
// Private Accessors
// ******************************************************************

GraphType_e CGRAPH_graphType(const ZL_Compressor* cgraph, ZL_GraphID graphid)
{
    ZL_ASSERT_NN(cgraph);
    return GM_graphType(cgraph->gm, graphid);
}

ZL_GraphID CGRAPH_getStartingGraphID(const ZL_Compressor* cgraph)
{
    ZL_DLOG(FRAME, "CGRAPH_getStartingGraphID");
    ZL_ASSERT_NN(cgraph);
    if (cgraph->starting_graph.gid > 0) {
        // Explicit selection
        return cgraph->starting_graph;
    }
    // Default: last registered graph
    return GM_getLastRegisteredGraph(cgraph->gm);
}

const CNode* CGRAPH_getCNode(const ZL_Compressor* cgraph, ZL_NodeID nodeid)
{
    ZL_ASSERT_NN(cgraph);
    return NM_getCNode(&cgraph->nmgr, nodeid);
}

NodeType_e CGRAPH_getNodeType(const ZL_Compressor* cgraph, ZL_NodeID nodeid)
{
    const CNode* const cnode = CGRAPH_getCNode(cgraph, nodeid);
    return cnode->nodetype;
}

const ZL_FunctionGraphDesc* CGRAPH_getMultiInputGraphDesc(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid)
{
    ZL_DLOG(SEQ, "CGRAPH_getMultiInputGraphDesc (gid=%u)", graphid.gid);
    ZL_ASSERT_NN(compressor);
    return GM_getMultiInputGraphDesc(compressor->gm, graphid);
}

const ZL_SegmenterDesc* CGRAPH_getSegmenterDesc(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid)
{
    ZL_ASSERT_NN(compressor);
    return GM_getSegmenterDesc(compressor->gm, graphid);
}

const void* CGRAPH_graphPrivateParam(
        const ZL_Compressor* cgraph,
        ZL_GraphID graphid)
{
    ZL_ASSERT_NN(cgraph);
    return GM_getPrivateParam(cgraph->gm, graphid);
}

// ******************************************************************
// Public reflection API
// ******************************************************************

ZL_Report ZL_Compressor_forEachGraph(
        const ZL_Compressor* compressor,
        ZL_Compressor_ForEachGraphCallback callback,
        void* opaque)
{
    return GM_forEachGraph(compressor->gm, callback, opaque, compressor);
}

ZL_Report ZL_Compressor_forEachNode(
        const ZL_Compressor* compressor,
        ZL_Compressor_ForEachNodeCallback callback,
        void* opaque)
{
    return NM_forEachNode(&compressor->nmgr, callback, opaque, compressor);
}

ZL_Report ZL_Compressor_forEachParam(
        const ZL_Compressor* compressor,
        ZL_Compressor_ForEachParamCallback callback,
        void* opaque)
{
    return GCParams_forEachParam(&compressor->gcparams, callback, opaque);
}

bool ZL_Compressor_getStartingGraphID(
        const ZL_Compressor* compressor,
        ZL_GraphID* graphID)
{
    *graphID = CGRAPH_getStartingGraphID(compressor);
    return graphID->gid != ZL_GRAPH_ILLEGAL.gid;
}

const char* ZL_Compressor_Graph_getName(
        const ZL_Compressor* cgraph,
        ZL_GraphID graphid)
{
    if (graphid.gid == ZL_GRAPH_ILLEGAL.gid) {
        return NULL;
    }
    ZL_ASSERT(CGRAPH_checkGraphIDExists(cgraph, graphid));
    return GM_getGraphName(cgraph->gm, graphid);
}

ZL_GraphType ZL_Compressor_getGraphType(
        const ZL_Compressor* compressor,
        ZL_GraphID graph)
{
    return GM_getGraphMetadata(compressor->gm, graph).graphType;
}

ZL_Type ZL_Compressor_Graph_getInput0Mask(
        const ZL_Compressor* cgraph,
        ZL_GraphID graphid)
{
    ZL_ASSERT_NN(cgraph);
    ZL_ASSERT_EQ(GM_getGraphNbInputs(cgraph->gm, graphid), 1);
    return GM_getGraphInput0Mask(cgraph->gm, graphid);
}

ZL_Type ZL_Compressor_Graph_getInputMask(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid,
        size_t inputIdx)
{
    const GM_GraphMetadata meta = GM_getGraphMetadata(compressor->gm, graphid);
    ZL_ASSERT_LT(inputIdx, meta.nbInputs);
    return meta.inputTypeMasks[inputIdx];
}

size_t ZL_Compressor_Graph_getNumInputs(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid)
{
    return GM_getGraphMetadata(compressor->gm, graphid).nbInputs;
}

bool ZL_Compressor_Graph_isVariableInput(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid)
{
    return GM_getGraphMetadata(compressor->gm, graphid).lastInputIsVariable;
}

ZL_NodeID ZL_Compressor_Graph_getHeadNode(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid)
{
    const GM_GraphMetadata meta = GM_getGraphMetadata(compressor->gm, graphid);
    if (meta.graphType == ZL_GraphType_static) {
        return meta.customNodes[0];
    } else {
        return ZL_NODE_ILLEGAL;
    }
}

ZL_GraphID ZL_Compressor_Graph_getBaseGraphID(
        ZL_Compressor const* compressor,
        ZL_GraphID graphid)
{
    return GM_getGraphMetadata(compressor->gm, graphid).baseGraphID;
}

ZL_GraphIDList ZL_Compressor_Graph_getSuccessors(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid)
{
    const GM_GraphMetadata meta = GM_getGraphMetadata(compressor->gm, graphid);
    if (meta.graphType == ZL_GraphType_static) {
        return (ZL_GraphIDList){ meta.customGraphs, meta.nbCustomGraphs };
    } else {
        return (ZL_GraphIDList){ NULL, 0 };
    }
}

ZL_NodeIDList ZL_Compressor_Graph_getCustomNodes(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid)
{
    const GM_GraphMetadata meta = GM_getGraphMetadata(compressor->gm, graphid);
    if (meta.graphType == ZL_GraphType_static) {
        return (ZL_NodeIDList){ NULL, 0 };
    } else {
        return (ZL_NodeIDList){ meta.customNodes, meta.nbCustomNodes };
    }
}

ZL_GraphIDList ZL_Compressor_Graph_getCustomGraphs(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid)
{
    const GM_GraphMetadata meta = GM_getGraphMetadata(compressor->gm, graphid);
    if (meta.graphType == ZL_GraphType_static) {
        return (ZL_GraphIDList){ NULL, 0 };
    } else {
        return (ZL_GraphIDList){ meta.customGraphs, meta.nbCustomGraphs };
    }
}

ZL_LocalParams ZL_Compressor_Graph_getLocalParams(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid)
{
    return GM_getGraphMetadata(compressor->gm, graphid).localParams;
}

size_t ZL_Compressor_Node_getNumInputs(
        ZL_Compressor const* cgraph,
        ZL_NodeID node)
{
    ZL_ASSERT_NN(cgraph);
    return CNODE_getNbInputPorts(CGRAPH_getCNode(cgraph, node));
}

ZL_Type ZL_Compressor_Node_getInput0Type(
        ZL_Compressor const* cgraph,
        ZL_NodeID nodeid)
{
    ZL_ASSERT_EQ(CNODE_getNbInputPorts(CGRAPH_getCNode(cgraph, nodeid)), 1);
    return CNODE_getInputType(CGRAPH_getCNode(cgraph, nodeid), 0);
}

ZL_Type ZL_Compressor_Node_getInputType(
        ZL_Compressor const* cgraph,
        ZL_NodeID nodeid,
        ZL_IDType inputIndex)
{
    ZL_ASSERT_LT(
            inputIndex, CNODE_getNbInputPorts(CGRAPH_getCNode(cgraph, nodeid)));
    return CNODE_getInputType(CGRAPH_getCNode(cgraph, nodeid), inputIndex);
}

bool ZL_Compressor_Node_isVariableInput(
        const ZL_Compressor* compressor,
        ZL_NodeID nodeid)
{
    return CNODE_isVITransform(CGRAPH_getCNode(compressor, nodeid));
}

size_t ZL_Compressor_Node_getNumOutcomes(
        ZL_Compressor const* cgraph,
        ZL_NodeID nodeid)
{
    return CNODE_getNbOutcomes(CGRAPH_getCNode(cgraph, nodeid));
}

size_t ZL_Compressor_Node_getNumVariableOutcomes(
        ZL_Compressor const* cgraph,
        ZL_NodeID nodeid)
{
    return CNODE_getNbVOs(CGRAPH_getCNode(cgraph, nodeid));
}

ZL_Type ZL_Compressor_Node_getOutputType(
        ZL_Compressor const* cgraph,
        ZL_NodeID nodeid,
        int outputIndex)
{
    return CNODE_getOutStreamType(CGRAPH_getCNode(cgraph, nodeid), outputIndex);
}

ZL_LocalParams ZL_Compressor_Node_getLocalParams(
        const ZL_Compressor* cgraph,
        ZL_NodeID nodeid)
{
    return *CNODE_getLocalParams(CGRAPH_getCNode(cgraph, nodeid));
}

unsigned ZL_Compressor_Node_getMaxVersion(
        ZL_Compressor const* cgraph,
        ZL_NodeID node)
{
    return CNODE_getFormatInfo(CGRAPH_getCNode(cgraph, node)).maxFormatVersion;
}

unsigned ZL_Compressor_Node_getMinVersion(
        ZL_Compressor const* cgraph,
        ZL_NodeID node)
{
    return CNODE_getFormatInfo(CGRAPH_getCNode(cgraph, node)).minFormatVersion;
}

ZL_IDType ZL_Compressor_Node_getCodecID(
        ZL_Compressor const* cgraph,
        ZL_NodeID node)
{
    return CNODE_getTransformID(CGRAPH_getCNode(cgraph, node)).trid;
}

ZL_NodeID ZL_Compressor_Node_getBaseNodeID(
        ZL_Compressor const* cgraph,
        ZL_NodeID node)
{
    return CNODE_getBaseNodeID(CGRAPH_getCNode(cgraph, node));
}

char const* ZL_Compressor_Node_getName(
        ZL_Compressor const* cgraph,
        ZL_NodeID node)
{
    return CNODE_getName(CGRAPH_getCNode(cgraph, node));
}

bool ZL_Compressor_Node_isStandard(ZL_Compressor const* cgraph, ZL_NodeID node)
{
    return CNODE_isTransformStandard(CGRAPH_getCNode(cgraph, node));
}

// ******************************************************************
// Errors & warnings
// ******************************************************************

ZL_CONST_FN
ZL_OperationContext* ZL_Compressor_getOperationContext(ZL_Compressor* cgraph)
{
    if (cgraph == NULL) {
        return NULL;
    }
    return &cgraph->opCtx;
}

char const* ZL_Compressor_getErrorContextString(
        ZL_Compressor const* cgraph,
        ZL_Report report)
{
    if (!ZL_isError(report)) {
        return NULL;
    }
    return ZL_OC_getErrorContextString(&cgraph->opCtx, ZL_RES_error(report));
}

const char* ZL_Compressor_getErrorContextString_fromError(
        const ZL_Compressor* cgraph,
        ZL_Error error)
{
    if (!ZL_E_isError(error)) {
        return NULL;
    }
    return ZL_OC_getErrorContextString(&cgraph->opCtx, error);
}

ZL_Error_Array ZL_Compressor_getWarnings(ZL_Compressor const* cgraph)
{
    return ZL_OC_getWarnings(&cgraph->opCtx);
}
