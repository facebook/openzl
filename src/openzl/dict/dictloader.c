// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/dict/dictloader.h"

#include <stdlib.h>

#include "openzl/codecs/zstd/common_zstd.h"
#include "openzl/common/allocation.h"
#include "openzl/common/assertion.h"
#include "openzl/common/materializer_ctx.h"
#include "openzl/dict/dict_constants.h"
#include "openzl/zl_errors.h"

static void DictLoader_freeOpaquePtr(const ZL_OpaquePtr* opaque)
{
    if (opaque->freeFn != NULL) {
        opaque->freeFn(opaque->freeOpaquePtr, opaque->ptr);
    }
}

size_t DL_MatMap_hash(const PublicTransformInfo* key)
{
    size_t h = (size_t)key->trt;
    h        = h * 31 + (size_t)key->trid;

    return h;
}

bool DL_MatMap_eq(
        const PublicTransformInfo* lhs,
        const PublicTransformInfo* rhs)
{
    return lhs->trt == rhs->trt && lhs->trid == rhs->trid;
}

ZL_DictLoader* ZL_DictLoader_create(const ZL_DictLoaderDesc* desc)
{
    if (desc == NULL || desc->fetchDictBundle == NULL) {
        return NULL;
    }

    ZL_DictLoader* loader = (ZL_DictLoader*)ZL_calloc(sizeof(ZL_DictLoader));
    if (loader == NULL) {
        return NULL;
    }

    loader->desc          = *desc;
    loader->materializers = DL_MatMap_create(ZL_MAX_DICTS_PER_BUNDLE);

    loader->persistentArena = ALLOC_HeapArena_create();
    if (loader->persistentArena == NULL) {
        ZL_DictLoader_free(loader);
        return NULL;
    }
    loader->scratchArena = ALLOC_StackArena_create();
    if (loader->scratchArena == NULL) {
        ZL_DictLoader_free(loader);
        return NULL;
    }

    if (ZL_isError(DictLoader_registerStandardMaterializers(loader))) {
        ZL_DictLoader_free(loader);
        return NULL;
    }

    return loader;
}

void ZL_DictLoader_free(ZL_DictLoader* loader)
{
    if (loader == NULL) {
        return;
    }

    if (loader->desc.opaque.freeFn != NULL) {
        loader->desc.opaque.freeFn(
                loader->desc.opaque.freeOpaquePtr, loader->desc.opaque.ptr);
    }

    DL_MatMap_Iter iter = DL_MatMap_iter(&loader->materializers);
    const DL_MatMap_Entry* entry;
    while ((entry = DL_MatMap_Iter_next(&iter)) != NULL) {
        DictLoader_freeOpaquePtr(&entry->val.opaque);
    }

    DL_MatMap_destroy(&loader->materializers);

    if (loader->scratchArena != NULL) {
        ALLOC_Arena_freeArena(loader->scratchArena);
    }
    if (loader->persistentArena != NULL) {
        ALLOC_Arena_freeArena(loader->persistentArena);
    }

    ZL_free(loader);
}

void* ZL_DictLoader_getOpaque(const ZL_DictLoader* loader)
{
    ZL_ASSERT_NN(loader);

    return loader->desc.opaque.ptr;
}

static ZL_Report DictLoader_registerMaterializer_inner(
        ZL_DictLoader* loader,
        ZL_IDType codecID,
        const ZL_MaterializerDesc2* materializer)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_NULL(loader, GENERIC);
    ZL_ERR_IF_NULL(materializer, GENERIC);
    ZL_ERR_IF_NULL(
            materializer->materializeFn,
            GENERIC,
            "materializer must have a materializeFn");
    ZL_ERR_IF_NULL(
            materializer->dematerializeFn,
            GENERIC,
            "materializer must have a dematerializeFn. If no dematerialization is needed, use ZL_NOOP_DEMATERIALIZE");

    PublicTransformInfo const key = { .trt = trt_custom, .trid = codecID };

    if (DL_MatMap_contains(&loader->materializers, &key)) {
        ZL_ERR(GENERIC,
               "materializer already registered for (custom) codec %u",
               codecID);
    }

    DL_MatMap_Entry entry = { .key = key, .val = *materializer };
    DL_MatMap_Insert ins  = DL_MatMap_insert(&loader->materializers, &entry);
    ZL_ERR_IF(ins.badAlloc, allocation);

    return ZL_returnSuccess();
}

