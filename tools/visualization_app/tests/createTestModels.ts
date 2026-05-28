// Copyright (c) Meta Platforms, Inc. and affiliates.

import {InteractiveStreamdumpGraph} from '../src/graphVisualization/models/InteractiveStreamdumpGraph';
import {Streamdump} from '../src/models/Streamdump';
import {Stream} from '../src/models/Stream';
import {Codec} from '../src/models/Codec';
import {Graph} from '../src/models/Graph';
import {LocalParamInfo} from '../src/models/LocalParamInfo';
import {
  ZL_Type,
  ZL_GraphType,
  OperationType,
  type ChunkID,
  type CodecID,
  type GraphID,
  type StreamID,
  type ZL_IDType,
} from '../src/models/idTypes';
import type {RF_edgeId} from '../src/graphVisualization/models/types';
import type {StreamPreviewData} from '../src/interfaces/SerializedStream';
import {Chunk} from '../src/models/Chunk';

// Match the values that decodeCbor's V0->V1 marshaller uses for synthetic
// streamdumps. traceVersion is 1 because we're constructing V1 directly.
const LIBRARY_VERSION = 100;
const FRAME_VERSION = -1;
const TRACE_VERSION = 1;

// Test fixtures live in chunk 0 (single-chunk streamdumps).
const CHUNK_ID: ChunkID = 0 as ChunkID;

// Helper function to create a Stream
export function createTestStream(
  id: number,
  type: ZL_Type = ZL_Type.ZL_Type_numeric,
  eltWidth = 4,
  numElts: number,
  cSize: number,
  share: number,
  outputIdx = 0,
  contentSize?: number,
  streamPreview?: StreamPreviewData,
  chunkId: ChunkID = CHUNK_ID,
): Stream {
  return new Stream(
    id as StreamID,
    chunkId,
    type,
    outputIdx,
    eltWidth,
    numElts,
    cSize,
    share,
    contentSize ?? cSize,
    streamPreview,
    `C${chunkId}-S${id}` as RF_edgeId,
  );
}

// Helper function to create a Codec
export function createTestCodec(
  id: number,
  name: string,
  cType = true,
  headerSize = 1,
  localParams: LocalParamInfo = new LocalParamInfo([], [], []),
  inputStreams: number[] = [],
  outputStreams: number[] = [],
  cID = 0,
  cFailureString = '',
  chunkId: ChunkID = CHUNK_ID,
): Codec {
  return new Codec(
    id as CodecID,
    chunkId,
    name,
    cType,
    cID as ZL_IDType,
    headerSize,
    cFailureString,
    localParams,
    inputStreams as StreamID[],
    outputStreams as StreamID[],
  );
}

// Helper function to create a Graph
export function createTestGraph(
  id: number,
  type: ZL_GraphType = ZL_GraphType.ZL_GraphType_standard,
  name: string,
  localParams: LocalParamInfo = new LocalParamInfo([], [], []),
  codecIDs: number[] = [],
  gFailureString = '',
  chunkId: ChunkID = CHUNK_ID,
): Graph {
  return new Graph(id as GraphID, chunkId, type, name, gFailureString, localParams, codecIDs as CodecID[]);
}

