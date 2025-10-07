// Copyright (c) Meta Platforms, Inc. and affiliates.

import {ZL_Type, type StreamID} from '../../models/idTypes';
import type {Stream} from '../../models/Stream';
import type {InternalNode} from './InternalNode';
import type {RF_edgeId} from './types';

export class InternalEdge {
  readonly streamId: StreamID;
  readonly rfid: RF_edgeId;

  // traced properties
  readonly type: ZL_Type;
  readonly outputIdx: number;
  readonly eltWidth: number;
  readonly numElts: number;
  readonly cSize: number;
  readonly share: number;
  readonly contentSize: number;

  // derived properties
  readonly label: string;

  // graph properties
  readonly source: InternalNode;
  readonly target: InternalNode;
  hidden = false;
  inLargestCompressionPath = false;

  constructor(
    rfid: RF_edgeId,
    // stream: Stream,
    streamId: StreamID,
    type: ZL_Type,
    outputIdx: number,
    eltWidth: number,
    numElts: number,
    cSize: number,
    share: number,
    contentSize: number,
    source: InternalNode,
    target: InternalNode,
    label: string,
  ) {
    this.rfid = rfid;
    this.streamId = streamId;
    this.type = type;
    this.outputIdx = outputIdx;
    this.eltWidth = eltWidth;
    this.numElts = numElts;
    this.cSize = cSize;
    this.share = share;
    this.contentSize = contentSize;

    this.source = source;
    this.target = target;
    this.label = label;
  }

  static constructFromStream(
    rfid: RF_edgeId,
    source: InternalNode,
    target: InternalNode,
    label: string,
    stream: Stream,
  ): InternalEdge {
    return new InternalEdge(
      rfid,
      stream.streamId,
      stream.type,
      stream.outputIdx,
      stream.eltWidth,
      stream.numElts,
      stream.cSize,
      stream.share,
      stream.contentSize,
      source,
      target,
      label,
    );
  }

  static constructFromInternalEdge(
    rfid: RF_edgeId,
    source: InternalNode,
    target: InternalNode,
    label: string,
    edge: InternalEdge,
  ): InternalEdge {
    return new InternalEdge(
      rfid,
      edge.streamId,
      edge.type,
      edge.outputIdx,
      edge.eltWidth,
      edge.numElts,
      edge.cSize,
      edge.share,
      edge.contentSize,
      source,
      target,
      label,
    );
  }

  streamTypeToString(): string {
    switch (this.type) {
      case ZL_Type.ZL_Type_serial:
        return 'Serialized';
      case ZL_Type.ZL_Type_struct:
        return 'Fixed_Width';
      case ZL_Type.ZL_Type_numeric:
        return 'Numeric';
      case ZL_Type.ZL_Type_string:
        return 'Variable_Size';
      default:
        return 'default';
    }
  }
}
