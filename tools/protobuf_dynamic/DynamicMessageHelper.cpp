// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "DynamicMessageHelper.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/message.h>

namespace openzl {
namespace protobuf {

DynamicMessageHelper::DynamicMessageHelper(
        const google::protobuf::DescriptorPool* pool)
        : pool_(pool),
          factory_(std::make_unique<google::protobuf::DynamicMessageFactory>())
{
    if (!pool_) {
        throw std::invalid_argument("DescriptorPool cannot be null");
    }
}

DynamicMessageHelper::~DynamicMessageHelper() = default;

DynamicMessageHelper::DynamicMessageHelper(DynamicMessageHelper&&) noexcept =
        default;
DynamicMessageHelper& DynamicMessageHelper::operator=(
        DynamicMessageHelper&&) noexcept = default;

std::unique_ptr<google::protobuf::Message> DynamicMessageHelper::newMessage(
        const std::string& type_name) const
{
    const auto* descriptor = getDescriptor(type_name);
    if (!descriptor) {
        return nullptr;
    }

    const auto* prototype = factory_->GetPrototype(descriptor);
    if (!prototype) {
        return nullptr;
    }

    return std::unique_ptr<google::protobuf::Message>(prototype->New());
}

std::unique_ptr<google::protobuf::Message> DynamicMessageHelper::parseMessage(
        const std::string& type_name,
        const void* data,
        size_t size) const
{
    auto message = newMessage(type_name);
    if (!message) {
        return nullptr;
    }

    if (!message->ParseFromArray(data, static_cast<int>(size))) {
        return nullptr;
    }

    return message;
}

const google::protobuf::Descriptor* DynamicMessageHelper::getDescriptor(
        const std::string& type_name) const
{
    return pool_->FindMessageTypeByName(type_name);
}

} // namespace protobuf
} // namespace openzl
