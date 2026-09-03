// Copyright (c) Meta Platforms, Inc. and affiliates.
// @vitest-environment jsdom

import {describe, it, expect, vi, beforeEach, afterEach} from 'vitest';
import {renderHook, act, cleanup} from '@testing-library/react';
import {useKeyboardNavigation} from '../../src/graphVisualization/controllers/useKeyboardShortcuts';
import {
  pressKey,
  buildSingleNodeTree,
  buildMixedNodeTypes,
  buildLinearTree,
  buildNestedGraph,
  buildFanOutTree,
} from '../utils/mockGraphNodes';

// Mock React Flow
vi.mock('@xyflow/react', async () => {
  const actual = await vi.importActual('@xyflow/react');
  const {mockUseReactFlow} = await import('../utils/mockGraphNodes');
  return {...actual, ...mockUseReactFlow()};
});

// Mock toaster
vi.mock('../../src/App', () => ({
  toaster: {create: vi.fn()},
}));

function createKeyboardNavMocks() {
  return {
    handleGraphCollapse: vi.fn(),
    handleNodeCollapse: vi.fn(),
    getCodecChildren: vi.fn(() => []),
  };
}

describe('Test keyboard drives graph state', () => {
  const {handleGraphCollapse, handleNodeCollapse, getCodecChildren} = createKeyboardNavMocks();

  beforeEach(() => {
    vi.clearAllMocks();
  });

  // Unmount rendered hooks after each test so their window 'keydown' listeners
  // are removed; otherwise leaked listeners from earlier tests fire on later
  // dispatched key events.
  afterEach(() => {
    cleanup();
  });

  it('activate finds start node (zl.#start) and selects it', () => {
    const nodes = buildSingleNodeTree();

    const {result} = renderHook(() =>
      useKeyboardNavigation(nodes, handleGraphCollapse, handleNodeCollapse, getCodecChildren),
    );

    act(() => {
      result.current.activate();
    });

    expect(result.current.selectedNodeId).toBe('start');
  });

  it('selectNode respects isActive guard and rejects non-selectable nodes', () => {
    const nodes = buildMixedNodeTypes();

    const {result} = renderHook(() =>
      useKeyboardNavigation(nodes, handleGraphCollapse, handleNodeCollapse, getCodecChildren),
    );

    // Not active – selectNode should be no-op
    act(() => {
      result.current.selectNode('codec1' as any);
    });
    expect(result.current.selectedNodeId).toBeNull();

    // Activate with a start node present
    const nodesWithStart = [...nodes, ...buildSingleNodeTree('start2')];
    const {result: result2} = renderHook(() =>
      useKeyboardNavigation(nodesWithStart, handleGraphCollapse, handleNodeCollapse, getCodecChildren),
    );

    act(() => {
      result2.current.activate();
    });
    expect(result2.current.selectedNodeId).toBe('start2');

    // Try to select expanded graph – should be rejected (non-selectable)
    act(() => {
      result2.current.selectNode('graph1' as any);
    });
    expect(result2.current.selectedNodeId).toBe('start2');

    // Try to select segmenter – should be rejected
    act(() => {
      result2.current.selectNode('seg1' as any);
    });
    expect(result2.current.selectedNodeId).toBe('start2');

    // Select codec – should work
    act(() => {
      result2.current.selectNode('codec1' as any);
    });
    expect(result2.current.selectedNodeId).toBe('codec1');
  });

  it('ArrowDown navigates to child, Enter expands collapsed node', async () => {
    const nodes = buildLinearTree();

    const {result} = renderHook(() =>
      useKeyboardNavigation(nodes, handleGraphCollapse, handleNodeCollapse, getCodecChildren),
    );

    act(() => {
      result.current.activate();
    });
    expect(result.current.selectedNodeId).toBe('root');

    // Press ArrowDown – should navigate to child
    await act(async () => {
      await pressKey('ArrowDown');
    });
    expect(result.current.selectedNodeId).toBe('child');

    // Press Enter – should expand collapsed node
    await act(async () => {
      await pressKey('Enter');
    });

    expect(handleNodeCollapse).toHaveBeenCalledTimes(1);
    expect(handleNodeCollapse).toHaveBeenCalledWith(expect.objectContaining({rfid: 'child'}));
  });

  it('Shift+Enter collapses node, selection follows parent graph', async () => {
    const {nodes, parentGraph} = buildNestedGraph();

    const localHandleGraphCollapse = vi.fn((g: any) => {
      g.isCollapsed = true;
    });

    const {result} = renderHook(() =>
      useKeyboardNavigation(nodes, localHandleGraphCollapse, handleNodeCollapse, getCodecChildren),
    );

    act(() => {
      result.current.activate();
    });
    expect(result.current.selectedNodeId).toBe('codec1');

    // Press Shift+Enter – should collapse parent graph
    await act(async () => {
      await pressKey('Enter', {shiftKey: true});
    });

    expect(localHandleGraphCollapse).toHaveBeenCalledTimes(1);
    expect(localHandleGraphCollapse).toHaveBeenCalledWith(parentGraph);
    expect(result.current.selectedNodeId).toBe('graph1');
  });

  it('Tab cycles siblings left-to-right with wrap-around', async () => {
    const nodes = buildFanOutTree();

    const {result} = renderHook(() =>
      useKeyboardNavigation(nodes, handleGraphCollapse, handleNodeCollapse, getCodecChildren),
    );

    act(() => {
      result.current.activate();
    });
    act(() => {
      result.current.selectNode('mid' as any);
    });
    expect(result.current.selectedNodeId).toBe('mid');

    // Press Tab – should move to right sibling
    await act(async () => {
      await pressKey('Tab');
    });

    expect(result.current.selectedNodeId).toBe('right');
  });

  it('deactivate clears selection state', () => {
    const nodes = buildSingleNodeTree();

    const {result} = renderHook(() =>
      useKeyboardNavigation(nodes, handleGraphCollapse, handleNodeCollapse, getCodecChildren),
    );

    act(() => {
      result.current.activate();
    });
    expect(result.current.selectedNodeId).toBe('start');

    act(() => {
      result.current.deactivate();
    });
    expect(result.current.selectedNodeId).toBeNull();
  });
});
