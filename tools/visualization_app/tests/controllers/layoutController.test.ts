// Copyright (c) Meta Platforms, Inc. and affiliates.
import {describe, it, expect} from 'vitest';
import {applyLayout, sortNavlinksByPosition} from '../../src/graphVisualization/controllers/LayoutController';
import {
  createSimpleTreeWithGraph,
  createDiamondGraph,
  createCollapsableDiamondGraph,
  createBranchingTreeWithGraph,
  createSingleNodeGraph,
} from '../utils/createTestModels';
import {getGraphDetails, layoutGraph} from '../utils/createTestModels';
import type {InternalNode} from '../../src/graphVisualization/models/InternalNode';
import {NodeType} from '../../src/graphVisualization/models/types';

describe('Test LayoutController convertToReactFlowElements', () => {
  it('expanded GraphNode is non-draggable, collapsed GraphNode is draggable', () => {
    // Graph nodes that are expanded act as containers and should not be draggable
    const interactiveGraph = createSimpleTreeWithGraph(false);
    const {nodes: positionedNodes} = layoutGraph(interactiveGraph);

    const graphNode = positionedNodes.find((n) => n.type === NodeType.Graph);
    expect(graphNode?.className).toBe('nodrag');

    // After collapsing, the graph behaves like a normal codec node
    const {graphs} = getGraphDetails(interactiveGraph);
    interactiveGraph.toggleGraphCollapse(graphs[0]);

    const {nodes: collapsedPositioned} = layoutGraph(interactiveGraph);
    const collapsedGraphNode = collapsedPositioned.find((n) => n.type === NodeType.Graph);
    expect(collapsedGraphNode?.className).toBeUndefined();
  });

  it('each InternalEdge becomes a React Flow node with in/out edges', () => {
    // Edges are rendered as nodes so they get proper spacing in the layout
    const interactiveGraph = createSimpleTreeWithGraph();
    const {edges: internalEdges} = getGraphDetails(interactiveGraph);
    const {nodes: positionedNodes, edges: reactFlowEdges} = layoutGraph(interactiveGraph);

    const edgeNodes = positionedNodes.filter((n) => n.type === NodeType.Edge);
    expect(edgeNodes).toHaveLength(internalEdges.length);

    for (const internalEdge of internalEdges) {
      const edgeNode = edgeNodes.find((n) => n.id === internalEdge.rfid);
      expect(edgeNode).toBeDefined();

      // Each edge node has an incoming edge (source codec -> edge node)
      // and an outgoing edge (edge node -> target codec)
      const inEdge = reactFlowEdges.find((e) => e.id === `${internalEdge.rfid}-in`);
      const outEdge = reactFlowEdges.find((e) => e.id === `${internalEdge.rfid}-out`);

      expect(inEdge?.source).toBe(internalEdge.source.rfid);
      expect(inEdge?.target).toBe(internalEdge.rfid);
      expect(outEdge?.source).toBe(internalEdge.rfid);
      expect(outEdge?.target).toBe(internalEdge.target.rfid);
    }
  });
});

