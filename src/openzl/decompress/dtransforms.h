// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_DTRANSFORMS_H
#define ZSTRONG_DTRANSFORMS_H

#include "openzl/common/allocation.h"
#include "openzl/common/errors_internal.h" // ZL_RESULT_DECLARE_TYPE()
#include "openzl/common/map.h"
#include "openzl/common/opaque.h"
#include "openzl/common/opaque_types_internal.h" // ZL_RESULT_OF(ZL_IDType)
#include "openzl/common/wire_format.h" // TransformType_e, ZL_StandardTransformID_end
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_dtransform.h" // ZL_PipeDecoderDesc
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h" // ZL_IDType

ZL_BEGIN_C_DECLS

typedef enum { dtr_pipe, dtr_split, dtr_typed, dtr_vo, dtr_mi } DTrType_e;

typedef union {
    ZL_PipeDecoderDesc dpt;
    ZL_SplitDecoderDesc dst;
    ZL_TypedDecoderDesc dtt;
    ZL_VODecoderDesc dvo;
    ZL_MIDecoderDesc dmi;
} DTrDesc;

typedef struct DTransform_s DTransform;

typedef ZL_Report (*DTransformFn)(
        ZL_Decoder* dictx,
        const DTransform* transform,
        const ZL_Data* src[],
        size_t nbSrcs);

struct DTransform_s {
    ZL_MIGraphDesc miGraphDesc;
    DTransformFn transformFn;
    void const* opaque;
    DTrDesc implDesc;
    DTrType_e type;
    void* state; /* store state, only for custom transforms */
};

// DTransform functions

// Accessors
const char* DT_getTransformName(const DTransform* dt);
const ZL_CodecStateManager* DT_getTransformStateMgr(const DTransform* dt);

int DT_isNbRegensCompatible(const DTransform* dt, size_t nbRegens);

// Note: if @regenIndex is too large, @return the type of the last regen.
// This behavior is compatible with VI decoder Transforms.
ZL_Type DT_getRegenType(const DTransform* dt, int regenIndex);

// DTransform wrappers
/* These functions are wrappers for respectively Typed and VO Transforms,
 * in order to present a common DTransformFn interface to decompression runners.
 * They are published because they are referenced from the Standard Decoders
 * array. */
ZL_Report DT_typedTransformWrapper(
        ZL_Decoder* dictx,
        DTransform const* transform,
        ZL_Data const* ins[],
        size_t nbIns);
ZL_Report DT_voTransformWrapper(
        ZL_Decoder* dictx,
        DTransform const* transform,
        ZL_Data const* ins[],
        size_t nbIns);
ZL_Report DT_miTransformWrapper(
        ZL_Decoder* dictx,
        DTransform const* transform,
        ZL_Data const* ins[],
        size_t nbIns);

// DTransform Manager

typedef const DTransform* DTrPtr;
ZL_RESULT_DECLARE_TYPE(DTrPtr);

ZL_DECLARE_MAP_TYPE(DTransformMap, ZL_IDType, DTransform);

typedef struct {
    DTransformMap dtmap;
    Arena* allocator;
    void* states[ZL_StandardTransformID_end]; /* state storage for Standard
                                                 Transforms */
    ZL_OpaquePtrRegistry opaquePtrs;
    ZL_OperationContext* opCtx;
} DTransforms_manager;

// Constructors / destructors

ZL_Report DTM_init(
        DTransforms_manager* dtm,
        ZL_OperationContext* opCtx,
        uint32_t maxNbTransforms);

void DTM_destroy(DTransforms_manager* dtm);

// Accessors

/* Note :
 * This function is designed as if it could not fail,
 * but it could, for example if local array is overflowed.
 * Even in the future, with dynamic array, allocation could fail.
 */
ZL_RESULT_OF(ZL_IDType)
DTM_registerDPipeTransform(
        DTransforms_manager* dtm,
        const ZL_PipeDecoderDesc* dptd);

/* Note :
 * This function is designed as if it could not fail,
 * but it could, for example if local array is overflowed.
 * Even in the future, with dynamic array, allocation could fail.
 */
ZL_RESULT_OF(ZL_IDType)
DTM_registerDSplitTransform(
        DTransforms_manager* dtm,
        const ZL_SplitDecoderDesc* dstd);

/* Note :
 * This function is designed as if it could not fail,
 * but it could, for example if local array is overflowed.
 * Even in the future, with dynamic array, allocation could fail.
 */
ZL_RESULT_OF(ZL_IDType)
DTM_registerDTypedTransform(
        DTransforms_manager* dtm,
        const ZL_TypedDecoderDesc* dttd);

ZL_RESULT_OF(ZL_IDType)
DTM_registerDVOTransform(
        DTransforms_manager* dtm,
        const ZL_VODecoderDesc* dvotd);

ZL_RESULT_OF(ZL_IDType)
DTM_registerDMITransform(
        DTransforms_manager* dtm,
        const ZL_MIDecoderDesc* dmitd);

ZL_RESULT_OF(DTrPtr)
DTM_getTransform(
        const DTransforms_manager* dtm,
        PublicTransformInfo trinfo,
        unsigned formatVersion);

// @return NULL in case of error.
// If the transform exists and does not have a name,
// @return "" empty string.
const char* DTM_getTransformName(
        const DTransforms_manager* dtm,
        PublicTransformInfo trinfo,
        unsigned formatVersion);

// @return a writable reference to a position in memory or NULL if @p trid is
// not valid. tracking a Transform State.
void** DTM_getStatePtr(DTransforms_manager* dtm, PublicTransformInfo trid);

ZL_END_C_DECLS

#endif // ZSTRONG_DTRANSFORMS_H
