// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/common/errors_internal.h"
#include "openzl/common/introspection.h" // WAYPOINT
#include "openzl/cpp/CompressIntrospectionHooks.hpp"
#include "openzl/zl_compressor.h"
#include "openzl/zl_config.h"        // ZL_ALLOW_INTROSPECTION
#include "openzl/zl_introspection.h" // ZL_CompressIntrospectionHooks
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_reflection.h"

#include "tests/datagen/DataGen.h"

#if !ZL_ALLOW_INTROSPECTION
#    error "Test must be compiled with introspection enabled"
#endif

using namespace ::testing;

namespace openzl::tests {

namespace {

static int ctr = 0;

class IncrementingHooks : public openzl::CompressIntrospectionHooks {
   public:
    void on_ZL_CCtx_compressMultiTypedRef_start(
            ZL_CCtx*,
            const void* const,
            const size_t,
            const ZL_TypedRef* const*,
            const size_t) override
    {
        ++ctr;
        std::cerr << "hello world" << std::endl;
    }
};

} // namespace

TEST(CompressIntrospectionTest, WHENhooksPassedTHENtheyAreExecuted)
{
    IncrementingHooks hooks;
    ctr                 = 0;
    ZL_CCtx* const cctx = ZL_CCtx_create();
    EXPECT_NE(nullptr, cctx);
    EXPECT_FALSE(ZL_isError(
            ZL_CCtx_attachIntrospectionHooks(cctx, hooks.getRawHooks())));
    EXPECT_EQ(0, ctr);
    EXPECT_TRUE(ZL_isError(
            ZL_CCtx_compressMultiTypedRef(cctx, nullptr, 0, nullptr, 0)));
    EXPECT_EQ(1, ctr);
    ZL_CCtx_free(cctx);
}

TEST(CompressIntrospectionTest, WHENhooksDetachedTHENtheyAreNotExecuted)
{
    IncrementingHooks hooks;
    ctr                 = 0;
    ZL_CCtx* const cctx = ZL_CCtx_create();
    EXPECT_NE(nullptr, cctx);
    EXPECT_FALSE(ZL_isError(
            ZL_CCtx_attachIntrospectionHooks(cctx, hooks.getRawHooks())));
    EXPECT_EQ(0, ctr);
    EXPECT_TRUE(ZL_isError(
            ZL_CCtx_compressMultiTypedRef(cctx, nullptr, 0, nullptr, 0)));
    EXPECT_EQ(1, ctr);
    EXPECT_FALSE(ZL_isError(ZL_CCtx_detachAllIntrospectionHooks(cctx)));
    EXPECT_TRUE(ZL_isError(
            ZL_CCtx_compressMultiTypedRef(cctx, nullptr, 0, nullptr, 0)));
    EXPECT_EQ(1, ctr);
    ZL_CCtx_free(cctx);
}

TEST(CompressIntrospectionTest, IFnoHooksTHENnoop)
{
    ctr                 = 0;
    ZL_CCtx* const cctx = ZL_CCtx_create();
    EXPECT_NE(nullptr, cctx);

    // No hooks object attached
    EXPECT_TRUE(ZL_isError(ZL_CCtx_compress(cctx, nullptr, 0, nullptr, 0)));
    EXPECT_EQ(0, ctr);

    // Null hook attached
    ZL_CompressIntrospectionHooks hooks = {};
    EXPECT_FALSE(ZL_isError(ZL_CCtx_attachIntrospectionHooks(cctx, &hooks)));
    EXPECT_TRUE(ZL_isError(
            ZL_CCtx_compressMultiTypedRef(cctx, nullptr, 0, nullptr, 0)));
    EXPECT_EQ(0, ctr);
    ZL_CCtx_free(cctx);
}

// code snippet containing a waypoint
static int func(ZL_CCtx* cctx)
{
    IF_CWAYPOINT_ENABLED(on_codecEncode_start, cctx)
    {
        return 1;
    }
    return 0;
}

TEST(CompressIntrospectionTest, IFwaypointEnabledTHENitIsExecuted)
{
    ZL_CompressIntrospectionHooks enabledHooks = {
        .on_codecEncode_start =
                [](void*,
                   ZL_Encoder*,
                   const ZL_Compressor*,
                   ZL_NodeID,
                   const ZL_Input*[],
                   size_t) noexcept {
                    std::cerr << "Starting transform" << std::endl;
                }
    };
    auto* cctx = ZL_CCtx_create();
    EXPECT_FALSE(
            ZL_isError(ZL_CCtx_attachIntrospectionHooks(cctx, &enabledHooks)));
    EXPECT_EQ(1, func(cctx));
    ZL_CCtx_free(cctx);
}

TEST(CompressIntrospectionTest, IFwaypointDisabledTHENitIsNotExecuted)
{
    ZL_CompressIntrospectionHooks notEnabledHooks = {};
    auto* cctx                                    = ZL_CCtx_create();
    EXPECT_FALSE(ZL_isError(
            ZL_CCtx_attachIntrospectionHooks(cctx, &notEnabledHooks)));
    EXPECT_EQ(0, func(cctx));
    ZL_CCtx_free(cctx);
}

class PrintingHooks : public openzl::CompressIntrospectionHooks {
   public:
    void on_codecEncode_start(
            ZL_Encoder* eictx,
            const ZL_Compressor* compressor,
            ZL_NodeID nid,
            const ZL_Input* inStreams[],
            size_t nbInStreams) override
    {
        std::cerr << "Starting transform "
                  << ZL_Compressor_Node_getName(compressor, nid) << std::endl;
        std::cerr << "  " << nbInStreams << " input streams:" << std::endl;
        for (size_t i = 0; i < nbInStreams; ++i) {
            std::cerr << "    - " << ZL_Input_id(inStreams[i]).sid << std::endl;
        }
    }
    void on_codecEncode_end(
            ZL_Encoder*,
            const ZL_Output* outStreams[],
            size_t nbOutputs,
            ZL_Report) override
    {
        std::cerr << "Ending transform. Generated " << nbOutputs
                  << " outstreams { ";
        for (size_t i = 0; i < nbOutputs; ++i) {
            std::cerr << ZL_Output_id(outStreams[i]).sid << " ";
        }
        std::cerr << "}" << std::endl;
    }
    void on_ZL_Encoder_getScratchSpace(ZL_Encoder* eictx, size_t size) override
    {
        (void)eictx;
        std::cerr << "Allocating scratch space of size " << size << std::endl;
    }
    void on_ZL_Encoder_sendCodecHeader(
            ZL_Encoder* eictx,
            const void* header,
            size_t size) override
    {
        (void)eictx;
        (void)header;
        std::cerr << "Sending transform header of size " << size << std::endl;
    }
    void on_ZL_Encoder_createTypedStream(
            ZL_Encoder* eic,
            int outStreamIndex,
            size_t eltsCapacity,
            size_t eltWidth,
            ZL_Output* createdStream) override
    {
        (void)eic;
        std::cerr << "Creating new stream [#" << ZL_Output_id(createdStream).sid
                  << "](" << outStreamIndex << "){ eltWidth: " << eltWidth
                  << ", nbElts: " << eltsCapacity << "}" << std::endl;
    }

