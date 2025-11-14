// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {SerializedStreamdump} from '../interfaces/SerializedStreamdump';
import {Codec} from './Codec';
import {Stream} from './Stream';
import {Graph} from './Graph';

export class Streamdump {
  readonly libraryVersion: number;
  readonly frameVersion: number;
  readonly traceVersion: number;
  readonly streams: Stream[];
  readonly codecs: Codec[];
  readonly graphs: Graph[];

  constructor(libraryVersion: number, frameVersion: number, traceVersion: number, streams: Stream[], codecs: Codec[], graphs: Graph[]) {
    this.libraryVersion = libraryVersion;
    this.frameVersion = frameVersion;
    this.traceVersion = traceVersion;
    this.streams = streams;
    this.codecs = codecs;
    this.graphs = graphs;
  }

  static fromObject(obj: SerializedStreamdump): Streamdump {
    return new Streamdump(
      obj.libraryVersion,
      obj.frameVersion,
      obj.traceVersion,
      obj.streams.map((stream, streamId) => Stream.fromObject(stream, streamId)),
      obj.codecs.map((codec, codecNum) => Codec.fromObject(codec, codecNum)),
      obj.graphs.map((graph, graphNum) => Graph.fromObject(graph, graphNum)),
    );
  }
}
