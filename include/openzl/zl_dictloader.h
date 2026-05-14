// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_ZL_DICTLOADER_H
#define OPENZL_ZL_DICTLOADER_H

#include <stddef.h> // size_t

#include "openzl/zl_dict.h" // ZL_BundleInfo, ZL_DictBundle, ZL_DictBundleConstPtr
#include "openzl/zl_errors.h"       // ZL_RESULT_OF
#include "openzl/zl_materializer.h" // ZL_MaterializerDesc
#include "openzl/zl_opaque_types.h" // ZL_Dict, ZL_FatBundleDictLoader

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * =============================== ZL_DictLoader ===============================
 *
 * The ZL_DictLoader is responsible for fetching and (de)materializing dicts and
 * bundles. It is used by the ZL_DCtx to provide dictionary functionality. Refer
 * to ZL_DCtx_refDictLoader() for usage.
 *
 * The fetching APIs are provided unimplemented to enable you to customize blob
 * sources and caching behavior. (A simple reference implementation is also
 * provided in ZL_FatBundleDictLoader. This is sufficient for use with the
 * trainer.)
 *
 * WARNING: The dict loader function implementations are required to be
 * thread-safe. It is expected that the same dict loader can be used by multiple
 * threads.
 *
 * Before usage, custom codecs must have their de/materializers registered with
 * the ZL_DictLoader.
 *
 * ** NOTE: The ZL_DictLoader is not used at compression time. The ZL_Compressor
 * assumes the responsibility of dict loading. Register compression-time
 * materialization instructions with the ZL_MIEncoderDesc and provide the raw
 * blobs to the ZL_Compressor using ZL_Compressor_useDict().
 */

typedef struct {
    /**
     * Fetch a dict bundle by its ID. Implementors are required to keep
     * materialized bundles and dicts alive for the lifetime of the
     * ZL_DictLoaderDesc. This should be accomplished using custom state defined
     * by @p opaque .
     */
    ZL_RESULT_OF(ZL_DictBundleConstPtr) (
            *fetchDictBundle)(ZL_DictLoader* loader, const ZL_BundleID* id);

    /**
     * The ZL_DictLoader will take ownership of this pointer. Ensure the
     * freeFn properly dematerializes any materialized dicts.
     */
    ZL_OpaquePtr opaque;
} ZL_DictLoaderDesc;

ZL_DictLoader* ZL_DictLoader_create(const ZL_DictLoaderDesc* desc);
void ZL_DictLoader_free(ZL_DictLoader* loader);

/**
 * Fetch the opaque pointer provided to ZL_DictLoader_create() via the
 * @p ZL_DictLoaderDesc
 */
void* ZL_DictLoader_getOpaque(const ZL_DictLoader* loader);

/**
 * Registers a dict materialization function for a particular custom codec. By
 * "materialization", we mean a function to turn a serialized string into an
 * in-memory object. By "dematerialization" we mean a function to free the
 * memory associated with a materialized object. Materialization is similar to
 * deserialization, but dematerialization does not produce any artifacts, unlike
 * serialization.
 *
 * Codecs that support dictionaries may fetch the materialized object (if any)
 * at both compression time and decompression time using ZL_Encoder_getDict()
 * and ZL_Decoder_getDict() respectively.
 *
 * The loader takes unconditional ownership of the materializer opaque pointer
 * and will free it upon destruction or failed registrations.
 *
 * WARNING: This function is not thread-safe!
 *
 * @returns an error if registration fails, for instance if the materializer is
 * invalid or an attempt is made to double-register a materializer.
 */
ZL_Report ZL_DictLoader_registerMaterializer(
        ZL_DictLoader* loader,
        ZL_IDType codecID,
        const ZL_MaterializerDesc2* materializer);

