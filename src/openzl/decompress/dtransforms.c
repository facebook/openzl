// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/decompress/dtransforms.h"
#include "openzl/codecs/decoder_registry.h" // SDecoders_array
#include "openzl/common/allocation.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/limits.h"
#include "openzl/common/vector.h"
#include "openzl/decompress/dictx.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_errors.h"

typedef ZL_Type* ZL_Type_p;
ZL_RESULT_DECLARE_TYPE(ZL_Type_p);

ZL_Report DTM_init(
        DTransforms_manager* dtm,
        ZL_OperationContext* opCtx,
        uint32_t maxNbTransforms)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);

    dtm->opCtx = opCtx;

    ZL_OpaquePtrRegistry_init(&dtm->opaquePtrs);
    dtm->dtmap     = DTransformMap_create(maxNbTransforms);
    dtm->allocator = ALLOC_HeapArena_create();
    ZL_ERR_IF_NULL(
            dtm->allocator, allocation, "DTM_init: failed creating allocator");
    return ZL_returnSuccess();
}

static void DTM_statesDestroy(DTransforms_manager* dtm)
{
    ZL_ASSERT_NN(dtm);
    for (size_t u = 0; u < ZL_StandardTransformID_end; u++) {
        if (dtm->states[u] != NULL) {
            ZL_CodecStateFree freeState =
                    DT_getTransformStateMgr(&SDecoders_array[u].dtr)->stateFree;
            ZL_ASSERT_NN(freeState);
            freeState(dtm->states[u]);
        }
    }
    DTransformMap_IterMut iter = DTransformMap_iterMut(&dtm->dtmap);
    for (DTransformMap_Entry* entry;
         (entry = DTransformMap_IterMut_next(&iter));) {
        if (entry->val.state != NULL) {
            ZL_CodecStateFree freeState =
                    DT_getTransformStateMgr(&entry->val)->stateFree;
            ZL_ASSERT_NN(freeState);
            freeState(entry->val.state);
        }
    }
}

void DTM_destroy(DTransforms_manager* dtm)
{
    ZL_OpaquePtrRegistry_destroy(&dtm->opaquePtrs);
    DTM_statesDestroy(dtm);
    DTransformMap_destroy(&dtm->dtmap);
    ALLOC_Arena_freeArena(dtm->allocator);
    dtm->allocator = NULL;
}

static ZL_Report DTM_storeTransformName(
        DTransforms_manager* dtm,
        const char** namePtr)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dtm->opCtx);

    const char* name = *namePtr;
    if (name != NULL) {
        const size_t len = strlen(name) + 1;
        ALLOC_ARENA_MALLOC_CHECKED(char, nameCopy, len, dtm->allocator);
        memcpy(nameCopy, name, len);
        *namePtr = nameCopy;
    }
    return ZL_returnSuccess();
}

static ZL_RESULT_OF(ZL_Type_p) DTM_storeStreamTypes(
        DTransforms_manager* dtm,
        ZL_Type const* types,
        unsigned nbTypes)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_Type_p, dtm->opCtx);

    const ZL_Type_p result =
            ALLOC_Arena_malloc(dtm->allocator, sizeof(ZL_Type) * nbTypes);
    ZL_ERR_IF_NULL(
            result,
            allocation,
            "DTM_storeStreamTypes: failed allocating buffer for stream types");
    if (nbTypes)
        memcpy(result, types, sizeof(ZL_Type) * nbTypes);
    return ZL_RESULT_WRAP_VALUE(ZL_Type_p, result);
}

static ZL_RESULT_OF(ZL_Type_p) DTM_setOutStreamTypes(
        DTransforms_manager* dtm,
        ZL_Type const type,
        unsigned nbTypes)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_Type_p, dtm->opCtx);

    const ZL_Type_p result =
            ALLOC_Arena_malloc(dtm->allocator, sizeof(ZL_Type) * nbTypes);
    ZL_ERR_IF_NULL(
            result,
            allocation,
            "DTM_setOutStreamTypes: failed allocating buffer for stream types");
    for (size_t i = 0; i < nbTypes; ++i) {
        result[i] = type;
    }
    return ZL_RESULT_WRAP_VALUE(ZL_Type_p, result);
}

