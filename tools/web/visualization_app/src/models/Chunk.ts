// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {SerializedChunk} from '../interfaces/SerializedChunk';
import type {ChunkID} from './idTypes';
import {Codec} from './Codec';
import {Stream} from './Stream';
import {Graph} from './Graph';

export class Chunk {
  readonly chunkId: ChunkID;
  readonly streams: Stream[];
  readonly codecs: Codec[];
  readonly graphs: Graph[];

  constructor(id: ChunkID, streams: Stream[], codecs: Codec[], graphs: Graph[]) {
    this.chunkId = id;
    this.streams = streams;
    this.codecs = codecs;
    this.graphs = graphs;
  }

  static fromObject(obj: SerializedChunk, idx: number): Chunk {
    return new Chunk(
      idx as ChunkID,
      obj.streams.map((stream, streamId) => Stream.fromObject(stream, streamId)),
      obj.codecs.map((codec, codecNum) => Codec.fromObject(codec, codecNum)),
      obj.graphs.map((graph, graphNum) => Graph.fromObject(graph, graphNum)),
    );
  }
}
