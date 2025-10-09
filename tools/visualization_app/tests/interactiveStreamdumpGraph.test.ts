// Copyright (c) Meta Platforms, Inc. and affiliates.

// /data/users/agandevia/fbsource/fbcode/data_compression/experimental/sd3/streamdump/visualization-app/tests/sample.test.ts

import {describe, it, expect} from 'vitest';
import {InteractiveStreamdumpGraph} from '../src/graphVisualization/models/InteractiveStreamdumpGraph';
import {Streamdump} from '../src/models/Streamdump';
import {Stream} from '../src/models/Stream';
import {Codec} from '../src/models/Codec';
import {Graph} from '../src/models/Graph';
import {LocalParamInfo} from '../src/models/LocalParamInfo';
import {ZL_Type, ZL_GraphType} from '../src/models/idTypes';
import {createSimpleTreeWithGraph, createBranchingTreeWithGraph} from './createTestModels';

describe('Test interactive streamdump graph creation', () => {
  it('Should initialize with a chain of codecs A->B->C->D where B and C are in a graph', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    // Test that thge model graph was created properly
    const {nodes, edges, graphs} = interactiveGraph.getVisibleStreamdumpGraph();
    expect(nodes.length).toBe(4);
    expect(edges.length).toBe(3);
    expect(graphs.length).toBe(1);

    // Test that the edges connect the correct nodes
    expect(edges.some((edge) => edge.source === 'T0' && edge.target === 'T1')).toBe(true); // A->B
    expect(edges.some((edge) => edge.source === 'T1' && edge.target === 'T2')).toBe(true); // B->C
    expect(edges.some((edge) => edge.source === 'T2' && edge.target === 'T3')).toBe(true); // C->D

    // Test that the graph contains the correct codecs
    expect(graphs[0].codecIds).toContain('T1'); // Graph contains codec B
    expect(graphs[0].codecIds).toContain('T2'); // Graph contains codec C
    expect(graphs[0].codecIds).not.toContain('T0'); // Graph doesn't contain codec A
    expect(graphs[0].codecIds).not.toContain('T3'); // Graph doesn't contain codec D

    // Test collapsing the graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    const {nodes: collapsedNodes, graphs: collapsedGraphs} = interactiveGraph.getVisibleStreamdumpGraph();

    // After collapsing, should have 2 nodes (codecs A and D) and 1 collapsed graph
    expect(collapsedNodes.length).toBe(2);
    expect(collapsedNodes.some((node) => node.id === 'T0')).toBe(true); // Codec A is still visible
    expect(collapsedNodes.some((node) => node.id === 'T3')).toBe(true); // Codec D is still visible
    expect(collapsedGraphs[0].isCollapsed).toBe(true);
  });
});

describe('Test collapsing and expanding a graph', () => {
  it('Should collapse and expand a graph, preserving its successors upon collapse and preserving its codecs upon expand', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {nodes, edges, graphs} = interactiveGraph.getVisibleStreamdumpGraph();

    // Test collapsing the graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    let {
      nodes: visibleNodes,
      edges: visibleEdges,
      graphs: visibleGraphs,
    } = interactiveGraph.getVisibleStreamdumpGraph();

    // After collapsing, should have 2 nodes (codecs A and D), 1 collapsed graph, and 2 edges (A -> collapsedGraph -> D)
    expect(visibleNodes.length).toBe(2);
    expect(visibleNodes.some((node) => node.id === 'T0')).toBe(true); // Codec A is still visible
    expect(visibleNodes.some((node) => node.id === 'T3')).toBe(true); // Codec D is still visible
    expect(visibleGraphs[0].isCollapsed).toBe(true);
    expect(visibleEdges[0].source === 'T0').toBe(true);
    expect(visibleEdges[0].target === 'G0').toBe(true);
    expect(visibleEdges[1].source === 'G0').toBe(true);
    expect(visibleEdges[1].target === 'T3').toBe(true);

    // Test expanding the graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    ({nodes: visibleNodes, edges: visibleEdges, graphs: visibleGraphs} = interactiveGraph.getVisibleStreamdumpGraph());
    expect(visibleNodes.length).toBe(4);
    expect(visibleGraphs.length).toBe(1);
    expect(visibleEdges.length).toBe(3);
    expect(visibleGraphs[0].isCollapsed).toBe(false);
  });
});

