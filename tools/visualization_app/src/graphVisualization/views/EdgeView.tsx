// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Handle, Position} from '@xyflow/react';
import type {InternalEdge} from '../models/InternalEdge';
import '../../styles/streamdumpGraph.css';

interface EdgeViewProps {
  data: {
    internalNode: InternalEdge;
  };
}

export function EdgeView({data}: EdgeViewProps) {
  const edge = data.internalNode;
  return (
    <div style={edge.inLargestCompressionPath ? {border: '7px solid #2ed78b'} : {}}>
      <Handle type="target" position={Position.Top} id="target" style={{background: '#555'}} />
      <Handle type="source" position={Position.Bottom} id="source" style={{background: '#555'}} />
      <div
        style={{
          borderStyle: 'solid',
          borderColor: 'black',
          borderWidth: '1px',
          padding: '5px',
        }}
        className="edge-label">
        {edge.genLabel()}
      </div>
    </div>
  );
}
