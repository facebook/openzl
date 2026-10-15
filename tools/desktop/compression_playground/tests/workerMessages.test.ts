// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, expect, it} from 'vitest';
import {parseWorkerMessage} from '../electron/worker-messages.cjs';

const RESULT = {
  rowId: 1,
  label: 'OpenZL serial',
  candidate: 0,
  iterations: 5,
  srcSize: 100,
  compressedSize: 40,
  ratio: 2.5,
  compressMs: 1,
  decompressMs: 0,
  compressMBps: 0.5,
  decompressMBps: Infinity,
};

describe('parseWorkerMessage', () => {
  it('accepts every message type and strips unknown keys', () => {
    const messages = [
      {type: 'child', pid: 42, state: 'spawned'},
      {type: 'progress', progress: {rowId: 1, rowIndex: 0, rowCount: 2, label: 'OpenZL serial', stage: 'benchmark'}},
      {type: 'progress', progress: {rowId: 1, rowIndex: 0, rowCount: 2, label: 'x', stage: 'train'}},
      {
        type: 'progress',
        progress: {
          rowId: 1,
          rowIndex: 0,
          rowCount: 2,
          label: 'x',
          stage: 'candidates',
          candidate: 1,
          candidateCount: 3,
        },
      },
      {type: 'partial', results: [RESULT]},
      {type: 'results', results: [RESULT]},
      {type: 'error', message: 'zli exited unexpectedly'},
      {type: 'cancelled'},
    ];
    for (const message of messages) {
      expect(parseWorkerMessage(message)).toEqual(message);
    }
    expect(parseWorkerMessage({type: 'results', results: [{...RESULT, secret: 'x'}], extra: 1})).toEqual({
      type: 'results',
      results: [RESULT],
    });
  });

  it('rejects malformed messages, naming the field', () => {
    const cases: [unknown, string][] = [
      [null, 'invalid message ('],
      [{type: 'shell', command: 'rm'}, 'invalid message (type:'],
      [{type: 'child', pid: 0, state: 'spawned'}, '(pid:'],
      [{type: 'child', pid: 42, state: 'zombie'}, '(state:'],
      [{type: 'results', results: [{...RESULT, srcSize: Number.NaN}]}, '(results.0.srcSize:'],
      [{type: 'results', results: [{...RESULT, compressMs: -1}]}, '(results.0.compressMs:'],
      [
        {
          type: 'progress',
          progress: {rowId: 1, rowIndex: 0, rowCount: 1, label: 'x', stage: 'candidates', candidate: 1},
        },
        '(progress.candidateCount:',
      ],
      [{type: 'error'}, '(message:'],
    ];
    for (const [message, expected] of cases) {
      expect(() => parseWorkerMessage(message)).toThrow(expected);
    }
  });
});
