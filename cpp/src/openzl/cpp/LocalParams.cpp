// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/LocalParams.hpp"

#include <cstring>

#include "openzl/cpp/Exception.hpp"

namespace openzl {

LocalParams::LocalParams(const LocalParams& other) : LocalParams(*other) {}

LocalParams::LocalParams(const ZL_LocalParams& src)
{
    intParams_.reserve(src.intParams.nbIntParams);
    for (size_t i = 0; i < src.intParams.nbIntParams; ++i) {
        addIntParam(src.intParams.intParams[i]);
    }

    copyParams_.reserve(src.copyParams.nbCopyParams);
    for (size_t i = 0; i < src.copyParams.nbCopyParams; ++i) {
        addCopyParam(src.copyParams.copyParams[i]);
    }

    refParams_.reserve(src.refParams.nbRefParams);
    for (size_t i = 0; i < src.refParams.nbRefParams; ++i) {
        addRefParam(src.refParams.refParams[i]);
    }
}

LocalParams& LocalParams::operator=(const LocalParams& other)
{
    if (this != &other) {
        *this = LocalParams(other);
    }
    return *this;
}

void LocalParams::insertKeyOrThrow(int key)
{
    auto [_it, inserted] = keys_.insert(key);
    if (!inserted) {
        throw Exception("Key already exists: " + std::to_string(key));
    }
}

void LocalParams::addIntParam(ZL_IntParam param)
{
    insertKeyOrThrow(param.paramId);
    intParams_.push_back(param);
    params_.intParams.intParams   = intParams_.data();
    params_.intParams.nbIntParams = intParams_.size();
}

void LocalParams::addIntParam(int key, int value)
{
    return addIntParam(ZL_IntParam{ key, value });
}

void LocalParams::addCopyParam(ZL_CopyParam param)
{
    insertKeyOrThrow(param.paramId);

    // Take ownership of the memory
    auto ptr = std::make_unique<uint8_t[]>(param.paramSize);
    if (param.paramSize > 0) {
        std::memcpy(ptr.get(), param.paramPtr, param.paramSize);
    }
    param.paramPtr = ptr.get();
    storage_.push_back(std::move(ptr));

    copyParams_.push_back(param);
    params_.copyParams.copyParams   = copyParams_.data();
    params_.copyParams.nbCopyParams = copyParams_.size();
}

void LocalParams::addCopyParam(int key, const void* valuePtr, size_t valueSize)
{
    return addCopyParam(ZL_CopyParam{ key, valuePtr, valueSize });
}

void LocalParams::addRefParam(ZL_RefParam param)
{
    insertKeyOrThrow(param.paramId);
    refParams_.push_back(param);
    params_.refParams.refParams   = refParams_.data();
    params_.refParams.nbRefParams = refParams_.size();
}

void LocalParams::addRefParam(int key, const void* ref, size_t size)
{
    return addRefParam(ZL_RefParam{ key, ref, size });
}
} // namespace openzl
