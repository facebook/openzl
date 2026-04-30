// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useCallback, useEffect, useMemo, useReducer, useRef} from 'react';
import {useReactFlow} from '@xyflow/react';
import type {Node} from '@xyflow/react';
import {toaster} from '../../App';
import {InternalCodecNode} from '../models/InternalCodecNode';
import {InternalGraphNode} from '../models/InternalGraphNode';
import type {InternalNode} from '../models/InternalNode';
import {NodeType, type RF_nodeId} from '../models/types';

type KeyboardNavState = {
  isActive: boolean;
  selectedNode: InternalNode | null;
};

type KeyboardNavAction =
  | {type: 'ACTIVATE'; node: InternalNode}
  | {type: 'DEACTIVATE'}
  | {type: 'SELECT_NODE'; node: InternalNode};

// use reducer to manage state and dispatch actions
function reducer(state: KeyboardNavState, action: KeyboardNavAction): KeyboardNavState {
  switch (action.type) {
    case 'ACTIVATE':
      return {isActive: true, selectedNode: action.node};
    case 'DEACTIVATE':
      return {isActive: false, selectedNode: null};
    case 'SELECT_NODE':
      return state.isActive ? {...state, selectedNode: action.node} : state;
  }
}

export interface KeyboardNavControls {
  selectedNodeId: RF_nodeId | null;
  activate: () => void;
  deactivate: () => void;
}

