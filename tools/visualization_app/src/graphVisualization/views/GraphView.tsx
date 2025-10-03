// Copyright (c) Meta Platforms, Inc. and affiliates.

import {InternalGraphNode} from '../models/InternalGraphNode';
import {Handle, Position} from '@xyflow/react';
import {LocalParamsPopover} from './LocalParamsView';
import {IconButton} from '@chakra-ui/react/button';
import {VscEye, VscEyeClosed} from 'react-icons/vsc';
import {Popover} from '@chakra-ui/react/popover';
import {Box} from '@chakra-ui/react/box';
import {Portal} from '@chakra-ui/react/portal';
import {ScrollablePopover} from './ScrollablePopover';

interface GraphViewProps {
  data: {
    internalNode: InternalGraphNode;
    onToggleGraphCollapse: (graph: InternalGraphNode) => void;
  };
}

export function GraphNodeView({data}: GraphViewProps) {
  const graph = data.internalNode;

  if (graph.gFailureString) {
    return (
      <Popover.Root>
        <Popover.Trigger asChild>
          <Box className={'graph-node error'}>
            <Handle type="target" position={Position.Top} id="target" style={{background: '#555'}} />
            {graph.gLocalParams.hasLocalParams() && <LocalParamsPopover localParams={graph.gLocalParams} />}
            <div className="node-header">
              {graph.gName} ({graph.rfid})
              <br />
              Type: {graph.getGraphTypeString()}
            </div>
            <div className="node-content">
              <div className="node-content">Click for error info</div>
            </div>
          </Box>
        </Popover.Trigger>
        <Portal>
          <Popover.Positioner>
            <Popover.Content css={{'--popover-bg': '#ffe0e0'}}>
              <Popover.Arrow />
              <Popover.Body>
                <ScrollablePopover className="error-popover-content" maxHeight="500px">
                  {graph.gFailureString}
                </ScrollablePopover>
              </Popover.Body>
            </Popover.Content>
          </Popover.Positioner>
        </Portal>
      </Popover.Root>
    );
  }
  return (
    <div
      className={`graph-node ${graph.isCollapsed ? 'collapsed' : ''}`}
      style={graph.inLargestCompressionPath ? {border: '7px solid #2ed78b'} : {}}>
      {/* Add handles for when graph is collapsed */}
      {graph.isCollapsed && (
        <>
          <Handle type="target" position={Position.Top} id="target" style={{background: '#555'}} />
          <Handle type="source" position={Position.Bottom} id="source" style={{background: '#555'}} />
        </>
      )}

      {graph.gLocalParams.hasLocalParams() && <LocalParamsPopover localParams={graph.gLocalParams} />}

      <div className="graph-node-header">
        {graph.gName} ({graph.rfid})
        <br />
        Type: {graph.getGraphTypeString()}
      </div>
      <div
        className="graph-collapse-or-expand-button"
        onClick={() => {
          data.onToggleGraphCollapse(graph);
        }}>
        <IconButton variant={'ghost'}>{graph.isCollapsed ? <VscEye /> : <VscEyeClosed />}</IconButton>
      </div>
    </div>
  );
}
