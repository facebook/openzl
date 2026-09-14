// Copyright (c) Meta Platforms, Inc. and affiliates.

import {ZL_GraphType, type ChunkID, type CodecID, type GraphID} from './idTypes';
import {LocalParamInfo} from './LocalParamInfo';
import type {SerializedGraph} from '../interfaces/SerializedGraph';
import type {RF_graphId} from '../graphVisualization/models/types';

export class Graph {
  readonly gNum: GraphID;
  readonly chunkId: ChunkID;

  readonly gType: ZL_GraphType;
  readonly gName: string;
  readonly gFailureString: string;
  readonly gLocalParams: LocalParamInfo;
  readonly codecIDs: CodecID[];

  // React-Force display properties
  readonly rfId: RF_graphId;

  constructor(
    gNum: GraphID,
    chunkId: ChunkID,
    gType: ZL_GraphType,
    gName: string,
    gFailureString: string,
    gLocalParams: LocalParamInfo,
    codecIDs: CodecID[],
  ) {
    this.gNum = gNum;
    this.chunkId = chunkId;
    this.gType = gType;
    this.gName = gName;
    this.gFailureString = gFailureString;
    this.gLocalParams = gLocalParams;
    this.codecIDs = codecIDs;

    this.rfId = `C${chunkId}-G${gNum}` as RF_graphId;
  }

  static fromObject(obj: SerializedGraph, gNum: number): Graph {
    return new Graph(
      gNum as GraphID,
      obj.chunkId as ChunkID,
      obj.gType,
      obj.gName,
      obj.gFailureString,
      LocalParamInfo.fromObject(obj.gLocalParams),
      obj.codecIDs as CodecID[],
    );
  }

  getGraphTypeString(): string {
    switch (this.gType) {
      case ZL_GraphType.ZL_GraphType_standard:
        return 'Standard';
      case ZL_GraphType.ZL_GraphType_static:
        return 'Static';
      case ZL_GraphType.ZL_GraphType_selector:
        return 'Selector';
      case ZL_GraphType.ZL_GraphType_function:
        return 'Function';
      case ZL_GraphType.ZL_GraphType_multiInput:
        return 'Multiple_Input';
      case ZL_GraphType.ZL_GraphType_parameterized:
        return 'Parameterized';
      default:
        return 'Unknown';
    }
  }

  // used to open a graph declaration in graphviz
  toStringDotFormatStart(): string {
    return `subgraph cluster_${this.gNum} {
        label="${this.gName}\\ntype=${this.getGraphTypeString()}\\n${this.gLocalParams.toStringDotFormat()}"
        color=maroon`.replace(/\s+/g, ' ');
  }

  // used to close a graph declaration in graphviz
  toStringDotFormatEnd(): string {
    return '}';
  }
}
