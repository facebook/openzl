// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#ifndef OPENZL_TESTS_CDICTMGR_TEST_HELPERS_H
#define OPENZL_TESTS_CDICTMGR_TEST_HELPERS_H

#include <cstring>

#include "openzl/common/operation_context.h"
#include "openzl/compress/cnodes.h"
#include "openzl/compress/compress_types.h"
#include "openzl/compress/nodemgr.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_materializer.h"

#include "tests/unittest/dict/DictTestHelpers.h"

/* ================================================================
 * Mock dict materializer
 * ================================================================
 *
 * CDictMgr_cacheDict calls materializeFn with the dict-materialization
 * calling convention: (ZL_Materializer*, const void* src, size_t srcSize).
 * This function copies the raw dict content into arena-allocated memory.
 * The dematerializer is ZL_NOOP_DEMATERIALIZE since memory is arena-backed.
 */
inline ZL_RESULT_OF(ZL_VoidPtr) mockDictCopyMaterialize(
        ZL_Materializer* matCtx,
        const void* src,
        size_t srcSize) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, nullptr);
    void* copy = ZL_Materializer_allocate(matCtx, srcSize);
    ZL_ERR_IF_NULL(copy, allocation);
    if (copy != nullptr) {
        std::memcpy(copy, src, srcSize);
    }
    return ZL_WRAP_VALUE(copy);
}

/// Build a default ZL_MaterializerDesc2 using the mock copy-materializer.
inline ZL_MaterializerDesc2 makeDefaultDictMaterializer()
{
    ZL_MaterializerDesc2 desc{};
    desc.materializeFn   = mockDictCopyMaterialize;
    desc.dematerializeFn = ZL_NOOP_DEMATERIALIZE;
    return desc;
}

/* ================================================================
 * MockNodesMgr
 * ================================================================
 *
 * Lightweight RAII wrapper that owns a ZL_OperationContext and a
 * Nodes_manager (with only the ctm field initialized via CTM_init).
 * Provides helpers to register CNodes carrying a dictID + materializer
 * so CDictMgr_findMaterializer can locate them during dict loading.
 */
class MockNodesMgr {
   public:
    MockNodesMgr()
    {
        ZL_OC_init(&opCtx_);
        ZL_REQUIRE_SUCCESS(NM_init(&nmgr_, &opCtx_));
    }

    ~MockNodesMgr()
    {
        NM_destroy(&nmgr_);
        ZL_OC_destroy(&opCtx_);
    }

    MockNodesMgr(const MockNodesMgr&)            = delete;
    MockNodesMgr& operator=(const MockNodesMgr&) = delete;

    /// Register a CNode with the given dictID and materializer descriptor.
    void addDictNode(
            ZL_DictID dictID,
            ZL_MaterializerDesc2 matDesc,
            bool isStandard = false)
    {
        ZL_Type inputType  = ZL_Type_serial;
        ZL_Type outputType = ZL_Type_serial;

        ZL_MIGraphDesc graphDesc{};
        graphDesc.CTid       = nextCtid_++;
        graphDesc.inputTypes = &inputType;
        graphDesc.nbInputs   = 1;
        graphDesc.soTypes    = &outputType;
        graphDesc.nbSOs      = 1;

        ZL_MIEncoderDesc publicDesc{};
        publicDesc.gd          = graphDesc;
        publicDesc.transform_f = dummyTransform;
        publicDesc.name        = "mock_dict_node";
        publicDesc.dictMat     = matDesc;
        publicDesc.dictID      = dictID;

        InternalTransform_Desc desc{};
        desc.publicDesc = publicDesc;

        if (isStandard) {
            ZL_REQUIRE_SUCCESS(CTM_registerStandardTransform(
                    &nmgr_.ctm,
                    &desc,
                    ZL_MIN_FORMAT_VERSION,
                    ZL_MAX_FORMAT_VERSION));
        } else {
            ZL_REQUIRE_SUCCESS(CTM_registerCustomTransform(&nmgr_.ctm, &desc));
        }
    }

    /// Convenience overload using the default mock copy-materializer.
    void addDictNode(ZL_DictID dictID)
    {
        addDictNode(dictID, makeDefaultDictMaterializer());
    }

    Nodes_manager* nodesManager()
    {
        return &nmgr_;
    }

    ZL_OperationContext* opCtx()
    {
        return &opCtx_;
    }

   private:
    static ZL_Report dummyTransform(
            ZL_Encoder* /*eictx*/,
            const ZL_Input* /*inputs*/[],
            size_t /*nbInputs*/) ZL_NOEXCEPT_FUNC_PTR
    {
        return ZL_returnSuccess();
    }

    ZL_OperationContext opCtx_{};
    Nodes_manager nmgr_{};
    ZL_IDType nextCtid_ = 1000;
};

#endif // OPENZL_TESTS_CDICTMGR_TEST_HELPERS_H
