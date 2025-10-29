// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/cnodes.h"
#include "openzl/common/allocation.h" // ALLOC_*, ZL_zeroes
#include "openzl/common/assertion.h"
#include "openzl/common/limits.h" // ZL_ENCODER_CUSTOM_NODE_LIMIT
#include "openzl/common/vector.h"
#include "openzl/common/wire_format.h" // trt_standard
#include "openzl/compress/cnode.h"
#include "openzl/compress/localparams.h"
#include "openzl/shared/mem.h" // ZL_memcpy
#include "openzl/shared/xxhash.h"
#include "openzl/zl_errors.h"

ZL_Report CTM_init(CNodes_manager* ctm)
{
    VECTOR_INIT(ctm->cnodes, ZL_ENCODER_CUSTOM_NODE_LIMIT);
    ZL_OpaquePtrRegistry_init(&ctm->opaquePtrs);
    // Note: this Arena could also be borrowed from the cgraph,
    // but it doesn't have one (yet).
    // Note 2: this is why this init function can fail
    ctm->allocator = ALLOC_HeapArena_create();
    ZL_RET_R_IF_NULL(allocation, ctm->allocator);
    return ZL_returnSuccess();
}

void CTM_destroy(CNodes_manager* ctm)
{
    ZL_DLOG(OBJ, "CTM_destroy");
    ZL_ASSERT_NN(ctm);
    ZL_OpaquePtrRegistry_destroy(&ctm->opaquePtrs);
    VECTOR_DESTROY(ctm->cnodes);
    ALLOC_Arena_freeArena(ctm->allocator);
    ZL_zeroes(ctm, sizeof(*ctm));
}

void CTM_reset(CNodes_manager* ctm)
{
    ZL_DLOG(FRAME, "CTM_reset");
    ZL_ASSERT_NN(ctm);
    ZL_OpaquePtrRegistry_reset(&ctm->opaquePtrs);
    VECTOR_RESET(ctm->cnodes);
    ALLOC_Arena_freeAll(ctm->allocator);
}

// Implementation Note: NULL is a valid return value for @bufferPtr,
// therefore it can't be used as an error signal
static ZL_Report
CTM_transferBuffer(CNodes_manager* ctm, const void** bufferPtr, size_t nbytes)
{
    if (nbytes == 0) {
        *bufferPtr = NULL;
    } else {
        void* const dst = ALLOC_Arena_malloc(ctm->allocator, nbytes);
        ZL_RET_R_IF_NULL(allocation, dst);
        ZL_memcpy(dst, *bufferPtr, nbytes);
        *bufferPtr = dst;
    }
    return ZL_returnSuccess();
}

static ZL_Report CTM_transferLocalParams(
        CNodes_manager* cnm,
        ZL_LocalParams* lp)
{
    return LP_transferLocalParams(cnm->allocator, lp);
}

/* CTM_transferStreamTypes():
 * Transfer streamType information from user-controlled memory
 * towards internal memory,
 * so that user-controlled memory can be flushed / released after registration.
 * @outST[0] is an in+out parameter,
 * it is modified to point into internal memory.
 */
static ZL_Report
CTM_transferStreamTypes(CNodes_manager* cnm, const ZL_Type** outST, size_t nbST)
{
    ZL_DLOG(BLOCK, "CTM_transferStreamTypes : nbST=%zu", nbST);
    void const* buffer = *outST;
    ZL_RET_R_IF_ERR(CTM_transferBuffer(cnm, &buffer, nbST * sizeof(**outST)));
    *outST = buffer;
    return ZL_returnSuccess();
}

static ZL_Report CTM_transferPrivateParam(
        CNodes_manager* cnm,
        InternalTransform_Desc* itd)
{
    ZL_ASSERT_NN(itd);
    ZL_DLOG(BLOCK, "CTM_transferPrivateParam: ppSize=%zu", itd->ppSize);
    return CTM_transferBuffer(cnm, &itd->privateParam, itd->ppSize);
}

