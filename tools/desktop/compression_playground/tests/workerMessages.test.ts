// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, expect, it} from 'vitest';
import {parseWorkerMessage} from '../electron/worker-messages.cjs';

const RESULT = {
  rowId: 1,
  candidate: {index: 1, total: 3},
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
      {type: 'step', fraction: 0.5},
      {type: 'result', result: RESULT},
      {type: 'result', result: {...RESULT, candidate: null}},
      {type: 'failure', failure: {rowId: 1, candidate: null, message: 'zli exited with code 1'}},
      {type: 'finished'},
      {type: 'failed', message: 'Native benchmark process exited unexpectedly'},
      {type: 'cancelled'},
    ];
    for (const message of messages) {
      expect(parseWorkerMessage(message)).toEqual(message);
    }
    expect(parseWorkerMessage({type: 'result', result: {...RESULT, secret: 'x'}, extra: 1})).toEqual({
      type: 'result',
      result: RESULT,
    });
  });

  it('rejects malformed messages, naming the field', () => {
    const cases: [unknown, string][] = [
      [null, 'invalid message ('],
      [{type: 'shell', command: 'rm'}, 'invalid message (type:'],
      [{type: 'child', pid: 0, state: 'spawned'}, '(pid:'],
      [{type: 'child', pid: 42, state: 'zombie'}, '(state:'],
      [{type: 'result', result: {...RESULT, srcSize: Number.NaN}}, '(result.srcSize:'],
      [{type: 'result', result: {...RESULT, compressMs: -1}}, '(result.compressMs:'],
      // Counted from 1, as the browser counts them.
      [{type: 'result', result: {...RESULT, candidate: {index: 0, total: 3}}}, '(result.candidate.index:'],
      [{type: 'result', result: {...RESULT, candidate: {index: 4, total: 3}}}, '(result.candidate.index:'],
      [{type: 'step', fraction: 1.5}, '(fraction:'],
      [{type: 'failure', failure: {rowId: 1, candidate: null}}, '(failure.message:'],
      [{type: 'failed'}, '(message:'],
    ];
    for (const [message, expected] of cases) {
      expect(() => parseWorkerMessage(message)).toThrow(expected);
    }
  });
});
