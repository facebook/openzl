// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TESTS_DICT_H
#define ZSTRONG_TESTS_DICT_H

#include <stdint.h>
#include <map>

#include "openzl/common/allocation.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

typedef struct {
    uint8_t bytes[32];
} ZL_SHA256;
ZL_RESULT_DECLARE_TYPE(ZL_SHA256);

typedef ZL_SHA256 ZL_DictID;
// ZL_Dict
typedef struct {
    ZL_DictID dictID; // this is a SHA of the serialized ZL_Dict
    ZL_IDType materializingCodec;
    void (*dematerializeFn)(void* dict);
    void* dictObj;
} ZL_Dict;

// the dict loader needs to support search using both a
// 32-bit ID and a SHA.
typedef struct {
    uint32_t id;
    ZL_SHA256 sha; // this is a SHA of the serialized ZL_DictBundle
} ZL_DictBundleID;

// ZL_DictBundle
typedef struct {
    ZL_DictBundleID bundleID;
    bool compressionOnly;
    size_t numDicts;
    ZL_SHA256* dicts;
} ZL_DictBundle;

// ======================================================
// ==================== ZL_DictBundle ===================
// ======================================================

size_t ZL_DictBundle_numDicts(const ZL_DictBundle* bundle);
const ZL_DictID* ZL_DictBundle_dictIDs(const ZL_DictBundle* bundle);

// ======================================================
// =================== ZL_DictLoader ====================
// ======================================================

// For clarity of illustration, this sample uses C++ class semantics. In prod,
// the C API will be similar but semantically more complex.
class ZL_DictLoader {
   public:
    // Use ZL_DictBundle_* helper functions
    virtual ZL_RESULT_OF(ZL_DictBundle*)
            fetchDictBundle(const ZL_DictBundleID* id) = 0;

    // A Compressor or DCtx is required to materialize properly
    virtual ZL_RESULT_OF(ZL_Dict*) fetchDict(
            const ZL_DictID* id,
            const ZL_Compressor* materializingCompressor) = 0;
    virtual ZL_RESULT_OF(ZL_Dict*) fetchDict(
            const ZL_DictID* id,
            const ZL_DCtx* materializingDCtx) = 0;
};

class ZL_StaticSetDictLoader : public ZL_DictLoader {
   public:
    ZL_StaticSetDictLoader(
            std::string bundleBlob,
            std::vector<std::string> dictBlobs)
            : alloc_(ALLOC_Arena_HeapArena_create()),
              bundle_(bundleBlob),
              dictBlobs_(dictBlobs)
    {
    }

    ZL_Dict* fetchDict(
            const ZL_DictID* id,
            const ZL_Compressor* materializingCompressor) override
    {
        size_t idx = dictHashes_.find(id) - dictHashes_.begin();
        auto blob  = dictBlobs_[idx];
        // preflight checks here
        auto codecID;
        auto payload;
        auto res = ZL_Compressor_materializeDict(
                materializingCompressor,
                codecID,
                payload.data(),
                payload.size(),
                this->alloc_);
        ZL_ERR_IF_ERR(res);
        return ZL_WRAP_VALUE(ZL_RES_value(res).ptr);
    }
    // other inherited functions not shown

   private:
    ZL_DictBundle bundle_;
    std::vector<std::string> dictBlobs_;
    std::vector<ZL_DictID> dictHashes_;

    std::map<std::pair<ZL_DictID, void* /* materializing fn ptr*/>, void*>
            dicts_;
};

class MCDictLoader : public ZL_DictLoader {
   public:
    MCDictLoader(ZL_DictAlloc* alloc);
    bool hasBundle(ZL_DictBundleID bundleID);

    // Implementors of ZL_DictLoader are encouraged to cache dicts, but it's not
    // required. For Managed Compression, this will be a requirement.
    ZL_Report prefetchDictsForBundle(
            const ZL_DictBundleID* id,
            ZL_Compressor* materializingCompressor);
    ZL_Report prefetchDictsForBundle(
            const ZL_DictBundleID* id,
            ZL_DCtx* materializingDCtx);
}

struct ZL_MaterializationResult {
    void* ptr;
    void* materializeFnPtr;
    void* dematerializeFnPtr;
};

// ======================================================
// ================== Compression APIs ==================
// ======================================================

ZL_RESULT_OF(ZL_MaterializationResult)
ZL_Compressor_materializeDict(
        ZL_Compressor* compressor,
        ZL_IDtype codecID,
        void* blob,
        size_t blobSize,
        ZL_DictAlloc* allocator);

// Attach in the same way we do ZL_CCtx_refCompressor()
ZL_Report ZL_CCtx_refDictLoader(ZL_CCtx* cctx, ZL_DictLoader* loader);

// If there is a dict bundle already set (e.g. via training), return its ID.
ZL_DictBundleID ZL_Compressor_getDictBundleID(ZL_Compressor* compressor);

void ZL_Compressor_useDictBundle(
        ZL_Compressor* compressor,
        ZL_DictBundleID bundleSha);

// ======================================================
// ================= Decompression APIs =================
// ======================================================

ZL_RESULT_OF(ZL_MaterializationResult)
ZL_DCtx_materializeDict(
        ZL_DCtx* dctx,
        ZL_IDtype codecID,
        void* blob,
        size_t blobSize,
        ZL_DictAlloc* allocator);

// Attach in the same way we do ZL_CCtx_refCompressor()
ZL_Report ZL_DCtx_refDictLoader(ZL_DCtx* dctx, ZL_DictLoader* loader);

// ======================================================
// ===================== Codec APIs =====================
// ======================================================

// If there is an associated dict, we can fetch it at encode/decode time. It
// will be the job of the CCtx/DCtx to properly request materialization and pass
// these blobs down
void* ZL_Encoder_getDict(ZL_Encoder* eictx);
void* ZL_Decoder_getDict(ZL_Decoder* dictx);

#endif // ZSTRONG_TESTS_DICT_H
