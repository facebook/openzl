// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment node

import {afterEach, describe, expect, it} from 'vitest';
import {run, type BenchmarkEvent} from '../electron/native-worker.cjs';
import {FAKE_ZSTD_BENCHMARK, fakeZli, inputFile, removeFakeZlis} from './fakeZli.ts';

afterEach(removeFakeZlis);

describe('run', () => {
  it('reports each row and stage as it starts, and each row’s results as it ends', async () => {
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
        event.type === 'progress' ? event.progress : {partial: event.results.map((r) => r.label)},
      ),
    ).toEqual([
      {rowId: 1, rowIndex: 0, rowCount: 3, label: 'OpenZL serial', stage: 'train'},
      {
        rowId: 1,
        rowIndex: 0,
        rowCount: 3,
        label: 'OpenZL serial',
        stage: 'candidates',
        candidate: 1,
        candidateCount: 1,
      },
      {partial: ['OpenZL serial']},
      {rowId: 2, rowIndex: 1, rowCount: 3, label: 'zstd 3', stage: 'benchmark'},
      {partial: ['zstd 3']},
      {rowId: 3, rowIndex: 2, rowCount: 3, label: 'gzip 5', stage: 'benchmark'},
      {partial: ['gzip 5']},
    ]);
    expect(results.map((result) => result.label)).toEqual(['OpenZL serial', 'zstd 3', 'gzip 5']);
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
