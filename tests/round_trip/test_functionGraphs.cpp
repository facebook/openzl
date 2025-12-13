// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

// standard C
#include <array>
#include <stdio.h> // printf

// Zstrong
#include <zstd.h> // ZSTD_c_* parameters
#include "openzl/codecs/zl_concat.h"
#include "openzl/codecs/zl_delta.h"
#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_illegal.h"
#include "openzl/compress/private_nodes.h" // ZL_NODE_ZSTD_FIXED_DEPRECATED
#include "openzl/zl_compress.h"            // ZL_CCtx_compress
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_decompress.h" // ZL_decompress
#include "openzl/zl_errors.h"     // ZL_TRY_LET_T
#include "openzl/zl_graph_api.h"  // ZL_FunctionGraphDesc
#include "openzl/zl_localParams.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_selector.h"
#include "openzl/zl_version.h" // ZL_MIN_FORMAT_VERSION

namespace {

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#if 0 // for debug only
void printHexa(const void* p, size_t size)
{
    const unsigned char* const b = (const unsigned char*)p;
    for (size_t n = 0; n < size; n++) {
        printf(" %02X ", b[n]);
    }
    printf("\n");
}
#endif

/* ------   create custom transforms   -------- */

/* none */

/* --------   define custom graphs   -------- */

static ZL_Report
justGoToZstd(ZL_Graph*, ZL_Edge* inputs[], size_t nbInputs) noexcept
{
    assert(nbInputs == 1);
    // send input to successor (which must be a Graph)
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(inputs[0], ZL_GRAPH_ZSTD));
    return ZL_returnSuccess();
}