/*
 * CTM_registerCNode() :
 * @return : ID of the registered CTransform *from a CTM perspective*.
 * The method copies parameters (integer and general) within local storage.
 * General Parameters are aligned on 8-bytes boundaries by default.
 * (Note : there is no way to control alignment of general parameters now,
 *         maybe this could be added later if needed).
 *
 * The following registration functions are essentially helpers.
 * They can also be achieved with just CTM_registerCNode(),
 * requiring just a few more lines at the invocation place
 * (nodemgr.c mostly for the time being).
 */
static ZL_RESULT_OF(CNodeID) CTM_registerCNode(
        CNodes_manager* ctm,
        const CNode* srcCNode,
        const char* prefix)
{
    ZL_ASSERT_NN(ctm);
    ZL_ASSERT_NN(srcCNode);
    ZL_DLOG(BLOCK,
            "CTM_registerCNode (type: %u) (for local ID=%u)",
            srcCNode->nodetype,
            VECTOR_SIZE(ctm->cnodes));
    ZL_ASSERT_NULL(
            srcCNode->transformDesc.publicDesc.opaque.freeFn,
            "Must already be registered with ZL_OpaquePtrRegistry");

    ZL_IDType const lnid = (ZL_IDType)VECTOR_SIZE(ctm->cnodes);

    // Need to check the name before pushing into the vector
    ZL_Name name;
    ZL_RET_T_IF_ERR(CNodeID, ZL_Name_init(&name, ctm->allocator, prefix, lnid));

    ZL_RET_T_IF_NOT(
            CNodeID,
            temporaryLibraryLimitation,
            VECTOR_PUSHBACK(ctm->cnodes, *srcCNode));

    CNode* const cnode = &VECTOR_AT(ctm->cnodes, lnid);
    ZL_DLOG(SEQ, "cnode address = %p", cnode);

    cnode->maybeName                     = name;
    cnode->transformDesc.publicDesc.name = ZL_Name_unique(&name);

    // Copy parameters into local storage (no dependency on input's memory)
    switch (cnode->nodetype) {
        /* only localParams */
        case node_internalTransform:
            ZL_RET_T_IF_ERR(
                    CNodeID,
                    CTM_transferPrivateParam(ctm, &cnode->transformDesc));
            ZL_MIEncoderDesc* const trDesc = &cnode->transformDesc.publicDesc;
            ZL_RET_T_IF_ERR(
                    CNodeID,
                    CTM_transferLocalParams(ctm, &trDesc->localParams));
            // A valid transform must have at least one input
            ZL_RET_T_IF_LT(
                    CNodeID,
                    node_invalid_input,
                    CNODE_getNbInputPorts(cnode),
                    1,
                    "Transform '%s' must declare at least 1 Input Port!",
                    CNODE_getName(cnode));
            // and at most ZL_runtimeNodeInputLimit() inputs
            ZL_RET_T_IF_GT(
                    CNodeID,
                    node_invalid_input,
                    CNODE_getNbInputPorts(cnode),
                    ZL_runtimeNodeInputLimit(ZL_MAX_FORMAT_VERSION),
                    "Too many inputs (%u) defined for transform '%s' (max=%u)",
                    CNODE_getNbInputPorts(cnode),
                    CNODE_getName(cnode),
                    ZL_runtimeNodeInputLimit(ZL_MAX_FORMAT_VERSION));
            ZL_RET_T_IF_ERR(
                    CNodeID,
                    CTM_transferStreamTypes(
                            ctm, &trDesc->gd.inputTypes, trDesc->gd.nbInputs));
            ZL_RET_T_IF_ERR(
                    CNodeID,
                    CTM_transferStreamTypes(
                            ctm, &trDesc->gd.soTypes, trDesc->gd.nbSOs));
            ZL_RET_T_IF_ERR(
                    CNodeID,
                    CTM_transferStreamTypes(
                            ctm, &trDesc->gd.voTypes, trDesc->gd.nbVOs));
            // Add automatic state ID when none provided
            if (trDesc->trStateMgr.optionalStateID == 0) {
                // Note: currently, void* opaque pointers are not exposed.
                // So using @transform_f only for the key.
                trDesc->trStateMgr.optionalStateID = (size_t)XXH3_64bits(
                        &trDesc->transform_f, sizeof(trDesc->transform_f));
            }
            break;
        case node_illegal:
            // We should never reach here with an illegal node
            ZL_ASSERT_FAIL("Impossible, illegal node type");
            ZL_RET_T_ERR(
                    CNodeID, GENERIC, "Trying to register an illegal node");
        default:
            ZL_ASSERT_FAIL(
                    "node type (%u) not possible at this stage",
                    cnode->nodetype);
    }
    return ZL_RESULT_WRAP_VALUE(CNodeID, (CNodeID){ lnid });
}

