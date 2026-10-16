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
  it('returns a valid request unchanged', () => {
    const valid = request();
    expect(validateRequest(valid)).toBe(valid);
  });

  it('accepts every boundary value', () => {
    const boundaries = [
      request({iterations: 1}),
      request({iterations: 1000}),
      request({compressors: [openZLRow({level: 1, candidates: 10})]}),
      request({compressors: [openZLRow({level: 9, candidates: 25, profileArg: 'x'.repeat(4096)})]}),
      request({compressors: [{id: 1, compressor: 'zstd', level: 19}]}),
      request({compressors: [{id: 1, compressor: 'gzip', level: 9}]}),
      request({input: {data: new Uint8Array([1, 2, 3])}}),
    ];
    for (const valid of boundaries) {
      expect(validateRequest(valid)).toBe(valid);
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

  it('accepts any number of compressors', () => {
    // Every level of every compressor is already a few dozen rows, and nothing
    // about a long run makes it unsafe, only slow.
    const many = request({compressors: Array.from({length: 200}, (_, id) => ({id, compressor: 'gzip', level: 1}))});
    expect(validateRequest(many)).toBe(many);
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
    for (const profileArg of [undefined, 'x'.repeat(4097)]) {
      rejects(request({compressors: [openZLRow({profileArg})]}), TypeError, 'OpenZL profile argument is invalid');
    }
  });

  it('rejects missing or malformed input', () => {
    rejects(request({input: null}), TypeError, 'Benchmark input is missing');
    for (const path of ['', 'sample\0.bin', 42]) {
      rejects(request({input: {path}}), TypeError, 'Invalid input path');
    }
    for (const input of [{}, {data: [1, 2, 3]}, {data: new ArrayBuffer(3)}]) {
      rejects(request({input}), TypeError, 'Benchmark input must contain a file path or bytes');
    }
  });
});
