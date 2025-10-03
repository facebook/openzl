// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useCallback, useState, useEffect, useMemo} from 'react';
import {useNodesState, useEdgesState, useReactFlow} from '@xyflow/react';
import type {Node, Edge} from '@xyflow/react';
import {InteractiveStreamdumpGraph} from '../models/InteractiveStreamdumpGraph';
import {LayoutController} from './LayoutController';
import type {NullableStreamdump} from '../../interfaces/NullableStreamdump';
import {InternalCodecNode} from '../models/InternalCodecNode';
import {InternalGraphNode} from '../models/InternalGraphNode';
import {NodeType, type RF_nodeId} from '../models/types';

const STANDARD_GRAPHS_START_COLLAPSED = true; // Default state of graph visualization upon loading data

export function useStreamdumpGraphController({data}: NullableStreamdump) {
  // Initializing the graph model
  const [interactiveStreamdumpGraph, setInteractiveStreamdumpGraph] = useState<InteractiveStreamdumpGraph | null>(null);
  // Initializing the nodes and edges
  const [nodes, setNodes, onNodesChange] = useNodesState<Node>([]);
  const [edges, setEdges, onEdgesChange] = useEdgesState<Edge>([]);
  // Standard graphs toggle
  const [areStandardGraphsCollapsed, toggleStandardGraphsCollapsedFlag] = useState(false);
  // React Flow instance for viewport control for the animations
  const reactFlowInstance = useReactFlow();

  // Function to handle a subgraph (rooted at a codec node) collapsing/expanding
  const handleNodeCollapse = useCallback(
    (node: InternalCodecNode) => {
      if (interactiveStreamdumpGraph) {
        const newlyVisibleNodes = interactiveStreamdumpGraph.toggleSubgraphCollapse(node);
        const {dagOrderedNodes: visibleNodes, edges: visibleEdges} =
          interactiveStreamdumpGraph.getVisibleStreamdumpGraph();
        const {nodes: updatedNodes, edges: updatedEdges} = LayoutController.applyLayout(visibleNodes, visibleEdges);
        setNodes(updatedNodes);
        setEdges(updatedEdges);

        setTimeout(() => {
          if (newlyVisibleNodes && newlyVisibleNodes.length > 0) {
            // For large subgraphs, we can expand to show the entire subgraph, and then focus back onto the expanded node
            if (newlyVisibleNodes.length > 20) {
              // Show newly expanded ndoes
              reactFlowInstance.fitView({
                padding: 0.2,
                includeHiddenNodes: false,
                duration: 1600, // Longer animation to show the entire subgraph smoothly
                nodes: updatedNodes.filter((n) => newlyVisibleNodes.includes(n.id as RF_nodeId)),
              });

              // Go back to node that was just expanded
              setTimeout(() => {
                const expandedNode = updatedNodes.find((n) => n.id === node.rfid);
                if (expandedNode) {
                  reactFlowInstance.fitView({
                    padding: 0.2,
                    includeHiddenNodes: false,
                    duration: 800,
                    nodes: [expandedNode],
                  });
                }
              }, 1700);
            } else {
              // Smaller subgraph expanding, so can just show the entire expanded subgraph
              reactFlowInstance.fitView({
                padding: 0.2,
                includeHiddenNodes: false,
                duration: 800,
                nodes: updatedNodes.filter((n) => newlyVisibleNodes.includes(n.id as RF_nodeId)),
              });
            }
          }
        }, 50);
      }
    },
    [interactiveStreamdumpGraph, setNodes, setEdges, reactFlowInstance],
  );

  // Function to handle a graph (a group of codec nodes) collapsing/expanding
  const handleGraphCollapse = useCallback(
    (graph: InternalGraphNode) => {
      if (interactiveStreamdumpGraph) {
        const newlyVisibleNodes = interactiveStreamdumpGraph.toggleGraphCollapse(graph);
        const {dagOrderedNodes: visibleNodes, edges: visibleEdges} =
          interactiveStreamdumpGraph.getVisibleStreamdumpGraph();
        const {nodes: updatedNodes, edges: updatedEdges} = LayoutController.applyLayout(visibleNodes, visibleEdges);
        setNodes(updatedNodes);
        setEdges(updatedEdges);

        // Focus on newly visible nodes after a short delay to allow DOM to update
        setTimeout(() => {
          if (newlyVisibleNodes && newlyVisibleNodes.length > 0) {
            // For large graphs, use a different approach
            if (newlyVisibleNodes.length > 20) {
              // First fit to the entire graph with a quick animation
              reactFlowInstance.fitView({
                padding: 0.4,
                includeHiddenNodes: false,
                duration: 400,
              });

              // Then focus on the expanded graph with a smoother animation
              setTimeout(() => {
                const expandedGraph = updatedNodes.find((n) => n.id === graph.rfid);
                if (expandedGraph) {
                  reactFlowInstance.fitView({
                    padding: 0.2,
                    includeHiddenNodes: false,
                    duration: 800,
                    nodes: [expandedGraph],
                  });
                }
              }, 500);
            } else {
              // For smaller graphs, use the existing approach
              reactFlowInstance.fitView({
                padding: 0.2,
                includeHiddenNodes: false,
                duration: 800,
                nodes: updatedNodes.filter((n) => newlyVisibleNodes.includes(n.id as RF_nodeId)),
              });
            }
          }
        }, 50);
      }
    },
    [interactiveStreamdumpGraph, setNodes, setEdges, reactFlowInstance],
  );

  const handleAllStandardGraphsCollapse = useCallback(() => {
    if (interactiveStreamdumpGraph) {
      interactiveStreamdumpGraph.toggleAllStandardGraphs(!areStandardGraphsCollapsed);
      const {dagOrderedNodes: visibleNodes, edges: visibleEdges} =
        interactiveStreamdumpGraph.getVisibleStreamdumpGraph();
      const {nodes: updatedNodes, edges: updatedEdges} = LayoutController.applyLayout(visibleNodes, visibleEdges);
      setNodes(updatedNodes);
      setEdges(updatedEdges);
      toggleStandardGraphsCollapsedFlag(!areStandardGraphsCollapsed);

      // Move to fit the entire graph in the screen afer expanding/collapsing all standard graphs
      setTimeout(() => {
        reactFlowInstance.fitView({
          padding: 0.2,
          includeHiddenNodes: false,
          duration: 800,
        });
      }, 50);
    }
  }, [interactiveStreamdumpGraph, areStandardGraphsCollapsed, setNodes, setEdges, reactFlowInstance]);

  const handleNodeExpandOneLevel = useCallback(
    (node: InternalCodecNode) => {
      if (interactiveStreamdumpGraph) {
        const newlyVisibleNodes = interactiveStreamdumpGraph.expandOneLevel(node);
        const {dagOrderedNodes: visibleNodes, edges: visibleEdges} =
          interactiveStreamdumpGraph.getVisibleStreamdumpGraph();
        const {nodes: updatedNodes, edges: updatedEdges} = LayoutController.applyLayout(visibleNodes, visibleEdges);
        setNodes(updatedNodes);
        setEdges(updatedEdges);

        // Focus on newly visible nodes after a short delay to allow DOM to update
        setTimeout(() => {
          reactFlowInstance.fitView({
            padding: 0.2,
            includeHiddenNodes: false,
            duration: 800,
            nodes: updatedNodes.filter((n) => newlyVisibleNodes.includes(n.id as RF_nodeId)),
          });
        }, 50);
      }
    },
    [interactiveStreamdumpGraph, setNodes, setEdges, reactFlowInstance],
  );

  // Providing nodes with their proper collapse handlers
  const nodesWithCollapseHandler = useMemo(() => {
    return nodes.map((node) => {
      if (node.type === NodeType.Codec) {
        return {
          ...node,
          data: {
            ...node.data,
            onToggleCollapse: handleNodeCollapse,
            expandOneLevel: handleNodeExpandOneLevel,
          },
        };
      } else if (node.type === NodeType.Graph) {
        return {
          ...node,
          data: {
            ...node.data,
            onToggleGraphCollapse: handleGraphCollapse,
          },
        };
      }
      return node;
    });
  }, [nodes, handleNodeCollapse, handleGraphCollapse, handleNodeExpandOneLevel]);

  // When a streamdump file is uploaded and the graph option is selected to visualize it
  const initializeGraph = useCallback(() => {
    if (data) {
      const newInteractiveStreamdumpGraph = new InteractiveStreamdumpGraph(data, STANDARD_GRAPHS_START_COLLAPSED);
      setInteractiveStreamdumpGraph(newInteractiveStreamdumpGraph);
      const {dagOrderedNodes: visibleNodes, edges: visibleEdges} =
        newInteractiveStreamdumpGraph.getVisibleStreamdumpGraph();
      const {nodes: laidOutNodes, edges: laidOutEdges} = LayoutController.applyLayout(visibleNodes, visibleEdges);
      setNodes(laidOutNodes);
      setEdges(laidOutEdges);
      toggleStandardGraphsCollapsedFlag(!areStandardGraphsCollapsed);

      // Display the entire graph upon start-up
      setTimeout(() => {
        reactFlowInstance.fitView({
          padding: 0.2,
          includeHiddenNodes: false,
          duration: 800,
        });
      }, 100);
    }
  }, [data, setNodes, setEdges, reactFlowInstance]);

  // Trigger graph visualization loading
  useEffect(() => {
    initializeGraph();
  }, [data, initializeGraph]);

  return {
    nodes: nodesWithCollapseHandler,
    edges,
    onNodesChange,
    onEdgesChange,
    handleAllStandardGraphsCollapse,
    areStandardGraphsCollapsed,
  };
}
