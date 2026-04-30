// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/nodemgr.h"

#include "openzl/codecs/encoder_registry.h" // STANDARD_ENCODERS_NB, SEncoders_array
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/limits.h"
#include "openzl/common/logging.h"

static ZL_Report
NM_fillStandardNodesCallback(void* opaque, ZL_NodeID node, const CNode* cnode)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    Nodes_manager* nmgr = opaque;
    const ZL_Name name  = CNODE_getNameObj(cnode);
    NodeMap_Insert insert =
            NodeMap_insertVal(&nmgr->nameMap, (NodeMap_Entry){ name, node });
    ZL_ERR_IF(insert.badAlloc, allocation);
    ZL_ASSERT_EQ(insert.ptr->val.nid, node.nid);
    return ZL_returnSuccess();
}

static ZL_Report NM_fillStandardNodes(Nodes_manager* nmgr)
{
    return ER_forEachStandardNode(NM_fillStandardNodesCallback, nmgr);
}

ZL_Report NM_init(Nodes_manager* nmgr, ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_ERR(CTM_init(&nmgr->ctm, opCtx));
    nmgr->nameMap = NodeMap_create(ZL_ENCODER_GRAPH_LIMIT);
    nmgr->opCtx   = opCtx;
    return NM_fillStandardNodes(nmgr);
}

void NM_destroy(Nodes_manager* nmgr)
{
    CTM_destroy(&nmgr->ctm);
    NodeMap_destroy(&nmgr->nameMap);
}

/* Implementation notes :
 * Using ID ranges to determine in which category (or manager) is stored a Node.
 * - Standard Nodes
 * -------- BASELINE_CUSTOM_NODE_IDS
 * - Custom Nodes
 */

#define BASELINE_CUSTOM_NODE_IDS STANDARD_ENCODERS_NB

static ZL_NodeID NM_NodeID_fromCNodeID(CNodeID cnodeid)
{
    return (ZL_NodeID){ cnodeid.cnid + BASELINE_CUSTOM_NODE_IDS };
}

int NM_isStandardNode(ZL_NodeID nodeid)
{
    return nodeid.nid < BASELINE_CUSTOM_NODE_IDS;
}

static CNodeID NM_CNodeID_fromNodeID(ZL_NodeID nodeid)
{
    ZL_ASSERT(!NM_isStandardNode(nodeid));
    return (CNodeID){ nodeid.nid - BASELINE_CUSTOM_NODE_IDS };
}

static ZL_Report NM_registerName(Nodes_manager* nmgr, ZL_NodeID node)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    const CNode* cnode = CTM_getCNode(&nmgr->ctm, NM_CNodeID_fromNodeID(node));
    ZL_ASSERT_NN(cnode);

    ZL_Name name = cnode->maybeName;
    ZL_ASSERT(!ZL_Name_isEmpty(&name));
    NodeMap_Insert insert =
            NodeMap_insertVal(&nmgr->nameMap, (NodeMap_Entry){ name, node });
    if (insert.badAlloc || !insert.inserted) {
        CTM_rollback(
                &nmgr->ctm, NM_CNodeID_fromNodeID(node)); // Rollback the state
        ZL_ERR_IF(insert.badAlloc, allocation);
        ZL_ASSERT(name.isAnchor, "Non-anchor is guaranteed to be unique");
        ZL_ERR(invalidName,
               "Node anchor name \"%s\" is not unique!",
               ZL_Name_unique(&name));
    }
    return ZL_returnSuccess();
}

ZL_RESULT_OF(ZL_NodeID)
NM_registerCustomTransform(
        Nodes_manager* nmgr,
        const InternalTransform_Desc* ctd)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_NodeID, NULL);
    ZL_ASSERT_NN(ctd);
    ZL_DLOG(BLOCK,
            "NM_registerCustomTransform '%s'",
            STR_REPLACE_NULL(ctd->publicDesc.name));
    ZL_ASSERT_NN(nmgr);
    // WARNING: Must not fail before this line otherwise opaque will be leaked
    ZL_RESULT_OF(CNodeID)
    cnodeidResult = CTM_registerCustomTransform(&nmgr->ctm, ctd);
    ZL_ERR_IF_ERR(cnodeidResult);
    ZL_NodeID gnid = NM_NodeID_fromCNodeID(ZL_RES_value(cnodeidResult));
    ZL_DLOG(SEQ,
            "Transform '%s' gets session ID %u",
            STR_REPLACE_NULL(ctd->publicDesc.name),
            gnid);
    ZL_ERR_IF_ERR(NM_registerName(nmgr, gnid));
    return ZL_RESULT_WRAP_VALUE(ZL_NodeID, gnid);
}

