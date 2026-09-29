// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_NODEMGR_H
#define ZSTRONG_COMPRESS_NODEMGR_H

#include "openzl/common/map.h"
#include "openzl/common/opaque_types_internal.h" // ZL_RESULT_OF(ZL_NodeID)
#include "openzl/compress/cnodes.h"              // CNodes_manager
#include "openzl/compress/name.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" // ZL_SelectorDesc
#include "openzl/zl_errors.h"     // ZL_RESULT_OF
#include "openzl/zl_reflection.h"

ZL_BEGIN_C_DECLS

ZL_DECLARE_PREDEF_MAP_TYPE(NodeMap, ZL_Name, ZL_NodeID);

typedef struct {
    CNodes_manager ctm;
    /// Contains a map from name -> node for all standard & custom nodes
    NodeMap nameMap;
    ZL_OperationContext* opCtx;
} Nodes_manager;

// Lifetime management

ZL_Report NM_init(Nodes_manager* nmgr, ZL_OperationContext* opCtx);
void NM_destroy(Nodes_manager* nmgr);

// Write Accessors

ZL_RESULT_OF(ZL_NodeID)
NM_registerCustomTransform(
        Nodes_manager* nmgr,
        const InternalTransform_Desc* ctd);

/* needed by encode_splitByStruct_binding */
ZL_RESULT_OF(ZL_NodeID)
NM_registerStandardTransform(
        Nodes_manager* nmgr,
        const InternalTransform_Desc* ctd,
        unsigned minFormatVersion,
        unsigned maxFormatVersion);

ZL_RESULT_OF(ZL_NodeID)
NM_parameterizeNode(Nodes_manager* nmgr, const ZL_ParameterizedNodeDesc* desc);

ZL_Report NM_overrideNodeParams(
        Nodes_manager* nmgr,
        ZL_NodeID node,
        const ZL_NodeParameters* np);

// Read Accessors

/*
 * NM_getCNode() :
 * @return : pointer to the CNode associated with @nodeid.
 *           or NULL if @nodeid is invalid.
 */
const CNode* NM_getCNode(const Nodes_manager* nmgr, ZL_NodeID nodeid);

ZL_NodeID NM_getNodeByName(const Nodes_manager* nmgr, const char* node);

int NM_isStandardNode(ZL_NodeID nodeid);

/// @see ZL_Compressor_forEachNode
ZL_Report NM_forEachNode(
        const Nodes_manager* nmgr,
        ZL_Compressor_ForEachNodeCallback callback,
        void* opaque,
        const ZL_Compressor* compressor);

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_NODEMGR_H
