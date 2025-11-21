// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/sddl/sddl2_profile.h"

#include "custom_parsers/shared_components/clustering.h"
#include "openzl/compress/graphs/sddl2/sddl2.h"

ZL_RESULT_OF(ZL_GraphID)
ZL_SDDL2_setupProfile(
        ZL_Compressor* const compressor,
        const void* const bytecode,
        const size_t bytecodeSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor);

    const ZL_GraphID clustering = ZS2_createGraph_genericClustering(compressor);
    const ZL_GraphID gid        = ZL_Compressor_registerSDDL2Graph(
            compressor, bytecode, bytecodeSize, clustering);
    ZL_ERR_IF(!ZL_GraphID_isValid(gid), graph_invalid);
    return ZL_WRAP_VALUE(gid);
}
