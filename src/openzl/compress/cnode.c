// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/cnode.h"

#include "openzl/common/debug.h"
#include "openzl/common/wire_format.h"
#include "openzl/compress/compress_types.h"
#include "openzl/shared/utils.h"

const ZL_LocalParams* CNODE_getLocalParams(CNode const* cnode)
{
    ZL_ASSERT_EQ(
            cnode->nodetype,
            node_internalTransform,
            "Node must be an internal transform, illegal to call with illegal node type");
    return &cnode->transformDesc.publicDesc.localParams;
}

ZL_LocalIntParams CNODE_getLocalIntParams(CNode const* cnode)
{
    ZL_ASSERT_EQ(
            cnode->nodetype,
            node_internalTransform,
            "Node must be an internal transform, illegal to call with illegal node type");
    return cnode->transformDesc.publicDesc.localParams.intParams;
}

ZL_LocalCopyParams CNODE_getLocalCopyParams(CNode const* cnode)
{
    ZL_ASSERT_EQ(
            cnode->nodetype,
            node_internalTransform,
            "Node must be an internal transform, illegal to call with illegal node type");
    return cnode->transformDesc.publicDesc.localParams.copyParams;
}

ZL_LocalRefParams CNODE_getLocalRefParams(CNode const* cnode)
{
    ZL_ASSERT_EQ(
            cnode->nodetype,
            node_internalTransform,
            "Node must be an internal transform, illegal to call with illegal node type");
    return cnode->transformDesc.publicDesc.localParams.refParams;
}

PublicTransformInfo CNODE_getTransformID(const CNode* cnode)
{
    ZL_ASSERT_NN(cnode);
    ZL_DLOG(SEQ, "CNODE_getTransformID (address:%p)", cnode);
    ZL_ASSERT_EQ(cnode->nodetype, node_internalTransform);
    ZL_DLOG(SEQ, "cnode->nodetype = %i", cnode->nodetype);
    return (PublicTransformInfo){
        .trt  = cnode->publicIDtype,
        .trid = cnode->transformDesc.publicDesc.gd.CTid
    };
}

ZL_NodeID CNODE_getBaseNodeID(CNode const* cnode)
{
    ZL_ASSERT_NN(cnode);
    if (cnode == NULL) {
        return ZL_NODE_ILLEGAL;
    }
    return cnode->baseNodeID;
}

size_t CNODE_getNbInputPorts(const CNode* cnode)
{
    ZL_ASSERT_NN(cnode);
    ZL_ASSERT_EQ(cnode->nodetype, node_internalTransform);
    return cnode->transformDesc.publicDesc.gd.nbInputs;
}

bool CNODE_isVITransform(const CNode* cnode)
{
    ZL_ASSERT_NN(cnode);
    ZL_ASSERT_EQ(cnode->nodetype, node_internalTransform);
    return cnode->transformDesc.publicDesc.gd.lastInputIsVariable;
}

bool CNODE_isNbInputsCompatible(const CNode* cnode, size_t nbInputs)
{
    if (CNODE_isVITransform(cnode)) {
        return nbInputs >= CNODE_getNbInputPorts(cnode) - 1;
    } else {
        /* fixed nb of inputs */
        return nbInputs == CNODE_getNbInputPorts(cnode);
    }
}

ZL_Type CNODE_getInputType(const CNode* cnode, ZL_IDType inputIndex)
{
    ZL_ASSERT_NN(cnode);
    ZL_ASSERT_EQ(
            cnode->nodetype,
            node_internalTransform,
            "Node must be an internal transform, illegal to call with illegal node type");
    inputIndex =
            ZL_MIN(inputIndex, (ZL_IDType)CNODE_getNbInputPorts(cnode) - 1);
    return cnode->transformDesc.publicDesc.gd.inputTypes[inputIndex];
}

size_t CNODE_getNbOut1s(const CNode* cnode)
{
    ZL_ASSERT_NN(cnode);
    ZL_ASSERT_EQ(cnode->nodetype, node_internalTransform);
    return cnode->transformDesc.publicDesc.gd.nbSOs;
}