ZL_Report ZL_DictLoader_registerMaterializer(
        ZL_DictLoader* loader,
        ZL_IDType codecID,
        const ZL_MaterializerDesc2* materializer)
{
    ZL_Report report = DictLoader_registerMaterializer_inner(
            loader, codecID, materializer);
    if (ZL_isError(report) && materializer != NULL) {
        DictLoader_freeOpaquePtr(&materializer->opaque);
    }
    return report;
}

const ZL_MaterializerDesc2* DictLoader_getMaterializer(
        const ZL_DictLoader* loader,
        ZL_IDType codecID,
        bool isCustomCodec)
{
    ZL_ASSERT_NN(loader);

    PublicTransformInfo const key = { .trt  = isCustomCodec ? trt_custom
                                                            : trt_standard,
                                      .trid = codecID };
    const DL_MatMap_Entry* entry = DL_MatMap_find(&loader->materializers, &key);
    if (entry == NULL) {
        return NULL;
    }

    return &entry->val;
}

static ZL_Report DictLoader_registerStandardMaterializer(
        ZL_DictLoader* loader,
        ZL_NodeID codecID,
        const ZL_MaterializerDesc2* mat)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT_NN(loader);
    ZL_ERR_IF_NULL(mat, GENERIC);

    ZL_ERR_IF_NULL(
            mat->materializeFn,
            logicError,
            "materializer must have a materializeFn");
    ZL_ERR_IF_NULL(
            mat->dematerializeFn,
            logicError,
            "materializer must have a dematerializeFn");
    ZL_ERR_IF_NN(
            mat->opaque.ptr,
            logicError,
            "standard materializer must not have an opaque ptr");

    PublicTransformInfo const key = { .trt  = trt_standard,
                                      .trid = codecID.nid };
    DL_MatMap_Entry entry         = { .key = key, .val = *mat };
    DL_MatMap_Insert ins = DL_MatMap_insert(&loader->materializers, &entry);
    ZL_ERR_IF(ins.badAlloc, allocation);

    return ZL_returnSuccess();
}

const ZL_DictBundle* DictLoader_getDictBundle(
        ZL_DictLoader* loader,
        const ZL_BundleID* id)
{
    ZL_ASSERT_NN(loader);
    ZL_ASSERT_NN(id);

    ZL_RESULT_OF(ZL_DictBundleConstPtr)
    res = loader->desc.fetchDictBundle(loader, id);
    if (ZL_RES_isError(res)) {
        return NULL;
    }
    return ZL_RES_value(res);
}

ZL_RESULT_OF(ZL_VoidPtr)
ZL_DictLoader_materialize(
        ZL_DictLoader* loader,
        ZL_IDType codecID,
        bool isCustomCodec,
        const void* src,
        size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, NULL);
    ZL_ERR_IF_NULL(loader, GENERIC);
    ZL_ERR_IF_NULL(src, GENERIC);

    const ZL_MaterializerDesc2* matDesc =
            DictLoader_getMaterializer(loader, codecID, isCustomCodec);
    ZL_ERR_IF_NULL(
            matDesc,
            dict_materialization,
            "no materializer registered for codec %u",
            codecID);

    ZL_ASSERT_EQ(ALLOC_Arena_memUsed(loader->scratchArena), 0);

    ZL_Materializer matCtx = {
        .persistentArena = loader->persistentArena,
        .scratchArena    = loader->scratchArena,
        .opaquePtr       = matDesc->opaque.ptr,
        .opCtx           = NULL,
    };
    ZL_RESULT_OF(ZL_VoidPtr)
    result = matDesc->materializeFn(&matCtx, src, srcSize);
    ALLOC_Arena_freeAll(loader->scratchArena);

    return result;
}

void ZL_DictLoader_dematerialize(
        ZL_DictLoader* loader,
        ZL_IDType codecID,
        bool isCustomCodec,
        void* materialized)
{
    if (loader == NULL || materialized == NULL) {
        return;
    }

    const ZL_MaterializerDesc2* matDesc =
            DictLoader_getMaterializer(loader, codecID, isCustomCodec);
    if (matDesc == NULL) {
        return;
    }

    ZL_Materializer matCtx = {
        .persistentArena = NULL,
        .scratchArena    = NULL,
        .opaquePtr       = matDesc->opaque.ptr,
        .opCtx           = NULL,
    };
    matDesc->dematerializeFn(&matCtx, materialized);
}

ZL_Report DictLoader_registerStandardMaterializers(ZL_DictLoader* loader)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_NULL(loader, GENERIC);

    ZL_ERR_IF_ERR(DictLoader_registerStandardMaterializer(
            loader,
            (ZL_NodeID){ ZL_StandardTransformID_zstd },
            &ZL_Zstd_ddict_materializer));

    return ZL_returnSuccess();
}
