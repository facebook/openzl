// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, expect, it} from 'vitest';
import {validateRequest} from '../electron/benchmark-request.cjs';

function openZLRow(overrides: Record<string, unknown> = {}) {
  return {
    id: 1,
    compressor: 'OpenZL',
    level: 6,
    profile: 'serial',
    trainRequested: true,
    candidates: 10,
    profileArg: '',
    ...overrides,
  };
}

function request(overrides: Record<string, unknown> = {}) {
  return {
    input: {path: '/tmp/sample.bin'},
    iterations: 5,
    compressors: [openZLRow(), {id: 2, compressor: 'zstd', level: 5}, {id: 3, compressor: 'gzip', level: 5}],
    ...overrides,
  };
}

function rejects(value: unknown, errorType: typeof TypeError | typeof RangeError, message: string) {
  const call = () => validateRequest(value);
  expect(call).toThrow(errorType);
  expect(call).toThrow(message);
}

describe('validateRequest', () => {
  it('returns a validated copy of a valid request', () => {
    const valid = request();
    expect(validateRequest(valid)).toEqual(valid);
  });

  it('strips keys the worker does not use', () => {
    const withExtras = {
      ...request({compressors: [{...openZLRow(), optionsOpen: true}]}),
      input: {path: '/tmp/sample.bin', note: 'renderer-only'},
      debug: true,
    };
    expect(validateRequest(withExtras)).toEqual(request({compressors: [openZLRow()]}));
  });

  it('accepts every boundary value', () => {
    const boundaries = [
      request({iterations: 1}),
      request({iterations: 1000}),
      request({compressors: [openZLRow({level: 1, candidates: 10})]}),
      request({compressors: [openZLRow({level: 9, candidates: 25, profileArg: 'x'.repeat(4096)})]}),
      request({compressors: [{id: 1, compressor: 'zstd', level: 19}]}),
      request({compressors: [{id: 1, compressor: 'gzip', level: 9}]}),
      request({input: {sample: true}}),
    ];
    for (const valid of boundaries) {
      expect(validateRequest(valid)).toEqual(valid);
    }
  });

  it('rejects a request that is not an object', () => {
    rejects(null, TypeError, 'Invalid benchmark request');
    rejects('run', TypeError, 'Invalid benchmark request');
  });

  it('rejects iterations outside 1 to 1000', () => {
    for (const iterations of [0, 1001, 1.5, '5', undefined]) {
      rejects(request({iterations}), RangeError, 'Iterations must be an integer from 1 to 1000');
    }
  });

  it('accepts an untrained OpenZL row without a candidate count', () => {
    const untrained: Record<string, unknown> = openZLRow({trainRequested: false});
    delete untrained.candidates;
    const valid = request({compressors: [untrained]});
    expect(validateRequest(valid)).toEqual(valid);
  });

  it('accepts any number of compressors', () => {
    // Every level of every compressor is already a few dozen rows, and nothing
    // about a long run makes it unsafe, only slow.
    const many = request({compressors: Array.from({length: 200}, (_, id) => ({id, compressor: 'gzip', level: 1}))});
    expect(validateRequest(many)).toEqual(many);
  });

  it('rejects an empty or missing compressor list', () => {
    for (const compressors of [[], undefined, {}]) {
      rejects(request({compressors}), RangeError, 'Choose at least one compressor');
    }
  });

  it('rejects malformed rows and unknown compressors', () => {
    rejects(request({compressors: [null]}), TypeError, 'Invalid compressor row');
    rejects(request({compressors: [{id: '1', compressor: 'gzip', level: 5}]}), TypeError, 'Invalid compressor row');
    rejects(request({compressors: [{id: 1, compressor: 'brotli', level: 5}]}), TypeError, 'Unknown compressor');
  });

  it('rejects levels outside each compressor range', () => {
    const cases: [Record<string, unknown>, string][] = [
      [openZLRow({level: 0}), 'Invalid OpenZL compression level'],
      [openZLRow({level: 10}), 'Invalid OpenZL compression level'],
      [{id: 1, compressor: 'zstd', level: 20}, 'Invalid zstd compression level'],
      [{id: 1, compressor: 'gzip', level: 10}, 'Invalid gzip compression level'],
      [{id: 1, compressor: 'gzip', level: 2.5}, 'Invalid gzip compression level'],
    ];
    for (const [row, message] of cases) {
      rejects(request({compressors: [row]}), RangeError, message);
    }
  });

  it('rejects invalid OpenZL options', () => {
    rejects(request({compressors: [openZLRow({profile: ''})]}), TypeError, 'OpenZL profile is missing');
    rejects(request({compressors: [openZLRow({trainRequested: 1})]}), TypeError, 'OpenZL training flag is invalid');
    for (const candidates of [9, 26, 10.5]) {
      rejects(
        request({compressors: [openZLRow({candidates})]}),
        RangeError,
        'OpenZL candidate count must be between 10 and 25',
      );
    }
    rejects(
      request({compressors: [openZLRow({candidates: undefined})]}),
      RangeError,
      'OpenZL candidate count must be between 10 and 25',
    );
    for (const profileArg of [undefined, 'x'.repeat(4097)]) {
      rejects(request({compressors: [openZLRow({profileArg})]}), TypeError, 'OpenZL profile argument is invalid');
    }
  });

  it('rejects missing or malformed input', () => {
    rejects(request({input: null}), TypeError, 'Benchmark input is missing');
    for (const path of ['', 'sample\0.bin', 42]) {
      rejects(request({input: {path}}), TypeError, 'Invalid input path');
    }
    for (const input of [
      {},
      {sample: false},
      {sample: 'yes'},
      // Bytes are no longer accepted: every input is a file on disk.
      {data: new Uint8Array(3)},
      {path: '/tmp/a', sample: true},
    ]) {
      rejects(request({input}), TypeError, 'Benchmark input must be a file path or the bundled sample');
    }
  });
});
