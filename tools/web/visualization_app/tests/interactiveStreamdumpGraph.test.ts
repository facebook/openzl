// Copyright (c) Meta Platforms, Inc. and affiliates.

// /data/users/agandevia/fbsource/fbcode/data_compression/experimental/sd3/streamdump/visualization-app/tests/sample.test.ts

import {describe, it, expect} from 'vitest';
import {
  createSimpleTreeWithGraph,
  createBranchingTreeWithGraph,
  createSingleNodeGraph,
  createDiamondGraph,
  createCollapsableDiamondGraph,
  createMultiChunkGraph,
} from './utils/createTestModels';
import {InternalGraphNode} from '../src/graphVisualization/models/InternalGraphNode';
import {getGraphDetails as getInteractiveGraphDetails} from './utils/createTestModels';

describe('Test interactive streamdump graph creation', () => {
  it('Should initialize with a chain of codecs A->B->C->D where B and C are in a graph', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    // Test that the model graph was created properly
    const {dagOrderedNodes, edges} = interactiveGraph.getVisibleStreamdumpGraph();

    expect(edges.length).toBe(3);
    expect(dagOrderedNodes.length).toBe(5);

    // Test that the edges connect the correct nodes
    expect(edges.some((edge) => edge.source.rfid === 'C0-T0' && edge.target.rfid === 'C0-T1')).toBe(true); // A->B
    expect(edges.some((edge) => edge.source.rfid === 'C0-T1' && edge.target.rfid === 'C0-T2')).toBe(true); // B->C
    expect(edges.some((edge) => edge.source.rfid === 'C0-T2' && edge.target.rfid === 'C0-T3')).toBe(true); // C->D

    // Extract graph nodes from dagOrderedNodes
    const graphs = dagOrderedNodes.filter((node): node is InternalGraphNode => node instanceof InternalGraphNode);
    // Test that the graph contains the correct codecs
    const graphCodecIds = graphs[0].codecs.map((c) => c.id);
    expect(graphCodecIds).toContain(1); // Graph contains codec B
    expect(graphCodecIds).toContain(2); // Graph contains codec C
    expect(graphCodecIds).not.toContain(0); // Graph doesn't contain codec A
    expect(graphCodecIds).not.toContain(3); // Graph doesn't contain codec D

    // Test collapsing the graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    const {dagOrderedNodes: collapsedNodes} = interactiveGraph.getVisibleStreamdumpGraph();
    const collapsedGraphs = collapsedNodes.filter(
      (node): node is InternalGraphNode => node instanceof InternalGraphNode,
    );
    const collapsedCodecs = collapsedNodes.filter((node) => !(node instanceof InternalGraphNode));

    // After collapsing, should have 2 codec nodes (A and D) and 1 collapsed graph
    expect(collapsedCodecs.length).toBe(2);
    expect(collapsedCodecs.some((node) => node.rfid === 'C0-T0')).toBe(true); // Codec A is still visible
    expect(collapsedCodecs.some((node) => node.rfid === 'C0-T3')).toBe(true); // Codec D is still visible
    expect(collapsedGraphs[0].isCollapsed).toBe(true);
  });
});

describe('Test collapsing and expanding a graph', () => {
  it('Should collapse and expand a graph, preserving its successors upon collapse and preserving its codecs upon expand', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {graphs} = getInteractiveGraphDetails(interactiveGraph);

    // Test collapsing the graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    let {nodes, graphs: visibleGraphs, edges} = getInteractiveGraphDetails(interactiveGraph);

    // After collapsing, should have 3 visible nodes (codec A, collapsed graph, codec D) and 2 edges (A -> collapsedGraph -> D)
    expect(nodes.length).toBe(3);
    expect(nodes.some((node) => node.rfid === 'C0-T0')).toBe(true); // Codec A is still visible
    expect(nodes.some((node) => node.rfid === 'C0-T3')).toBe(true); // Codec D is still visible

    expect(visibleGraphs[0].isCollapsed).toBe(true);

    expect(edges[0].source.rfid === 'C0-T0').toBe(true);
    expect(edges[0].target.rfid === 'C0-G0').toBe(true);
    expect(edges[1].source.rfid === 'C0-G0').toBe(true);
    expect(edges[1].target.rfid === 'C0-T3').toBe(true);

    // Test expanding the graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    ({nodes, graphs: visibleGraphs, edges} = getInteractiveGraphDetails(interactiveGraph));

    expect(nodes.length).toBe(5);
    expect(visibleGraphs.length).toBe(1);
    expect(edges.length).toBe(3);
    expect(visibleGraphs[0].isCollapsed).toBe(false);
  });
});

