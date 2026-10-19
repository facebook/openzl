// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment node

import {afterEach, describe, expect, it} from 'vitest';
import {run, type BenchmarkEvent} from '../electron/native-worker.cjs';
import {FAKE_ZSTD_BENCHMARK, fakeZli, inputFile, removeFakeZlis} from './fakeZli.ts';

afterEach(removeFakeZlis);

describe('run', () => {
  it('reports in the browser worker’s vocabulary: steps through training and per candidate, then a result per measurement', async () => {
    const events: BenchmarkEvent[] = [];
    const results = await run(
      {
        zliPath: fakeZli(),
        zstdBenchmarkPath: fakeZli(FAKE_ZSTD_BENCHMARK, 'zstd-benchmark'),
        request: {
          input: {path: inputFile(new TextEncoder().encode('OpenZL '.repeat(1000)))},
          iterations: 2,
          compressors: [
            {
              id: 1,
              compressor: 'OpenZL',
              level: 6,
              profile: 'serial',
              trainRequested: true,
              candidates: 10,
              profileArg: '',
            },
            {id: 2, compressor: 'zstd', level: 3},
            {id: 3, compressor: 'gzip', level: 5},
          ],
        },
      },
      {onEvent: (event) => events.push(event)},
    );

    expect(
      events.map((event) =>
        event.type === 'result' ? {result: event.result.rowId, candidate: event.result.candidate} : event,
      ),
    ).toEqual([
      {type: 'step', fraction: 0},
      {type: 'step', fraction: 0.5},
      {type: 'step', fraction: 1},
      {type: 'step', fraction: 0},
      {result: 1, candidate: {index: 1, total: 1}},
      {result: 2, candidate: null},
      {result: 3, candidate: null},
    ]);
    expect(results.map((result) => result.rowId)).toEqual([1, 2, 3]);
  });

  it('reports a row it cannot measure as a failure and goes on to the next', async () => {
    const events: BenchmarkEvent[] = [];
    const results = await run(
      {
        zliPath: fakeZli(),
        zstdBenchmarkPath: fakeZli(
          "process.stderr.write('Cannot open input file\\n'); process.exit(1);",
          'zstd-benchmark',
        ),
        request: {
          input: {path: inputFile(new Uint8Array(100))},
          iterations: 1,
          compressors: [
            {id: 1, compressor: 'zstd', level: 3},
            {id: 2, compressor: 'gzip', level: 5},
          ],
        },
      },
      {onEvent: (event) => events.push(event)},
    );

    expect(events.map((event) => event.type)).toEqual(['failure', 'result']);
    expect(events[0]).toEqual({
      type: 'failure',
      failure: {rowId: 1, candidate: null, message: expect.stringContaining('Cannot open input file')},
    });
    expect(results.map((result) => result.rowId)).toEqual([2]);
  });

  it('stops at a cancel rather than reporting it as a failed row', async () => {
    const controller = new AbortController();
    controller.abort(new Error('Benchmark cancelled'));
    const events: BenchmarkEvent[] = [];

    await expect(
      run(
        {
          zliPath: fakeZli(),
          zstdBenchmarkPath: fakeZli(FAKE_ZSTD_BENCHMARK, 'zstd-benchmark'),
          request: {
            input: {path: inputFile(new Uint8Array(100))},
            iterations: 1,
            compressors: [{id: 1, compressor: 'gzip', level: 5}],
          },
        },
        {signal: controller.signal, onEvent: (event) => events.push(event)},
      ),
    ).rejects.toThrow('Benchmark cancelled');
    expect(events).toEqual([]);
  });

  it('measures zstd with zstd-benchmark and derives throughput from its timings', async () => {
    const [result] = await run({
      zliPath: fakeZli(),
      zstdBenchmarkPath: fakeZli(FAKE_ZSTD_BENCHMARK, 'zstd-benchmark'),
      request: {
        input: {path: inputFile(new Uint8Array(4000))},
        iterations: 5,
        compressors: [{id: 1, compressor: 'zstd', level: 9}],
      },
    });

    // 4000 bytes, 5 times: 20 kB in 2 ms to compress and 1 ms to decompress.
    expect(result).toMatchObject({
      srcSize: 4000,
      compressedSize: 9,
      ratio: 4000 / 9,
      compressMBps: 10,
      decompressMBps: 20,
    });
  });
});