describe('Test preserving collapsed descendant node', () => {
  it('Should collapse a node, collapse an ancestor of the collapsed node, and upon expanding the ancestor, the descendant should be collapse', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {nodes, edges, graphs} = interactiveGraph.getVisibleStreamdumpGraph();

    // collapse a node with an ancestor
    interactiveGraph.toggleSubgraphCollapse(nodes[2]);
    // Collapse the ancestor
    interactiveGraph.toggleSubgraphCollapse(nodes[0]);
    let {
      nodes: visibleNodes,
      edges: visibleEdges,
      graphs: visibleGraphs,
    } = interactiveGraph.getVisibleStreamdumpGraph();
    expect(visibleNodes.length).toBe(1);
    expect(visibleGraphs.length).toBe(0);
    expect(visibleEdges.length).toBe(0);

    // Expand the ancestor
    interactiveGraph.toggleSubgraphCollapse(nodes[0]);
    ({nodes: visibleNodes, edges: visibleEdges, graphs: visibleGraphs} = interactiveGraph.getVisibleStreamdumpGraph());
    expect(visibleNodes.length).toBe(3);
    expect(visibleGraphs.length).toBe(1);
    expect(visibleEdges.length).toBe(2);

    // Expand the descendant
    interactiveGraph.toggleSubgraphCollapse(nodes[2]);
    ({nodes: visibleNodes, edges: visibleEdges, graphs: visibleGraphs} = interactiveGraph.getVisibleStreamdumpGraph());
    expect(visibleNodes.length).toBe(4);
    expect(visibleGraphs.length).toBe(1);
    expect(visibleEdges.length).toBe(3);
  });
});

describe('Test preserving collapsed descendant graph', () => {
  it('Should collapse a graph, collapse an ancestor of the graph, and upon expanding the ancestor, the graph should still be collapsed', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {nodes, edges, graphs} = interactiveGraph.getVisibleStreamdumpGraph();

    // collapse the graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    // collapse an ancestor node of the graph
    interactiveGraph.toggleSubgraphCollapse(nodes[0]);
    let {
      nodes: visibleNodes,
      edges: visibleEdges,
      graphs: visibleGraphs,
    } = interactiveGraph.getVisibleStreamdumpGraph();
    expect(visibleNodes.length).toBe(1);
    expect(visibleGraphs.length).toBe(0);
    expect(visibleEdges.length).toBe(0);

    // Expand ancestor node
    interactiveGraph.toggleSubgraphCollapse(nodes[0]);
    ({nodes: visibleNodes, edges: visibleEdges, graphs: visibleGraphs} = interactiveGraph.getVisibleStreamdumpGraph());
    expect(visibleNodes.length).toBe(2); // Ancestor node and the successor of the graph
    expect(visibleGraphs.length).toBe(1);
    expect(visibleGraphs[0].isCollapsed).toBe(true);
    expect(visibleEdges.length).toBe(2);
  });
});

describe('Test preserving collapsed node in collapsed graph', () => {
  it('Should collapse a node within a graph, collapse and expand the graph, and see if the internal collapsed node is still collapsed', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {nodes, edges, graphs} = interactiveGraph.getVisibleStreamdumpGraph();

    // Collapse a node within the graph
    interactiveGraph.toggleSubgraphCollapse(nodes[2]);
    // Collapse the graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    let {
      nodes: visibleNodes,
      edges: visibleEdges,
      graphs: visibleGraphs,
    } = interactiveGraph.getVisibleStreamdumpGraph();
    expect(visibleNodes.length).toBe(1);
    expect(visibleGraphs.length).toBe(1);
    expect(visibleEdges.length).toBe(1);

    // Expand graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    ({nodes: visibleNodes, edges: visibleEdges, graphs: visibleGraphs} = interactiveGraph.getVisibleStreamdumpGraph());
    expect(visibleNodes.length).toBe(3);
    expect(visibleGraphs.length).toBe(1);
    expect(visibleEdges.length).toBe(2);
    expect(nodes[2].isCollapsed).toBe(true);
    expect(nodes[3].isCollapsed).toBe(false);
  });
});

describe('Test expanding 1 level of a graph', () => {
  it('Should expand a graph by just 1 level', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {nodes, edges, graphs} = interactiveGraph.getVisibleStreamdumpGraph();

    // Collapse child to root node
    interactiveGraph.toggleSubgraphCollapse(nodes[1]);
    let {
      nodes: visibleNodes,
      edges: visibleEdges,
      graphs: visibleGraphs,
    } = interactiveGraph.getVisibleStreamdumpGraph();
    expect(visibleNodes.length).toBe(2); // Just the root node, and the collapsed child node
    expect(visibleEdges.length).toBe(1);
    expect(visibleGraphs.length).toBe(1);

    interactiveGraph.expandOneLevel(nodes[1]);
    ({nodes: visibleNodes, edges: visibleEdges, graphs: visibleGraphs} = interactiveGraph.getVisibleStreamdumpGraph());
    expect(visibleNodes.length).toBe(3); // Root node, the child node, and the grandchild node
    expect(visibleEdges.length).toBe(2);
    expect(visibleGraphs.length).toBe(1);
  });
});

