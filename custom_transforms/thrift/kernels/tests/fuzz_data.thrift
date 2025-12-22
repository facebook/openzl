// Copyright (c) Meta Platforms, Inc. and affiliates.

include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

namespace cpp2 zstrong.thrift.tests

union ThriftKernelData {
  1: map<i32, float> mapI32Float;
  2: map<i32, list<float>> mapI32ArrayFloat;
  3: map<i32, list<i64>> mapI32ArrayI64;
  4: map<i32, list<list<i64>>> mapI32ArrayArrayI64;
  5: map<i32, map<i64, float>> mapI32MapI64Float;
  6: list<i64> arrayI64;
  7: list<i32> arrayI32;
  8: list<float> arrayFloat;
}
