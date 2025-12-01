// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {SerializedCodec} from './SerializedCodec';
import type {SerializedGraph} from './SerializedGraph';
import type {SerializedStream} from './SerializedStream';

export interface SerializedChunk {
  streams: SerializedStream[];
  codecs: SerializedCodec[];
  graphs: SerializedGraph[];
}
