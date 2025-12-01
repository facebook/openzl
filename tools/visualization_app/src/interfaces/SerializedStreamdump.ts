// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {SerializedStream} from './SerializedStream';
import type {SerializedCodec} from './SerializedCodec';
import type {SerializedGraph} from './SerializedGraph';
import type {SerializedChunk} from './SerializedChunk';

export interface SerializedStreamdumpV1 {
  libraryVersion: number;
  frameVersion: number;
  traceVersion: number;
  chunks: SerializedChunk[];
}

// previous version of the streamdump format
export interface SerializedStreamdumpV0 {
  libraryVersion: number;
  frameVersion: number;
  traceVersion: number;
  streams: SerializedStream[];
  codecs: SerializedCodec[];
  graphs: SerializedGraph[];
}
