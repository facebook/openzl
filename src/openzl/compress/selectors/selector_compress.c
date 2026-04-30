// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/selectors/selector_compress.h"
#include "openzl/common/assertion.h"
#include "openzl/compress/private_nodes.h" // ZS2_GRAPH_COMPRESS_*
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_opaque_types.h"

ZL_Report
MultiInputGraph_compress(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs)
{
    ZL_DLOG(SEQ, "MultiInputGraph_compress: %zu inputs", nbInputs);
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(inputs[n], ZL_GRAPH_COMPRESS1));
    }
    return ZL_returnSuccess();
}

/* SI_selector_compress():
 *
 * Dispatch to type-specific compress selectors.
 */
ZL_GraphID SI_selector_compress(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    (void)selCtx;
    (void)customGraphs;
    (void)nbCustomGraphs;
    ZL_Type const st = ZL_Input_type(inputStream);
    ZL_DLOG(BLOCK, "SI_selector_compress (inType=%u)", st);

    ZL_GraphID graph;

    switch (st) {
        case ZL_Type_serial:
            graph = ZL_GRAPH_SERIAL_COMPRESS;
            break;
        case ZL_Type_struct:
            graph = ZL_GRAPH_STRUCT_COMPRESS;
            break;
        case ZL_Type_numeric:
            graph = ZL_GRAPH_NUMERIC_COMPRESS;
            break;
        case ZL_Type_string:
            graph = ZL_GRAPH_STRING_COMPRESS;
            break;
        default:
            ZL_ASSERT_FAIL("invalid stream type");
            return ZL_GRAPH_ILLEGAL;
    }
    return graph;
}

ZL_GraphID SI_selector_compress_serial(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    ZL_ASSERT_EQ(ZL_Input_type(inputStream), ZL_Type_serial);
    (void)selCtx;
    (void)customGraphs;
    (void)nbCustomGraphs;
    // In the future, we will probably arbitrate here between several methods.
    // Other LZ engines such as FastLZ and ROLZ come to mind.
    // Might even compete with Huffman or STORE.
    // This will require a more refined selector, maybe ML driven.
    // For the time being, just defer to zstd as a generic well-proven backup.
    return ZL_GRAPH_ZSTD;
}

ZL_GraphID SI_selector_compress_struct(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    ZL_ASSERT_EQ(ZL_Input_type(inputStream), ZL_Type_struct);
    (void)selCtx;
    (void)customGraphs;
    (void)nbCustomGraphs;
    return ZL_GRAPH_FIELD_LZ;
}

ZL_GraphID SI_selector_compress_numeric(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    ZL_ASSERT_EQ(ZL_Input_type(inputStream), ZL_Type_numeric);
    (void)selCtx;
    (void)customGraphs;
    (void)nbCustomGraphs;
    // There is no generic graph for numeric streams yet.
    // This will likely evolve in the future.
    // For the time being, defer to Fixed-size fields,
    // which will likely employ FieldLZ.
    return ZL_GRAPH_STRUCT_COMPRESS;
}

ZL_GraphID SI_selector_compress_string(
        const ZL_Selector* selCtx,
        const ZL_Input* inputStream,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs)
{
    ZL_ASSERT_EQ(ZL_Input_type(inputStream), ZL_Type_string);
    (void)selCtx;
    (void)customGraphs;
    (void)nbCustomGraphs;
    // For the time being, just split VSF Stream into its components,
    // and compress them independently, using generic compression graphs.
    // In the future, more specialized compressors, dedicated to variable size
    // fields, might compete with this simple generic split strategy.
    return ZL_GRAPH_STRING_SEPARATE_COMPRESS;
}
