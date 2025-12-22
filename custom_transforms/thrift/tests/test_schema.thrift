// Copyright (c) Meta Platforms, Inc. and affiliates.

include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

namespace cpp2 zstrong.thrift.tests.cpp2

struct InnerTestStruct {
  -1: optional byte int8;
  -2: optional bool bool1;
  1: optional i16 int16;
  42: optional i32 int32;
  2: optional i64 int64;
  3: optional float float32;
  4: optional double float64;
  5: optional string string1;
  6: optional list<bool> list1;
  7: optional map<string, bool> map1;
  8: optional set<i32> set1;
}

# Used for round-trip testing
struct TestStruct {
  4: InnerTestStruct struct1;
  3: InnerTestStruct struct2;
  2: set<InnerTestStruct> set1;
  1: map<InnerTestStruct, InnerTestStruct> map1;

  # Ensure test coverage for structure-optimized kernels
  100: list<i16> i16List;
  101: list<i32> i32List;
  102: list<i64> i64List;
  103: list<float> floatList;
  104: list<double> doubleList;
  105: map<i32, i32> i32i32Map;
  106: map<i32, i64> i32i64Map;
  107: map<i32, float> i32FloatMap;
  108: map<i32, double> i32DoubleMap;
  109: map<i64, i32> i64i32Map;
  110: map<i64, i64> i64i64Map;
  111: map<i64, float> i64FloatMap;
  112: map<i64, double> i64DoubleMap;
  113: map<i64, string> i64StringMap;
  114: map<string, i64> stringI64Map;
  115: map<string, string> stringStringMap;
  116: map<i64, InnerTestStruct> i64InnerTestStructMap;
}

# Used for testing the representation of primitive types
struct PrimitiveTestStruct {
  1: bool field_bool;
  2: byte field_byte;
  3: i16 field_i16;
  4: i32 field_i32;
  5: i64 field_i64;
  6: float field_float32;
  7: double field_float64;
  8: string field_string;
}

# Used for testing the representation of collections
struct CollectionTestStruct {
  1: list<bool> field_list_bool;
  2: set<i32> field_set_int32;
  3: map<i32, bool> field_map_diff_types;
  4: map<float, float> field_map_same_types;
}

# Used for testing the empty-cluster scenario
struct UnknownFieldsTestStruct {
  1234: string string1;
  5678: string string2;
}

struct StringTestStruct {
  1: optional string field1;
  2: optional string field2;
}
