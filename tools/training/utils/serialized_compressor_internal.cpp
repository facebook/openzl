// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "tools/training/utils/serialized_compressor_internal.h"

namespace openzl::training {

SerializedCompressorInternal::SerializedCompressorInternal(std::string&& str)
        : storage_(std::move(str))
{
}

std::string_view SerializedCompressorInternal::operator*() const
{
    return storage_;
}

} // namespace openzl::training
