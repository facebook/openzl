// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

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
        trackingMaterialize(const void*, const ZL_LocalParams* params)
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

static void trackingDematerialize(const void*, void* materialized)
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
        ZL_Report r = CTM_init(&ctm_, NULL);
        ASSERT_FALSE(ZL_isError(r)) << "CTM_init should succeed";
        nextCtid_ = 1000;
    }

    void TearDown() override
    {
        CTM_destroy(&ctm_);
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

    CNodes_manager ctm_{};
    ZL_IDType nextCtid_;
    ZL_MaterializerDesc trackingMaterializer_{
        .materializeFn   = trackingMaterialize,
        .dematerializeFn = trackingDematerialize,
        .paramId         = MATERIALIZED_PARAM_ID,
    };
};

} // namespace

} // namespace openzl
