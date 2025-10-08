// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useState, useEffect} from 'react';
import {ReactFlow, Controls, Background, ConnectionLineType, Panel, useReactFlow} from '@xyflow/react';
import type {Node, Edge, NodeChange, EdgeChange} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import {nodeTypes} from './NodeView';
import {edgeTypes} from './CustomEdge';
import {Box} from '@chakra-ui/react/box';
import {Button} from '@chakra-ui/react/button';
import {HStack} from '@chakra-ui/react/stack';

const showExpansionButton = false;

interface StreamdumpGraphViewProps {
  nodes: Node[];
  edges: Edge[];
  onNodesChange: (changes: NodeChange[]) => void;
  onEdgesChange: (changes: EdgeChange[]) => void;
  handleAllStandardGraphsCollapse: () => void;
  areStandardGraphsCollapsed: boolean;
}

export function StreamdumpGraphView({
  nodes,
  edges,
  onNodesChange,
  onEdgesChange,
  handleAllStandardGraphsCollapse,
  areStandardGraphsCollapsed,
}: StreamdumpGraphViewProps) {
  const [isTrackpadMode, setIsTrackpadMode] = useState<boolean>(false);
  const reactFlowInstance = useReactFlow();

  // Make the React Flow instance available to the controller
  useEffect(() => {
    // The controller will use this instance for viewport manipulation, which is handled internally by React Flow
  }, [reactFlowInstance]);
  return (
    <Box w={'100%'} h={'100%'}>
      <ReactFlow
        nodes={nodes}
        edges={edges}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        nodeTypes={nodeTypes}
        edgeTypes={edgeTypes}
        connectionLineType={ConnectionLineType.SmoothStep}
        fitView
        minZoom={0.01}
        zoomOnScroll={!isTrackpadMode}
        panOnScroll={isTrackpadMode}>
        <Background />
        <Controls />
        <Panel>
          <HStack gap={4}>
            <Button
              variant="surface"
              onClick={() => setIsTrackpadMode(!isTrackpadMode)}
              title={
                isTrackpadMode
                  ? 'Pinch to zoom in/out, swipe or click and drag to move across graph'
                  : 'Scroll to zoom in/out, click and drag to move across graph'
              }>
              {isTrackpadMode ? 'Switch to Mouse Controls' : 'Switch to Trackpad Controls'}
            </Button>
            {/* TODO: re-enable once expansion is fixed */}
            {showExpansionButton && (
              <Button onClick={handleAllStandardGraphsCollapse}>
                {areStandardGraphsCollapsed ? 'Expand all standard graphs' : 'Collapse all standard graphs'}
              </Button>
            )}
          </HStack>
        </Panel>
      </ReactFlow>
    </Box>
  );
}
