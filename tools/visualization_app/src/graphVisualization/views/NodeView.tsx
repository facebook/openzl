// Copyright (c) Meta Platforms, Inc. and affiliates.

import {useState} from 'react';
import {Handle, Position} from '@xyflow/react';
import {InternalCodecNode} from '../models/InternalCodecNode';
import {GraphNodeView} from './GraphView';
import {renderLocalParams} from './LocalParamsView';
import {LocalParamsPopover} from './LocalParamsView';
import {Float, IconButton} from '@chakra-ui/react';
import {VscChevronDown, VscFoldDown, VscFoldUp} from 'react-icons/vsc';
import {Popover} from '@chakra-ui/react/popover';
import {Box} from '@chakra-ui/react/box';
import {Portal} from '@chakra-ui/react/portal';
import {ScrollablePopover} from './ScrollablePopover';
import {EdgeView} from './EdgeView';

interface NodeViewProps {
  data: {
    internalNode: InternalCodecNode;
    onToggleCollapse: (node: InternalCodecNode) => void;
    expandOneLevel: (node: InternalCodecNode) => void;
  };
}

export function CodecNode({data}: NodeViewProps) {
  const codec = data.internalNode;
  const [showLocalParams] = useState(false);

  if (codec.name === 'zl.#in_progress') {
    return (
      <Popover.Root>
        <Popover.Trigger asChild>
          <Box className={'codec-node error'}>
            <Handle type="target" position={Position.Top} id="target" style={{background: '#555'}} />
            <div className="node-header">{codec.cFailureString ? 'Click for error info' : '...'}</div>
          </Box>
        </Popover.Trigger>
        <Portal>
          <Popover.Positioner>
            <Popover.Content css={{'--popover-bg': '#ffe0e0'}}>
              <Popover.Arrow />
              <Popover.Body>
                <ScrollablePopover className="error-popover-content" maxHeight="500px">
                  {codec.cFailureString ??
                    'Execution terminated before all streams were stored. Display may not represent full graph.'}
                </ScrollablePopover>
              </Popover.Body>
            </Popover.Content>
          </Popover.Positioner>
        </Portal>
      </Popover.Root>
    );
  }
  if (codec.cFailureString) {
    return (
      <Popover.Root>
        <Popover.Trigger asChild>
          <Box className={`codec-node ${codec.cFailureString ? 'error' : ''}`}>
            <Handle type="target" position={Position.Top} id="target" style={{background: '#555'}} />
            {codec.cLocalParams.hasLocalParams() && <LocalParamsPopover localParams={codec.cLocalParams} />}
            <div className="node-header">
              {codec.name} ({codec.cID}) ({codec.rfid})
            </div>
            <div className="node-content">
              <div>
                {codec.codecTypeToString()} | {codec.cHeaderSize}
              </div>
              <div className="node-content">Click for error iinfo</div>
              {showLocalParams && renderLocalParams(codec.cLocalParams)}
            </div>
          </Box>
        </Popover.Trigger>
        <Portal>
          <Popover.Positioner>
            <Popover.Content css={{'--popover-bg': '#ffe0e0'}}>
              <Popover.Arrow />
              <Popover.Body>
                <ScrollablePopover className="error-popover-content" maxHeight="500px">
                  {codec.cFailureString}
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
      className={`codec-node ${codec.isCollapsed ? 'collapsed' : ''} ${codec.name === 'zl.store' ? 'store-node' : ''}`}
      style={codec.inLargestCompressionPath ? {border: '7px solid #2ed78b'} : {}}>
      {/* Input edge handle declaration for a node*/}
      <Handle type="target" position={Position.Top} id="target" style={{background: '#555'}} />
      {codec.cLocalParams.hasLocalParams() && <LocalParamsPopover localParams={codec.cLocalParams} />}
      <div className="node-header">
        {codec.name} ({codec.cID}) ({codec.rfid})
      </div>
      <div className="node-content">
        <div>
          {codec.codecTypeToString()} | {codec.cHeaderSize}
        </div>
        {showLocalParams && renderLocalParams(codec.cLocalParams)}
      </div>
      <Float placement={'bottom-end'} offsetX={10} offsetY={5}>
        {codec.isCollapsed && (
          <IconButton
            variant={'ghost'}
            onClick={() => {
              data.expandOneLevel(codec);
            }}>
            <VscChevronDown />
          </IconButton>
        )}
        {codec.outputStreams.length !== 0 && (
          <IconButton
            variant={'ghost'}
            onClick={() => {
              data.onToggleCollapse(codec);
            }}>
            {codec.isCollapsed ? <VscFoldDown /> : <VscFoldUp />}
          </IconButton>
        )}
      </Float>
      {/* Output edge handle declaration for a node*/}
      <Handle type="source" position={Position.Bottom} id="source" style={{background: '#555'}} />
    </div>
  );
}

// Node types for React Flow
export const nodeTypes = {
  codec: CodecNode,
  graph: GraphNodeView,
  edge: EdgeView,
};