describe('Test preserving collapsed descendant node', () => {
  it('Should collapse a node, collapse an ancestor of the collapsed node, and upon expanding the ancestor, the descendant should be collapse', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {codecs} = getInteractiveGraphDetails(interactiveGraph);

    // collapse a node with an ancestor (CodecC then CodecA)
    interactiveGraph.toggleSubgraphCollapse(codecs[2]);
    interactiveGraph.toggleSubgraphCollapse(codecs[0]);
    let {nodes, graphs, edges} = getInteractiveGraphDetails(interactiveGraph);

    expect(nodes.length).toBe(1);
    expect(graphs.length).toBe(0);
    expect(edges.length).toBe(0);

    // Expand the ancestor
    interactiveGraph.toggleSubgraphCollapse(codecs[0]);
    ({nodes, graphs, edges} = getInteractiveGraphDetails(interactiveGraph));

    expect(nodes.length).toBe(4);
    expect(graphs.length).toBe(1);
    expect(edges.length).toBe(2);

    // Expand the descendant
    interactiveGraph.toggleSubgraphCollapse(codecs[2]);
    ({nodes, graphs, edges} = getInteractiveGraphDetails(interactiveGraph));

    expect(nodes.length).toBe(5);
    expect(graphs.length).toBe(1);
    expect(edges.length).toBe(3);
  });
});

describe('Test preserving collapsed descendant graph', () => {
  it('Should collapse a graph, collapse an ancestor of the graph, and upon expanding the ancestor, the graph should still be collapsed', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {codecs, graphs} = getInteractiveGraphDetails(interactiveGraph);

    // collapse the graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    // collapse an ancestor node of the graph
    interactiveGraph.toggleSubgraphCollapse(codecs[0]);
    let {nodes, graphs: visibleGraphs, edges} = getInteractiveGraphDetails(interactiveGraph);
    expect(nodes.length).toBe(1);
    expect(visibleGraphs.length).toBe(0);
    expect(edges.length).toBe(0);

    // Expand ancestor node
    interactiveGraph.toggleSubgraphCollapse(codecs[0]);
    ({nodes, graphs: visibleGraphs, edges} = getInteractiveGraphDetails(interactiveGraph));
    expect(nodes.length).toBe(3); // Codec A, collapsed graph, and codec D
    expect(visibleGraphs.length).toBe(1);
    expect(visibleGraphs[0].isCollapsed).toBe(true);
    expect(edges.length).toBe(2);
  });
});

describe('Test preserving collapsed node in collapsed graph', () => {
  it('Should collapse a node within a graph, collapse and expand the graph, and see if the internal collapsed node is still collapsed', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {codecs, graphs} = getInteractiveGraphDetails(interactiveGraph);

    // Collapse a node within the graph
    interactiveGraph.toggleSubgraphCollapse(codecs[2]);
    // Collapse the graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    let {nodes, graphs: visibleGraphs, edges} = getInteractiveGraphDetails(interactiveGraph);

    expect(nodes.length).toBe(2);
    expect(visibleGraphs.length).toBe(1);
    expect(edges.length).toBe(1);

    // Expand graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    ({nodes, graphs: visibleGraphs, edges} = getInteractiveGraphDetails(interactiveGraph));
    expect(nodes.length).toBe(4);
    expect(visibleGraphs.length).toBe(1);
    expect(edges.length).toBe(2);
    expect(codecs[2].isCollapsed).toBe(true);
    expect(codecs[3].isCollapsed).toBe(false);
  });
});

describe('Test expanding 1 level of a graph', () => {
  it('Should expand a graph by just 1 level', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {codecs} = getInteractiveGraphDetails(interactiveGraph);

    // Collapse child to root node
    interactiveGraph.toggleSubgraphCollapse(codecs[1]);
    let {nodes, graphs, edges} = getInteractiveGraphDetails(interactiveGraph);
    expect(nodes.length).toBe(3); // Root node, collapsed child node, and graph node
    expect(edges.length).toBe(1);
    expect(graphs.length).toBe(1);

    interactiveGraph.expandOneLevel(codecs[1]);
    ({nodes, graphs, edges} = getInteractiveGraphDetails(interactiveGraph));

    expect(nodes.length).toBe(4); // Root node, child node, grandchild node, and graph node
    expect(edges.length).toBe(2);
    expect(graphs.length).toBe(1);
  });
});

