// Copyright (c) Meta Platforms, Inc. and affiliates.
// #include <google/protobuf/util/message_differencer.h>
#include <google/protobuf/util/field_comparator.h>
#include <google/protobuf/util/message_differencer.h>
#include "data_compression/experimental/zstrong/tools/protobuf/tests/test_schema.pb.h"
#include "openzl/common/errors_internal.h"
#include "security/lionhead/utils/lib_ftest/ftest.h"
#include "tools/protobuf/ProtoDeserializer.h"
#include "tools/protobuf/ProtoSerializer.h"
namespace openzl {
namespace protobuf {
namespace testing {
namespace {

class ProtobufSerializerTest : public ::testing::Test {
   public:
    ProtobufSerializerTest() = default;

    ProtoSerializer serializer_;
    ProtoDeserializer deserializer_;
};
} // namespace

using google::protobuf::util::DefaultFieldComparator;
using google::protobuf::util::MessageDifferencer;

FUZZ_F(ProtobufSerializerTest, FuzzRandomInput)
{
    std::string data{ (const char*)Data, Size };

    TestSchema obj;
    obj.ParseFromString(data);

    auto serialized   = serializer_.serialize(obj);
    auto deserialized = deserializer_.deserialize<TestSchema>(serialized);

    MessageDifferencer differencer;
    differencer.set_message_field_comparison(MessageDifferencer::EQUIVALENT);

    DefaultFieldComparator comparator;
    comparator.set_treat_nan_as_equal(true);
    differencer.set_field_comparator(&comparator);

    ZL_REQUIRE(differencer.Compare(obj, deserialized));
}
} // namespace testing
} // namespace protobuf
} // namespace openzl
