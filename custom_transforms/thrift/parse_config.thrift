// Copyright (c) Meta Platforms, Inc. and affiliates.

// @nolint

include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

namespace cpp2 zstrong.thrift.cpp2
namespace py3 zstrong.thrift

struct PathInfo {
  1: i16 logicalId;
  2: byte type;
}

struct LogicalCluster {
  1: list<i16> idList;
  2: optional i32 successor;
}

struct BaseConfig {
  // Information needed for both encoding and decoding
  1: map<list<i32>, PathInfo> pathMap;
  2: byte rootType;
  3: optional list<LogicalCluster> clusters;
}

struct EncoderConfig {
  1: BaseConfig baseConfig;

  // Information only needed for encoding
  2: map<i32, i32> successorMap;
  3: optional bool parseTulipV2;
  4: optional i32 minFormatVersion;
  5: map<i32, i32> typeSuccessorMap;
}

struct DecoderConfig {
  1: BaseConfig baseConfig;

  // Information only needed for decoding
  2: i64 originalSize;
  3: optional bool unparseMessageHeaders;
}
