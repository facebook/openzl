// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include <google/protobuf/message.h>
#include <google/protobuf/reflection.h>
#include <string>
#include "openzl/cpp/DCtx.hpp"
#include "tools/protobuf/Types.h"

namespace openzl {
namespace protobuf {
class ProtoDeserializer {
   public:
    ProtoDeserializer()          = default;
    virtual ~ProtoDeserializer() = default;

    /**
     * Deserializes a protobuf message from a string.
     *
     * @param serialized The serialized message.
     * @param message The message to deserialize into.
     */
    void deserialize(const std::string& serialized, Message& message);

    template <typename T>
    T deserialize(const std::string& serialized)
    {
        T message;
        deserialize(serialized, message);
        return message;
    }

   private:
    DCtx dctx_;
};
} // namespace protobuf
} // namespace openzl
