// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, it, expect} from 'vitest';
import {CodecDag} from '../src/graphVisualization/models/CodecDag';
import {InternalCodecNode} from '../src/graphVisualization/models/InternalCodecNode';
import {NodeType} from '../src/graphVisualization/models/types';
import {Codec} from '../src/models/Codec';
import {Stream} from '../src/models/Stream';
import {LocalParamInfo} from '../src/models/LocalParamInfo';
import {ZL_Type} from '../src/models/idTypes';
import type {CodecID, StreamID, ChunkID, ZL_IDType} from '../src/models/idTypes';
import type {RF_edgeId, RF_codecId} from '../src/graphVisualization/models/types';

/**
 * Build a CodecDag from a declarative description.
 *
 * nodes: list of codec names, e.g. ['A','B','C']
 * edges: list of [sourceName, targetName] pairs, e.g. [['A','B'], ['B','C']]
 *
 * Returns the dag and a name->InternalCodecNode map for easy lookup in tests.
 * Stream IDs and codec input/output lists are derived automatically,
 * ensuring consistency.
 */
function buildCodecDag(
  nodes: string[],
  edges: [string, string][],
): {dag: CodecDag; codecs: Record<string, InternalCodecNode>} {
  const nameToId = new Map<string, number>();
  nodes.forEach((name, idx) => nameToId.set(name, idx));

  // Collect input/output stream IDs per codec
  const inputStreams = new Map<number, number[]>();
  const outputStreams = new Map<number, number[]>();
  nodes.forEach((_, idx) => {
    inputStreams.set(idx, []);
    outputStreams.set(idx, []);
  });

  const streams: Stream[] = edges.map(([srcName, dstName], streamId) => {
    const srcId = nameToId.get(srcName);
    const dstId = nameToId.get(dstName);
    if (srcId === undefined || dstId === undefined) {
      throw new Error(`Edge ${srcName}->${dstName} references unknown node`);
    }
    outputStreams.get(srcId)!.push(streamId);
    inputStreams.get(dstId)!.push(streamId);

    const stream = new Stream(
      streamId as StreamID,
      0 as ChunkID,
      ZL_Type.ZL_Type_numeric,
      0,
      4,
      100,
      400,
      100,
      400,
      undefined,
      `C0-S${streamId}` as RF_edgeId,
    );
    // CodecDag reads stream.targetCodec to build adjacency
    stream.sourceCodec = srcId as CodecID;
    stream.targetCodec = dstId as CodecID;
    return stream;
  });

  const codecNodes: InternalCodecNode[] = [];
  const codecsByName: Record<string, InternalCodecNode> = {};

  nodes.forEach((name, id) => {
    const codec = new Codec(
      id as CodecID,
      0 as ChunkID,
      name,
      true,
      0 as ZL_IDType,
      1,
      '',
      new LocalParamInfo([], [], []),
      (inputStreams.get(id) ?? []) as StreamID[],
      (outputStreams.get(id) ?? []) as StreamID[],
    );
    const node = new InternalCodecNode(`C0-T${id}` as RF_codecId, NodeType.Codec, codec, null);
    codecNodes.push(node);
    codecsByName[name] = node;
  });

  const dag = new CodecDag(codecNodes, streams);
  return {dag, codecs: codecsByName};
}

// Shared topology fixtures – each built once and reused across method tests
const singleNode = buildCodecDag(['A'], []);
const linearChain = buildCodecDag(
  ['A', 'B', 'C', 'D'],
  [
    ['A', 'B'],
    ['B', 'C'],
    ['C', 'D'],
  ],
);
const diamond = buildCodecDag(
  ['A', 'B', 'C', 'D'],
  [
    ['A', 'B'],
    ['A', 'C'],
    ['B', 'D'],
    ['C', 'D'],
  ],
);
const branchingTree = buildCodecDag(
  ['A', 'B', 'C', 'D', 'E', 'F'],
  [
    ['A', 'B'],
    ['A', 'E'],
    ['B', 'C'],
    ['B', 'D'],
    ['E', 'F'],
  ],
);
const disconnected = buildCodecDag(
  ['A', 'B', 'C', 'D'],
  [
    ['A', 'B'],
    ['C', 'D'],
  ],
);

