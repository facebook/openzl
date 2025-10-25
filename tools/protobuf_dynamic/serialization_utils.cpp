// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "serialization_utils.h"
#include "ProtoSerializer.h"
#include "ProtoDeserializer.h"
#include <google/protobuf/util/json_util.h>
#include "openzl/common/assertion.h"

namespace openzl {
namespace protobuf {

using JsonPrintOptions = google::protobuf::util::JsonPrintOptions;
using JsonParseOptions = google::protobuf::util::JsonParseOptions;

std::string serialize(
    const google::protobuf::Message& obj,
    Protocol protocol,
    ProtoSerializer& serializer)
{
    std::string serialized;
    if (protocol == Protocol::Proto) {
        serialized = obj.SerializeAsString();
    } else if (protocol == Protocol::ZL) {
        serialized = serializer.serialize(obj);
    } else if (protocol == Protocol::JSON) {
        auto status = google::protobuf::util::MessageToJsonString(
                obj, &serialized, JsonPrintOptions());
        ZL_REQUIRE(
                status.ok(),
                "Failed to serialize to JSON: %s",
                status.message());
    }
    return serialized;
}

void deserialize(
    const std::string& serialized,
    Protocol protocol,
    ProtoDeserializer& deserializer,
    google::protobuf::Message& obj)
{
    if (protocol == Protocol::Proto) {
        obj.ParseFromString(serialized);
    } else if (protocol == Protocol::ZL) {
        deserializer.deserialize(serialized, obj);
    } else if (protocol == Protocol::JSON) {
        auto status = google::protobuf::util::JsonStringToMessage(
                serialized, &obj, JsonParseOptions());
        ZL_REQUIRE(status.ok(), "Failed to parse JSON: %s", status.message());
    }
}

} // namespace protobuf
} // namespace openzl
