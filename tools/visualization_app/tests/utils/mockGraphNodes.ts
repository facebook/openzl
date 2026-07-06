// Copyright (c) Meta Platforms, Inc. and affiliates.

import {act} from '@testing-library/react';
import {InternalCodecNode} from '../../src/graphVisualization/models/InternalCodecNode';
import {InternalGraphNode} from '../../src/graphVisualization/models/InternalGraphNode';
import {NodeType} from '../../src/graphVisualization/models/types';
import type {useStreamdumpGraphController} from '../../src/graphVisualization/controllers/StreamdumpGraphController';

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
