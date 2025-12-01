// Copyright (c) Meta Platforms, Inc. and affiliates.

import {LocalParamInfo} from './LocalParamInfo';
import type {ZL_IDType, StreamID, CodecID, GraphID, ChunkID} from './idTypes';
import type {SerializedCodec} from '../interfaces/SerializedCodec';
import type {RF_codecId} from '../graphVisualization/models/types';

export class Codec {
  readonly id: CodecID;
  readonly chunkId: ChunkID;

  // traced properties
  readonly cID: ZL_IDType; // COdec ID as defined by OpenZL, unrelated to the "id" of this Typescript Codec object
  readonly name: string;
  readonly cType: boolean;
  readonly cHeaderSize: number;
  readonly cFailureString: string;
  readonly cLocalParams: LocalParamInfo;

  // graph properties
  readonly inputStreams: StreamID[];
  outputStreams: StreamID[];
  owningGraph: GraphID | null;

  // React-Flow display properties
  readonly rfId: RF_codecId;
  // isCollapsed: boolean = false;
  // isHidden: boolean = false;
  // hasChildren: boolean = false;

  constructor(
    id: CodecID,
    chunkId: ChunkID,
    name: string,
    cType: boolean,
    cID: ZL_IDType,
    cHeaderSize: number,
    cFailureString: string,
    cLocalParams: LocalParamInfo,
    inputStreams: StreamID[],
    outputStreams: StreamID[],
    owningGraph?: GraphID | null,
  ) {
    this.id = id;
    this.chunkId = chunkId;

    this.cID = cID;
    this.name = name;
    this.cType = cType;
    this.cHeaderSize = cHeaderSize;
    this.cFailureString = cFailureString;
    this.cLocalParams = cLocalParams;

    this.inputStreams = inputStreams;
    this.outputStreams = outputStreams;
    this.owningGraph = owningGraph ?? null;

    this.rfId = `C${chunkId}-T${id}` as RF_codecId;
  }

  codecTypeToString(): string {
    return this.cType ? 'Standard' : 'Custom';
  }

  static fromObject(obj: SerializedCodec, idx: number): Codec {
    return new Codec(
      idx as CodecID,
      obj.chunkId as ChunkID,
      obj.name,
      obj.cType,
      obj.cID,
      obj.cHeaderSize,
      obj.cFailureString,
      LocalParamInfo.fromObject(obj.cLocalParams),
      obj.inputStreams,
      obj.outputStreams,
    );
  }

  toStringDotFormat(): string {
    //print the codec
    const codecStr = `T${this.id} [shape=Mrecord, label="${this.name}(ID: ${this.cID})\\n
                          ${this.codecTypeToString()} transform ${this.id}\\n
                            Header size: ${this.cHeaderSize}\\n
                            ${this.cLocalParams.toStringDotFormat()}"];`;

    // out edges from codec
    const sortedOutputStreams = [...this.outputStreams].sort((a, b) => b - a);
    let labelNum = sortedOutputStreams.length - 1;
    const outputStreamsStr = sortedOutputStreams
      .map((streamID) => `T${this.id} -> S${streamID}[label="#${labelNum--}"];`)
      .join('\n');

    // in edges from codec
    labelNum = 0;
    const sortedInputStreams = [...this.inputStreams].sort((a, b) => a - b);
    const inputStreamsStr = sortedInputStreams
      .map((streamID) => `S${streamID} -> T${this.id}[label="#${labelNum++}"];`)
      .join('\n');

    return `${codecStr.replace(/\s+/g, ' ')}\n${outputStreamsStr}\n${inputStreamsStr}`;
  }
}