const char* DT_getTransformName(const DTransform* dt)
{
    ZL_ASSERT_NN(dt);
    switch (dt->type) {
        case dtr_pipe:
            return STR_REPLACE_NULL(dt->implDesc.dpt.name);
        case dtr_split:
            return STR_REPLACE_NULL(dt->implDesc.dst.name);
        case dtr_typed:
            return STR_REPLACE_NULL(dt->implDesc.dtt.name);
        case dtr_vo:
            return STR_REPLACE_NULL(dt->implDesc.dvo.name);
        case dtr_mi:
            ZL_DLOG(SEQ + 1,
                    "DT_getTransformName (MITransform [%p] : '%s'[%p])",
                    (const void*)dt,
                    STR_REPLACE_NULL(dt->implDesc.dmi.name),
                    dt->implDesc.dmi.name);
            return STR_REPLACE_NULL(dt->implDesc.dmi.name);
        default:
            ZL_ASSERT_FAIL("unknown transform type");
            return "";
    }
}

int DT_isNbRegensCompatible(const DTransform* dt, size_t nbRegens)
{
    ZL_ASSERT_NN(dt);
    if (dt->miGraphDesc.lastInputIsVariable) {
        return nbRegens >= dt->miGraphDesc.nbInputs - 1;
    }
    return nbRegens == dt->miGraphDesc.nbInputs;
}

ZL_Type DT_getRegenType(const DTransform* dt, int regenIdx)
{
    ZL_ASSERT_NN(dt);
    ZL_ASSERT_GE(regenIdx, 0);
    // Note: miGraphDesc is shared across encoder and decoder.
    // In the encoder direction, a "regen" is an "input"
    if (regenIdx >= (int)dt->miGraphDesc.nbInputs)
        regenIdx = (int)dt->miGraphDesc.nbInputs - 1;
    return dt->miGraphDesc.inputTypes[regenIdx];
}

const ZL_CodecStateManager* DT_getTransformStateMgr(const DTransform* dt)
{
    ZL_ASSERT_NN(dt);
    switch (dt->type) {
        case dtr_typed:
            return &dt->implDesc.dtt.trStateMgr;
        case dtr_vo:
            return &dt->implDesc.dvo.trStateMgr;
        case dtr_mi:
            return &dt->implDesc.dmi.trStateMgr;
        case dtr_pipe:
        case dtr_split:
            return NULL;
        default:;
    }
    ZL_ASSERT_FAIL("unknown transform type");
    return NULL;
}

static ZL_RESULT_OF(ZL_IDType) DTM_registerDCustomTransform(
        DTransforms_manager* dtm,
        const DTransform* dct)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_IDType, dtm->opCtx);

    ZL_ASSERT_NN(dtm);
    ZL_ASSERT_NN(dct);

    DTransformMap_Entry entry   = { .key = dct->miGraphDesc.CTid, .val = *dct };
    DTransformMap_Insert insert = DTransformMap_insert(&dtm->dtmap, &entry);
    ZL_ERR_IF(
            insert.badAlloc,
            allocation,
            "DTM_registerDCustomTransform: failed pushing dct into map");

    // TODO: we silently let the write fail and leave the old value in place
    // if there's a collision. Consider raising an error? (This means also
    // changing the early returns elsewhere.)

    return ZL_RESULT_WRAP_VALUE(ZL_IDType, insert.ptr->val.miGraphDesc.CTid);
}

static ZL_Report pipeTransformWrapper(
        ZL_Decoder* dictx,
        const DTransform* transform,
        const ZL_Data* ins[],
        size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(ins);
    ZL_ASSERT_EQ(nbIns, 1);
    (void)nbIns;
    ZL_ASSERT_EQ(ZL_Data_type(ins[0]), ZL_Type_serial);
    void const* src    = ZL_Data_rPtr(ins[0]);
    size_t srcSize     = ZL_Data_numElts(ins[0]);
    size_t dstCapacity = srcSize;
    ZL_ASSERT_NN(transform);
    if (transform->implDesc.dpt.dstBound_f != NULL) {
        dstCapacity = transform->implDesc.dpt.dstBound_f(src, srcSize);
    }

    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, dstCapacity, 1);
    ZL_ERR_IF_NULL(out, allocation);

    void* const dst = ZL_Output_ptr(out);
    size_t const dstSize =
            transform->implDesc.dpt.transform_f(dst, dstCapacity, src, srcSize);

    ZL_ERR_IF_GT(
            dstSize,
            dstCapacity,
            transform_executionFailure,
            "transform %s failed",
            DT_getTransformName(transform));

    ZL_ERR_IF_ERR(ZL_Output_commit(out, dstSize));

    return ZL_returnValue(1);
}

