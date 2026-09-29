// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/FatBundleDictLoader.hpp"

#include "openzl/cpp/Exception.hpp"
#include "openzl/zl_errors.h"

namespace openzl {

FatBundleDictLoader::FatBundleDictLoader()
        : FatBundleDictLoader(ZL_FatBundleDictLoader_create())
{
}

FatBundleDictLoader::FatBundleDictLoader(ZL_FatBundleDictLoader* fbdl)
        : DictLoader(
                  [&] {
                      if (fbdl == nullptr) {
                          throw Exception(
                                  "Failed to create ZL_FatBundleDictLoader");
                      }
                      return ZL_FatBundleDictLoader_getDictLoader(fbdl);
                  }(),
                  nullptr),
          fbdl_(fbdl, ZL_FatBundleDictLoader_free)
{
}

void FatBundleDictLoader::loadFatBundle(poly::string_view fatBundle)
{
    auto report = ZL_FatBundleDictLoader_loadFatBundle(
            fbdl_.get(), fatBundle.data(), fatBundle.size());
    if (ZL_isError(report)) {
        throw Exception("Failed to load fat dict bundle");
    }
}

const ZL_DictBundle* FatBundleDictLoader::fetchDictBundle(const ZL_BundleID& id)
{
    return unwrap(ZL_DictLoader_fetchDictBundle(
            ZL_FatBundleDictLoader_getDictLoader(get()), &id));
}

} // namespace openzl
