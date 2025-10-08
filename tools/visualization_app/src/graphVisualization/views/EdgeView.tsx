// Copyright (c) Meta Platforms, Inc. and affiliates.

import {Handle, Position} from '@xyflow/react';
import type {InternalEdge} from '../models/InternalEdge';
import {Box} from '@chakra-ui/react/box';
import {Separator} from '@chakra-ui/react/separator';
import {ZL_Type} from '../../models/idTypes';

interface EdgeViewProps {
  data: {
    internalNode: InternalEdge;
  };
}

export function EdgeView({data}: EdgeViewProps) {
  const edge = data.internalNode;
  let typeAndWidthInfo = edge.streamTypeToString();
  if (edge.type === ZL_Type.ZL_Type_numeric || edge.type === ZL_Type.ZL_Type_struct) {
    typeAndWidthInfo += `[${edge.eltWidth}]`;
  }
  return (
    <Box
      bg={edge.inLargestCompressionPath ? '#d9ffee' : '#ffffffb0'}
      borderWidth={edge.inLargestCompressionPath ? '7px' : '1px'}
      borderColor={edge.inLargestCompressionPath ? '#2ed78b' : 'black'}
      p={'15px'}
      color={'black'}
      textAlign="center">
      <Handle type="target" position={Position.Top} id="target" style={{background: '#555'}} />
      <Handle type="source" position={Position.Bottom} id="source" style={{background: '#555'}} />
      <p style={{fontSize: '20px'}}>
        <b>{`#${edge.outputIdx} (${edge.rfid}) | ${typeAndWidthInfo}`}</b>
      </p>
      <Separator />
      <p>{`${edge.cSize} [${edge.share.toFixed(2)}%]`}</p>
      <p>{`${edge.numElts} elts`}</p>
    </Box>
  );
}