    void on_migraphEncode_start(
            ZL_Graph* gctx,
            const ZL_Compressor* compressor,
            ZL_GraphID gid,
            ZL_Edge* inputs[],
            size_t nbInputs) override
    {
        (void)gctx;
        (void)inputs;
        (void)nbInputs;
        std::cerr << "Starting graph fn "
                  << ZL_Compressor_Graph_getName(compressor, gid) << std::endl;
    }
    void on_migraphEncode_end(
            ZL_Graph* gctx,
            ZL_GraphID successorGraphs[],
            size_t nbSuccessors,
            ZL_Report graphExecResult) override
    {
        (void)gctx;
        std::cerr << "Ending graph fn. Successors: { ";
        for (size_t i = 0; i < nbSuccessors; ++i) {
            std::cerr << successorGraphs[i].gid << " ";
        }
        std::cerr << "}" << std::endl;
    }
    void on_ZL_Graph_getScratchSpace(ZL_Graph* gctx, size_t size) override
    {
        (void)gctx;
        std::cerr << "Allocating scratch space of size " << size << std::endl;
    }
    void on_ZL_Edge_setMultiInputDestination_wParams(
            ZL_Graph* gctx,
            ZL_Edge* edge[],
            size_t nbInputs,
            ZL_GraphID gid,
            const ZL_LocalParams* lparams) override
    {
        (void)gctx;
        (void)lparams;
        std::cerr << "Setting multi-input destination of edges { ";
        for (size_t i = 0; i < nbInputs; ++i) {
            std::cerr << ZL_Input_id(ZL_Edge_getData(edge[i])).sid << " ";
        }
        std::cerr << "} to graph " << gid.gid << std::endl;
    }
};

TEST(CompressIntrospectionTest, EncoderSpecific)
{
    PrintingHooks hooks;
    ZL_CCtx* const mcctx = ZL_CCtx_create();
    ASSERT_NE(nullptr, mcctx);
    ASSERT_FALSE(ZL_isError(
            ZL_CCtx_attachIntrospectionHooks(mcctx, hooks.getRawHooks())));

    // use a custom transform that exercises all of the EICtx functions
    ZL_Type inputsTypes[] = { ZL_Type_numeric, ZL_Type_string };
    ZL_Type soTypes[] = { ZL_Type_numeric, ZL_Type_serial, ZL_Type_numeric };
    auto const encfn  = [](ZL_Encoder* eictx,
                          const ZL_Input* inputs[],
                          size_t) noexcept -> ZL_Report {
        auto so = ZL_Encoder_createTypedStream(
                eictx,
                0,
                ZL_Input_numElts(inputs[0]),
                ZL_Input_eltWidth(inputs[0]));
        memcpy(ZL_Output_ptr(so),
               ZL_Input_ptr(inputs[0]),
               ZL_Input_numElts(inputs[0]) * ZL_Input_eltWidth(inputs[0]));
        ZL_REQUIRE_SUCCESS(ZL_Output_commit(so, ZL_Input_numElts(inputs[0])));
        auto vo1 = ZL_Encoder_createTypedStream(
                eictx, 1, ZL_Input_contentSize(inputs[1]), 1);
        memcpy(ZL_Output_ptr(vo1),
               ZL_Input_ptr(inputs[1]),
               ZL_Input_contentSize(inputs[1]));
        ZL_REQUIRE_SUCCESS(
                ZL_Output_commit(vo1, ZL_Input_contentSize(inputs[1])));
        auto vo2 = ZL_Encoder_createTypedStream(
                eictx, 2, ZL_Input_numElts(inputs[1]), sizeof(uint32_t));
        memcpy(ZL_Output_ptr(vo2),
               ZL_Input_stringLens(inputs[1]),
               ZL_Input_numElts(inputs[1]) * sizeof(uint32_t));
        ZL_REQUIRE_SUCCESS(ZL_Output_commit(vo2, ZL_Input_numElts(inputs[1])));
        auto e = ZL_Encoder_getScratchSpace(eictx, 100);
        ZL_REQUIRE_NN(e, NULL);
        ZL_Encoder_sendCodecHeader(eictx, ZL_Input_ptr(inputs[0]), 12);
        return ZL_returnSuccess();
    };

    ZL_MIGraphDesc mgd = {
        .CTid                = 1003,
        .inputTypes          = inputsTypes,
        .nbInputs            = 2,
        .lastInputIsVariable = false,
        .soTypes             = soTypes,
        .nbSOs               = 3,
        .nbVOs               = 0,
    };
    ZL_MIEncoderDesc mtd = {
        .gd          = mgd,
        .transform_f = encfn,
        // .localParams = nullptr, // no LPs
        .name = "test",
    };

    // create a compressor
    auto* compressor   = ZL_Compressor_create();
    auto nid           = ZL_Compressor_registerMIEncoder(compressor, &mtd);
    ZL_GraphID succs[] = {
        ZL_GRAPH_STORE,
        ZL_GRAPH_STORE,
        ZL_GRAPH_STORE,
    };
    auto gid = ZL_Compressor_registerStaticGraph_fromNode(
            compressor, nid, succs, 3);
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(compressor, gid));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_refCompressor(mcctx, compressor));

    // compress some data
    auto dg = datagen::DataGen();
    // numric, string
    std::vector<uint16_t> nums =
            dg.randVector<uint16_t>("numbers", 0, 10000, 5000);
    std::string str               = dg.randString("string");
    std::vector<uint32_t> strLens = {
        (uint32_t)str.size() / 2,
        (uint32_t)str.size() - ((uint32_t)str.size() / 2),
    };
    auto* numsRef = ZL_TypedRef_createNumeric(
            nums.data(), sizeof(nums[0]), nums.size());
    auto* strRef = ZL_TypedRef_createString(
            str.data(), str.size(), strLens.data(), strLens.size());
    std::string dst((nums.size() * sizeof(nums[0]) + str.size()) * 2, '\0');
    ZL_TypedRef const* inputRefs[] = { numsRef, strRef };
    ZL_REQUIRE_SUCCESS(
            ZL_CCtx_setParameter(mcctx, ZL_CParam_formatVersion, 18));
    ZL_REQUIRE_SUCCESS(ZL_CCtx_compressMultiTypedRef(
            mcctx, dst.data(), dst.size(), inputRefs, 2));

    ZL_TypedRef_free(strRef);
    ZL_TypedRef_free(numsRef);
    ZL_Compressor_free(compressor);
    ZL_CCtx_free(mcctx);
}

} // namespace openzl::tests
