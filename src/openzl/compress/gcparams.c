// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string.h>

#include "openzl/codecs/zl_illegal.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/wire_format.h"
#include "openzl/compress/dyngraph_interface.h" // ZL_transferRuntimeGraphParams
#include "openzl/compress/gcparams.h"           // GCParams
#include "openzl/shared/mem.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_compress.h" // ZL_CParam

// All defaults for Global parameters
const GCParams GCParams_default = {
    .compressionLevel      = ZL_COMPRESSIONLEVEL_DEFAULT,
    .decompressionLevel    = ZL_DECOMPRESSIONLEVEL_DEFAULT,
    .formatVersion         = 0,
    .stickyParameters      = 0,
    .explicitStart         = 0,
    .permissiveCompression = ZL_TernaryParam_disable,
    // We want to checksum by default and we want to make sure that
    // after all parameters are applied we are left with either enable/disable
    // so we don't need to manage the case of `ZL_TernaryParam_auto`.
    .compressedChecksum = ZL_TernaryParam_enable,
    .contentChecksum    = ZL_TernaryParam_enable,
    .storeOnExpansion   = ZL_TernaryParam_enable,
    .minStreamSize      = ZL_MINSTREAMSIZE_DEFAULT,
};

typedef struct {
    const char** names;
    size_t nbNames;
} GCParamNames;

typedef struct {
    ZL_CParam param;
    /// Must have at least 1 name
    /// The first name is the canonical name
    GCParamNames names;
} GCParamToName;

const GCParamToName GCParams_kAllParams[] = {
    { ZL_CParam_stickyParameters,
      { (const char*[]){ "stickyParameters" }, 1 } },
    { ZL_CParam_compressionLevel,
      { (const char*[]){ "compressionLevel" }, 1 } },
    { ZL_CParam_decompressionLevel,
      { (const char*[]){ "decompressionLevel" }, 1 } },
    { ZL_CParam_formatVersion, { (const char*[]){ "formatVersion" }, 1 } },
    { ZL_CParam_permissiveCompression,
      { (const char*[]){ "permissiveCompression" }, 1 } },
    { ZL_CParam_compressedChecksum,
      { (const char*[]){ "compressedChecksum" }, 1 } },
    { ZL_CParam_contentChecksum, { (const char*[]){ "contentChecksum" }, 1 } },
    { ZL_CParam_storeOnExpansion,
      { (const char*[]){ "storeOnExpansion" }, 1 } },
    { ZL_CParam_minStreamSize, { (const char*[]){ "minStreamSize" }, 1 } }
};

static ZL_Report setTernaryParam(ZL_TernaryParam* param, int value)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    switch (value) {
        case ZL_TernaryParam_enable:
        case ZL_TernaryParam_disable:
        case ZL_TernaryParam_auto:
            *param = (ZL_TernaryParam)value;
            return ZL_returnSuccess();
        default:
            ZL_ERR(compressionParameter_invalid,
                   "Ternary param value invalid: %d",
                   value);
    }
}

ZL_Report
GCParams_setParameter(GCParams* gcparams, ZL_CParam paramId, int value)
{
    ZL_ASSERT_NN(gcparams);
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    switch (paramId) {
        case ZL_CParam_stickyParameters:
            gcparams->stickyParameters = (value != 0); // 0 or 1
            break;
        case ZL_CParam_compressionLevel:
            // TODO (@Cyan): provide bounds
            gcparams->compressionLevel = value;
            break;
        case ZL_CParam_decompressionLevel:
            // TODO (@Cyan): check bounds
            gcparams->decompressionLevel = value;
            break;
        case ZL_CParam_permissiveCompression:
            ZL_ERR_IF_ERR(
                    setTernaryParam(&gcparams->permissiveCompression, value));
            break;
        case ZL_CParam_compressedChecksum:
            ZL_ERR_IF_ERR(
                    setTernaryParam(&gcparams->compressedChecksum, value));
            break;
        case ZL_CParam_contentChecksum:
            ZL_ERR_IF_ERR(setTernaryParam(&gcparams->contentChecksum, value));
            break;
        case ZL_CParam_storeOnExpansion:
            ZL_ERR_IF_ERR(setTernaryParam(&gcparams->storeOnExpansion, value));
            break;
        case ZL_CParam_minStreamSize:
            // TODO (@Cyan): provide bounds
            gcparams->minStreamSize = (unsigned)value;
            break;
        case ZL_CParam_formatVersion:
            if (!(value == 0 || ZL_isFormatVersionSupported((uint32_t)value)))
                ZL_ERR(formatVersion_unsupported);
            gcparams->formatVersion = (uint32_t)value;
            break;
        default:
            ZL_ERR(compressionParameter_invalid);
    }
    return ZL_returnSuccess();
}

ZL_Report GCParams_setStartingGraphID(
        GCParams* gcparams,
        ZL_GraphID graphid,
        const ZL_RuntimeGraphParameters* rgp,
        Arena* arena)
{
    ZL_ASSERT_NN(gcparams);
    gcparams->explicitStart   = 1;
    gcparams->startingGraphID = graphid;
    gcparams->rgp             = ZL_transferRuntimeGraphParams(arena, rgp);
    return ZL_returnSuccess();
}

ZL_Report GCParams_resetStartingGraphID(GCParams* gcparams)
{
    ZL_ASSERT_NN(gcparams);
    gcparams->explicitStart   = 0;
    gcparams->startingGraphID = ZL_GRAPH_ILLEGAL;
    gcparams->rgp             = NULL;
    return ZL_returnSuccess();
}