ZL_RESULT_OF(CNodeID)
CTM_registerCustomTransform(
        CNodes_manager* ctm,
        const InternalTransform_Desc* ctd)
{
    ZL_DLOG(BLOCK, "CTM_registerCustomTransform");
    ZL_RET_T_IF_ERR(
            CNodeID,
            ZL_OpaquePtrRegistry_register(
                    &ctm->opaquePtrs, ctd->publicDesc.opaque));
    CNode cnode      = { .nodetype      = node_internalTransform,
                         .publicIDtype  = trt_custom,
                         .transformDesc = *ctd,
                         .baseNodeID    = ZL_NODE_ILLEGAL };
    const char* name = ctd->publicDesc.name;
    // Registered => No need to free
    cnode.transformDesc.publicDesc.opaque.freeFn = NULL;
    return CTM_registerCNode(ctm, &cnode, name);
}

ZL_RESULT_OF(CNodeID)
CTM_registerStandardTransform(
        CNodes_manager* ctm,
        const InternalTransform_Desc* ctd,
        unsigned minFormatVersion,
        unsigned maxFormatVersion)
{
    ZL_DLOG(BLOCK, "CTM_registerStandardTransform");
    ZL_RET_T_IF_ERR(
            CNodeID,
            ZL_OpaquePtrRegistry_register(
                    &ctm->opaquePtrs, ctd->publicDesc.opaque));
    CNode cnode      = { .nodetype         = node_internalTransform,
                         .publicIDtype     = trt_standard,
                         .minFormatVersion = minFormatVersion,
                         .maxFormatVersion = maxFormatVersion,
                         .transformDesc    = *ctd,
                         .baseNodeID       = ZL_NODE_ILLEGAL };
    const char* name = ctd->publicDesc.name;
    // Registered => No need to free
    cnode.transformDesc.publicDesc.opaque.freeFn = NULL;
    return CTM_registerCNode(ctm, &cnode, name);
}

ZL_RESULT_OF(CNodeID)
CTM_parameterizeNode(
        CNodes_manager* ctm,
        const CNode* srcCNode,
        const ZL_ParameterizedNodeDesc* desc)
{
    ZL_RET_T_IF_NE(
            CNodeID,
            node_invalid,
            srcCNode->nodetype,
            node_internalTransform,
            "Invalid CNode");
    CNode clonedCNode      = *srcCNode;
    clonedCNode.baseNodeID = desc->node;
    if (desc->localParams) {
        clonedCNode.transformDesc.publicDesc.localParams = *desc->localParams;
    }
    if (desc->name == NULL) {
        const ZL_Name name = CNODE_getNameObj(srcCNode);
        // Use the name prefix rather than the unique name, because this node
        // needs a new non-anchor name.
        const char* prefix = ZL_Name_prefix(&name);
        return CTM_registerCNode(ctm, &clonedCNode, prefix);
    } else {
        return CTM_registerCNode(ctm, &clonedCNode, desc->name);
    }
}

void CTM_rollback(CNodes_manager* ctm, CNodeID id)
{
    ZL_ASSERT_EQ(id.cnid + 1, VECTOR_SIZE(ctm->cnodes));
    VECTOR_POPBACK(ctm->cnodes);
}

const CNode* CTM_getCNode(const CNodes_manager* ctm, CNodeID cnodeid)
{
    ZL_ASSERT_NN(ctm);
    if (cnodeid.cnid >= VECTOR_SIZE(ctm->cnodes))
        return NULL;
    return &VECTOR_AT(ctm->cnodes, cnodeid.cnid);
}

ZL_IDType CTM_nbCNodes(const CNodes_manager* ctm)
{
    return (ZL_IDType)VECTOR_SIZE(ctm->cnodes);
}