describe('Test DAG ordering', () => {
  it('single node returns single element', () => {
    expect(singleNode.dag.dagOrder().map((c) => c.name)).toEqual(['A']);
  });

  it('linear chain orders A->B->C->D', () => {
    expect(linearChain.dag.dagOrder().map((c) => c.name)).toEqual(['A', 'B', 'C', 'D']);
  });

  it('diamond places A first, D last, B/C middle', () => {
    const order = diamond.dag.dagOrder().map((c) => c.name);
    expect(order[0]).toBe('A');
    expect(order[3]).toBe('D');
    expect(new Set(order.slice(1, 3))).toEqual(new Set(['B', 'C']));
  });

  it('branching tree respects parent-before-child', () => {
    const order = branchingTree.dag.dagOrder().map((c) => c.name);
    expect(order.indexOf('A')).toBeLessThan(order.indexOf('B'));
    expect(order.indexOf('A')).toBeLessThan(order.indexOf('E'));
    expect(order.indexOf('B')).toBeLessThan(order.indexOf('C'));
    expect(order.indexOf('B')).toBeLessThan(order.indexOf('D'));
    expect(order.indexOf('E')).toBeLessThan(order.indexOf('F'));
  });

  it('disconnected components include all nodes with intra-component ordering', () => {
    const order = disconnected.dag.dagOrder().map((c) => c.name);
    expect(order).toHaveLength(4);
    expect(order.indexOf('A')).toBeLessThan(order.indexOf('B'));
    expect(order.indexOf('C')).toBeLessThan(order.indexOf('D'));
  });
});

describe('Test reverse DAG order', () => {
  it('single node returns single element', () => {
    expect(singleNode.dag.reverseDagOrder().map((c) => c.name)).toEqual(['A']);
  });

  it('linear chain reverses correctly', () => {
    expect(linearChain.dag.reverseDagOrder().map((c) => c.name)).toEqual(['D', 'C', 'B', 'A']);
  });
});

describe('Test DAG children', () => {
  it('single node has no children', () => {
    const A = singleNode.codecs.A;
    expect(singleNode.dag.getChildren(A)).toEqual([]);
  });

  it('linear chain A has child B', () => {
    const A = linearChain.codecs.A;
    expect(linearChain.dag.getChildren(A).map((c) => c.name)).toEqual(['B']);
  });

  it('diamond root has children B and C', () => {
    const A = diamond.codecs.A;
    expect(
      diamond.dag
        .getChildren(A)
        .map((c) => c.name)
        .sort(),
    ).toEqual(['B', 'C']);
  });

  it('branching tree children are correct', () => {
    const {A, B} = branchingTree.codecs;
    expect(
      branchingTree.dag
        .getChildren(A)
        .map((c) => c.name)
        .sort(),
    ).toEqual(['B', 'E']);
    expect(
      branchingTree.dag
        .getChildren(B)
        .map((c) => c.name)
        .sort(),
    ).toEqual(['C', 'D']);
  });
});

describe('Test DAG parents', () => {
  it('single node has no parents', () => {
    const A = singleNode.codecs.A;
    expect(singleNode.dag.getParents(A)).toEqual([]);
  });

  it('linear chain D has parent C', () => {
    const D = linearChain.codecs.D;
    expect(linearChain.dag.getParents(D).map((c) => c.name)).toEqual(['C']);
  });

  it('diamond merge has parents B and C', () => {
    const D = diamond.codecs.D;
    expect(
      diamond.dag
        .getParents(D)
        .map((c) => c.name)
        .sort(),
    ).toEqual(['B', 'C']);
  });

  it('branching tree F has parent E', () => {
    const F = branchingTree.codecs.F;
    expect(branchingTree.dag.getParents(F).map((c) => c.name)).toEqual(['E']);
  });
});
