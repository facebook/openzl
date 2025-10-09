// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/selectors/selector_store.h"
#include "openzl/compress/private_nodes.h" // ZL_GRAPH_*
#include "openzl/zl_data.h"
#include "openzl/zl_graph_api.h"

ZL_Report
MultiInputGraph_store(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs)
{
    (void)gctx;
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_RET_R_IF_ERR(ZL_Edge_setDestination(inputs[n], ZL_GRAPH_STORE1));
    }
    return ZL_returnSuccess();
}

/* SI_selector_store():
 *
 * Just dispatch between Variable-Size-Fields (VSF)
 * and other stream types which go towards ZL_GRAPH_STORE_SERIAL.
 */
ZL_GraphID SI_selector_store(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    ZL_Type const st = ZL_Input_type(inputStream);
    (void)selCtx;
    (void)customGraphs;
    (void)nbCustomGraphs;

    if (st == ZL_Type_string)
        return ZL_GRAPH_STRING_STORE;
    return ZL_GRAPH_SERIAL_STORE;
}