export function useKeyboardNavigation(
  nodes: Node[],
  handleGraphCollapse: (node: InternalGraphNode) => void,
  handleNodeCollapse: (node: InternalCodecNode) => void,
  getCodecChildren: (codec: InternalCodecNode) => InternalCodecNode[],
): KeyboardNavControls {
  const [state, dispatch] = useReducer(reducer, {
    isActive: false,
    selectedNode: null,
  });

  const reactFlow = useReactFlow();
  const nodesRef = useRef(nodes);
  nodesRef.current = nodes;

  const stateRef = useRef(state);
  stateRef.current = state;

  const keyboardDrivenChangeRef = useRef(false);

  const handleGraphCollapseRef = useRef(handleGraphCollapse);
  const handleNodeCollapseRef = useRef(handleNodeCollapse);
  const getCodecChildrenRef = useRef(getCodecChildren);
  handleGraphCollapseRef.current = handleGraphCollapse;
  handleNodeCollapseRef.current = handleNodeCollapse;
  getCodecChildrenRef.current = getCodecChildren;

  // Lookup table from  RF_nodeId to InternalNode
  const nodeMap = useMemo(() => {
    const map = new Map<RF_nodeId, InternalNode>();
    for (const n of nodes) {
      if (!n.hidden && n.data?.internalNode) {
        map.set(n.id as RF_nodeId, n.data.internalNode as InternalNode);
      }
    }
    return map;
  }, [nodes]);

  const nodeMapRef = useRef(nodeMap);
  nodeMapRef.current = nodeMap;
  // Selectable nodes: codecs and collapsed graphs (pill-shaped).
  // Non-selectable: segmenters and expanded graphs (dotted containers).
  const isSelectable = (node: InternalNode): boolean => {
    if (node.type === NodeType.Codec) return true;
    if (node.type === NodeType.Graph && node.isCollapsed) return true;
    return false;
  };

  // Follow nav links, skipping non-selectable nodes.
  const resolveToSelectable = useCallback(
    (rfid: RF_nodeId | undefined, direction: 'up' | 'down'): InternalNode | null => {
      if (!rfid) return null;
      const node = nodeMapRef.current.get(rfid);
      if (!node) return null;
      if (isSelectable(node)) return node;
      const next = direction === 'down' ? node.children[0] : node.parents[0];
      return resolveToSelectable(next, direction);
    },
    [],
  );

  const findNeighbor = useCallback(
    (direction: 'up' | 'down' | 'left' | 'right' | 'tab' | 'enter', modifier?: 'shift' | null): InternalNode | null => {
      const currentNode = state.selectedNode;
      if (!currentNode) return null;

      switch (direction) {
        case 'down':
          return resolveToSelectable(currentNode.children[0], 'down');
        case 'up':
          return resolveToSelectable(currentNode.parents[0], 'up');
        case 'tab': {
          // Tab: navigate siblings sorted by stream share
          if (currentNode.parents.length === 0) return null;
          const parent = nodeMapRef.current.get(currentNode.parents[0]);
          if (!parent) return null;

          const sortedSiblings = parent.sortedChildren;
          // if 0 or 1 sorted sibling this is trivial
          if (sortedSiblings.length <= 1) return null;

          const currentIndex = sortedSiblings.indexOf(currentNode.rfid);
          if (currentIndex === -1) return null;

          // tab = next smallest, shift+tab = next largest
          let nextIndex = modifier === 'shift' ? currentIndex - 1 : currentIndex + 1;
          if (nextIndex < 0) {
            nextIndex = sortedSiblings.length - 1;
          }
          if (nextIndex >= sortedSiblings.length) {
            nextIndex = 0;
          }

          return nodeMapRef.current.get(sortedSiblings[nextIndex]) ?? null;
        }
        case 'right':
        case 'left': {
          // Left/Right: move among siblings (children of same parent)
          if (currentNode.parents.length === 0) return null;
          const parent = nodeMapRef.current.get(currentNode.parents[0]);
          if (!parent) return null;

          const siblings = parent.children;
          if (siblings.length <= 1) return null;

          const currentIndex = siblings.indexOf(currentNode.rfid);
          if (currentIndex === -1) return null;

          const nextIndex = direction === 'right' ? currentIndex + 1 : currentIndex - 1;
          if (nextIndex < 0 || nextIndex >= siblings.length) return null;

          return nodeMapRef.current.get(siblings[nextIndex]) ?? null;
        }
        case 'enter':
        default:
          return null;
      }
    },
    [state.selectedNode, resolveToSelectable],
  );

  const activate = useCallback(() => {
    const startRfNode = nodesRef.current.find(
      (n) =>
        !n.hidden &&
        n.data?.internalNode &&
        ((n.data.internalNode as {name?: string}).name === 'zl.#start' ||
          (n.data.internalNode as {name?: string}).name === 'zl.#regen'),
    );
    if (startRfNode) {
      dispatch({type: 'ACTIVATE', node: startRfNode.data?.internalNode as InternalNode});
      reactFlow.fitView({
        nodes: [{id: startRfNode.id}],
        duration: 800,
        padding: 0.2,
        maxZoom: 0.75,
      });
    } else {
      toaster.create({
        title: 'Cannot activate keyboard shortcuts',
        description: 'No start node (zl.#start) found.',
        type: 'warning',
        duration: 3000,
      });
    }
  }, [reactFlow]);

  useEffect(() => {
    if (!state.isActive) return;

    const handleKeyDown = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement;
      if (
        target.tagName === 'INPUT' ||
        target.tagName === 'TEXTAREA' ||
        target.tagName === 'SELECT' ||
        target.isContentEditable
      ) {
        // Graph naviagtion fires only when focus on graph canvas itself
        return;
      }

      const directionMap: Record<string, 'up' | 'down' | 'left' | 'right' | 'tab' | 'enter'> = {
        ArrowUp: 'up',
        ArrowDown: 'down',
        ArrowLeft: 'left',
        ArrowRight: 'right',
        Tab: 'tab',
        Enter: 'enter',
      };

      const direction = directionMap[e.key];
      if (!direction) return;
      e.preventDefault();

      const tryExpand = () => {
        const currentNode = state.selectedNode;
        if (!currentNode || !currentNode.isCollapsed) return;

        if (currentNode instanceof InternalGraphNode) {
          const firstCodec = currentNode.codecs[0];
          // cannot expand collapsed graph node if there is no codec nodes in it
          if (firstCodec == null) return;
          keyboardDrivenChangeRef.current = true;
          handleGraphCollapseRef.current(currentNode);
          // skip non-selectable expanded graph node
          if (firstCodec) dispatch({type: 'SELECT_NODE', node: firstCodec});
        } else if (currentNode instanceof InternalCodecNode) {
          keyboardDrivenChangeRef.current = true;
          handleNodeCollapseRef.current(currentNode);
        }
      };

      const tryCollapse = () => {
        const currentNode = state.selectedNode;
        if (!currentNode) return;

        // Codec is expanded
        if (!currentNode.isCollapsed) {
          if (currentNode instanceof InternalCodecNode) {
            keyboardDrivenChangeRef.current = true;
            if (currentNode.parentGraph && !currentNode.parentGraph.isCollapsed) {
              // Collapse the parent graph (symmetric with Enter expand, matches eye icon)
              handleGraphCollapseRef.current(currentNode.parentGraph);
              dispatch({type: 'SELECT_NODE', node: currentNode.parentGraph});
            } else {
              // No parent graph, collapse the subgraph
              handleNodeCollapseRef.current(currentNode);
            }
          }
          return;
        }

        // Node is already collapsed bubble up and collapse its parent graph,
        // then move selection to the now-collapsed parent.
        if (
          currentNode instanceof InternalCodecNode &&
          currentNode.parentGraph &&
          !currentNode.parentGraph.isCollapsed
        ) {
          // This is because we cannot directly navigate to a expanded graph node so we will use any child of expanded graph to collapse it
          keyboardDrivenChangeRef.current = true;
          handleGraphCollapseRef.current(currentNode.parentGraph);
          dispatch({type: 'SELECT_NODE', node: currentNode.parentGraph});
        }
      };

      const modifier: 'shift' | null = e.shiftKey ? 'shift' : null;
      const neighbor = findNeighbor(direction, modifier);

      if (direction === 'enter' && state.selectedNode && modifier === 'shift') {
        tryCollapse();
      } else if (direction === 'enter' && state.selectedNode) {
        tryExpand();
      } else if (!neighbor && direction === 'down' && state.selectedNode?.isCollapsed) {
        tryExpand();
      } else if (neighbor) {
        dispatch({type: 'SELECT_NODE', node: neighbor});
        reactFlow.fitView({
          nodes: [{id: neighbor.rfid}],
          duration: 100,
          padding: 0.2,
          maxZoom: 0.75,
        });
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [state.isActive, state.selectedNode, findNeighbor, reactFlow]);

  // When a user manually hides or expands a subgraph we want the keyboard nav to follow
  useEffect(() => {
    if (keyboardDrivenChangeRef.current) {
      keyboardDrivenChangeRef.current = false;
      return;
    }
    const {isActive, selectedNode} = stateRef.current;
    if (!isActive || !selectedNode) return;

    if (isSelectable(selectedNode) && nodeMapRef.current.has(selectedNode.rfid)) return;

    // If user expands a graph manually
    if (selectedNode instanceof InternalGraphNode && !selectedNode.isCollapsed) {
      const firstCodec = selectedNode.codecs[0];
      if (firstCodec?.isVisible) {
        dispatch({type: 'SELECT_NODE', node: firstCodec});
        return;
      }
    }

    // If node is codec in expanded graph, and graph was manually collapsed
    if (
      selectedNode instanceof InternalCodecNode &&
      selectedNode.parentGraph?.isVisible &&
      selectedNode.parentGraph.isCollapsed
    ) {
      dispatch({type: 'SELECT_NODE', node: selectedNode.parentGraph});
      return;
    }

    // Fallback: selected node is gone (e.g. segmenter changed chunks), select start node
    const startRfNode = nodesRef.current.find(
      (n) =>
        !n.hidden &&
        n.data?.internalNode &&
        ((n.data.internalNode as {name?: string}).name === 'zl.#start' ||
          (n.data.internalNode as {name?: string}).name === 'zl.#regen'),
    );
    if (startRfNode?.data?.internalNode) {
      dispatch({type: 'SELECT_NODE', node: startRfNode.data.internalNode as InternalNode});
    }
  }, [nodes]);

  const deactivate = useCallback(() => dispatch({type: 'DEACTIVATE'}), []);

  return useMemo(
    () => ({
      selectedNodeId: state.selectedNode?.rfid ?? null,
      activate,
      deactivate,
    }),
    [state.selectedNode?.rfid, activate, deactivate],
  );
}
