// Copyright (c) Meta Platforms, Inc. and affiliates.

include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

namespace cpp2 zstrong.tulip_v2.tests

struct InnerStruct {
  1: optional map<i32, i32> mapI32I32Data;
}

union InnerUnion {
  1: map<i32, float> floatFeatures;
  2: map<i64, map<i64, float>> mapI64MapI64FloatData;
}

struct TulipV2Data {
  1: optional map<i32, float> floatFeatures;
  2: optional map<i64, float> mapI64FloatData;
  3: optional map<i32, list<float>> floatListFeatures;
  4: optional i32 i32Data;
  5: optional bool boolData;
  6: optional map<i32, list<i64>> idListFeatures;
  7: optional map<i32, list<list<i64>>> idListListFeatures;
  8: optional InnerStruct innerStruct;
  9: optional set<string> setStringData;
  10: optional map<i32, map<i64, float>> idScoreListFeatures;
  11: optional list<i64> arrayi64Data;
  12: optional list<bool> arrayBoolData;
  13: optional string stringData;
  14: optional binary binaryData;
  16: optional InnerUnion innerUnion;
}
