// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/DictLoader.hpp"

#include "openzl/cpp/Exception.hpp"
#include "openzl/dict/dictloader.h"
#include "openzl/zl_errors.h"

namespace openzl {

namespace {

ZL_RESULT_OF(ZL_DictBundleConstPtr)
fetchDictBundleTrampoline(ZL_DictLoader* loader, const ZL_BundleID* id)
        ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE(ZL_DictBundleConstPtr, (ZL_DCtx*)nullptr);
    ZL_ERR_IF_NULL(loader, GENERIC);
    ZL_ERR_IF_NULL(id, GENERIC);

    auto* self = static_cast<DictLoader*>(ZL_DictLoader_getOpaque(loader));
    ZL_ERR_IF_NULL(self, GENERIC);

    try {
        const ZL_DictBundle* bundle = self->fetchDictBundle(*id);
        if (bundle == nullptr) {
            ZL_ERR(GENERIC, "fetchDictBundle returned nullptr");
        }
        return ZL_WRAP_VALUE(bundle);
    } catch (const Exception& e) {
        ZL_ERR(GENERIC, "C++ openzl::Exception: %s", e.what());
    } catch (const std::exception& e) {
        ZL_ERR(GENERIC, "C++ std::exception: %s", e.what());
    } catch (...) {
        ZL_ERR(GENERIC, "C++ unknown exception");
    }
    return ZL_WRAP_VALUE((const ZL_DictBundle*)nullptr);
}

} // namespace

DictLoader::DictLoader()
        : loader_(
                  [this] {
                      ZL_DictLoaderDesc desc{};
                      desc.fetchDictBundle = fetchDictBundleTrampoline;
                      desc.opaque.ptr      = this;
                      auto* raw            = ZL_DictLoader_create(&desc);
                      if (raw == nullptr) {
                          throw Exception("Failed to create ZL_DictLoader");
                      }
                      return raw;
                  }(),
                  ZL_DictLoader_free)
{
}

DictLoader::~DictLoader() = default;

void DictLoader::registerMaterializer(
        ZL_IDType codecID,
        const ZL_MaterializerDesc2* materializer)
{
    unwrap(ZL_DictLoader_registerMaterializer(
                   loader_.get(), codecID, materializer),
           "ZL_DictLoader_registerMaterializer() failed");
}

void* DictLoader::materialize(
        ZL_IDType codecID,
        bool isCustomCodec,
        const void* src,
        size_t srcSize)
{
    return unwrap(
            ZL_DictLoader_materialize(
                    loader_.get(), codecID, isCustomCodec, src, srcSize),
            "ZL_DictLoader_materialize() failed");
}

void DictLoader::dematerialize(
        ZL_IDType codecID,
        bool isCustomCodec,
        void* materialized)
{
    ZL_DictLoader_dematerialize(
            loader_.get(), codecID, isCustomCodec, materialized);
}

} // namespace openzl
