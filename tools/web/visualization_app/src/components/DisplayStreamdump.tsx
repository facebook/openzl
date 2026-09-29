// Copyright (c) Meta Platforms, Inc. and affiliates.

import {renderStreamdump} from '../utils/renderStreamdump.tsx';
import type {NullableStreamdump} from '../interfaces/NullableStreamdump.ts';

export function StreamdumpDisplayBox({data}: NullableStreamdump) {
  return (
    <div
      style={{
        width: '100%',
        margin: 'auto',
        backgroundColor: 'white',
        color: 'black',
        padding: '10px',
        borderRadius: '8px',
        boxShadow: '0 0 10px rgba(0, 0, 0, 0.1)',
        fontFamily: 'monospace',
        overflowX: 'auto',
        whiteSpace: 'pre-wrap',
        textAlign: 'left',
      }}>
      {data ? renderStreamdump(data) : 'Eagerly awaiting data...'}
    </div>
  );
}
