// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {SerializedStreamdumpV1} from '../interfaces/SerializedStreamdump';
import {Chunk} from './Chunk';

export class Streamdump {
  readonly libraryVersion: number;
  readonly frameVersion: number;
  readonly traceVersion: number;
  readonly chunks: Chunk[];

  constructor(libraryVersion: number, frameVersion: number, traceVersion: number, chunks: Chunk[]) {
    this.libraryVersion = libraryVersion;
    this.frameVersion = frameVersion;
    this.traceVersion = traceVersion;
    this.chunks = chunks;
  }

  static fromObject(obj: SerializedStreamdumpV1): Streamdump {
    return new Streamdump(
      obj.libraryVersion,
      obj.frameVersion,
      obj.traceVersion,
      obj.chunks.map((chunk, chunkId) => Chunk.fromObject(chunk, chunkId)),
    );
  }
}
