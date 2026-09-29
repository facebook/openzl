// Copyright (c) Meta Platforms, Inc. and affiliates.
// @vitest-environment jsdom

import {describe, it, expect, beforeEach, afterEach, vi} from 'vitest';
import {renderHook, act} from '@testing-library/react';
import {useStreamdumpGraphController} from '../../src/graphVisualization/controllers/StreamdumpGraphController';
import {NodeType} from '../../src/graphVisualization/models/types';
import {
  createSimpleTreeStreamdump,
  createDiamondStreamdump,
  createMultiChunkStreamdump,
} from '../utils/createTestModels';
import {codecNodes, codecNames, findNode, toggleCodec, toggleGraph} from '../utils/mockGraphNodes';
import type {InternalCodecNode} from '../../src/graphVisualization/models/InternalCodecNode';

vi.mock('@xyflow/react', async () => {
  const actual = await vi.importActual('@xyflow/react');
  const reactFlowInstance = {fitView: vi.fn()};
  return {...actual, useReactFlow: () => reactFlowInstance};
});

vi.mock('../../src/App', () => ({toaster: {create: vi.fn()}}));

describe('Test controller orchestrates graph state from streamdump data', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    // Fake timers so the controller's deferred fitView() animations don't fire
    // after the hook unmounts.
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.clearAllTimers();
    vi.useRealTimers();
  });

  it('produces no nodes/edges when there is no data', () => {
    const {result} = renderHook(() => useStreamdumpGraphController({data: null}));

    expect(result.current.nodes).toEqual([]);
    expect(result.current.edges).toEqual([]);
  });

  it('builds nodes and edges from streamdump data on mount', () => {
    const data = createSimpleTreeStreamdump();
    const {result} = renderHook(() => useStreamdumpGraphController({data}));

    expect(result.current.nodes.length).toBeGreaterThan(0);
    expect(result.current.edges.length).toBeGreaterThan(0);
    // Every node carries its backing internal model node.
    for (const node of result.current.nodes) {
      expect(node.data.internalNode).toBeDefined();
    }
  });

  it('Test that node specific handler are correctly specified and do not leak ', () => {
    const data = createSimpleTreeStreamdump();
    const {result} = renderHook(() => useStreamdumpGraphController({data}));

    const codecs = result.current.nodes.filter((n) => n.type === NodeType.Codec);
    const graphs = result.current.nodes.filter((n) => n.type === NodeType.Graph);
    const edgeNodes = result.current.nodes.filter((n) => n.type === NodeType.Edge);

    for (const node of codecs) {
      expect(typeof node.data.onToggleCollapse).toBe('function');
      expect(typeof node.data.expandOneLevel).toBe('function');
      expect(typeof node.data.onCtrlClick).toBe('function');
      // Graph-only handler must not leak onto codecs.
      expect(node.data.onToggleGraphCollapse).toBeUndefined();
    }

    for (const node of graphs) {
      expect(typeof node.data.onToggleGraphCollapse).toBe('function');
      expect(typeof node.data.onCtrlClick).toBe('function');
      // Codec-only handlers must not leak onto graphs.
      expect(node.data.onToggleCollapse).toBeUndefined();
      expect(node.data.expandOneLevel).toBeUndefined();
    }

    expect(edgeNodes.length).toBeGreaterThan(0);
    for (const node of edgeNodes) {
      expect(node.data.onToggleCollapse).toBeUndefined();
      expect(node.data.onToggleGraphCollapse).toBeUndefined();
      expect(node.data.onSetVisibleChunk).toBeUndefined();
    }
  });

  it('collapsing a codec hides its descendants; expanding restores them', () => {
    const data = createDiamondStreamdump(); // Root -> Left/Right -> Merge, no graphs
    const {result} = renderHook(() => useStreamdumpGraphController({data}));

    // visible codec node names start out as the full diamond
    expect(codecNames(result.current.nodes).sort()).toEqual(['Left', 'Merge', 'Right', 'Root']);

    // collapse the root: every descendant disappears, leaving just the root
    toggleCodec(findNode(result.current.nodes, NodeType.Codec, 'Root')!);
    expect(codecNames(result.current.nodes)).toEqual(['Root']);

    // expand the root again (same handler toggles it back): all descendants return
    toggleCodec(findNode(result.current.nodes, NodeType.Codec, 'Root')!);
    expect(codecNames(result.current.nodes).sort()).toEqual(['Left', 'Merge', 'Right', 'Root']);
  });

  it('expandOneLevel reveals only the immediate children of a collapsed codec', () => {
    const data = createDiamondStreamdump(); // Root -> Left/Right -> Merge
    const {result} = renderHook(() => useStreamdumpGraphController({data}));

    // collapse the root so only Root is visible
    toggleCodec(findNode(result.current.nodes, NodeType.Codec, 'Root')!);
    expect(codecNames(result.current.nodes)).toEqual(['Root']);

    // expand one level: only the direct children (Left, Right) appear; Merge
    // stays hidden because Left/Right are re-collapsed
    act(() => {
      const root = findNode(result.current.nodes, NodeType.Codec, 'Root')!;
      (root.data.expandOneLevel as (n: InternalCodecNode) => void)(root.data.internalNode as InternalCodecNode);
    });
    expect(codecNames(result.current.nodes).sort()).toEqual(['Left', 'Right', 'Root']);
  });

  it('toggling a graph node hides then restores its member codecs', () => {
    const data = createSimpleTreeStreamdump(); // GraphBC starts collapsed
    const {result} = renderHook(() => useStreamdumpGraphController({data}));

    const collapsedCodecCount = codecNodes(result.current.nodes).length;
    // a graph node should be present
    expect(findNode(result.current.nodes, NodeType.Graph)).toBeDefined();

    // expand the standard graph via its toggle handler: grouped codecs appear
    toggleGraph(findNode(result.current.nodes, NodeType.Graph)!);
    expect(codecNodes(result.current.nodes).length).toBeGreaterThan(collapsedCodecCount);

    // collapse it again: the member codecs disappear back into the graph node
    toggleGraph(findNode(result.current.nodes, NodeType.Graph)!);
    expect(codecNodes(result.current.nodes).length).toBe(collapsedCodecCount);
  });

  it('selecting a chunk merges that chunk’s codecs into the visible graph', () => {
    const data = createMultiChunkStreamdump(); // chunk 1: zl.#start -> CodecB1 -> CodecC1
    const {result} = renderHook(() => useStreamdumpGraphController({data}));

    // only chunk 0 is visible initially (codec node names)
    expect(codecNames(result.current.nodes)).toContain('CodecA0');
    expect(codecNames(result.current.nodes)).not.toContain('CodecB1');

    // select chunk 1 via the segmenter's handler
    act(() => {
      const segmenter = findNode(result.current.nodes, NodeType.Segmenter)!;
      (segmenter.data.onSetVisibleChunk as (n: number) => void)(1);
    });

    // chunk 1's codecs are now merged in alongside chunk 0's segmenter
    expect(codecNames(result.current.nodes)).toContain('CodecB1');
    expect(codecNames(result.current.nodes)).toContain('CodecC1');
    // the segmenter (chunk selector) is still present
    expect(findNode(result.current.nodes, NodeType.Segmenter)).toBeDefined();
  });

  it('handleAllStandardGraphsCollapse flips the collapsed flag', () => {
    const data = createSimpleTreeStreamdump();
    const {result} = renderHook(() => useStreamdumpGraphController({data}));

    const before = result.current.areStandardGraphsCollapsed;
    act(() => {
      result.current.handleAllStandardGraphsCollapse();
    });

    expect(result.current.areStandardGraphsCollapsed).toBe(!before);
  });

  it('exposes keyboard navigation controls wired to the graph', () => {
    const data = createSimpleTreeStreamdump();
    const {result} = renderHook(() => useStreamdumpGraphController({data}));

    expect(typeof result.current.keyboardNav.activate).toBe('function');
    expect(typeof result.current.keyboardNav.deactivate).toBe('function');
    expect(typeof result.current.keyboardNav.selectNode).toBe('function');
    expect(result.current.keyboardNav.selectedNodeId).toBeNull();
  });
});