size_t CNODE_getNbVOs(const CNode* cnode)
{
    ZL_ASSERT_NN(cnode);
    ZL_ASSERT_EQ(cnode->nodetype, node_internalTransform);
    return cnode->transformDesc.publicDesc.gd.nbVOs;
}

size_t CNODE_getNbOutcomes(const CNode* cnode)
{
    ZL_ASSERT_NN(cnode);
    ZL_ASSERT_EQ(cnode->nodetype, node_internalTransform);
    return CNODE_getNbOut1s(cnode) + CNODE_getNbVOs(cnode);
}

int CNODE_isVO(CNode const* cnode, int outStreamIndex)
{
    ZL_ASSERT_GE(outStreamIndex, 0);
    ZL_ASSERT_LE((size_t)outStreamIndex, CNODE_getNbOutcomes(cnode));
    return ((size_t)outStreamIndex >= CNODE_getNbOut1s(cnode));
}

ZL_Type CNODE_getOutStreamType(const CNode* cnode, int outStreamIndex)
{
    ZL_ASSERT_NN(cnode);
    ZL_ASSERT_GE(outStreamIndex, 0);
    ZL_ASSERT_LT((size_t)outStreamIndex, CNODE_getNbOutcomes(cnode));
    const int nbOut1            = (int)CNODE_getNbOut1s(cnode);
    const int isOut1            = !CNODE_isVO(cnode, outStreamIndex);
    const ZL_MIGraphDesc* gdptr = &cnode->transformDesc.publicDesc.gd;
    if (isOut1)
        return gdptr->soTypes[outStreamIndex];
    return gdptr->voTypes[outStreamIndex - nbOut1];
}

static unsigned CNODE_getMinFormatVersion(CNode const* cnode)
{
    ZL_ASSERT_NN(cnode);
    if (cnode->publicIDtype == trt_standard) {
        // Must not be unset, but may be older than the minimum
        ZL_ASSERT_NE(cnode->minFormatVersion, 0);
        return ZL_MAX(cnode->minFormatVersion, ZL_MIN_FORMAT_VERSION);
    }
    // Unset for custom nodes
    ZL_ASSERT_EQ(cnode->minFormatVersion, 0);
    return ZL_MIN_FORMAT_VERSION;
}

static unsigned CNODE_getMaxFormatVersion(CNode const* cnode)
{
    ZL_ASSERT_NN(cnode);
    if (cnode->publicIDtype == trt_standard) {
        // Must not be unset
        ZL_ASSERT_NE(cnode->maxFormatVersion, 0);
        ZL_ASSERT_LE(cnode->maxFormatVersion, ZL_MAX_FORMAT_VERSION);
        return cnode->maxFormatVersion;
    }
    // Unset for custom nodes
    ZL_ASSERT_EQ(cnode->maxFormatVersion, 0);
    return ZL_MAX_FORMAT_VERSION;
}

CNODE_FormatInfo CNODE_getFormatInfo(CNode const* cnode)
{
    CNODE_FormatInfo info;
    info.minFormatVersion = CNODE_getMinFormatVersion(cnode);
    info.maxFormatVersion = CNODE_getMaxFormatVersion(cnode);
    return info;
}

ZL_Name CNODE_getNameObj(CNode const* cnode)
{
    if (ZL_Name_isEmpty(&cnode->maybeName)) {
        ZL_ASSERT_EQ(cnode->publicIDtype, trt_standard);
        return ZS2_Name_wrapStandard(cnode->transformDesc.publicDesc.name);
    } else {
        ZL_ASSERT_EQ(
                strcmp(ZL_Name_unique(&cnode->maybeName),
                       cnode->transformDesc.publicDesc.name),
                0);
        return cnode->maybeName;
    }
}

char const* CNODE_getName(CNode const* cnode)
{
    const ZL_Name name = CNODE_getNameObj(cnode);
    return ZL_Name_unique(&name);
}

bool CNODE_isTransformStandard(CNode const* cnode)
{
    return cnode->publicIDtype == trt_standard;
}

ZL_DictID CNODE_getDictID(CNode const* cnode)
{
    ZL_ASSERT_NN(cnode);
    ZL_ASSERT_EQ(cnode->nodetype, node_internalTransform);
    return cnode->transformDesc.publicDesc.dictID;
}