static const ZL_Type kSerializedType = ZL_Type_serial;

ZL_RESULT_OF(ZL_IDType)
DTM_registerDPipeTransform(
        DTransforms_manager* dtm,
        const ZL_PipeDecoderDesc* dpt)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_IDType, dtm->opCtx);

    ZL_MIGraphDesc migd = {
        .CTid       = dpt->CTid,
        .inputTypes = &kSerializedType,
        .nbInputs   = 1,
        .soTypes    = &kSerializedType,
        .nbSOs      = 1,
    };
    DTransform transform = {
        .miGraphDesc  = migd,
        .transformFn  = pipeTransformWrapper,
        .implDesc.dpt = *dpt,
        .type         = dtr_pipe,
    };

    ZL_ERR_IF_ERR(DTM_storeTransformName(dtm, &transform.implDesc.dpt.name));

    return DTM_registerDCustomTransform(dtm, &transform);
}

DECLARE_VECTOR_TYPE(ZL_RBuffer)

static ZL_Report splitTransformWrapper(
        ZL_Decoder* dictx,
        DTransform const* transform,
        ZL_Data const* ins[],
        size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    size_t const nbInputStreams = transform->miGraphDesc.nbSOs;
    ZL_ASSERT_EQ(nbIns, nbInputStreams);
    (void)nbIns;

    VECTOR(ZL_RBuffer)
    srcs = VECTOR_EMPTY(
            ZL_transformOutStreamsLimit(DI_getFrameFormatVersion(dictx)));
    ZL_ERR_IF_LT(
            VECTOR_RESERVE(srcs, nbInputStreams),
            nbInputStreams,
            allocation,
            "splitTransformWrapper: Failed reserving vector");
    for (size_t i = 0; i < nbInputStreams; ++i) {
        ZL_ASSERT_EQ(ZL_Data_type(ins[i]), ZL_Type_serial);
        ZL_RBuffer src = { .start = ZL_Data_rPtr(ins[i]),
                           .size  = ZL_Data_numElts(ins[i]) };
        // Should never fail as we reserved before
        (void)VECTOR_PUSHBACK(srcs, src);
    }

    // TODO: Accept vectors in dstBound and transforms functions
    size_t const dstCapacity =
            transform->implDesc.dst.dstBound_f(VECTOR_DATA(srcs));

    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, dstCapacity, 1);
    if (out == NULL) {
        VECTOR_DESTROY(srcs);
        ZL_ERR(allocation);
    }
    ZL_WBuffer dst = { .start = ZL_Output_ptr(out), .capacity = dstCapacity };
    size_t const dstSize =
            transform->implDesc.dst.transform_f(dst, VECTOR_DATA(srcs));

    VECTOR_DESTROY(srcs);

    ZL_ERR_IF_GT(
            dstSize,
            dstCapacity,
            transform_executionFailure,
            "transform %s failed",
            DT_getTransformName(transform));

    ZL_ERR_IF_ERR(ZL_Output_commit(out, dstSize));

    return ZL_returnValue(1);
}