/**
 * Materializes a raw dict blob using the materializer registered for
 * @p codecID. The loader manages the ZL_Materializer lifecycle internally.
 * Persistent allocations made by the materializer are owned by the loader.
 *
 * WARNING: This function is not thread-safe!
 *
 * @param loader The dict loader.
 * @param codecID Codec to look up.
 * @param isCustomCodec true if @p codecID refers to a custom codec.
 * @param src Pointer to the raw dict blob.
 * @param srcSize Size of the raw dict blob in bytes.
 * @returns The materialized object pointer, or an error.
 */
ZL_RESULT_OF(ZL_VoidPtr)
ZL_DictLoader_materialize(
        ZL_DictLoader* loader,
        ZL_IDType codecID,
        bool isCustomCodec,
        const void* src,
        size_t srcSize);

/**
 * Dematerializes (frees non-arena resources of) a previously materialized
 * object. If the materializer is not found or @p materialized is NULL, this
 * is a no-op.
 *
 * WARNING: This function is not thread-safe!
 *
 * @param loader The dict loader.
 * @param codecID Codec to look up.
 * @param isCustomCodec true if @p codecID refers to a custom codec.
 * @param materialized The object to dematerialize.
 */
void ZL_DictLoader_dematerialize(
        ZL_DictLoader* loader,
        ZL_IDType codecID,
        bool isCustomCodec,
        void* materialized);

/**
 * ========================== Fat Bundle ZL_DictLoader =========================
 *
 * This is a convenience implementation of the ZL_DictLoaderDesc interface that
 * uses a provided serialized "fat" bundle to power the fetchDictBundle()
 * function. The proper materializers must be pre-registered on the
 * @p loader before calling this function.
 *
 * The training script generates an all-in-one "fat" bundle compatible with
 * ZL_FatBundleDictLoader_loadFatBundle() if provided the option --fat-bundle.
 * This function may be called multiple times to add multiple bundles and their
 * dicts to the loader.
 * Usage:
 *   1. Create with ZL_FatBundleDictLoader_create().
 *   2. Register a materializer for each codec that uses dicts via
 *      ZL_FatBundleDictLoader_registerMaterializer().
 *   3. Load a fat bundle via ZL_FatBundleDictLoader_loadFatBundle().
 *   4. Attach to a DCtx via ZL_DCtx_refFatBundleDictLoader().
 *   5. Free with ZL_FatBundleDictLoader_free() after decompression.
 */

/**
 * @brief Creates a new fat bundle dict loader.
 *
 * @return Pointer to new loader, or NULL on allocation failure.
 */
ZL_FatBundleDictLoader* ZL_FatBundleDictLoader_create(void);

/**
 * @brief Frees a fat bundle dict loader and all materialized dicts it owns.
 *
 * @param loader Loader to free (NULL-safe).
 */
void ZL_FatBundleDictLoader_free(ZL_FatBundleDictLoader* loader);

/**
 * @brief Loads and parses a fat bundle generated by
 * ZL_DictBundle_packFatBundle().
 *
 * Each dict in the bundle whose codec type ID has a registered materializer
 * will be materialized. The fat bundle buffer does not need to remain valid
 * after this call returns. Multiple fat bundles may be loaded into the same
 * loader. Dicts will be auto-deduped.
 *
 * @param loader The dict loader.
 * @param fatBundle Pointer to the serialized fat bundle.
 * @param fatBundleSize Size of the fat bundle in bytes.
 * @return Error code or success.
 */
ZL_Report ZL_FatBundleDictLoader_loadFatBundle(
        ZL_FatBundleDictLoader* loader,
        const void* fatBundle,
        size_t fatBundleSize);

/**
 * @brief Returns the internal ZL_DictLoader owned by this fat bundle loader.
 *
 * The returned pointer is valid for the lifetime of the fat bundle loader.
 */
ZL_DictLoader* ZL_FatBundleDictLoader_getDictLoader(
        ZL_FatBundleDictLoader* loader);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_ZL_DICTLOADER_H
