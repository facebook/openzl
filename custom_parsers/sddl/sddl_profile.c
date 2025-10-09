// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/sddl/sddl_profile.h"

#include "custom_parsers/shared_components/clustering.h"

ZL_RESULT_OF(ZL_GraphID)
ZL_SDDL_setupProfile(
        ZL_Compressor* const compressor,
        const void* const description,
        const size_t descriptionSize)
{
    const ZL_GraphID clustering = ZL_createGraph_genericClustering(compressor);
    return ZL_Compressor_buildSDDLGraph(
            compressor, description, descriptionSize, clustering);
}