ZL_RESULT_OF(ZL_IDType)
DTM_registerDSplitTransform(
        DTransforms_manager* dtm,
        const ZL_SplitDecoderDesc* dst)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_IDType, dtm->opCtx);

    if (DTransformMap_findVal(&dtm->dtmap, dst->CTid) != NULL) {
        // Early return; transform already registered; avoid allocating new
        // space for it.
        return ZL_RESULT_WRAP_VALUE(ZL_IDType, dst->CTid);
    }

    ZL_TRY_LET_CONST(
            ZL_Type_p,
            outStreamTypes,
            DTM_setOutStreamTypes(
                    dtm, ZL_Type_serial, (unsigned)dst->nbInputStreams));

    ZL_MIGraphDesc migd = {
        .CTid       = dst->CTid,
        .inputTypes = &kSerializedType,
        .nbInputs   = 1,
        .soTypes    = outStreamTypes,
        .nbSOs      = dst->nbInputStreams,
    };
    DTransform transform = {
        .miGraphDesc  = migd,
        .transformFn  = splitTransformWrapper,
        .implDesc.dst = *dst,
        .type         = dtr_split,
    };

    ZL_ERR_IF_ERR(DTM_storeTransformName(dtm, &transform.implDesc.dst.name));

    return DTM_registerDCustomTransform(dtm, &transform);
}

ZL_Report DT_typedTransformWrapper(
        ZL_Decoder* dictx,
        DTransform const* transform,
        ZL_Data const* ins[],
        size_t nbIns)
{
    // We should have already validated this
    ZL_ASSERT_EQ(nbIns, transform->miGraphDesc.nbSOs);
    (void)nbIns;
    return transform->implDesc.dtt.transform_f(
            dictx, ZL_codemodDatasAsInputs(ins));
}

ZL_RESULT_OF(ZL_IDType)
DTM_registerDTypedTransform(
        DTransforms_manager* dtm,
        const ZL_TypedDecoderDesc* dtt)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_IDType, dtm->opCtx);

    if (DTransformMap_findVal(&dtm->dtmap, dtt->gd.CTid) != NULL) {
        // Early return; transform already registered; avoid allocating new
        // space for it.
        ZL_OpaquePtr_free(dtt->opaque);
        return ZL_RESULT_WRAP_VALUE(ZL_IDType, dtt->gd.CTid);
    }
    ZL_ERR_IF_ERR(ZL_OpaquePtrRegistry_register(&dtm->opaquePtrs, dtt->opaque));

    ZL_MIGraphDesc migd = {
        .CTid = dtt->gd.CTid,
    };

    // Transfer input type
    ZL_TRY_SET(
            ZL_Type_p,
            migd.inputTypes,
            DTM_storeStreamTypes(dtm, ZL_STREAMTYPELIST(dtt->gd.inStreamType)));
    migd.nbInputs = 1;

    // Transfer output types
    ZL_TRY_SET(
            ZL_Type_p,
            migd.soTypes,
            DTM_storeStreamTypes(
                    dtm,
                    dtt->gd.outStreamTypes,
                    (unsigned)dtt->gd.nbOutStreams));
    migd.nbSOs = dtt->gd.nbOutStreams;

    DTransform transform = { .miGraphDesc  = migd,
                             .type         = dtr_typed,
                             .transformFn  = DT_typedTransformWrapper,
                             .implDesc.dtt = *dtt,
                             .opaque       = dtt->opaque.ptr };

    ZL_ERR_IF_ERR(DTM_storeTransformName(dtm, &transform.implDesc.dtt.name));

    return DTM_registerDCustomTransform(dtm, &transform);
}

ZL_Report DT_voTransformWrapper(
        ZL_Decoder* dictx,
        DTransform const* transform,
        ZL_Data const* ins[],
        size_t nbIns)
{
    size_t const nbO1s = transform->miGraphDesc.nbSOs;
    ZL_ASSERT(nbIns >= nbO1s);
    return transform->implDesc.dvo.transform_f(
            dictx,
            ZL_codemodDatasAsInputs(ins),
            nbO1s,
            nbO1s ? ZL_codemodDatasAsInputs(ins) + nbO1s
                  : ZL_codemodDatasAsInputs(ins),
            nbIns - nbO1s);
}

