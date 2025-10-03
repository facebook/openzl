// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Codec} from '../../models/Codec';
import type {CodecID, StreamID, ZL_IDType} from '../../models/idTypes';
import type {LocalParamInfo} from '../../models/LocalParamInfo';
import type {InternalGraphNode} from './InternalGraphNode';
import {InternalNode} from './InternalNode';
import type {NodeType} from './types';
import type {RF_codecId} from './types';

export class InternalCodecNode extends InternalNode {
  readonly id: CodecID;

  // traced properties
  readonly cID: ZL_IDType; // Codec ID as defined by OpenZL, unrelated to the "id" of this Typescript Codec object
  readonly name: string;
  readonly cType: boolean;
  readonly cHeaderSize: number;
  readonly cFailureString: string;
  readonly cLocalParams: LocalParamInfo;

  // graph properties
  readonly inputStreams: StreamID[];
  outputStreams: StreamID[];
  readonly parentGraph: InternalGraphNode | null = null;

  // Properties for node collapsing
  // isCollapsed: boolean = false;
  // isHidden: boolean = false;

  codecTypeToString(): string {
    return this.cType ? 'Standard' : 'Custom';
  }

  constructor(rfid: RF_codecId, type: NodeType, codec: Codec, parentGraph: InternalGraphNode | null) {
    super(rfid, type);
    this.rfid = rfid;
    this.id = codec.id;
    this.cID = codec.cID;
    this.name = codec.name;
    this.cType = codec.cType;
    this.cHeaderSize = codec.cHeaderSize;
    this.cFailureString = codec.cFailureString;
    this.cLocalParams = codec.cLocalParams;
    this.inputStreams = codec.inputStreams;
    this.outputStreams = codec.outputStreams;
    this.parentGraph = parentGraph;
  }
}