/* needed by encode_splitByStruct_binding */
ZL_RESULT_OF(ZL_NodeID)
NM_registerStandardTransform(
        Nodes_manager* nmgr,
        const InternalTransform_Desc* ctd,
        unsigned minFormatVersion,
        unsigned maxFormatVersion)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_NodeID, NULL);
    ZL_DLOG(BLOCK, "NM_registerStandardTransform");
    ZL_ASSERT_NN(nmgr);
    ZL_ASSERT_NULL(ctd->publicDesc.opaque.freeFn);
    // WARNING: Must not fail before this line otherwise opaque will be leaked
    ZL_RESULT_OF(CNodeID)
    cnodeidResult = CTM_registerStandardTransform(
            &nmgr->ctm, ctd, minFormatVersion, maxFormatVersion);
    ZL_ERR_IF_ERR(cnodeidResult);
    ZL_NodeID gnid = NM_NodeID_fromCNodeID(ZL_RES_value(cnodeidResult));
    ZL_ERR_IF_ERR(NM_registerName(nmgr, gnid));
    return ZL_RESULT_WRAP_VALUE(
            ZL_NodeID, NM_NodeID_fromCNodeID(ZL_RES_value(cnodeidResult)));
}

ZL_RESULT_OF(ZL_NodeID)
NM_parameterizeNode(Nodes_manager* nmgr, const ZL_ParameterizedNodeDesc* desc)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_NodeID, NULL);
    ZL_DLOG(BLOCK, "NM_parameterizeNode");
    const ZL_NodeID nodeid      = desc->node;
    const CNode* const srcCNode = NM_getCNode(nmgr, nodeid);
    ZL_ASSERT_NN(srcCNode);
    ZL_ASSERT_NN(nmgr);
    ZL_RESULT_OF(CNodeID)
    cnodeidResult = CTM_parameterizeNode(&nmgr->ctm, srcCNode, desc);
    ZL_ERR_IF_ERR(cnodeidResult);
    ZL_NodeID gnid = NM_NodeID_fromCNodeID(ZL_RES_value(cnodeidResult));
    ZL_ERR_IF_ERR(NM_registerName(nmgr, gnid));
    return ZL_RESULT_WRAP_VALUE(
            ZL_NodeID, NM_NodeID_fromCNodeID(ZL_RES_value(cnodeidResult)));
}

const CNode* NM_getCNode(const Nodes_manager* nmgr, ZL_NodeID nodeid)
{
    if (NM_isStandardNode(nodeid)) {
        return ER_standardNodes + nodeid.nid;
    }
    ZL_ASSERT_NN(nmgr);
    return CTM_getCNode(&nmgr->ctm, NM_CNodeID_fromNodeID(nodeid));
}

ZL_NodeID NM_getNodeByName(const Nodes_manager* nmgr, const char* node)
{
    // Lookup should not be done using anchor names
    if (node == NULL || ZL_keyIsAnchor(node)) {
        return ZL_NODE_ILLEGAL;
    }
    const ZL_Name key          = ZL_Name_wrapKey(node);
    const NodeMap_Entry* entry = NodeMap_find(&nmgr->nameMap, &key);
    if (entry != NULL) {
        return entry->val;
    } else {
        return ZL_NODE_ILLEGAL;
    }
}

ZL_Report NM_forEachNode(
        const Nodes_manager* nmgr,
        ZL_Compressor_ForEachNodeCallback callback,
        void* opaque,
        const ZL_Compressor* compressor)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    const size_t nbCNodes = CTM_nbCNodes(&nmgr->ctm);
    for (ZL_IDType id = 0; id < nbCNodes; ++id) {
        CNodeID cnodeID  = { id };
        ZL_NodeID nodeID = NM_NodeID_fromCNodeID(cnodeID);
        ZL_ERR_IF_ERR(callback(opaque, compressor, nodeID));
    }
    return ZL_returnSuccess();
}
