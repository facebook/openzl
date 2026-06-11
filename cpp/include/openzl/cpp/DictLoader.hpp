// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/cpp/detail/NonNullUniqueCPtr.hpp"
#include "openzl/cpp/poly/StringView.hpp"
#include "openzl/zl_dict.h"
#include "openzl/zl_dictloader.h"
#include "openzl/zl_materializer.h"
#include "openzl/zl_opaque_types.h"

namespace openzl {

/**
 * Abstract base class for C++ dict loaders.
 *
 * Subclass this and override fetchDictBundle() to implement custom
 * dict loading logic. The base class creates an internal ZL_DictLoader
 * with a C trampoline that dispatches to the virtual method.
 *
 * Standard materializers for built-in codecs (e.g. zstd) are
 * registered automatically during construction.
 */
class DictLoader {
   public:
    DictLoader();
    virtual ~DictLoader();

    DictLoader(const DictLoader&)             = delete;
    DictLoader& operator=(const DictLoader&)  = delete;
    DictLoader(DictLoader&& other)            = delete;
    DictLoader& operator=(DictLoader&& other) = delete;

    /**
     * Fetch a dict bundle by its ID. Subclasses must implement this.
     * @returns pointer to the bundle, or nullptr if not found.
     */
    virtual const ZL_DictBundle* fetchDictBundle(const ZL_BundleID& id) = 0;

    /// @returns the underlying ZL_DictLoader* for use with
    /// ZL_DCtx_refDictLoader.
    ZL_DictLoader* get()
    {
        return loader_.get();
    }

    /**
     * Register a dict materializer for a custom codec. Wraps
     * ZL_DictLoader_registerMaterializer().
     * @throws Exception on failure (e.g. double-registration).
     */
    void registerMaterializer(
            ZL_IDType codecID,
            const ZL_MaterializerDesc2* materializer);

    /**
     * Materialize a raw dict blob using the materializer registered for
     * @p codecID. Wraps ZL_DictLoader_materialize().
     * WARNING: Not thread-safe.
     * @returns The materialized object pointer.
     * @throws Exception on failure.
     */
    void* materialize(
            ZL_IDType codecID,
            bool isCustomCodec,
            const void* src,
            size_t srcSize);

    /**
     * Dematerialize (free) a previously materialized object. Wraps
     * ZL_DictLoader_dematerialize().
     * WARNING: Not thread-safe.
     */
    void
    dematerialize(ZL_IDType codecID, bool isCustomCodec, void* materialized);

   protected:
    /**
     * Construct a DictLoader that wraps an externally-owned ZL_DictLoader.
     * The @p deleter controls ownership: pass nullptr for non-owning refs,
     * or the appropriate free function for owning refs.
     *
     * NOTE: Only used as a convenience wrapper for FatBundleDictLoader.
     * Subclass implementors need not worry about this.
     */
    DictLoader(
            ZL_DictLoader* loader,
            detail::NonNullUniqueCPtr<ZL_DictLoader>::DeleterFn deleter)
            : loader_(loader, deleter)
    {
    }

   private:
    detail::NonNullUniqueCPtr<ZL_DictLoader> loader_;
};

} // namespace openzl
