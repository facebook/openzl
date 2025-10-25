// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <google/protobuf/message.h>

namespace openzl {
namespace protobuf {

class ProtoSerializer;
class ProtoDeserializer;

enum class Protocol {
    Proto,
    ZL,
    JSON
};

/**
 * Serialize a protobuf message using the specified protocol.
 * Works with any google::protobuf::Message subclass.
 */
std::string serialize(
    const google::protobuf::Message& obj,
    Protocol protocol,
    ProtoSerializer& serializer);

/**
 * Deserialize a protobuf message using the specified protocol.
 * Works with any google::protobuf::Message subclass.
 */
void deserialize(
    const std::string& serialized,
    Protocol protocol,
    ProtoDeserializer& deserializer,
    google::protobuf::Message& obj);

} // namespace protobuf
} // namespace openzl
