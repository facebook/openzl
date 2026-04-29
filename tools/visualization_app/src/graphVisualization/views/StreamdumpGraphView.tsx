// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useEffect} from 'react';
import {ReactFlow, Controls, Background, ConnectionLineType, Panel, useReactFlow} from '@xyflow/react';
import type {Node, Edge, NodeChange, EdgeChange} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import {nodeTypes} from './NodeView';
import {Box} from '@chakra-ui/react/box';
import {Button} from '@chakra-ui/react/button';
import {Flex} from '@chakra-ui/react/flex';

import {OperationType} from '../../models/idTypes';
import type {KeyboardNavControls} from '../controllers/useKeyboardShortcuts';

const showExpansionButton = false;

interface VersionInfo {
  libraryVersion: number;
  frameVersion: number;
  traceVersion: number;
  operationType: OperationType;
}

interface StreamdumpGraphViewProps {
  nodes: Node[];
  edges: Edge[];
  onNodesChange: (changes: NodeChange[]) => void;
  onEdgesChange: (changes: EdgeChange[]) => void;
  handleAllStandardGraphsCollapse: () => void;
  areStandardGraphsCollapsed: boolean;
  versionInfo: VersionInfo;
  isTrackpadMode: boolean;
  isKeyboardMode: boolean;
  keyboardNav: KeyboardNavControls;
}

export function StreamdumpGraphView({
  nodes,
  edges,
  onNodesChange,
  onEdgesChange,
  handleAllStandardGraphsCollapse,
  areStandardGraphsCollapsed,
  versionInfo,
  isTrackpadMode,
  isKeyboardMode,
  keyboardNav,
}: StreamdumpGraphViewProps) {
  const reactFlowInstance = useReactFlow();

  // Make the React Flow instance available to the controller
  useEffect(() => {
    // The controller will use this instance for viewport manipulation, which is handled internally by React Flow
  }, [reactFlowInstance]);
  const selectedNodeId = isKeyboardMode ? keyboardNav.selectedNodeId : null;

  // TODO: Nice to have. Move DOM focus to the selected node and add ARIA attributes (e.g. aria-selected,
  // aria-current) during arrow-key navigation so keyboard and screen-reader users have a
  // programmatic indication of which node is currently selected.
  return (
    <Box
      w={'100%'}
      h={'100%'}
      className={versionInfo.operationType === OperationType.Decompress ? 'decompress-trace' : ''}>
      {selectedNodeId && (
        <style>{`[data-id="${selectedNodeId}"] .codec-node,
          [data-id="${selectedNodeId}"] .graph-node {
            box-shadow: 0 0 25px 5px #e7e43a;
          }`}</style>
      )}
      <ReactFlow
        nodes={nodes}
        edges={edges}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        nodeTypes={nodeTypes}
        connectionLineType={ConnectionLineType.SimpleBezier}
        fitView
        minZoom={0.01}
        zoomOnScroll={!isTrackpadMode}
        panOnScroll={isTrackpadMode}>
        <Background />
        <Controls />
        <Panel style={{width: '100%'}}>
          <Flex justify="space-between">
            <div className="header-text">
              <p style={{fontWeight: 'bold'}}>
                {versionInfo.operationType === OperationType.Decompress ? 'Decompression Trace' : 'Compression Trace'}
              </p>
              <p>Library Version: {versionInfo.libraryVersion}</p>
              <p>Frame Version: {versionInfo.frameVersion}</p>
              <p>Trace Version: {versionInfo.traceVersion}</p>
            </div>
            {/* TODO: re-enable once expansion is fixed */}
            {showExpansionButton && (
              <Button onClick={handleAllStandardGraphsCollapse}>
                {areStandardGraphsCollapsed ? 'Expand all standard graphs' : 'Collapse all standard graphs'}
              </Button>
            )}
          </Flex>
        </Panel>
      </ReactFlow>
    </Box>
  );
}
