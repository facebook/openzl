// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Codec} from '../../models/Codec';
import type {InternalGraphNode} from './InternalGraphNode';
import {InternalCodecNode} from './InternalCodecNode';
import {NodeType} from './types';
import type {RF_codecId, RF_nodeId} from './types';

export class InternalSegmenterNode extends InternalCodecNode {
  // segmenter-specific properties
  numChunks = -1;

  constructor(rfid: RF_nodeId, type: NodeType, codec: Codec, parentGraph: InternalGraphNode | null) {
    super(rfid as RF_codecId, type, codec, parentGraph);
    this.type = NodeType.Segmenter;
  }

  segmenterTypeToString(): string {
    return this.cType ? 'Standard' : 'Custom';
  }
}
