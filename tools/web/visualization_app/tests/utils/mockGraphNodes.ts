// Copyright (c) Meta Platforms, Inc. and affiliates.

import {vi} from 'vitest';
import {act} from '@testing-library/react';
import {InternalCodecNode} from '../../src/graphVisualization/models/InternalCodecNode';
import {InternalGraphNode} from '../../src/graphVisualization/models/InternalGraphNode';
import type {InternalNode} from '../../src/graphVisualization/models/InternalNode';
import {NodeType} from '../../src/graphVisualization/models/types';
import type {useStreamdumpGraphController} from '../../src/graphVisualization/controllers/StreamdumpGraphController';
import type {Node} from '@xyflow/react';

/**
 * Create a mock codec node with the proper prototype chain
 */
export function mockCodecNode(
  rfid: string,
  name?: string,
  children: string[] = [],
  parents: string[] = [],
  isCollapsed = true,
): InternalNode {
  const node = {
    rfid,
    name: name ?? rfid,
    type: NodeType.Codec,
    isCollapsed,
    children: [...children],
    parents: [...parents],
    isVisible: true,
    sortedChildren: [...children],
  };
  Object.setPrototypeOf(node, InternalCodecNode.prototype);
  return node as unknown as InternalNode;
}

/**
 * Create a mock graph node with the proper prototype chain
 */
export function mockGraphNode(
  rfid: string,
  name?: string,
  isCollapsed = true,
  codecs: InternalCodecNode[] = [],
): InternalNode {
  const node = {
    rfid,
    name: name ?? rfid,
    type: NodeType.Graph,
    isCollapsed,
    children: [],
    parents: [],
    isVisible: true,
    codecs,
    sortedChildren: [],
  };
  Object.setPrototypeOf(node, InternalGraphNode.prototype);
  return node as unknown as InternalNode;
}

export function mockSegmenterNode(rfid: string, name?: string): InternalNode {
  return {
    rfid,
    name: name ?? rfid,
    type: NodeType.Segmenter,
    isCollapsed: true,
    children: [],
    parents: [],
    isVisible: true,
    sortedChildren: [],
  } as unknown as InternalNode;
}

/**
 * Wrap an InternalNode in a React Flow Node, as the hooks expect
 */
export function createRFNode(id: string, internalNode: InternalNode, type = NodeType.Codec): Node {
  return {
    id,
    type,
    position: {x: 0, y: 0},
    data: {internalNode},
    hidden: false,
  };
}

/**
 * Dispatch a keydown event to window and wait for React state updates.
 */
export async function pressKey(
  key: string,
  options: {shiftKey?: boolean; ctrlKey?: boolean; altKey?: boolean} = {},
): Promise<void> {
  const event = new KeyboardEvent('keydown', {
    key,
    bubbles: true,
    cancelable: true,
    ...options,
  });
  // Bypass input-focus guard – tests that need to verify the guard should
  // use userEvent with a focused input element instead.
  Object.defineProperty(event, 'target', {value: document.body, enumerable: true});
  // jsdom's KeyboardEvent has a read-only preventDefault.
  Object.defineProperty(event, 'preventDefault', {value: vi.fn()});
  window.dispatchEvent(event);
  // Allow React state updates to flush.
  await new Promise((resolve) => setTimeout(resolve, 0));
}

/**
 * Mock for @xyflow/react useReactFlow. Provides a no-op fitView since the
 * animation timing is irrelevant to unit tests.
 */
export function mockUseReactFlow() {
  return {
    useReactFlow: () => ({
      fitView: vi.fn(),
    }),
  };
}

/** Single start one-node tree. */
export function buildSingleNodeTree(id = 'start'): Node[] {
  return [createRFNode(id, mockCodecNode(id, 'zl.#start'))];
}

/**
 * Three unconnected nodes of different types — a collapsed codec, an expanded
 * graph, and a segmenter (not a tree)
 */
export function buildMixedNodeTypes(): Node[] {
  return [
    createRFNode('codec1', mockCodecNode('codec1'), NodeType.Codec),
    createRFNode('graph1', mockGraphNode('graph1', 'Graph', false), NodeType.Graph),
    createRFNode('seg1', mockSegmenterNode('seg1'), NodeType.Segmenter),
  ];
}

/**
 * Tree with start codec linked to a single collapsed child
 */
export function buildLinearTree(): Node[] {
  const child = mockCodecNode('child', 'Child');
  const root = mockCodecNode('root', 'zl.#start', ['child'], []);
  (child as unknown as {parents: string[]}).parents = ['root'];
  return [createRFNode('root', root), createRFNode('child', child)];
}

/**
 * Tree with start codec  nested inside an expanded, visible graph
 */
export function buildNestedGraph(): {nodes: Node[]; parentGraph: InternalNode} {
  const parentGraph = mockGraphNode('graph1', 'MyGraph', false);
  (parentGraph as unknown as {isVisible: boolean}).isVisible = true;
  const codec = mockCodecNode('codec1', 'zl.#start');
  (codec as unknown as {parentGraph: InternalNode}).parentGraph = parentGraph;
  (codec as unknown as {isCollapsed: boolean}).isCollapsed = false;
  return {
    nodes: [createRFNode('codec1', codec, NodeType.Codec), createRFNode('graph1', parentGraph, NodeType.Graph)],
    parentGraph,
  };
}

/**
 * Tree containing start codec with three ordered sibling children
 */
export function buildFanOutTree(): Node[] {
  const left = mockCodecNode('left', 'Left');
  const mid = mockCodecNode('mid', 'Mid');
  const right = mockCodecNode('right', 'Right');
  const parent = mockCodecNode('parent', 'zl.#start', ['left', 'mid', 'right'], []);
  (parent as unknown as {sortedChildren: string[]}).sortedChildren = ['left', 'mid', 'right'];
  (left as unknown as {parents: string[]}).parents = ['parent'];
  (mid as unknown as {parents: string[]}).parents = ['parent'];
  (right as unknown as {parents: string[]}).parents = ['parent'];
  return [
    createRFNode('parent', parent),
    createRFNode('left', left),
    createRFNode('mid', mid),
    createRFNode('right', right),
  ];
}

// Helpers for asserting on / driving the React Flow nodes

type Ctrl = ReturnType<typeof useStreamdumpGraphController>;
export type RFNode = Ctrl['nodes'][number];

// Visible codec nodes.
export const codecNodes = (nodes: RFNode[]) => nodes.filter((n) => n.type === NodeType.Codec);

// Names of the visible codec nodes.
export const codecNames = (nodes: RFNode[]) =>
  codecNodes(nodes).map((n) => (n.data.internalNode as InternalCodecNode).name);

// Find the first node of a given type, optionally matching its backing model
// node's name. Returns undefined if none match.
export const findNode = (nodes: RFNode[], type: NodeType, name?: string) =>
  nodes.find(
    (n) => n.type === type && (name === undefined || (n.data.internalNode as InternalCodecNode).name === name),
  );

// Fire a codec node's collapse/expand toggle handler inside act().
export function toggleCodec(node: RFNode) {
  const handler = node.data.onToggleCollapse as (n: InternalCodecNode) => void;
  act(() => handler(node.data.internalNode as InternalCodecNode));
}
// Fire a graph node's collapse toggle handler inside act().
export function toggleGraph(node: RFNode) {
  const handler = node.data.onToggleGraphCollapse as (n: InternalGraphNode) => void;
  act(() => handler(node.data.internalNode as InternalGraphNode));
}