#define SET_DEFAULT(dst, defaults, param) \
    if (dst->param == 0) {                \
        dst->param = defaults->param;     \
    }
void GCParams_applyDefaults(GCParams* dst, const GCParams* defaults)
{
    // note: stickyParameters aren't overridden by defaults
    SET_DEFAULT(dst, defaults, compressionLevel);
    SET_DEFAULT(dst, defaults, decompressionLevel);
    SET_DEFAULT(dst, defaults, permissiveCompression);
    SET_DEFAULT(dst, defaults, formatVersion);
    SET_DEFAULT(dst, defaults, compressedChecksum);
    SET_DEFAULT(dst, defaults, contentChecksum);
    SET_DEFAULT(dst, defaults, storeOnExpansion);
    SET_DEFAULT(dst, defaults, minStreamSize);
}
#undef SET_DEFAULT

ZL_Report GCParams_finalize(GCParams* gcparams)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    uint32_t const formatVersion =
            (uint32_t)GCParams_getParameter(gcparams, ZL_CParam_formatVersion);

    // Check if the format version is unset
    ZL_ERR_IF_EQ(formatVersion, 0, formatVersion_notSet);

    // Turn off checksums for format versions that don't support them.
    if (formatVersion <= 3) {
        ZL_Report const r1 = GCParams_setParameter(
                gcparams, ZL_CParam_contentChecksum, ZL_TernaryParam_disable);
        ZL_Report const r2 = GCParams_setParameter(
                gcparams,
                ZL_CParam_compressedChecksum,
                ZL_TernaryParam_disable);
        // These functions cannot fail.
        ZL_ASSERT_SUCCESS(r1);
        ZL_ASSERT_SUCCESS(r2);
    }

    return ZL_returnSuccess();
}

int GCParams_getParameter(const GCParams* gcparams, ZL_CParam paramId)
{
    ZL_ASSERT_NN(gcparams);
    switch (paramId) {
        case ZL_CParam_stickyParameters:
            return gcparams->stickyParameters;
        case ZL_CParam_compressionLevel:
            return gcparams->compressionLevel;
        case ZL_CParam_decompressionLevel:
            return gcparams->decompressionLevel;
        case ZL_CParam_permissiveCompression:
            return (int)gcparams->permissiveCompression;
        case ZL_CParam_formatVersion:
            return (int)gcparams->formatVersion;
        case ZL_CParam_compressedChecksum:
            return (int)gcparams->compressedChecksum;
        case ZL_CParam_contentChecksum:
            return (int)gcparams->contentChecksum;
        case ZL_CParam_storeOnExpansion:
            return (int)gcparams->storeOnExpansion;
        case ZL_CParam_minStreamSize:
            return (int)gcparams->minStreamSize;
        default:
            return 0;
    }
}

ZL_Report GCParams_forEachParam(
        const GCParams* gcparams,
        ZL_Compressor_ForEachParamCallback callback,
        void* opaque)
{
    ZL_ASSERT_NN(gcparams);
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    for (size_t i = 0; i < ZL_ARRAY_SIZE(GCParams_kAllParams); ++i) {
        const ZL_CParam param = GCParams_kAllParams[i].param;
        const int value       = GCParams_getParameter(gcparams, param);
        if (value != 0) {
            ZL_ERR_IF_ERR(callback(opaque, param, value));
        }
    }
    return ZL_returnSuccess();
}

int GCParams_explicitStartSet(const GCParams* gcparams)
{
    return gcparams->explicitStart != 0;
}

/* should only be invoked after checking that explicitStart != 0 */
ZL_GraphID GCParams_explicitStart(const GCParams* gcparams)
{
    ZL_ASSERT_NN(gcparams);
    ZL_ASSERT_NE(gcparams->explicitStart, 0);
    return gcparams->startingGraphID;
}

/* should only be invoked after checking that explicitStart != 0 */
const ZL_RuntimeGraphParameters* GCParams_startParams(const GCParams* gcparams)
{
    ZL_ASSERT_NN(gcparams);
    ZL_ASSERT_NE(gcparams->explicitStart, 0);
    return gcparams->rgp; /* note: can be NULL, if not set */
}

void GCParams_copy(GCParams* dst, const GCParams* src)
{
    ZL_memcpy(dst, src, sizeof(GCParams));
}

ZL_Report GCParams_strToParam(const char* param)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    const size_t len = strlen(param);
    for (size_t i = 0; i < ZL_ARRAY_SIZE(GCParams_kAllParams); ++i) {
        for (size_t n = 0; n < GCParams_kAllParams[i].names.nbNames; ++n) {
            const char* candidate = GCParams_kAllParams[i].names.names[n];
            if (strlen(candidate) == len && !memcmp(param, candidate, len)) {
                return ZL_returnValue((size_t)GCParams_kAllParams[i].param);
            }
        }
    }
    ZL_ERR(compressionParameter_invalid, "Parameter string invalid: %s", param);
}

const char* GCParams_paramToStr(ZL_CParam param)
{
    for (size_t i = 0; i < ZL_ARRAY_SIZE(GCParams_kAllParams); ++i) {
        if (param == GCParams_kAllParams[i].param) {
            return GCParams_kAllParams[i].names.names[0];
        }
    }
    return NULL;
}