describe('Test expanding 1 level of a graph with child collapsed graph', () => {
  it('Should expand a graph by just 1 level, amd the child codec is in a collapsed graph, so the level expansion should should the collapsed graph', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {codecs, graphs} = getInteractiveGraphDetails(interactiveGraph);

    // Collapse the graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    interactiveGraph.toggleSubgraphCollapse(codecs[0]);

    let {nodes, graphs: visibleGraphs, edges} = getInteractiveGraphDetails(interactiveGraph);

    expect(nodes.length).toBe(1);
    expect(edges.length).toBe(0);
    expect(visibleGraphs.length).toBe(0);

    // Expand the collapsed node by 1 level
    interactiveGraph.expandOneLevel(codecs[0]);
    ({nodes, graphs: visibleGraphs, edges} = getInteractiveGraphDetails(interactiveGraph));

    expect(nodes.length).toBe(2);
    expect(edges.length).toBe(1);
    expect(visibleGraphs.length).toBe(1);
    expect(visibleGraphs[0].isCollapsed).toBe(true);

    // Expand the collapsed graph, the root node of the graph should be visible but collapsed
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    ({nodes, graphs: visibleGraphs, edges} = getInteractiveGraphDetails(interactiveGraph));

    expect(nodes.length).toBe(3);
    expect(edges.length).toBe(1);
    expect(visibleGraphs.length).toBe(1);
    expect(codecs[0].isCollapsed).toBe(false);
    expect(codecs[1].isCollapsed).toBe(true);
  });
});

describe('Collapse/Expanding all standard graphs', () => {
  it('Should test default standard graph collapsed state, where successors are also hidden', () => {
    const interactiveGraph = createSimpleTreeWithGraph(true); // Default collapse all standard graphs
    let {nodes, graphs, edges} = getInteractiveGraphDetails(interactiveGraph);

    expect(nodes.length).toBe(2);
    expect(edges.length).toBe(1);
    expect(graphs.length).toBe(1);
    expect(graphs[0].isCollapsed).toBe(true);

    // Expand all standard graphs
    interactiveGraph.toggleAllStandardGraphs(false);
    ({nodes, graphs, edges} = getInteractiveGraphDetails(interactiveGraph));
    expect(nodes.length).toBe(3);
    expect(edges.length).toBe(2);
    expect(graphs.length).toBe(1);
  });
});

describe('Collapse/Expanding all standard graphs with all hidden', () => {
  it('Attempts to collapse/expand all standard graphs when they are all hidden, should do nothing', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {codecs} = getInteractiveGraphDetails(interactiveGraph);

    // Collapse root node to hide standard graph
    interactiveGraph.toggleSubgraphCollapse(codecs[0]);
    let {graphs} = getInteractiveGraphDetails(interactiveGraph);

    expect(graphs.length).toBe(0);

    // Attempt to collapse all standard graphs
    interactiveGraph.toggleAllStandardGraphs(true);
    ({graphs} = getInteractiveGraphDetails(interactiveGraph));
    expect(graphs.length).toBe(0);

    // Attempt to expand all standard graphs
    interactiveGraph.toggleAllStandardGraphs(false);
    ({graphs} = getInteractiveGraphDetails(interactiveGraph));
    expect(graphs.length).toBe(0);
  });
});

describe('Collapse/Expanding all standard graphs with no standard graphs', () => {
  it('Attempts to collapse/expand all standard graphs when there are no standard graphs, should do nothing', () => {
    const interactiveGraph = createBranchingTreeWithGraph();
    // Collapse all standard graphs
    interactiveGraph.toggleAllStandardGraphs(true);
    const {graphs} = getInteractiveGraphDetails(interactiveGraph);

    expect(graphs.length).toBe(2);
    expect(graphs[0].isCollapsed && graphs[1].isCollapsed).toBe(false);
  });
});

describe('Test the largest compression path', () => {
  it('Creates a graph, and ensures that the largest compression path is the correct path', () => {
    const interactiveGraph = createBranchingTreeWithGraph();
    const {codecs} = getInteractiveGraphDetails(interactiveGraph);
    const codecByName = (name: string) => codecs.find((c) => c.name === name)!;

    // A -> B -> C is the largest compression path (DAG order is not ID order)
    expect(codecByName('Root').inLargestCompressionPath).toBe(true); // A
    expect(codecByName('LeftBranch').inLargestCompressionPath).toBe(true); // B
    expect(codecByName('LeftLeaf1').inLargestCompressionPath).toBe(true); // C
    expect(codecByName('LeftLeaf2').inLargestCompressionPath).toBe(false); // D
    expect(codecByName('RightBranch').inLargestCompressionPath).toBe(false); // E
    expect(codecByName('RightLeaf').inLargestCompressionPath).toBe(false); // F
  });
});

