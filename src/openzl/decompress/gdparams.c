// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/decompress/gdparams.h" // GDParams
#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h"
#include "openzl/zl_decompress.h" // ZL_DParam

// All defaults for Global parameters
const GDParams GDParams_default = {
    .stickyParameters        = 0,
    .checkCompressedChecksum = ZL_TernaryParam_enable,
    .checkContentChecksum    = ZL_TernaryParam_enable,
    .enableCodecFusion       = ZL_TernaryParam_enable,
};

ZL_Report
GDParams_setParameter(GDParams* gdparams, ZL_DParam paramId, int value)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT_NN(gdparams);
    switch (paramId) {
        case ZL_DParam_stickyParameters:
            gdparams->stickyParameters = (value != 0); // 0 or 1
            break;
        case ZL_DParam_checkCompressedChecksum:
            gdparams->checkCompressedChecksum = (ZL_TernaryParam)value;
            break;
        case ZL_DParam_checkContentChecksum:
            gdparams->checkContentChecksum = (ZL_TernaryParam)value;
            break;
        case ZL_DParam_enableCodecFusion:
            gdparams->enableCodecFusion = (ZL_TernaryParam)value;
            break;
        default:
            ZL_ERR(compressionParameter_invalid);
    }
    return ZL_returnSuccess();
}

#define SET_DEFAULT(dst, defaults, param) \
    if (dst->param == 0) {                \
        dst->param = defaults->param;     \
    }
void GDParams_applyDefaults(GDParams* dst, const GDParams* defaults)
{
    // Note: stickyParameters aren't overridden by defaults
    SET_DEFAULT(dst, defaults, checkCompressedChecksum);
    SET_DEFAULT(dst, defaults, checkContentChecksum);
    SET_DEFAULT(dst, defaults, enableCodecFusion);
}
#undef SET_DEFAULT

ZL_Report GDParams_finalize(GDParams* gdparams)
{
    ZL_ASSERT_NN(gdparams);
    return ZL_returnSuccess();
}

int GDParams_getParameter(const GDParams* gdparams, ZL_DParam paramId)
{
    ZL_ASSERT_NN(gdparams);
    switch (paramId) {
        case ZL_DParam_stickyParameters:
            return gdparams->stickyParameters;
        case ZL_DParam_checkCompressedChecksum:
            return (int)gdparams->checkCompressedChecksum;
        case ZL_DParam_checkContentChecksum:
            return (int)gdparams->checkContentChecksum;
        case ZL_DParam_enableCodecFusion:
            return (int)gdparams->enableCodecFusion;
        default:
            return 0;
    }
}

void GDParams_copy(GDParams* dst, const GDParams* src)
{
    ZL_memcpy(dst, src, sizeof(GDParams));
}
