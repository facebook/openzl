// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {SerializedStreamdumpV1} from '../interfaces/SerializedStreamdump';
import {Chunk} from './Chunk';
import {OperationType} from './idTypes';

export class Streamdump {
  readonly libraryVersion: number;
  readonly frameVersion: number;
  readonly traceVersion: number;
  readonly operationType: OperationType;
  readonly chunks: Chunk[];

  constructor(
    libraryVersion: number,
    frameVersion: number,
    traceVersion: number,
    operationType: OperationType,
    chunks: Chunk[],
  ) {
    this.libraryVersion = libraryVersion;
    this.frameVersion = frameVersion;
    this.traceVersion = traceVersion;
    this.operationType = operationType;
    this.chunks = chunks;
  }

  static fromObject(obj: SerializedStreamdumpV1): Streamdump {
    const operationType = obj.operationType === 1 ? OperationType.Decompress : OperationType.Compress;
    return new Streamdump(
      obj.libraryVersion,
      obj.frameVersion,
      obj.traceVersion,
      operationType,
      obj.chunks.map((chunk, chunkId) => Chunk.fromObject(chunk, chunkId)),
    );
  }
}