static ZL_Type serialInputType                     = ZL_Type_serial;
static ZL_FunctionGraphDesc const justGoToZstd_dgd = {
    .name                = "just-go-to-zstd function graph",
    .graph_f             = justGoToZstd,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

static ZL_Report
dg_zstd_wLevel(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    // Require presence of compression level parameter
    ZL_IntParam const param_lvl =
            ZL_Graph_getLocalIntParam(gctx, ZSTD_c_compressionLevel);
    ZL_RET_R_IF_NE(
            graphParameter_invalid,
            param_lvl.paramId,
            (int)ZSTD_c_compressionLevel);
    // Run zstd Node with runtime parameters
    ZL_LocalParams const lps = { .intParams = { &param_lvl, 1 } };
    ZL_TRY_LET_T(
            ZL_EdgeList,
            out,
            ZL_Edge_runNode_withParams(input, ZL_NODE_ZSTD, &lps));
    assert(out.nbEdges == 1);
    // store output
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(out.edges[0], ZL_GRAPH_STORE));
    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const zstd_wLevel_dgd = {
    .name           = "compress with zstd, but require a compression level",
    .graph_f        = dg_zstd_wLevel,
    .inputTypeMasks = &serialInputType,
    .nbInputs       = 1,
    .lastInputIsVariable = false,
};

// note: this global variable is set within registerDynGraph()
ZL_GraphID g_zstd_wLevel_graphid = ZL_GRAPH_ILLEGAL;
static ZL_Report runZstdGraph_withParameters(
        ZL_Graph* gctx,
        ZL_Edge* inputs[],
        size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    ZL_IntParam const param_lvl =
            ZL_Graph_getLocalIntParam(gctx, ZSTD_c_compressionLevel);
    assert(param_lvl.paramId == ZSTD_c_compressionLevel);
    int const clevel = param_lvl.paramValue;

    // Create runtime parameters
    ZL_IntParam const zstd_cparams[] = { { ZSTD_c_compressionLevel, clevel } };
    ZL_LocalParams const lps         = { .intParams = { zstd_cparams,
                                                        ARRAY_SIZE(zstd_cparams) } };
    ZL_RuntimeGraphParameters const rgp = { .localParams = &lps };

    // Set Successor (zstd Graph) with runtime parameters
    // Note that it's fine to use the stack for the parameters.
    ZL_RET_R_IF_ERR(ZL_Edge_setParameterizedDestination(
            &input, 1, g_zstd_wLevel_graphid, &rgp));

    return ZL_returnSuccess();
}

static ZL_IntParam const zstd_level1     = { ZSTD_c_compressionLevel, 1 };
static ZL_LocalParams const lp_zstd_lvl1 = { .intParams = { &zstd_level1, 1 } };

static ZL_IntParam const zstd_level19     = { ZSTD_c_compressionLevel, 19 };
static ZL_LocalParams const lp_zstd_lvl19 = { .intParams = { &zstd_level19,
                                                             1 } };

static ZL_FunctionGraphDesc const runZstdGraph_withParameters_lvl1_dgd = {
    .name    = "Invoke zstd_wLevel Graph with runtime parameter (level 1)",
    .graph_f = runZstdGraph_withParameters,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
    .localParams         = lp_zstd_lvl1,
};

static ZL_FunctionGraphDesc const runZstdGraph_withParameters_lvl19_dgd = {
    .name    = "Invoke zstd_wLevel Graph with runtime parameter (level 19)",
    .graph_f = runZstdGraph_withParameters,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
    .localParams         = lp_zstd_lvl19,
};

static ZL_Report runStandardZstdGraph_withParameters(
        ZL_Graph* gctx,
        ZL_Edge* inputs[],
        size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    ZL_IntParam const param_lvl =
            ZL_Graph_getLocalIntParam(gctx, ZSTD_c_compressionLevel);
    assert(param_lvl.paramId == ZSTD_c_compressionLevel);
    int const clevel = param_lvl.paramValue;

    // Create runtime parameters
    ZL_IntParam const zstd_cparams[] = { { ZSTD_c_compressionLevel, clevel } };
    ZL_LocalParams const lps         = { .intParams = { zstd_cparams,
                                                        ARRAY_SIZE(zstd_cparams) } };
    ZL_RuntimeGraphParameters const rgp = { .localParams = &lps };

    // Set Successor (zstd Graph) with runtime parameters
    // Note that it's fine to use the stack for the parameters.
    ZL_RET_R_IF_ERR(ZL_Edge_setParameterizedDestination(
            &input, 1, ZL_GRAPH_ZSTD, &rgp));

    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const runStandardZstdGraph_withParameters_lvl1_dgd = {
    .name    = "Invoke standard ZL_GRAPH_ZSTD with runtime parameter (level 1)",
    .graph_f = runStandardZstdGraph_withParameters,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
    .localParams         = lp_zstd_lvl1,
};

static ZL_FunctionGraphDesc const runStandardZstdGraph_withParameters_lvl19_dgd = {
    .name = "Invoke standard ZL_GRAPH_ZSTD with runtime parameter (level 19)",
    .graph_f             = runStandardZstdGraph_withParameters,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
    .localParams         = lp_zstd_lvl19,
};

// This mock validation function always fails
static int justFailValidation(
        const ZL_Compressor*,
        const ZL_FunctionGraphDesc*) noexcept
{
    return 0;
}

static ZL_FunctionGraphDesc const justFailValidation_dgd = {
    .name                = "registration of the function graph always fails",
    .graph_f             = justGoToZstd,
    .validate_f          = justFailValidation,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

static ZL_Report selectFirstValidCustomGraph(
        ZL_Graph* gctx,
        ZL_Edge* inputs[],
        size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input             = inputs[0];
    ZL_GraphIDList const glist = ZL_Graph_getCustomGraphs(gctx);
    EXPECT_GT((int)glist.nbGraphIDs, 0);
    EXPECT_NE(glist.graphids, nullptr);

    /* go through the list of custom graphs,
     * select the first valid one */
    for (size_t n = 0; n < glist.nbGraphIDs; n++) {
        const ZL_GraphID gid = glist.graphids[n];
        if (ZL_GraphID_isValid(gid)) {
            // input's successor is the defined custom Graph 0
            ZL_RET_R_IF_ERR(ZL_Edge_setDestination(input, gid));
            break;
        }
    }

    // Piggy-back scratch allocator test
    void* unusedBuffer = ZL_Graph_getScratchSpace(gctx, 1000000);
    (void)unusedBuffer;

    return ZL_returnSuccess();
}

static const ZL_GraphID sfvcg_customGraphs[] = { ZL_GRAPH_ILLEGAL,
                                                 ZL_GRAPH_ZSTD };
static const size_t sfvcg_size =
        sizeof(sfvcg_customGraphs) / sizeof(*sfvcg_customGraphs);
static ZL_FunctionGraphDesc const justSelectCustomGraph0_dgd = {
    .name                = "select the first valid graph successor",
    .graph_f             = selectFirstValidCustomGraph,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
    .customGraphs        = sfvcg_customGraphs,
    .nbCustomGraphs      = sfvcg_size,
};

static ZL_Report selectFirstValidCustomNode(
        ZL_Graph* gctx,
        ZL_Edge* inputs[],
        size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input            = inputs[0];
    ZL_NodeIDList const nlist = ZL_Graph_getCustomNodes(gctx);
    EXPECT_GT((int)nlist.nbNodeIDs, 0);
    EXPECT_NE(nlist.nodeids, nullptr);

    /* go through the list of custom graphs,
     * select the first valid one */
    for (size_t n = 0; n < nlist.nbNodeIDs; n++) {
        const ZL_NodeID nid = nlist.nodeids[n];
        if (ZL_Graph_isNodeSupported(gctx, nid)) {
            ZL_TRY_LET_T(ZL_EdgeList, successors, ZL_Edge_runNode(input, nid));
            for (size_t i = 0; i < successors.nbEdges; ++i) {
                ZL_RET_R_IF_ERR(ZL_Edge_setDestination(
                        successors.edges[i], ZL_GRAPH_STORE));
            }
            break;
        }
    }

    return ZL_returnSuccess();
}

static constexpr std::array<ZL_NodeID, 2> sfvcn_customNodes = { ZL_NODE_ILLEGAL,
                                                                ZL_NODE_ZSTD };
static ZL_FunctionGraphDesc const selectFirstValidCustomNode_dgd = {
    .name                = "select the first valid node successor",
    .graph_f             = selectFirstValidCustomNode,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
    .customNodes         = sfvcn_customNodes.data(),
    .nbCustomNodes       = sfvcn_customNodes.size(),
};

static ZL_Report
createRuntimeNode(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    (void)gctx;
    const size_t seg1Size = 12;
    assert(ZL_Input_contentSize(ZL_Edge_getData(input)) > seg1Size);

    // Create new parameters (split input arbitrarily into 2 segments [12-N])
    const size_t segSizes[] = { seg1Size, 0 /* all the rest */ };

    // Run Node with runtime parameters, collect outputs
    ZL_TRY_LET_T(
            ZL_EdgeList,
            so,
            ZL_Edge_runSplitNode(input, segSizes, ARRAY_SIZE(segSizes)));
    EXPECT_EQ((int)so.nbEdges, 2);

    // Assign dummy successors to each output stream, for a valid graph
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(so.edges[0], ZL_GRAPH_STORE));
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(so.edges[1], ZL_GRAPH_STORE));

    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const createRuntimeNode_dgd = {
    .name           = "Function Graph creates and run a new Node at runtime",
    .graph_f        = createRuntimeNode,
    .inputTypeMasks = &serialInputType,
    .nbInputs       = 1,
    .lastInputIsVariable = false,
};

static ZL_Report runZstdNode_withParameters(
        ZL_Graph* gctx,
        ZL_Edge* inputs[],
        size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    (void)gctx;

    ZL_IntParam const param_lvl =
            ZL_Graph_getLocalIntParam(gctx, ZSTD_c_compressionLevel);
    assert(param_lvl.paramId == ZSTD_c_compressionLevel);
    int const clevel = param_lvl.paramValue;

    // Create runtime parameters
    ZL_IntParam const zstd_cparams[] = { { ZSTD_c_compressionLevel, clevel },
                                         { ZSTD_c_windowLog, 15 },
                                         { ZSTD_c_checksumFlag, 0 } };
    ZL_LocalParams const lps         = { .intParams = { zstd_cparams,
                                                        ARRAY_SIZE(zstd_cparams) } };

    // Run zstd Node with runtime parameters
    ZL_TRY_LET_T(
            ZL_EdgeList,
            so,
            ZL_Edge_runNode_withParams(input, ZL_NODE_ZSTD, &lps));

    // Assign successor to collected output stream
    EXPECT_EQ((int)so.nbEdges, 1);
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(so.edges[0], ZL_GRAPH_STORE));

    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const runZstdNode_withParameters_lvl1_dgd = {
    .name    = "Function Graph runs zstd with runtime parameters and level 1",
    .graph_f = runZstdNode_withParameters,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
    .localParams         = lp_zstd_lvl1,
};

static ZL_FunctionGraphDesc const runZstdNode_withParameters_lvl19_dgd = {
    .name    = "Function Graph runs zstd with runtime parameters and level 19",
    .graph_f = runZstdNode_withParameters,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
    .localParams         = lp_zstd_lvl19,
};

static ZL_Report
invalidNodeVersion(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    (void)gctx;
    // ZSTD_FIXED is now a deprecated Node (last valid version == 10)
    // This does not respect the Graph Description, which states
    // supporting up to ZL_MAX_FORMAT_VERSION.
    // Such a mismatch will nonetheless be caught at runtime
    ZL_TRY_LET_T(
            ZL_EdgeList,
            co,
            ZL_Edge_runNode(input, ZL_NODE_CONVERT_SERIAL_TO_TOKEN4));
    EXPECT_EQ((int)co.nbEdges, 1);

    ZL_TRY_LET_T(
            ZL_EdgeList,
            zo,
            ZL_Edge_runNode(co.edges[0], ZL_NODE_ZSTD_FIXED_DEPRECATED));
    EXPECT_EQ((int)zo.nbEdges, 1);

    // The previous error could have been avoided by checking Node compatibility
    // with Decoder Profile (Version) at runtime
    EXPECT_FALSE(ZL_Graph_isNodeSupported(gctx, ZL_NODE_ZSTD_FIXED_DEPRECATED));

    // The first transform in the pipeline was valid
    EXPECT_TRUE(
            ZL_Graph_isNodeSupported(gctx, ZL_NODE_CONVERT_SERIAL_TO_TOKEN4));

    // Finish with dummy successor
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(zo.edges[0], ZL_GRAPH_STORE));

    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const invalidNodeVersion_dgd = {
    .name                = "Function Graph selects a deprecated Node",
    .graph_f             = invalidNodeVersion,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

static ZL_Report
illegalSuccessor(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    (void)gctx; // not used in this example
    EXPECT_FALSE(ZL_GraphID_isValid(ZL_GRAPH_ILLEGAL));
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(input, ZL_GRAPH_ILLEGAL));
    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const illegalSuccessor_dgd = {
    .name = "Selector as function graph setting ZL_GRAPH_ILLEGAL as successor",
    .graph_f             = illegalSuccessor,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

static ZL_Report
invalidSuccessor(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    (void)gctx; // not used in this example
    // create a completely bogus successor
    ZL_GraphID const invalidSuccessor = { .gid = 999 };
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(input, invalidSuccessor));
    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const invalidSuccessor_dgd = {
    .name           = "Selector as function graph setting an invalid successor",
    .graph_f        = invalidSuccessor,
    .inputTypeMasks = &serialInputType,
    .nbInputs       = 1,
    .lastInputIsVariable = false,
};

/* invalid graph, which forgets to set a Successor */
static ZL_Report
justDoNothing(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    (void)gctx; // not used in this example
    (void)input;
    // no action: forgets to set a Successor
    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const justDoNothing_dgd = {
    .name    = "simple function graph that does not even set a Successor",
    .graph_f = justDoNothing,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

/* test compression parameters */
#define CLEVEL 2
#define INTPARAM0_ID 24
static const int k_ip0 = 324;
#define REFPARAM1_ID 581
static const int k_rp1[] = { 2, 8, 5 };
#define FLATPARAM2_ID 753
static const int k_fp2[] = { 18, 51, 72, 89 };
static ZL_Report
readCParams(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    printf("Function Graph reading compression parameters \n");

    const int clevel = ZL_Graph_getCParam(gctx, ZL_CParam_compressionLevel);
    EXPECT_EQ(clevel, CLEVEL);

    const ZL_IntParam ip0 = ZL_Graph_getLocalIntParam(gctx, INTPARAM0_ID);
    EXPECT_EQ(ip0.paramId, INTPARAM0_ID);
    EXPECT_EQ(ip0.paramValue, k_ip0);

    const ZL_RefParam rp1 = ZL_Graph_getLocalRefParam(gctx, REFPARAM1_ID);
    EXPECT_EQ(rp1.paramId, REFPARAM1_ID);
    EXPECT_TRUE(rp1.paramRef == k_rp1); // only passed by reference

    const ZL_RefParam rp2 = ZL_Graph_getLocalRefParam(gctx, FLATPARAM2_ID);
    EXPECT_EQ(rp2.paramId, FLATPARAM2_ID);
    EXPECT_NE(rp2.paramRef, k_fp2); // flatParams are copied locally
    EXPECT_EQ(memcmp(k_fp2, rp2.paramRef, sizeof(k_fp2)), 0);

    // mock action to correctly complete
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(input, ZL_GRAPH_ZSTD));
    return ZL_returnSuccess();
}

static ZL_IntParam const k_ip = {
    .paramId    = INTPARAM0_ID,
    .paramValue = k_ip0,
};
static ZL_LocalIntParams const k_lip = {
    .intParams   = &k_ip,
    .nbIntParams = 1,
};
static ZL_RefParam const k_rp = {
    .paramId  = REFPARAM1_ID,
    .paramRef = k_rp1,
};
static ZL_LocalRefParams const k_lrp = {
    .refParams   = &k_rp,
    .nbRefParams = 1,
};
static ZL_CopyParam const k_cp = {
    .paramId   = FLATPARAM2_ID,
    .paramPtr  = k_fp2,
    .paramSize = sizeof(k_fp2),
};
static ZL_LocalCopyParams const k_lcp = {
    .copyParams   = &k_cp,
    .nbCopyParams = 1,
};
static ZL_LocalParams const k_lp = {
    .intParams  = k_lip,
    .copyParams = k_lcp,
    .refParams  = k_lrp,
};
static ZL_FunctionGraphDesc const readLocalParams_dgd = {
    .name                = "Function graph reading compression parameters",
    .graph_f             = readCParams,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
    .localParams         = k_lp,
};

static ZL_Report
intPipelineDynGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    (void)gctx; // unused
    ZL_TRY_LET_T(
            ZL_EdgeList,
            sl1,
            ZL_Edge_runNode(input, ZL_NODE_INTERPRET_AS_LE32));
    EXPECT_EQ((int)sl1.nbEdges, 1);

    ZL_TRY_LET_T(
            ZL_EdgeList, sl2, ZL_Edge_runNode(sl1.edges[0], ZL_NODE_DELTA_INT));
    EXPECT_EQ((int)sl2.nbEdges, 1);

    // send final stream to successor Graph
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(sl2.edges[0], ZL_GRAPH_ZSTD));
    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const intPipelineDynGraph_dgd = {
    .name                = "numeric pipeline implemented as a function graph",
    .graph_f             = intPipelineDynGraph,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

/* Function Graph settings 4 Successor graphs */
static ZL_Report
dynGraphTree(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    (void)gctx; // unused
    ZL_TRY_LET_T(
            ZL_EdgeList,
            sl1,
            ZL_Edge_runNode(input, ZL_NODE_CONVERT_SERIAL_TO_TOKEN4));
    EXPECT_EQ((int)sl1.nbEdges, 1);

    ZL_TRY_LET_T(
            ZL_EdgeList,
            sl2,
            ZL_Edge_runNode(sl1.edges[0], ZL_NODE_TRANSPOSE_SPLIT));
    EXPECT_EQ((int)sl2.nbEdges, 4);

    // send final edges to successor Graph
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(sl2.edges[0], ZL_GRAPH_ZSTD));
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(sl2.edges[1], ZL_GRAPH_ZSTD));
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(sl2.edges[2], ZL_GRAPH_ZSTD));
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(sl2.edges[3], ZL_GRAPH_ZSTD));
    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const dynGraphTree_dgd = {
    .name                = "Function Graph settings 4 Successor graphs",
    .graph_f             = dynGraphTree,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

/* this function graph forgets to set final Graph */
static ZL_Report
unfinishedPipeline(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    (void)gctx; // unused
    ZL_TRY_LET_T(
            ZL_EdgeList,
            sl1,
            ZL_Edge_runNode(input, ZL_NODE_INTERPRET_AS_LE32));
    EXPECT_EQ((int)sl1.nbEdges, 1);

    ZL_TRY_LET_T(
            ZL_EdgeList, sl2, ZL_Edge_runNode(sl1.edges[0], ZL_NODE_DELTA_INT));
    EXPECT_EQ((int)sl2.nbEdges, 1);

    // forget to send final stream to successor Graph
    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const unfinishedPipeline_dgd = {
    .name    = "pipeline generating a dangling stream with no graph successor",
    .graph_f = unfinishedPipeline,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

/* this function graph attempts to process the same Stream twice */
static ZL_Report
doubleProcessed(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    (void)gctx; // unused
    ZL_TRY_LET_T(
            ZL_EdgeList,
            sl1,
            ZL_Edge_runNode(input, ZL_NODE_INTERPRET_AS_LE32));
    EXPECT_EQ((int)sl1.nbEdges, 1);

    ZL_TRY_LET_T(
            ZL_EdgeList, sl2, ZL_Edge_runNode(sl1.edges[0], ZL_NODE_DELTA_INT));
    EXPECT_EQ((int)sl2.nbEdges, 1);

    // Trying to process sl1 stream twice -> should error out
    ZL_TRY_LET_T(
            ZL_EdgeList, sl3, ZL_Edge_runNode(sl1.edges[0], ZL_NODE_DELTA_INT));
    EXPECT_EQ((int)sl2.nbEdges, 1);

    // send final streams to successor Graph
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(sl2.edges[0], ZL_GRAPH_ZSTD));
    // send final streams to successor Graph
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(sl3.edges[0], ZL_GRAPH_ZSTD));
    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const doubleProcessed_dgd = {
    .name           = "pipeline error: trying to process same Stream twice",
    .graph_f        = doubleProcessed,
    .inputTypeMasks = &serialInputType,
    .nbInputs       = 1,
    .lastInputIsVariable = false,
};

/* this graph incorrectly set a successor to an already assigned Stream,
 * and does not check the return code for error */
static ZL_Report
noCheckSuccessor(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    (void)gctx; // unused
    ZL_TRY_LET_T(
            ZL_EdgeList,
            sl1,
            ZL_Edge_runNode(input, ZL_NODE_INTERPRET_AS_LE32));
    EXPECT_EQ((int)sl1.nbEdges, 1);

    ZL_TRY_LET_T(
            ZL_EdgeList, sl2, ZL_Edge_runNode(sl1.edges[0], ZL_NODE_DELTA_INT));
    EXPECT_EQ((int)sl2.nbEdges, 1);

    // send final streams to successor Graph,
    // intentionally discard (does not check) success status
    (void)ZL_Edge_setDestination(sl2.edges[0], ZL_GRAPH_ZSTD);
    // This one is wrong (already processed)
    (void)ZL_Edge_setDestination(sl1.edges[0], ZL_GRAPH_ZSTD);

    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const noCheckSuccessor_dgd = {
    .name = "set a Successor Graph to an already processed Stream, and does not check return status",
    .graph_f             = noCheckSuccessor,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

/* @has2Inputs simply features 2 Singular Inputs, basic MultiInputGraph scenario
 */
static ZL_Report
has2Inputs(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs) noexcept
{
    printf("Running Multi-Inputs Graph 'has2Inputs' \n");
    (void)gctx;
    EXPECT_EQ((int)nbInputs, 2);
    EXPECT_TRUE(inputs);
    EXPECT_TRUE(inputs[0]);
    EXPECT_EQ(ZL_Input_type(ZL_Edge_getData(inputs[0])), ZL_Type_serial);
    ZL_RET_R_IF_NE(
            GENERIC,
            (int)ZL_Input_type(ZL_Edge_getData(inputs[0])),
            (int)ZL_Type_serial);
    EXPECT_TRUE(inputs[1]);
    EXPECT_EQ(ZL_Input_type(ZL_Edge_getData(inputs[1])), ZL_Type_serial);
    ZL_RET_R_IF_NE(
            GENERIC,
            (int)ZL_Input_type(ZL_Edge_getData(inputs[1])),
            (int)ZL_Type_serial);
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(inputs[0], ZL_GRAPH_ZSTD));
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(inputs[1], ZL_GRAPH_ZSTD));
    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const has2Inputs_migd = {
    .name           = "Graph accepting 2 Serial inputs",
    .graph_f        = has2Inputs,
    .inputTypeMasks = (const ZL_Type[]){ ZL_Type_serial, ZL_Type_serial },
    .nbInputs       = 2,
};

/* @has1PlusInputs features 1 Singular Input and 1 Variable Input */
static ZL_Report
has1PlusInputs(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs) noexcept
{
    printf("Running Multi-Inputs Graph 'has2Inputs' \n");
    (void)gctx;
    EXPECT_GE((int)nbInputs, 1);
    EXPECT_TRUE(inputs);
    for (size_t n = 0; n < nbInputs; n++) {
        EXPECT_TRUE(inputs[n]);
        ZL_RET_R_IF_ERR(ZL_Edge_setDestination(inputs[n], ZL_GRAPH_ZSTD));
    }
    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const has1PlusInputs_migd = {
    .name                = "Graph with 1 Singular Input and 1 Variable Input",
    .graph_f             = has1PlusInputs,
    .inputTypeMasks      = (const ZL_Type[]){ ZL_Type_serial, ZL_Type_serial },
    .nbInputs            = 2,
    .lastInputIsVariable = 1,
};

ZL_NodeID g_split2_nodeid           = ZL_NODE_ILLEGAL;
ZL_NodeID g_split3_nodeid           = ZL_NODE_ILLEGAL;
ZL_GraphID g_has2Inputs_graphid     = ZL_GRAPH_ILLEGAL;
ZL_GraphID g_has1PlusInputs_graphid = ZL_GRAPH_ILLEGAL;

/* basic MultiInputGraph scenario:
 * Splits a Serial input into 2 parts, pass them to @has2Inputs,
 * */
static ZL_Report
split_then2Inputs(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    printf("Running DynGraph 'split_then2Inputs' \n");
    (void)gctx; // unused

    ZL_TRY_LET_T(ZL_EdgeList, sl, ZL_Edge_runNode(input, g_split2_nodeid));
    EXPECT_EQ((int)sl.nbEdges, 2);

    return ZL_Edge_setParameterizedDestination(
            sl.edges, 2, g_has2Inputs_graphid, NULL);
}

static ZL_FunctionGraphDesc const split_then2Inputs_dgd = {
    .name    = "Splits Serial input into 2 parts, pass them to @has2Inputs",
    .graph_f = split_then2Inputs,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

/* basic MultiInputGraph scenario:
 * Splits a Serial input into 2 parts, pass them to @has2Inputs,
 * */
static ZL_Report
conversion2Inputs(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    printf("Running DynGraph 'split_then2Inputs' \n");
    (void)gctx; // unused

    ZL_TRY_LET_T(ZL_EdgeList, sl, ZL_Edge_runNode(input, g_split2_nodeid));
    EXPECT_EQ((int)sl.nbEdges, 2);

    ZL_TRY_LET_T(
            ZL_EdgeList,
            convert,
            ZL_Edge_runNode(sl.edges[0], ZL_NODE_CONVERT_SERIAL_TO_TOKEN4));
    EXPECT_EQ((int)convert.nbEdges, 1);

    ZL_Edge* outputs[2] = { convert.edges[0], sl.edges[1] };

    return ZL_Edge_setParameterizedDestination(
            outputs, 2, g_has2Inputs_graphid, NULL);
}

static ZL_FunctionGraphDesc const conversion2Inputs_dgd = {
    .name                = "Implicit Conversion required for @has2Inputs",
    .graph_f             = conversion2Inputs,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

/* push 3 inputs into an MultiInputGraph with Variable inputs
 * */
static ZL_Report
variableInputs(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    printf("Running DynGraph 'variableInputs' \n");
    (void)gctx; // unused

    ZL_TRY_LET_T(ZL_EdgeList, sl, ZL_Edge_runNode(input, g_split3_nodeid));
    EXPECT_EQ((int)sl.nbEdges, 3);

    return ZL_Edge_setParameterizedDestination(
            sl.edges, 3, g_has1PlusInputs_graphid, NULL);
}

static ZL_FunctionGraphDesc const variableInputs_dgd = {
    .name    = "push 3 inputs into an MultiInputGraph with Variable inputs",
    .graph_f = variableInputs,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

/* invalid scenario:
 * push 3 inputs into an MultiInputGraph with 2 inputs
 * */
static ZL_Report
invalid_tooManyInputs(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    printf("Running DynGraph 'invalid_tooManyInputs' \n");
    (void)gctx; // unused

    ZL_TRY_LET_T(ZL_EdgeList, sl, ZL_Edge_runNode(input, g_split3_nodeid));
    EXPECT_EQ((int)sl.nbEdges, 3);

    return ZL_Edge_setParameterizedDestination(
            sl.edges, 3, g_has2Inputs_graphid, NULL);
}

static ZL_FunctionGraphDesc const invalid_tooManyInputs_dgd = {
    .name                = "Invalid: pass 3 inputs to @has2Inputs",
    .graph_f             = invalid_tooManyInputs,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

/* invalid scenario:
 * provide only 1 input to an MultiInputGraph with 2 inputs
 * */
static ZL_Report invalid_notEnoughInputs(
        ZL_Graph* gctx,
        ZL_Edge* inputs[],
        size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* input = inputs[0];
    printf("Running DynGraph 'invalid_notEnoughInputs' \n");
    (void)gctx; // unused

    return ZL_Edge_setParameterizedDestination(
            &input, 1, g_has2Inputs_graphid, NULL);
}

static ZL_FunctionGraphDesc const invalid_notEnoughInputs_dgd = {
    .name                = "Invalid: pass only 1 input to @has2Inputs",
    .graph_f             = invalid_notEnoughInputs,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

/* invalid scenario:
 * this Function Graph will start with a few successful transforms,
 * one of which is guaranteed to produce an output (as opposed to reference)
 * and then fail deeper in the pipeline.
 * It's meant to illustrate the dangers of releasing Streams too early.
 * */
static ZL_Report dyngraph_failDeep_stage2(
        ZL_Graph* gctx,
        ZL_Edge* inputs[],
        size_t nbInputs) noexcept
{
    printf("Running 'dyngraph_failDeep_stage2' \n");
    (void)gctx; // unused
    assert(nbInputs == 1);
    ZL_Edge* input = inputs[0];

    // This operation ensures the created Stream is not a reference to @input
    ZL_TRY_LET_T(
            ZL_EdgeList,
            node2Result,
            ZL_Edge_runNode(input, ZL_NODE_DELTA_INT));
    assert(node2Result.nbEdges == 1);
    ZL_TRY_LET_T(
            ZL_EdgeList,
            node3Result,
            ZL_Edge_runNode(
                    node2Result.edges[0], ZL_NODE_CONVERT_NUM_TO_SERIAL));
    assert(node3Result.nbEdges == 1);
    // This operation should fail (wrong type)
    ZL_TRY_LET_T(
            ZL_EdgeList,
            node4Result,
            ZL_Edge_runNode(node3Result.edges[0], ZL_NODE_DELTA_INT));
    // Note: this code should not be reached, it should fail just above
    assert(0);
    assert(node2Result.nbEdges == 1);
    return ZL_Edge_setDestination(node4Result.edges[0], ZL_GRAPH_STORE);
}

static ZL_Type const dyngraph_failDeep_inputType        = ZL_Type_numeric;
static ZL_FunctionGraphDesc const dyngraph_failDeep_dgd = {
    .name    = "Function Graph failing have passing a first few Transforms",
    .graph_f = dyngraph_failDeep_stage2,
    .inputTypeMasks      = &dyngraph_failDeep_inputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

/* ------   create the cgraph   -------- */

// This graph function follows the ZL_GraphFn definition
// It's in charge of registering custom graphs and nodes
// and the one passed via unit-wide variable @g_dynGraph_dgdPtr.
static const ZL_FunctionGraphDesc* g_dynGraph_dgdPtr = nullptr;
static ZL_GraphID registerDynGraph(ZL_Compressor* cgraph) noexcept
{
    ZL_Report const setr = ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION);
    if (ZL_isError(setr))
        abort();

    ZL_Report const clr = ZL_Compressor_setParameter(
            cgraph, ZL_CParam_compressionLevel, CLEVEL);
    if (ZL_isError(clr))
        abort();

    const size_t split2_ss[2] = { 100, 0 };
    g_split2_nodeid           = ZL_Compressor_registerSplitNode_withParams(
            cgraph, ZL_Type_serial, split2_ss, 2);
    const size_t split3_ss[3] = { 10, 20, 0 };
    g_split3_nodeid           = ZL_Compressor_registerSplitNode_withParams(
            cgraph, ZL_Type_serial, split3_ss, 3);
    g_has2Inputs_graphid =
            ZL_Compressor_registerFunctionGraph(cgraph, &has2Inputs_migd);
    g_has1PlusInputs_graphid =
            ZL_Compressor_registerFunctionGraph(cgraph, &has1PlusInputs_migd);
    g_zstd_wLevel_graphid =
            ZL_Compressor_registerFunctionGraph(cgraph, &zstd_wLevel_dgd);

    return ZL_Compressor_registerFunctionGraph(cgraph, g_dynGraph_dgdPtr);
}

static ZL_GraphID registerDynGraph_deep(ZL_Compressor* cgraph) noexcept
{
    ZL_Report const setr = ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION);
    if (ZL_isError(setr))
        abort();

    ZL_Report const clr = ZL_Compressor_setParameter(
            cgraph, ZL_CParam_compressionLevel, CLEVEL);
    if (ZL_isError(clr))
        abort();

    const ZL_NodeID transforms[] = { ZL_NODE_INTERPRET_AS_LE32,
                                     ZL_NODE_DELTA_INT };
    return ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
            cgraph,
            transforms,
            2,
            ZL_Compressor_registerFunctionGraph(cgraph, g_dynGraph_dgdPtr));
}

/* ------   compress, using provided graph function   -------- */

static size_t compress(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize,
        ZL_GraphFn graphf)
{
    assert(dstCapacity >= ZL_compressBound(srcSize));

    ZL_CCtx* const cctx = ZL_CCtx_create();
    assert(cctx);
    ZL_Compressor* const cgraph = ZL_Compressor_create();
    assert(cgraph);
    ZL_Report const gssr = ZL_Compressor_initUsingGraphFn(cgraph, graphf);
    EXPECT_EQ(ZL_isError(gssr), 0) << "cgraph initialization failed\n";
    ZL_Report const rcgr = ZL_CCtx_refCompressor(cctx, cgraph);
    EXPECT_EQ(ZL_isError(rcgr), 0) << "CGraph reference failed\n";
    ZL_Report const r = ZL_CCtx_compress(cctx, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 0) << "compression failed \n";

    ZL_Compressor_free(cgraph);
    ZL_CCtx_free(cctx);
    return ZL_validResult(r);
}

/* ------ define custom decoder transforms ------- */

/* none */

/* ------   decompress   -------- */

static size_t
decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    // Check buffer size
    ZL_Report const dr = ZL_getDecompressedSize(src, srcSize);
    assert(!ZL_isError(dr));
    size_t const dstSize = ZL_validResult(dr);
    assert(dstCapacity >= dstSize);
    (void)dstSize;

    // Create a single decompression state, to store the custom decoder(s)
    // The decompression state will be re-employed
    static ZL_DCtx* const dctx = ZL_DCtx_create();
    assert(dctx);

    // register custom decoders

    // Decompress, using custom decoder(s)
    ZL_Report const r =
            ZL_DCtx_decompress(dctx, dst, dstCapacity, src, srcSize);
    EXPECT_EQ(ZL_isError(r), 0) << "decompression failed \n";

    return ZL_validResult(r);
}

/* ------   round trip test   ------ */

static size_t roundTripTest(
        ZL_GraphFn graphf,
        const void* input,
        size_t inputSize,
        const char* name)
{
    printf("\n=========================== \n");
    printf(" %s \n", name);
    printf("--------------------------- \n");
    // Generate test input
    size_t const compressedBound = ZL_compressBound(inputSize);
    void* const compressed       = malloc(compressedBound);
    assert(compressed);

    size_t const compressedSize =
            compress(compressed, compressedBound, input, inputSize, graphf);
    printf("compressed %zu input bytes into %zu compressed bytes \n",
           inputSize,
           compressedSize);

    void* const decompressed = malloc(inputSize);
    assert(decompressed);

    size_t const decompressedSize =
            decompress(decompressed, inputSize, compressed, compressedSize);
    printf("decompressed %zu input bytes into %zu original bytes \n",
           compressedSize,
           decompressedSize);

    // round-trip check
    EXPECT_EQ((int)decompressedSize, (int)inputSize)
            << "Error : decompressed size != original size \n";
    if (inputSize) {
        EXPECT_EQ(memcmp(input, decompressed, inputSize), 0)
                << "Error : decompressed content differs from original (corruption issue) !!!  \n";
    }

    printf("round-trip success \n");
    free(decompressed);
    free(compressed);
    return compressedSize;
}

static size_t roundTripIntegers(ZL_GraphFn graphf, const char* name)
{
    // Generate test input
#define NB_INTS 84
    int input[NB_INTS];
    for (int i = 0; i < NB_INTS; i++)
        input[i] = i;

    return roundTripTest(graphf, input, sizeof(input), name);
}

/* this test is expected to fail predictably */
static int cFailTest(ZL_GraphFn graphf, const char* testName)
{
    printf("\n=========================== \n");
    printf(" %s \n", testName);
    printf("--------------------------- \n");
    // Generate test input => too short, will fail
    char input[40];
    for (int i = 0; i < 40; i++)
        input[i] = (char)i;

#define COMPRESSED_BOUND ZL_COMPRESSBOUND(sizeof(input))
    char compressed[COMPRESSED_BOUND] = { 0 };

    ZL_Report const r = ZL_compress_usingGraphFn(
            compressed, COMPRESSED_BOUND, input, sizeof(input), graphf);
    EXPECT_EQ(ZL_isError(r), 1) << "compression should have failed \n";

    printf("Compression failure observed as expected : %s \n",
           ZL_ErrorCode_toString(r._code));
    return 0;
}

static ZL_GraphID permissiveGraph(
        ZL_Compressor* cgraph,
        ZL_GraphFn failingGraph)
{
    assert(cgraph != nullptr);
    ZL_Report const spp = ZL_Compressor_setParameter(
            cgraph, ZL_CParam_permissiveCompression, 1);
    EXPECT_FALSE(ZL_isError(spp));
    return failingGraph(cgraph);
}

static ZL_GraphFn g_failingGraph_forPermissive;
static ZL_GraphID permissiveGraph_asGraphF(ZL_Compressor* cgraph) noexcept
{
    return permissiveGraph(cgraph, g_failingGraph_forPermissive);
}

static size_t permissiveTest(ZL_GraphFn graphf, const char* testName)
{
    printf("\n=========================== \n");
    printf(" Testing Permissive Mode \n");
    g_failingGraph_forPermissive = graphf;
    return roundTripIntegers(permissiveGraph_asGraphF, testName);
}

// ************************
// Published list of tests
// ************************

TEST(DynGraphs, just_zstd)
{
    g_dynGraph_dgdPtr = &justGoToZstd_dgd;
    (void)roundTripIntegers(
            registerDynGraph, "Trivial function graph, always returns zstd");
}

TEST(DynGraphs, createRuntimeNode)
{
    g_dynGraph_dgdPtr = &createRuntimeNode_dgd;
    (void)roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, selectFirstValidCustomGraph)
{
    g_dynGraph_dgdPtr = &justSelectCustomGraph0_dgd;
    (void)roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, selectFirstValidCustomNode)
{
    g_dynGraph_dgdPtr = &selectFirstValidCustomNode_dgd;
    (void)roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, runNode_withRuntimeParameters)
{
    g_dynGraph_dgdPtr = &runZstdNode_withParameters_lvl1_dgd;
    size_t const cSize_lvl1 =
            roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
    g_dynGraph_dgdPtr = &runZstdNode_withParameters_lvl19_dgd;
    size_t const cSize_lvl19 =
            roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
    EXPECT_GT(cSize_lvl1, cSize_lvl19);
    printf("As anticipated, level 19 compresses more (%zu < %zu) than level 1 \n",
           cSize_lvl19,
           cSize_lvl1);
}

TEST(DynGraphs, runGraph_withRuntimeParameters)
{
    g_dynGraph_dgdPtr = &runZstdGraph_withParameters_lvl1_dgd;
    size_t const cSize_lvl1 =
            roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
    g_dynGraph_dgdPtr = &runZstdGraph_withParameters_lvl19_dgd;
    size_t const cSize_lvl19 =
            roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
    EXPECT_GT(cSize_lvl1, cSize_lvl19);
    printf("As anticipated, level 19 compresses more (%zu < %zu) than level 1 \n",
           cSize_lvl19,
           cSize_lvl1);
}

TEST(DynGraphs, runStandardGraph_withRuntimeParameters)
{
    g_dynGraph_dgdPtr = &runStandardZstdGraph_withParameters_lvl1_dgd;
    size_t const cSize_lvl1 =
            roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
    g_dynGraph_dgdPtr = &runStandardZstdGraph_withParameters_lvl19_dgd;
    size_t const cSize_lvl19 =
            roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
    EXPECT_GT(cSize_lvl1, cSize_lvl19);
    printf("As anticipated, level 19 compresses more (%zu < %zu) than level 1 \n",
           cSize_lvl19,
           cSize_lvl1);
}

TEST(DynGraphs, integer_pipeline)
{
    g_dynGraph_dgdPtr = &intPipelineDynGraph_dgd;
    (void)roundTripIntegers(
            registerDynGraph,
            "Simple numeric pipeline implemented as function graph");
}

TEST(DynGraphs, integer_tree)
{
    g_dynGraph_dgdPtr = &dynGraphTree_dgd;
    (void)roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, readLocalParams)
{
    g_dynGraph_dgdPtr = &readLocalParams_dgd;
    (void)roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, graph2Inputs)
{
    g_dynGraph_dgdPtr = &split_then2Inputs_dgd;
    (void)roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, conversion2Inputs)
{
    g_dynGraph_dgdPtr = &conversion2Inputs_dgd;
    (void)roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, graphVariableInputs)
{
    g_dynGraph_dgdPtr = &variableInputs_dgd;
    (void)roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, failRegistrationValidation)
{
    g_dynGraph_dgdPtr = &justFailValidation_dgd;
    cFailTest(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, invalidNodeVersion)
{
    g_dynGraph_dgdPtr = &invalidNodeVersion_dgd;
    cFailTest(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, parameterMissing)
{
    g_dynGraph_dgdPtr = &zstd_wLevel_dgd;
    cFailTest(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, illegal_successor)
{
    g_dynGraph_dgdPtr = &illegalSuccessor_dgd;
    cFailTest(
            registerDynGraph,
            "trivial function graph (==Selector) provides ZL_GRAPH_ILLEGAL as successor");
}

TEST(DynGraphs, invalid_successor)
{
    g_dynGraph_dgdPtr = &invalidSuccessor_dgd;
    cFailTest(
            registerDynGraph,
            "trivial function graph (==Selector) provides an invalid graph as successor");
}

TEST(DynGraphs, forget_successor)
{
    g_dynGraph_dgdPtr = &justDoNothing_dgd;
    cFailTest(
            registerDynGraph,
            "trivial function graph (==Selector) which forgets to set any successor");
}

TEST(DynGraphs, unfinishedPipeline)
{
    g_dynGraph_dgdPtr = &unfinishedPipeline_dgd;
    cFailTest(
            registerDynGraph,
            "function graph generating an unfinished pipeline featuring a dangling stream");
}

TEST(DynGraphs, doubleProcessed)
{
    g_dynGraph_dgdPtr = &doubleProcessed_dgd;
    cFailTest(
            registerDynGraph,
            "function graph generating a faulty pipeline trying to process a Stream twice");
}

TEST(DynGraphs, noCheckSuccessor)
{
    g_dynGraph_dgdPtr = &noCheckSuccessor_dgd;
    cFailTest(
            registerDynGraph,
            "function graph passing a Graph Successor to incorrect Stream without checking return status");
}

TEST(DynGraphs, invalid_tooManyInputs)
{
    g_dynGraph_dgdPtr = &invalid_tooManyInputs_dgd;
    cFailTest(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, invalid_notEnoughInputs)
{
    g_dynGraph_dgdPtr = &invalid_notEnoughInputs_dgd;
    cFailTest(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, graphFailure_deep)
{
    g_dynGraph_dgdPtr = &dyngraph_failDeep_dgd;
    cFailTest(registerDynGraph_deep, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, invalidNodeVersion_permissive)
{
    g_dynGraph_dgdPtr = &invalidNodeVersion_dgd;
    permissiveTest(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, parameterMissing_permissive)
{
    g_dynGraph_dgdPtr = &zstd_wLevel_dgd;
    permissiveTest(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, illegal_successor_permissive)
{
    g_dynGraph_dgdPtr = &illegalSuccessor_dgd;
    permissiveTest(
            registerDynGraph,
            "trivial function graph (==Selector) provides ZL_GRAPH_ILLEGAL as successor");
}

TEST(DynGraphs, invalid_successor_permissive)
{
    g_dynGraph_dgdPtr = &invalidSuccessor_dgd;
    permissiveTest(
            registerDynGraph,
            "trivial function graph (==Selector) provides an invalid graph as successor");
}

TEST(DynGraphs, forget_successor_permissive)
{
    g_dynGraph_dgdPtr = &justDoNothing_dgd;
    permissiveTest(
            registerDynGraph,
            "trivial function graph (==Selector) which forgets to set any successor");
}

TEST(DynGraphs, unfinishedPipeline_permissive)
{
    g_dynGraph_dgdPtr = &unfinishedPipeline_dgd;
    permissiveTest(
            registerDynGraph,
            "function graph generating an unfinished pipeline featuring a dangling stream");
}

TEST(DynGraphs, doubleProcessed_permissive)
{
    g_dynGraph_dgdPtr = &doubleProcessed_dgd;
    permissiveTest(
            registerDynGraph,
            "function graph generating a faulty pipeline trying to process a Stream twice");
}

TEST(DynGraphs, noCheckSuccessor_permissive)
{
    g_dynGraph_dgdPtr = &noCheckSuccessor_dgd;
    permissiveTest(
            registerDynGraph,
            "function graph passing a Graph Successor to incorrect Stream without checking return status");
}

TEST(DynGraphs, invalid_tooManyInputs_permissive)
{
    g_dynGraph_dgdPtr = &invalid_tooManyInputs_dgd;
    permissiveTest(registerDynGraph, g_dynGraph_dgdPtr->name);
}

TEST(DynGraphs, graphFailure_deep_permissive)
{
    g_dynGraph_dgdPtr = &dyngraph_failDeep_dgd;
    permissiveTest(registerDynGraph_deep, g_dynGraph_dgdPtr->name);
}

// ---------------------------------------------
// Testing ZL_Edge_setParameterizedDestination()
// ---------------------------------------------

static ZL_Report dg_change_static_graph_output(
        ZL_Graph* gctx,
        ZL_Edge* inputs[],
        size_t nbIns) noexcept
{
    (void)gctx;
    assert(nbIns == 1);
    assert(inputs != NULL);
    ZL_Edge* input = inputs[0];
    assert(input != NULL);
    // convert to integer
    ZL_TRY_LET_T(
            ZL_EdgeList,
            sl1,
            ZL_Edge_runNode(input, ZL_NODE_INTERPRET_AS_LE32));
    EXPECT_EQ((int)sl1.nbEdges, 1);
    // Select static graph
    ZL_GraphID const targetGraph =
            (ZL_GraphID){ ZL_PrivateStandardGraphID_delta_zstd_internal };
    // Set Destination on parameterized target graph, changing its output to
    // STORE
    ZL_GraphID store                    = ZL_GRAPH_STORE;
    const ZL_RuntimeGraphParameters rgp = {
        .customGraphs   = &store,
        .nbCustomGraphs = 1,
    };
    ZL_RET_R_IF_ERR(ZL_Edge_setParameterizedDestination(
            sl1.edges, 1, targetGraph, &rgp));
    store = ZL_GRAPH_ILLEGAL; // Check that the parameterized Graph doesn't keep
                              // a pointer to origin array.
    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const parameterizedStaticGraph_dgd = {
    .name                = "change the output of a Standard Static Graph",
    .graph_f             = dg_change_static_graph_output,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

TEST(DynGraphs, parameterizeStandardStaticGraph)
{
    g_dynGraph_dgdPtr = &parameterizedStaticGraph_dgd;
    (void)roundTripIntegers(registerDynGraph, g_dynGraph_dgdPtr->name);
}

// ----------------------------------------------------------
// Testing transmission of custom Nodes via parameterization
// ----------------------------------------------------------

#define MAX_NB_NID 16
static ZL_NodeID g_checkedCustomNodes[MAX_NB_NID] = {};
static size_t g_nbCheckedCustomNodes              = 0;

static ZL_Report fgraph_checkCustomNodes(
        ZL_Graph* gctx,
        ZL_Edge* inputs[],
        size_t nbIns) noexcept
{
    assert(nbIns == 1);
    ZL_Edge* const input = inputs[0];
    assert(input != NULL);

    /* retrieve the list of custom Nodes*/
    ZL_NodeIDList nil = ZL_Graph_getCustomNodes(gctx);
    /* ensure it's identical to expectation */
    EXPECT_EQ(nil.nbNodeIDs, g_nbCheckedCustomNodes);
    if (g_nbCheckedCustomNodes)
        assert(g_checkedCustomNodes != NULL);
    for (size_t n = 0; n < g_nbCheckedCustomNodes; n++) {
        EXPECT_EQ(nil.nodeids[n].nid, g_checkedCustomNodes[n].nid);
    }

    // Send input to basic successor
    ZL_RET_R_IF_ERR(ZL_Edge_setDestination(input, ZL_GRAPH_COMPRESS_GENERIC));

    return ZL_returnSuccess();
}

static ZL_FunctionGraphDesc const fgraph_checkCustomNodes_dgd = {
    .name                = "function graph Check Custom Nodes",
    .graph_f             = fgraph_checkCustomNodes,
    .inputTypeMasks      = &serialInputType,
    .nbInputs            = 1,
    .lastInputIsVariable = false,
};

static ZL_GraphID registerParameterizedGraph(ZL_Compressor* cgraph) noexcept
{
    ZL_Report const setr = ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION);
    if (ZL_isError(setr))
        abort();

    ZL_Report const clr = ZL_Compressor_setParameter(
            cgraph, ZL_CParam_compressionLevel, CLEVEL);
    if (ZL_isError(clr))
        abort();

    assert(g_nbCheckedCustomNodes <= MAX_NB_NID);
    ZL_NodeID nidArray[MAX_NB_NID];
    memcpy(nidArray,
           g_checkedCustomNodes,
           g_nbCheckedCustomNodes * sizeof(nidArray[0]));

    ZL_GraphID sgid =
            ZL_Compressor_registerFunctionGraph(cgraph, g_dynGraph_dgdPtr);
    assert(ZL_GraphID_isValid(sgid));

    ZL_ParameterizedGraphDesc const pgd = {
        .graph         = sgid,
        .customNodes   = nidArray,
        .nbCustomNodes = g_nbCheckedCustomNodes,
    };

    ZL_GraphID fgid = ZL_Compressor_registerParameterizedGraph(cgraph, &pgd);

    // erase content, to make sure it's not just referenced
    memset(nidArray, 0, sizeof(nidArray));

    return fgid;
}

TEST(DynGraphs, parameterizedCustomNodes_1)
{
    g_nbCheckedCustomNodes  = 1;
    g_checkedCustomNodes[0] = ZL_NODE_DELTA_INT;

    g_dynGraph_dgdPtr = &fgraph_checkCustomNodes_dgd;
    (void)roundTripIntegers(registerParameterizedGraph, "pass 1 custom Node");
}

TEST(DynGraphs, parameterizedCustomNodes_2)
{
    g_nbCheckedCustomNodes  = 2;
    g_checkedCustomNodes[0] = ZL_NODE_BITPACK_INT;
    g_checkedCustomNodes[1] = ZL_NODE_CONSTANT_FIXED;

    g_dynGraph_dgdPtr = &fgraph_checkCustomNodes_dgd;
    (void)roundTripIntegers(registerParameterizedGraph, "pass 2 custom Nodes");
}

TEST(DynGraphs, parameterizedCustomNodes_7)
{
    g_nbCheckedCustomNodes  = 7;
    g_checkedCustomNodes[0] = ZL_NODE_CONSTANT_SERIAL;
    g_checkedCustomNodes[1] = ZL_NODE_BITPACK_SERIAL;
    g_checkedCustomNodes[2] = ZL_NODE_CONCAT_SERIAL;
    g_checkedCustomNodes[3] = ZL_NODE_CONCAT_NUMERIC;
    g_checkedCustomNodes[4] = ZL_NODE_CONCAT_STRING;
    g_checkedCustomNodes[5] = ZL_NODE_SETSTRINGLENS;
    g_checkedCustomNodes[6] = ZL_NODE_TOKENIZE_STRING;

    g_dynGraph_dgdPtr = &fgraph_checkCustomNodes_dgd;
    (void)roundTripIntegers(registerParameterizedGraph, "pass 7 custom Nodes");
}

TEST(DynGraphs, parameterizedCustomNodes_0)
{
    g_nbCheckedCustomNodes = 0;

    g_dynGraph_dgdPtr = &fgraph_checkCustomNodes_dgd;
    (void)roundTripIntegers(registerParameterizedGraph, "pass 0 custom Nodes");
}

} // namespace
