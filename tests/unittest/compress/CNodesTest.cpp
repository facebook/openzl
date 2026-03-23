// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <random>

#include "openzl/common/operation_context.h"
#include "openzl/compress/cnodes.h"
#include "openzl/compress/compress_types.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_errors.h"

using namespace ::testing;

namespace openzl {

// Param IDs for test
constexpr int MATERIALIZED_PARAM_ID = 200;

namespace {

struct MaterializationCounter {
    int materializeCount   = 0;
    int dematerializeCount = 0;

    void reset()
    {
        materializeCount   = 0;
        dematerializeCount = 0;
    }

    bool balanced() const
    {
        return materializeCount == dematerializeCount;
    }
};

static MaterializationCounter gCounter{};

static ZL_RESULT_OF(ZL_VoidPtr)
        trackingMaterialize(ZL_Materializer*, const ZL_LocalParams* params)
                ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, nullptr);
    gCounter.materializeCount++;
    int* data = new int(0);
    if (params->intParams.nbIntParams > 0) {
        *data = params->intParams.intParams[0].paramValue;
    }
    return ZL_WRAP_VALUE(data);
}

static void trackingDematerialize(ZL_Materializer*, void* materialized)
        ZL_NOEXCEPT_FUNC_PTR
{
    gCounter.dematerializeCount++;
    auto* data = static_cast<int*>(materialized);
    delete data;
}

static ZL_Report dummyTransform(
        ZL_Encoder* eictx,
        const ZL_Input* inputs[],
        size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
{
    return ZL_returnSuccess();
}

class CNodesTest : public Test {
   protected:
    void SetUp() override
    {
        gCounter.reset();
        ZL_OC_init(&opCtx_);
        ZL_Report r = CTM_init(&ctm_, &opCtx_);
        ASSERT_FALSE(ZL_isError(r)) << "CTM_init should succeed";
        nextCtid_ = 1000;
    }

    void TearDown() override
    {
        CTM_destroy(&ctm_);
        ZL_OC_destroy(&opCtx_);
        EXPECT_TRUE(gCounter.balanced())
                << "materializeCount=" << gCounter.materializeCount
                << " dematerializeCount=" << gCounter.dematerializeCount;
    }

    InternalTransform_Desc createTransformDesc(
            const ZL_LocalParams& localParams,
            const ZL_MaterializerDesc* materializer)
    {
        static ZL_Type inputType  = ZL_Type_serial;
        static ZL_Type outputType = ZL_Type_serial;

        ZL_MIGraphDesc graphDesc{
            .CTid                = nextCtid_++,
            .inputTypes          = &inputType,
            .nbInputs            = 1,
            .lastInputIsVariable = false,
            .soTypes             = &outputType,
            .nbSOs               = 1,
            .voTypes             = nullptr,
            .nbVOs               = 0,
        };

        ZL_MIEncoderDesc publicDesc{
            .gd          = graphDesc,
            .transform_f = dummyTransform,
            .localParams = localParams,
            .name        = "test_transform",
        };
        if (materializer != nullptr) {
            publicDesc.materializer = *materializer;
        }

        return InternalTransform_Desc{
            .publicDesc   = publicDesc,
            .privateParam = nullptr,
            .ppSize       = 0,
        };
    }

    CNodeID registerCustomNode(
            const ZL_LocalParams& localParams,
            const ZL_MaterializerDesc* materializer)
    {
        InternalTransform_Desc desc =
                createTransformDesc(localParams, materializer);
        auto result = CTM_registerCustomTransform(&ctm_, &desc);
        EXPECT_FALSE(ZL_RES_isError(result));
        return ZL_RES_value(result);
    }

    CNodeID parameterizeNode(CNodeID baseId, const ZL_LocalParams& localParams)
    {
        const CNode* srcNode = CTM_getCNode(&ctm_, baseId);
        if (srcNode == nullptr) {
            return CNodeID{ .cnid = (ZL_IDType)-1 };
        }
        ZL_ParameterizedNodeDesc paramDesc{
            .node        = ZL_NodeID{ .nid = baseId.cnid },
            .localParams = &localParams,
        };
        auto result = CTM_parameterizeNode(&ctm_, srcNode, &paramDesc);
        EXPECT_FALSE(ZL_RES_isError(result));
        return ZL_RES_value(result);
    }

    ZL_LocalParams createLocalParamsWithInt(ZL_IntParam* intParam)
    {
        return ZL_LocalParams{
            .intParams = {
                .intParams   = intParam,
                .nbIntParams = 1,
            },
        };
    }

    ZL_OperationContext opCtx_{};
    CNodes_manager ctm_{};
    ZL_IDType nextCtid_;
    ZL_MaterializerDesc trackingMaterializer_{
        .materializeFn   = trackingMaterialize,
        .dematerializeFn = trackingDematerialize,
        .paramId         = MATERIALIZED_PARAM_ID,
    };
};

} // namespace

