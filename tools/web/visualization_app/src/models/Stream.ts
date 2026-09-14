// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {ChunkID, CodecID, StreamID} from './idTypes';
import {ZL_Type} from './idTypes';
import type {SerializedStream, StreamPreviewData} from '../interfaces/SerializedStream';
import type {RF_edgeId} from '../graphVisualization/models/types';

export class Stream {
  static readonly NO_TARGET: CodecID = -1 as CodecID;
  static readonly NO_SOURCE: CodecID = -1 as CodecID;

  readonly streamId: StreamID;
  readonly chunkId: ChunkID;

  // traced properties
  readonly type: ZL_Type;
  readonly outputIdx: number;
  readonly eltWidth: number;
  readonly numElts: number;
  readonly cSize: number;
  readonly share: number;
  readonly contentSize: number;
  readonly streamPreview?: StreamPreviewData;

  // graph properties
  sourceCodec: CodecID = Stream.NO_SOURCE;
  targetCodec: CodecID = Stream.NO_TARGET;

  // React-Flow display properties
  readonly rfId: RF_edgeId;
  // hidden: boolean = false;

  constructor(
    streamId: StreamID,
    chunkId: ChunkID,
    type: ZL_Type,
    outputIdx: number,
    eltWidth: number,
    numElts: number,
    cSize: number,
    share: number,
    contentSize: number,
    streamPreview: StreamPreviewData | undefined,
    rfId: RF_edgeId,
  ) {
    this.streamId = streamId;
    this.chunkId = chunkId;

    this.type = type;
    this.outputIdx = outputIdx;
    this.eltWidth = eltWidth;
    this.numElts = numElts;
    this.cSize = cSize;
    this.share = share;
    this.contentSize = contentSize;
    this.streamPreview = streamPreview;

    this.rfId = rfId;
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

  static fromObject(obj: SerializedStream, idx: number): Stream {
    return new Stream(
      idx as StreamID,
      obj.chunkId as ChunkID,
      obj.type,
      obj.outputIdx,
      obj.eltWidth,
      obj.numElts,
      obj.cSize,
      obj.share,
      obj.contentSize,
      obj.streamPreview,
      `C${obj.chunkId}-S${idx}` as RF_edgeId,
    );
  }

  toStringDotFormat(): string {
    const streamStr = `S${this.streamId} [shape=record, label="Stream ${this.streamId}\\n
        Type: #${this.streamTypeToString()}\\n
        OutputIdx: ${this.outputIdx}\\n
        EltWidth: ${this.eltWidth}\\n
        #Elts: ${this.numElts}\\n
        CSize: ${this.cSize}\\n
        Share: ${this.share.toFixed(2)}%"];`;
    // Removes extra spaces and newlines that are not defined within the string.
    // Used for code clarity purposes within a code editor, so it isn't one large
    // string spanning beyond screen width.
    return streamStr.replace(/\s+/g, ' ');
  }
}
