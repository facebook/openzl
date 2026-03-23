// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_CNODES_H
#define ZSTRONG_COMPRESS_CNODES_H

#include "openzl/common/allocation.h" // Arena
#include "openzl/common/opaque.h"
#include "openzl/common/vector.h"
#include "openzl/compress/cnode.h"          // CNode
#include "openzl/compress/compress_types.h" // InternalTransform_Desc
#include "openzl/compress/materializer.h"   // MaterializedParamMap
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

typedef struct {
    ZL_IDType cnid;
} CNodeID;
ZL_RESULT_DECLARE_TYPE(CNodeID);

DECLARE_VECTOR_TYPE(CNode)

typedef struct CNodes_manager_s {
    VECTOR(CNode) cnodes;
    ZL_OpaquePtrRegistry opaquePtrs;
    Arena* allocator;
    MaterializedParamMap materializedParams;
    ZL_OperationContext* opCtx; // Non-owning pointer to error context
} CNodes_manager;

// Lifetime Management

ZL_Report CTM_init(CNodes_manager* ctm, ZL_OperationContext* opCtx);

void CTM_destroy(CNodes_manager* ctm);

/* Note: used to be called from runtime CCtx Node manager
 * which doesn't exist anymore.
 * So this reset capability is currently unused
 * since the CTM is now only employed in the CGraph,
 * where it's only initialized once */
void CTM_reset(CNodes_manager* ctm);

// Accessors

/*
 * CTM_getCNode() :
 * @return : pointer to the CNode associated with @ctrid.
 *           or NULL if @ctrid is invalid.
 */
const CNode* CTM_getCNode(const CNodes_manager* ctm, CNodeID ctrid);

/// @returns The number of registered cnodes
ZL_IDType CTM_nbCNodes(const CNodes_manager* ctm);

// Registation Actions

ZL_RESULT_OF(CNodeID)
CTM_parameterizeNode(
        CNodes_manager* ctm,
        const CNode* src,
        const ZL_ParameterizedNodeDesc* desc);

ZL_RESULT_OF(CNodeID)
CTM_registerCustomTransform(
        CNodes_manager* ctm,
        const InternalTransform_Desc* ctd);

/* needed by encode_splitByStruct_binding */
ZL_RESULT_OF(CNodeID)
CTM_registerStandardTransform(
        CNodes_manager* ctm,
        const InternalTransform_Desc* ctd,
        unsigned minFormatVersion,
        unsigned maxFormatVersion);

/**
 * Rolls back the registration of @p id
 * @warning This only works when @p id was the last node registered. If local
 * params are transferred or a materialized param created, it will not be freed.
 */
void CTM_rollback(CNodes_manager* ctm, CNodeID id);

ZL_END_C_DECLS

#endif