TEST_F(CNodesTest, WHENaMaterializerRequestsPersistentMemoryTHENitIsAllocated)
{
    const auto materializeWithPersistent =
            [](ZL_Materializer* matCtx, const ZL_LocalParams* params)
                    ZL_NOEXCEPT_FUNC_PTR -> ZL_RESULT_OF(ZL_VoidPtr) {
        ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, nullptr);

        // Allocate persistent memory from the arena
        if (params->intParams.nbIntParams == 0) {
            return ZL_WRAP_VALUE(nullptr);
        }
        auto amt = params->intParams.intParams[0].paramValue;
        int* persistent =
                static_cast<int*>(ZL_Materializer_allocate(matCtx, amt));
        ZL_ERR_IF_NULL(
                persistent, allocation, "Failed to allocate persistent memory");

        return ZL_WRAP_VALUE(persistent);
    };

    ZL_MaterializerDesc materializer = {
        .materializeFn   = materializeWithPersistent,
        .dematerializeFn = ZL_NOOP_DEMATERIALIZE,
        .paramId         = MATERIALIZED_PARAM_ID,
    };

    Arena* arena = ctm_.allocator;

    CNodeID nodeId   = registerCustomNode({}, &materializer);
    auto allocBefore = ALLOC_Arena_memAllocated(arena);
    auto usedBefore  = ALLOC_Arena_memUsed(arena);

    // Parameterize the node and allocate memory
    std::mt19937 gen(67);
    std::uniform_int_distribution<> uniform(1000, 1100);
    size_t memForNewParamNode;

    // Track the number of unique values
    std::set<int> values;
    {
        auto value = uniform(gen);
        values.insert(value);
        ZL_IntParam intParam = {
            .paramId    = 1,
            .paramValue = value,
        };
        ZL_LocalParams localParams = createLocalParamsWithInt(&intParam);
        parameterizeNode(nodeId, localParams);
        memForNewParamNode =
                ALLOC_Arena_memAllocated(arena) - allocBefore - value;
        ASSERT_EQ(
                memForNewParamNode,
                ALLOC_Arena_memUsed(arena) - usedBefore - value);
    }

    for (size_t i = 0; i < 20; ++i) {
        auto value = uniform(gen);
        values.insert(value);
        ZL_IntParam intParam = {
            .paramId    = 1,
            .paramValue = value,
        };
        ZL_LocalParams localParams = createLocalParamsWithInt(&intParam);
        parameterizeNode(nodeId, localParams);
    }
    const auto totalAlloc = std::accumulate(values.begin(), values.end(), 0);
    ASSERT_EQ(
            allocBefore + memForNewParamNode * 21 + totalAlloc,
            ALLOC_Arena_memAllocated(arena));
    ASSERT_EQ(
            usedBefore + memForNewParamNode * 21 + totalAlloc,
            ALLOC_Arena_memUsed(arena));
}

TEST_F(CNodesTest,
       WHENaMaterializerRequestsScratchSpaceTHENitIsAllocatedAndFreedAfterwards)
{
    // ASAN will verify proper memory lifetimes
    const auto materializeWithTransient =
            [](ZL_Materializer* matCtx, const ZL_LocalParams*)
                    ZL_NOEXCEPT_FUNC_PTR -> ZL_RESULT_OF(ZL_VoidPtr) {
        ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, nullptr);

        // Allocate transient scratch space
        int* scratch1 = static_cast<int*>(
                ZL_Materializer_getScratchSpace(matCtx, sizeof(int) * 10));
        ZL_ERR_IF_NULL(
                scratch1, allocation, "Failed to allocate scratch space 1");

        int* scratch2 = static_cast<int*>(
                ZL_Materializer_getScratchSpace(matCtx, sizeof(int) * 20));
        ZL_ERR_IF_NULL(
                scratch2, allocation, "Failed to allocate scratch space 2");

        // Use scratch space for something
        for (int i = 0; i < 10; i++) {
            scratch1[i] = i * 2;
        }

        return ZL_WRAP_VALUE(nullptr);
    };

    ZL_MaterializerDesc materializer = {
        .materializeFn   = materializeWithTransient,
        .dematerializeFn = ZL_NOOP_DEMATERIALIZE,
        .paramId         = MATERIALIZED_PARAM_ID,
    };

    // Register node with transient memory materializer
    CNodeID nodeId       = registerCustomNode({}, &materializer);
    ZL_IntParam intParam = {
        .paramId    = 1,
        .paramValue = 10000,
    };
    parameterizeNode(nodeId, createLocalParamsWithInt(&intParam));
}

} // namespace openzl
