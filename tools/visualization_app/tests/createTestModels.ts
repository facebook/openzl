// Copyright (c) Meta Platforms, Inc. and affiliates.

import {InteractiveStreamdumpGraph} from '../src/graphVisualization/models/InteractiveStreamdumpGraph';
import {Streamdump} from '../src/models/Streamdump';
import {Stream} from '../src/models/Stream';
import {Codec} from '../src/models/Codec';
import {Graph} from '../src/models/Graph';
import {LocalParamInfo} from '../src/models/LocalParamInfo';
import {ZL_Type, ZL_GraphType} from '../src/models/idTypes';

// Helper function to create a Stream
export function createTestStream(
  id: number,
  type: ZL_Type = ZL_Type.ZL_Type_numeric,
  eltWidth = 4,
  numElts: number,
  cSize: number,
  share: number,
): Stream {
  return new Stream(id, type, eltWidth, numElts, cSize, share);
}

// Helper function to create a Codec
export function createTestCodec(
  id: number,
  name: string,
  cType = true,
  headerSize = 1,
  localParamsSize = 16,
  localParams: LocalParamInfo = new LocalParamInfo([], [], []),
  inputStreams: number[] = [],
  outputStreams: number[] = [],
): Codec {
  return new Codec(id, name, cType, headerSize, localParamsSize, localParams, inputStreams, outputStreams);
}

// Helper function to create a Graph
export function createTestGraph(
  id: number,
  type: ZL_GraphType = ZL_GraphType.ZL_GraphType_standard,
  name: string,
  localParams: LocalParamInfo = new LocalParamInfo([], [], []),
  codecIDs: number[] = [],
): Graph {
  return new Graph(id, type, name, localParams, codecIDs);
}

// Create a simple tree with a graph: A->B->C->D where B and C are in a graph
export function createSimpleTreeWithGraph(isDefaultCollapsed = false) {
  const emptyLocalParams = new LocalParamInfo([], [], []);

  // Create streams
  const stream0 = createTestStream(0, ZL_Type.ZL_Type_numeric, 4, 100, 400, 33.3);
  const stream1 = createTestStream(1, ZL_Type.ZL_Type_numeric, 4, 80, 320, 33.3);
  const stream2 = createTestStream(2, ZL_Type.ZL_Type_numeric, 4, 60, 240, 33.3);

  // Create codecs
  const codecA = createTestCodec(0, 'CodecA', true, 100, 16, emptyLocalParams, [], [0]);
  const codecB = createTestCodec(1, 'CodecB', true, 200, 16, emptyLocalParams, [0], [1]);
  const codecC = createTestCodec(2, 'CodecC', true, 300, 16, emptyLocalParams, [1], [2]);
  const codecD = createTestCodec(3, 'CodecD', true, 400, 16, emptyLocalParams, [2], []);

  // Create graph
  const graph = createTestGraph(0, ZL_GraphType.ZL_GraphType_standard, 'GraphBC', emptyLocalParams, [1, 2]);

  // Create streamdump
  const streamdump = new Streamdump([stream0, stream1, stream2], [codecA, codecB, codecC, codecD], [graph]);

  return new InteractiveStreamdumpGraph(streamdump, isDefaultCollapsed);
}

// Create a branching tree with two paths: A->B->C/D and A->E->F
export function createBranchingTreeWithGraph(isDefaultCollapsed = false) {
  const emptyLocalParams = new LocalParamInfo([], [], []);

  // Create streams
  // Left branch (A->B->C and A->B->D) will have higher compression
  const streamAB = createTestStream(0, ZL_Type.ZL_Type_numeric, 4, 100, 500, 25.0);
  const streamBC = createTestStream(1, ZL_Type.ZL_Type_numeric, 4, 80, 450, 22.5);
  const streamBD = createTestStream(2, ZL_Type.ZL_Type_numeric, 4, 70, 350, 17.5);

  // Right branch (A->E->F) will have lower compression
  const streamAE = createTestStream(3, ZL_Type.ZL_Type_numeric, 4, 90, 300, 15.0);
  const streamEF = createTestStream(4, ZL_Type.ZL_Type_numeric, 4, 60, 200, 10.0);

  // Create codecs
  const codecA = createTestCodec(0, 'Root', true, 100, 16, emptyLocalParams, [], [0, 3]);
  const codecB = createTestCodec(1, 'LeftBranch', true, 200, 16, emptyLocalParams, [0], [1, 2]);
  const codecC = createTestCodec(2, 'LeftLeaf1', true, 300, 16, emptyLocalParams, [1], []);
  const codecD = createTestCodec(3, 'LeftLeaf2', true, 400, 16, emptyLocalParams, [2], []);
  const codecE = createTestCodec(4, 'RightBranch', true, 500, 16, emptyLocalParams, [3], [4]);
  const codecF = createTestCodec(5, 'RightLeaf', true, 600, 16, emptyLocalParams, [4], []);

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

  // Create streamdump
  const streamdump = new Streamdump(
    [streamAB, streamBC, streamBD, streamAE, streamEF],
    [codecA, codecB, codecC, codecD, codecE, codecF],
    [leftGraph, rightGraph],
  );

  return new InteractiveStreamdumpGraph(streamdump, isDefaultCollapsed);
}