describe('Test LayoutController calculateLayout', () => {
  it('expanded graph contains its codecs', () => {
    const interactiveGraph = createSimpleTreeWithGraph(false);
    let {graphs} = getGraphDetails(interactiveGraph);

    const {nodes: positionedNodes} = layoutGraph(interactiveGraph);
    const graphNode = positionedNodes.find((n) => n.type === NodeType.Graph)!;

    const expandedGraphWidth = (graphNode.style as any)?.width as number;
    const expandedGraphHeight = (graphNode.style as any)?.height as number;

    // Child codecs should lie inside the graph bounding box
    const codecB = positionedNodes.find((n) => n.id === 'C0-T1')!;
    const gx = graphNode.position.x;
    const gy = graphNode.position.y;

    expect(codecB.position.x).toBeGreaterThanOrEqual(gx);
    expect(codecB.position.x).toBeLessThanOrEqual(gx + expandedGraphWidth);
    expect(codecB.position.y).toBeGreaterThanOrEqual(gy);
    expect(codecB.position.y).toBeLessThanOrEqual(gy + expandedGraphHeight);

    // Collapsed graph should be less than expanded graph w/h
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    const {nodes: collapsedPositioned} = layoutGraph(interactiveGraph);
    const collapsedGraphNode = collapsedPositioned.find((n) => n.type === NodeType.Graph)!;

    expect((collapsedGraphNode.style as any)?.width).toBeLessThanOrEqual(expandedGraphWidth);
    expect((collapsedGraphNode.style as any)?.height).toBeLessThanOrEqual(expandedGraphHeight);
  });

  it('diamond graph lays out without crashing', () => {
    const interactiveGraph = createDiamondGraph();
    const {nodes: positionedNodes} = layoutGraph(interactiveGraph);

    const edgeNodes = positionedNodes.filter((n) => n.type === NodeType.Edge);
    expect(edgeNodes.length).toBeGreaterThan(0);

    for (const edgeNode of edgeNodes) {
      expect(Number.isFinite(edgeNode.position.x)).toBe(true);
      expect(Number.isFinite(edgeNode.position.y)).toBe(true);
      expect(edgeNode.position.x !== 0 || edgeNode.position.y !== 0).toBe(true);
    }
  });

  it('single node graph lays out without crashing', () => {
    const interactiveGraph = createSingleNodeGraph();
    const {nodes: positionedNodes, edges: rfEdges} = layoutGraph(interactiveGraph);

    expect(positionedNodes).toHaveLength(1);
    expect(rfEdges).toHaveLength(0);
    expect(Number.isFinite(positionedNodes[0].position.x)).toBe(true);
    expect(Number.isFinite(positionedNodes[0].position.y)).toBe(true);
  });
});

describe('Test LayoutController sortNavlinksByPosition', () => {
  it('sorts navigable nodes left-to-right and skips Edge nodes', () => {
    const interactiveGraph = createCollapsableDiamondGraph();
    interactiveGraph.buildAllNavlinks();

    const {nodes, edges, codecs} = getGraphDetails(interactiveGraph);
    const {nodes: positionedNodes} = applyLayout(nodes, edges);

    // Scramble root's children to ensure the sort actually does work
    const root = codecs.find((c) => c.name === 'Root')!;
    root.children.reverse();

    // Full scan must not crash on Edge-type nodes
    expect(() => sortNavlinksByPosition(positionedNodes)).not.toThrow();

    // Left child (higher share) should be leftmost after sort
    const leftRfid = codecs.find((c) => c.name === 'Left')!.rfid;
    const rightRfid = codecs.find((c) => c.name === 'Right')!.rfid;

    expect(root.children[0]).toBe(leftRfid);
    expect(root.children[1]).toBe(rightRfid);

    // Verify x positions match the ordering
    const leftNode = positionedNodes.find((n) => n.id === leftRfid)!;
    const rightNode = positionedNodes.find((n) => n.id === rightRfid)!;
    expect(leftNode.position.x).toBeLessThanOrEqual(rightNode.position.x);
  });

  it('sorts only the specified changedNodes when provided', () => {
    const interactiveGraph = createBranchingTreeWithGraph();
    interactiveGraph.buildAllNavlinks();

    const {nodes, edges, codecs} = getGraphDetails(interactiveGraph);
    const {nodes: positionedNodes} = applyLayout(nodes, edges);

    const root = codecs.find((c) => c.name === 'Root')!;
    const leftBranch = codecs.find((c) => c.name === 'LeftBranch')!;

    // Scramble both nodes' children
    root.children.reverse();
    leftBranch.children.reverse();

    const leftChildrenBefore = [...leftBranch.children];

    // Sort only root
    sortNavlinksByPosition(positionedNodes, [root as InternalNode]);
    expect(leftBranch.children).toEqual(leftChildrenBefore);

    // Now sort LeftBranch too
    sortNavlinksByPosition(positionedNodes, [leftBranch as InternalNode]);
    expect(leftBranch.children).not.toEqual(leftChildrenBefore);
  });
});