ZL_RESULT_OF(ZL_IDType)
DTM_registerDVOTransform(
        DTransforms_manager* dtm,
        const ZL_VODecoderDesc* dvotd)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_IDType, dtm->opCtx);

    if (DTransformMap_findVal(&dtm->dtmap, dvotd->gd.CTid) != NULL) {
        // Early return; transform already registered; avoid allocating new
        // space for it.
        ZL_OpaquePtr_free(dvotd->opaque);
        return ZL_RESULT_WRAP_VALUE(ZL_IDType, dvotd->gd.CTid);
    }
    ZL_ERR_IF_ERR(
            ZL_OpaquePtrRegistry_register(&dtm->opaquePtrs, dvotd->opaque));

    ZL_MIGraphDesc dmitd = {
        .CTid = dvotd->gd.CTid,
    };

    // Transfer input type
    ZL_TRY_SET(
            ZL_Type_p,
            dmitd.inputTypes,
            DTM_storeStreamTypes(
                    dtm, ZL_STREAMTYPELIST(dvotd->gd.inStreamType)));
    dmitd.nbInputs = 1;

    // Transfer singleton output types
    ZL_TRY_SET(
            ZL_Type_p,
            dmitd.soTypes,
            DTM_storeStreamTypes(
                    dtm,
                    dvotd->gd.singletonTypes,
                    (unsigned)dvotd->gd.nbSingletons));
    dmitd.nbSOs = dvotd->gd.nbSingletons;

    // Transfer variable output types
    ZL_TRY_SET(
            ZL_Type_p,
            dmitd.voTypes,
            DTM_storeStreamTypes(
                    dtm, dvotd->gd.voTypes, (unsigned)dvotd->gd.nbVOs));
    dmitd.nbVOs = dvotd->gd.nbVOs;

    DTransform transform = { .miGraphDesc  = dmitd,
                             .type         = dtr_vo,
                             .transformFn  = DT_voTransformWrapper,
                             .implDesc.dvo = *dvotd,
                             .opaque       = dvotd->opaque.ptr };

    ZL_ERR_IF_ERR(DTM_storeTransformName(dtm, &transform.implDesc.dvo.name));

    return DTM_registerDCustomTransform(dtm, &transform);
}

ZL_Report DT_miTransformWrapper(
        ZL_Decoder* dictx,
        DTransform const* transform,
        ZL_Data const* ins[],
        size_t nbIns)
{
    size_t const nbO1s = transform->miGraphDesc.nbSOs;
    ZL_ASSERT(nbIns >= nbO1s);
    ZL_Input const** inputs = ZL_codemodDatasAsInputs(ins);
    return transform->implDesc.dmi.transform_f(
            dictx,
            inputs,
            nbO1s,
            nbO1s == 0 ? inputs : inputs + nbO1s,
            nbIns - nbO1s);
}

ZL_RESULT_OF(ZL_IDType)
DTM_registerDMITransform(
        DTransforms_manager* dtm,
        const ZL_MIDecoderDesc* dmitd)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_IDType, dtm->opCtx);

    ZL_DLOG(BLOCK,
            "DTM_registerDMITransform ('%s')",
            STR_REPLACE_NULL(dmitd->name));
    if (DTransformMap_findVal(&dtm->dtmap, dmitd->gd.CTid) != NULL) {
        // Early return; transform already registered; avoid allocating new
        // space for it.
        ZL_OpaquePtr_free(dmitd->opaque);
        return ZL_RESULT_WRAP_VALUE(ZL_IDType, dmitd->gd.CTid);
    }
    ZL_ERR_IF_ERR(
            ZL_OpaquePtrRegistry_register(&dtm->opaquePtrs, dmitd->opaque));

    ZL_MIGraphDesc dmitgd = dmitd->gd;

    // Check inputs
    ZL_ERR_IF_LT(
            dmitgd.nbInputs,
            1,
            invalidTransform,
            "Decoder Transform '%s' must declare at least one regenerated stream",
            STR_REPLACE_NULL(dmitd->name));

    // Transfer input types
    ZL_TRY_SET(
            ZL_Type_p,
            dmitgd.inputTypes,
            DTM_storeStreamTypes(
                    dtm, dmitd->gd.inputTypes, (unsigned)dmitd->gd.nbInputs));

    // Transfer singleton output types
    ZL_TRY_SET(
            ZL_Type_p,
            dmitgd.soTypes,
            DTM_storeStreamTypes(
                    dtm, dmitd->gd.soTypes, (unsigned)dmitd->gd.nbSOs));

    // Transfer variable output types
    ZL_TRY_SET(
            ZL_Type_p,
            dmitgd.voTypes,
            DTM_storeStreamTypes(
                    dtm, dmitd->gd.voTypes, (unsigned)dmitd->gd.nbVOs));

    DTransform transform = { .miGraphDesc  = dmitgd,
                             .type         = dtr_mi,
                             .transformFn  = DT_miTransformWrapper,
                             .implDesc.dmi = *dmitd,
                             .opaque       = dmitd->opaque.ptr };

    ZL_ERR_IF_ERR(DTM_storeTransformName(dtm, &transform.implDesc.dmi.name));

    return DTM_registerDCustomTransform(dtm, &transform);
}

