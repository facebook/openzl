// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "graphmgr.h"
#include "openzl/common/errors_internal.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_graph_api.h"

static ZL_Report CGraph_validateGraphAtGid(
        ZL_Compressor* cgraph,
        ZL_GraphID gid)
{
    ZL_DLOG(BLOCK, "CGraph_validateGraphAtGid (%u)", gid.gid);

    ZL_RET_R_IF_NOT(graph_invalid, ZL_GraphID_isValid(gid));

    return ZL_returnSuccess();
}

ZL_Report ZL_Compressor_validate(
        ZL_Compressor* cgraph,
        const ZL_GraphID starting_graph)
{
    ZL_DLOG(BLOCK, "ZL_Compressor_validate");
    // Check that we start with a valid gid
    ZL_RET_R_IF_ERR(CGraph_validateGraphAtGid(cgraph, starting_graph));
    // Note (@Cyan): since zstrong supports Typed Inputs, there is no longer a
    // requirement for Starting Graph to support Serial Input.
    return ZL_returnSuccess();
}