describe('Test expanding 1 level of a graph with child collapsed graph', () => {
  it('Should expand a graph by just 1 level, amd the child codec is in a collapsed graph, so the level expansion should should the collapsed graph', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {nodes, edges, graphs} = interactiveGraph.getVisibleStreamdumpGraph();
    // Collapse the graph
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    interactiveGraph.toggleSubgraphCollapse(nodes[0]);

    let {
      nodes: visibleNodes,
      edges: visibleEdges,
      graphs: visibleGraphs,
    } = interactiveGraph.getVisibleStreamdumpGraph();
    expect(visibleNodes.length).toBe(1);
    expect(visibleEdges.length).toBe(0);
    expect(visibleGraphs.length).toBe(0);

    // Expand the collapsed node by 1 level
    interactiveGraph.expandOneLevel(nodes[0]);
    ({nodes: visibleNodes, edges: visibleEdges, graphs: visibleGraphs} = interactiveGraph.getVisibleStreamdumpGraph());
    expect(visibleNodes.length).toBe(1);
    expect(visibleEdges.length).toBe(1);
    expect(visibleGraphs.length).toBe(1);
    expect(visibleGraphs[0].isCollapsed).toBe(true);

    // Expand the collapsed graph, the root node of the graph should be visible but collapsed
    interactiveGraph.toggleGraphCollapse(graphs[0]);
    ({nodes: visibleNodes, edges: visibleEdges, graphs: visibleGraphs} = interactiveGraph.getVisibleStreamdumpGraph());
    expect(visibleNodes.length).toBe(2);
    expect(visibleEdges.length).toBe(1);
    expect(visibleGraphs.length).toBe(1);
    expect(visibleNodes[0].isCollapsed).toBe(false);
    expect(visibleNodes[1].isCollapsed).toBe(true);
  });
});

describe('Collapse/Expanding all standard graphs', () => {
  it('Should test default standard graph collapsed state, where successors are also hidden', () => {
    const interactiveGraph = createSimpleTreeWithGraph(true); // Default collapse all standard graphs
    const {nodes, edges, graphs} = interactiveGraph.getVisibleStreamdumpGraph();
    expect(nodes.length).toBe(1);
    expect(edges.length).toBe(1);
    expect(graphs.length).toBe(1);
    expect(graphs[0].isCollapsed).toBe(true);

    // Expand all standard graphs
    interactiveGraph.toggleAllStandardGraphs(false);
    const {
      nodes: visibleNodes,
      edges: visibleEdges,
      graphs: visibleGraphs,
    } = interactiveGraph.getVisibleStreamdumpGraph();
    expect(visibleNodes.length).toBe(2);
    expect(visibleEdges.length).toBe(1);
    expect(visibleGraphs.length).toBe(1);
  });
});

describe('Collapse/Expanding all standard graphs with all hidden', () => {
  it('Attempts to collapse/expand all standard graphs when they are all hidden, should do nothing', () => {
    const interactiveGraph = createSimpleTreeWithGraph();
    const {nodes, edges, graphs} = interactiveGraph.getVisibleStreamdumpGraph();
    // Collapse root node to hide standard graph
    interactiveGraph.toggleSubgraphCollapse(nodes[0]);
    let {
      nodes: visibleNodes,
      edges: visibleEdges,
      graphs: visibleGraphs,
    } = interactiveGraph.getVisibleStreamdumpGraph();
    expect(visibleGraphs.length).toBe(0);

    // Attempt to collapse all standard graphs
    interactiveGraph.toggleAllStandardGraphs(true);
    ({nodes: visibleNodes, edges: visibleEdges, graphs: visibleGraphs} = interactiveGraph.getVisibleStreamdumpGraph());
    expect(visibleGraphs.length).toBe(0);

    // Attempt to expand all standard graphs
    interactiveGraph.toggleAllStandardGraphs(false);
    ({nodes: visibleNodes, edges: visibleEdges, graphs: visibleGraphs} = interactiveGraph.getVisibleStreamdumpGraph());
    expect(visibleGraphs.length).toBe(0);
  });
});

describe('Collapse/Expanding all standard graphs with no standard graphs', () => {
  it('Attempts to collapse/expand all standard graphs when there are no standard graphs, should do nothing', () => {
    const interactiveGraph = createBranchingTreeWithGraph();
    // Collapse all standard graphs
    interactiveGraph.toggleAllStandardGraphs(true);
    const {nodes, edges, graphs} = interactiveGraph.getVisibleStreamdumpGraph();
    expect(graphs.length).toBe(2);
    expect(graphs[0].isCollapsed && graphs[1].isCollapsed).toBe(false);
  });
});

describe('Test the largest compression path', () => {
  it('Creates a graph, and ensures that the largest compression path is the correct path', () => {
    const interactiveGraph = createBranchingTreeWithGraph();
    const {nodes, edges, graphs} = interactiveGraph.getVisibleStreamdumpGraph();
    // A -> B -> C is the largest compression path
    expect(nodes[0].inLargestCompressionPath).toBe(true); // A
    expect(nodes[1].inLargestCompressionPath).toBe(true); // B
    expect(nodes[2].inLargestCompressionPath).toBe(true); // C
    expect(nodes[3].inLargestCompressionPath).toBe(false); // D
    expect(nodes[4].inLargestCompressionPath).toBe(false); // E
    expect(nodes[5].inLargestCompressionPath).toBe(false); // F
  });
});
