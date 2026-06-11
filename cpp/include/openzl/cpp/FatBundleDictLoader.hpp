// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/cpp/DictLoader.hpp"
#include "openzl/cpp/detail/NonNullUniqueCPtr.hpp"
#include "openzl/cpp/poly/StringView.hpp"
#include "openzl/zl_dictloader.h"
#include "openzl/zl_opaque_types.h"

namespace openzl {

/**
 * RAII wrapper for ZL_FatBundleDictLoader.
 *
 * Convenience dict loader that serves dicts from a serialized "fat bundle"
 * blob produced by the training pipeline. Standard materializers for
 * built-in codecs (e.g. zstd) are registered automatically.
 *
 * Usage:
 *   FatBundleDictLoader loader;
 *   loader.loadFatBundle(fatBundleBytes);
 *   dctx.refDictLoader(loader);
 */
class FatBundleDictLoader : public DictLoader {
   public:
    FatBundleDictLoader();

    FatBundleDictLoader(const FatBundleDictLoader&)                = delete;
    FatBundleDictLoader& operator=(const FatBundleDictLoader&)     = delete;
    FatBundleDictLoader(FatBundleDictLoader&&) noexcept            = delete;
    FatBundleDictLoader& operator=(FatBundleDictLoader&&) noexcept = delete;

    ~FatBundleDictLoader() override = default;

    /**
     * Load and parse a fat bundle. Each dict in the bundle whose codec has
     * a registered materializer will be materialized. The @p fatBundle
     * buffer does not need to remain valid after this call returns.
     * May be called multiple times to add multiple bundles.
     * @throws Exception on parse or materialization failure.
     */
    void loadFatBundle(poly::string_view fatBundle);

    const ZL_DictBundle* fetchDictBundle(const ZL_BundleID& id) override;

    ZL_FatBundleDictLoader* get()
    {
        return fbdl_.get();
    }
    const ZL_FatBundleDictLoader* get() const
    {
        return fbdl_.get();
    }

   private:
    // Delegated constructor trickery required to extract the base
    // ZL_DictLoader* to properly construct the base class.
    explicit FatBundleDictLoader(ZL_FatBundleDictLoader* fbdl);

    detail::NonNullUniqueCPtr<ZL_FatBundleDictLoader> fbdl_;
};

} // namespace openzl
