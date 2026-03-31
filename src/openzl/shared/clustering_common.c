// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/shared/clustering.h"

ZL_Report ZL_ContextClustering_decode(
        ZL_ContextClustering* clustering,
        ZL_RC* src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    //> Read the max symbol value
    if (ZL_RC_avail(src) < 1) {
        ZL_ERR(GENERIC);
    }
    size_t const maxSymbol = ZL_RC_pop(src);
    clustering->maxSymbol  = maxSymbol;

    //> Read the contextToCluster map
    if (ZL_RC_avail(src) < maxSymbol + 1) {
        ZL_ERR(GENERIC);
    }
    memcpy(clustering->contextToCluster, ZL_RC_ptr(src), maxSymbol + 1);
    ZL_RC_advance(src, maxSymbol + 1);

    //> Compute the number of clusters
    size_t numClusters = 0;
    for (size_t context = 0; context <= maxSymbol; ++context) {
        if (clustering->contextToCluster[context] >= numClusters) {
            numClusters = (size_t)clustering->contextToCluster[context] + 1;
        }
    }
    clustering->numClusters = numClusters;

    return ZL_returnSuccess();
}