static ZL_RESULT_OF(DTrPtr) DTM_getStandardTransform(
        ZL_OperationContext* opCtx,
        ZL_IDType transformID,
        unsigned formatVersion)
{
    ZL_RESULT_DECLARE_SCOPE(DTrPtr, opCtx);

    ZL_ERR_IF_GE(
            transformID,
            ZL_StandardTransformID_end,
            logicError,
            "standard transform ID supposed to be pre-validated");
    StandardDTransform const* dtr = &SDecoders_array[transformID];
    if (formatVersion < dtr->minFormatVersion
        || formatVersion > dtr->maxFormatVersion) {
        ZL_ERR(formatVersion_unsupported,
               "Transform %u is not supported in formatVersion %u - it is supported in versions [%u, %u]",
               transformID,
               formatVersion,
               dtr->minFormatVersion,
               dtr->maxFormatVersion);
    }
    switch (dtr->dtr.type) {
        case dtr_typed:
        case dtr_vo:
        case dtr_mi:
            return ZL_RESULT_WRAP_VALUE(DTrPtr, &dtr->dtr);
        case dtr_pipe:
        case dtr_split:
        default:
            ZL_ASSERT_FAIL("unsupported standard decoder type");
            ZL_ERR(logicError, "unsupported standard decoder type");
    }
}

ZL_RESULT_OF(DTrPtr)
DTM_getTransform(
        const DTransforms_manager* dtm,
        PublicTransformInfo trid,
        unsigned formatVersion)
{
    ZL_RESULT_DECLARE_SCOPE(DTrPtr, dtm->opCtx);

    ZL_IDType const id = trid.trid;
    if (trid.trt == trt_standard)
        return DTM_getStandardTransform(dtm->opCtx, id, formatVersion);

    // Now, this is a custom transform
    // TODO(terrelln): formatVersion isn't used for custom transforms.
    (void)formatVersion;
    ZL_ASSERT_EQ(trid.trt, trt_custom);
    ZL_ASSERT_NN(dtm);

    const DTransformMap_Entry* entry = DTransformMap_findVal(&dtm->dtmap, id);
    ZL_ERR_IF_NULL(
            entry,
            graph_invalid,
            "Custom decoder transform %u not found!",
            (unsigned)id);
    return ZL_RESULT_WRAP_VALUE(DTrPtr, &entry->val);
}

const char* DTM_getTransformName(
        const DTransforms_manager* dtm,
        PublicTransformInfo trinfo,
        unsigned formatVersion)
{
    ZL_DLOG(SEQ,
            "DTM_getTransformName (trid=[%s:%u])",
            trinfo.trt ? "custom" : "standard",
            trinfo.trid);
    ZL_RESULT_OF(DTrPtr)
    rdt = DTM_getTransform(dtm, trinfo, formatVersion);
    if (ZL_RES_isError(rdt))
        return NULL;
    DTrPtr const dt = ZL_RES_value(rdt);

    return DT_getTransformName(dt);
}

void** DTM_getStatePtr(DTransforms_manager* dtm, PublicTransformInfo trid)
{
    ZL_ASSERT_NN(dtm);
    ZL_IDType const id = trid.trid;
    if (trid.trt == trt_standard) {
        ZL_ASSERT_LT(id, ZL_StandardTransformID_end);
        return &dtm->states[id];
    }
    ZL_ASSERT_EQ(trid.trt, trt_custom);
    DTransformMap_Entry* entry = DTransformMap_findMutVal(&dtm->dtmap, id);
    if (entry == NULL) {
        return NULL;
    }
    return &entry->val.state;
}