describe('Test multi-chunk segmenter merging', () => {
  it('should merge chunk 0 segmenter with chunk 1', () => {
    const interactiveGraph = createMultiChunkGraph();
    // Initially only chunk 0 visible: segmenter -> CodecA0
    let {nodes, edges, codecs} = getInteractiveGraphDetails(interactiveGraph);
    expect(nodes.length).toBe(2);
    expect(edges.length).toBe(1);

    // Set visible chunk to 1: segmenter merges into chunk 1, zl.#start omitted
    interactiveGraph.setVisibleChunk(1);
    ({nodes, edges, codecs} = getInteractiveGraphDetails(interactiveGraph));
    expect(nodes.length).toBe(4); // segmenter, CodecA0, CodecB1, CodecC1
    expect(codecs.map((c) => c.name)).not.toContain('zl.#start'); // omitted
    expect(edges.length).toBe(3);
    // segmenter (C0-T0) replaces zl.#start as CodecB1's (C1-T1) parent
    expect(edges.some((e) => e.source.rfid === 'C0-T0' && e.target.rfid === 'C1-T1')).toBe(true);

    // Set back to null (only chunk 0)
    interactiveGraph.setVisibleChunk(null);
    ({nodes, edges} = getInteractiveGraphDetails(interactiveGraph));
    expect(nodes.length).toBe(2);
    expect(edges.length).toBe(1);
  });
});

describe('Diamond topology tests', () => {
  it('diamond graph has correct topological order and child ordering', () => {
    const interactiveGraph = createDiamondGraph();
    const {codecs, edges, nodes} = getInteractiveGraphDetails(interactiveGraph);

    expect(nodes.length).toBe(4);
    expect(edges.length).toBe(4);

    // Topological order: Root first, Merge last, Left/Right in between
    const names = codecs.map((c) => c.name);
    expect(names[0]).toBe('Root');
    expect(names[3]).toBe('Merge');
    expect(new Set(names.slice(1, 3))).toEqual(new Set(['Left', 'Right']));

    interactiveGraph.buildAllNavlinks();
    const root = codecs.find((c) => c.name === 'Root')!;

    // sortedChildren orders by share: Left (60%) before Right (40%)
    const sortedChildNames = root.sortedChildren.map((rfid) => codecs.find((c) => c.rfid === rfid)?.name);
    expect(sortedChildNames).toEqual(['Left', 'Right']);
  });

  it('diamond with graph collapses to proxy edges', () => {
    const interactiveGraph = createCollapsableDiamondGraph();
    const {graphs} = getInteractiveGraphDetails(interactiveGraph);
    expect(graphs.length).toBe(1);
    // Graph contains Left (C0-T1) and Right (C0-T2), rfid = C0-G0
    expect(graphs[0].rfid).toBe('C0-G0');

    interactiveGraph.toggleGraphCollapse(graphs[0]);
    let {nodes, edges, graphs: collapsedGraphs} = getInteractiveGraphDetails(interactiveGraph);
    expect(collapsedGraphs[0].isCollapsed).toBe(true);
    expect(nodes.length).toBe(3); // Root, collapsed graph, Merge
    expect(edges.length).toBe(2);

    // Verify proxy edges replace internal codecs: Root -> graph -> Merge
    expect(edges.some((e) => e.source.rfid === 'C0-T0' && e.target.rfid === 'C0-G0')).toBe(true); // Root -> graph
    expect(edges.some((e) => e.source.rfid === 'C0-G0' && e.target.rfid === 'C0-T3')).toBe(true); // graph -> Merge

    // Expand back
    interactiveGraph.toggleGraphCollapse(collapsedGraphs[0]);
    ({nodes, edges, graphs: collapsedGraphs} = getInteractiveGraphDetails(interactiveGraph));
    expect(collapsedGraphs[0].isCollapsed).toBe(false);
    expect(nodes.length).toBe(5); // Root, Left, Right, Merge, graph node
    expect(edges.length).toBe(4);
  });
});

describe('Single node edge case', () => {
  it('single node graph renders with no edges', () => {
    const interactiveGraph = createSingleNodeGraph();
    const {nodes, edges, codecs} = getInteractiveGraphDetails(interactiveGraph);
    expect(nodes.length).toBe(1);
    expect(edges.length).toBe(0);
    expect(codecs[0].name).toBe('SingleNode');
  });
});