// Create a simple tree with a graph: A->B->C->D where B and C are in a graph
export function createSimpleTreeWithGraph(isDefaultCollapsed = false) {
  const emptyLocalParams = new LocalParamInfo([], [], []);

  // Create streams (every codec has at most one output, so outputIdx defaults to 0)
  const stream0 = createTestStream(0, ZL_Type.ZL_Type_numeric, 4, 100, 400, 33.3);
  const stream1 = createTestStream(1, ZL_Type.ZL_Type_numeric, 4, 80, 320, 33.3);
  const stream2 = createTestStream(2, ZL_Type.ZL_Type_numeric, 4, 60, 240, 33.3);

  // Create codecs
  const codecA = createTestCodec(0, 'CodecA', true, 100, emptyLocalParams, [], [0]);
  const codecB = createTestCodec(1, 'CodecB', true, 200, emptyLocalParams, [0], [1]);
  const codecC = createTestCodec(2, 'CodecC', true, 300, emptyLocalParams, [1], [2]);
  const codecD = createTestCodec(3, 'CodecD', true, 400, emptyLocalParams, [2], []);

  // Create graph
  const graph = createTestGraph(0, ZL_GraphType.ZL_GraphType_standard, 'GraphBC', emptyLocalParams, [1, 2]);

  // Create streamdump (single chunk)
  const streamdump = new Streamdump(LIBRARY_VERSION, FRAME_VERSION, TRACE_VERSION, OperationType.Compress, [
    new Chunk(CHUNK_ID, [stream0, stream1, stream2], [codecA, codecB, codecC, codecD], [graph]),
  ]);

  return new InteractiveStreamdumpGraph(streamdump, isDefaultCollapsed);
}

// Create a branching tree with two paths: A->B->C/D and A->E->F
export function createBranchingTreeWithGraph(isDefaultCollapsed = false) {
  const emptyLocalParams = new LocalParamInfo([], [], []);

  // Create streams. outputIdx is the slot of the producing codec's output list:
  //   A produces [streamAB (idx 0), streamAE (idx 1)]
  //   B produces [streamBC (idx 0), streamBD (idx 1)]
  //   E produces [streamEF (idx 0)]
  // Left branch (A->B->C and A->B->D) will have higher compression
  const streamAB = createTestStream(0, ZL_Type.ZL_Type_numeric, 4, 100, 500, 25.0, 0);
  const streamBC = createTestStream(1, ZL_Type.ZL_Type_numeric, 4, 80, 450, 22.5, 0);
  const streamBD = createTestStream(2, ZL_Type.ZL_Type_numeric, 4, 70, 350, 17.5, 1);

  // Right branch (A->E->F) will have lower compression
  const streamAE = createTestStream(3, ZL_Type.ZL_Type_numeric, 4, 90, 300, 15.0, 1);
  const streamEF = createTestStream(4, ZL_Type.ZL_Type_numeric, 4, 60, 200, 10.0, 0);

  // Create codecs
  const codecA = createTestCodec(0, 'Root', true, 100, emptyLocalParams, [], [0, 3]);
  const codecB = createTestCodec(1, 'LeftBranch', true, 200, emptyLocalParams, [0], [1, 2]);
  const codecC = createTestCodec(2, 'LeftLeaf1', true, 300, emptyLocalParams, [1], []);
  const codecD = createTestCodec(3, 'LeftLeaf2', true, 400, emptyLocalParams, [2], []);
  const codecE = createTestCodec(4, 'RightBranch', true, 500, emptyLocalParams, [3], [4]);
  const codecF = createTestCodec(5, 'RightLeaf', true, 600, emptyLocalParams, [4], []);

  // Create graphs
  const leftGraph = createTestGraph(
    0,
    ZL_GraphType.ZL_GraphType_function,
    'LeftBranchGraph',
    emptyLocalParams,
    [1, 2, 3],
  );
  const rightGraph = createTestGraph(
    1,
    ZL_GraphType.ZL_GraphType_function,
    'RightBranchGraph',
    emptyLocalParams,
    [4, 5],
  );

  // Create streamdump (single chunk)
  const streamdump = new Streamdump(LIBRARY_VERSION, FRAME_VERSION, TRACE_VERSION, OperationType.Compress, [
    new Chunk(
      CHUNK_ID,
      [streamAB, streamBC, streamBD, streamAE, streamEF],
      [codecA, codecB, codecC, codecD, codecE, codecF],
      [leftGraph, rightGraph],
    ),
  ]);

  return new InteractiveStreamdumpGraph(streamdump, isDefaultCollapsed);
}
