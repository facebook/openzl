// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, expect, it, vi} from 'vitest';
import {toCompressorConfig} from '../src/benchmarkTypes.ts';
import {createCompressorRow} from '../src/compressors.ts';
import type {BenchmarkResult} from '../../../wasm/js/wasm_api.js';
import type {WorkerMessage} from '../src/benchmarkTypes.ts';

const CANDIDATES = 3;

/** Every candidate measures the same; only the order of events is under test. */
const METRICS: BenchmarkResult = {
  iterations: 1,
  srcSize: 4000,
  compressedSize: 2000,
  ratio: 2,
  compressMs: 1,
  decompressMs: 1,
  compressMBps: 100,
  decompressMBps: 200,
};

/** Counted when the module is asked to measure, so the test can say when. */
const resultsPostedWhenBenchmarked: number[] = [];
const messages: WorkerMessage[] = [];

vi.mock('../../../wasm/js/wasm_api.js', () => ({
  getOpenZLMaxIterations: () => Promise.resolve(10),
  createOpenZL: () =>
    Promise.resolve({
      maxBenchmarkIterations: 10,
      trainParetoCandidates: 6,
      getSerializedCompressor: () => new Uint8Array([1]),
      train: () => Array.from({length: CANDIDATES}, (_, index) => new Uint8Array([index])),
      benchmark: () => {
        resultsPostedWhenBenchmarked.push(messages.filter((message) => message.type === 'result').length);
        return METRICS;
      },
      compress: () => new Uint8Array(),
      decompress: () => new Uint8Array(),
    }),
}));

describe('measureRun on a trained job', () => {
  it('posts each candidate as it is measured rather than after the last one', async () => {
    // Training runs for minutes, so a frontier collected and posted in one go
    // leaves the page still for the whole job and loses every candidate
    // already measured if a later one throws.
    const {measureRun} = await import('../src/measureRun.ts');
    const row = createCompressorRow(1, 'OpenZL');

    await measureRun(
      {
        input: new File([new TextEncoder().encode('a'.repeat(4000))], 'sample.txt'),
        compressors: [toCompressorConfig(row)],
        iterations: 1,
      },
      (message) => messages.push(message),
    );

    const results = messages.flatMap((message) => (message.type === 'result' ? [message.result] : []));
    expect(results).toHaveLength(CANDIDATES);
    expect(results.map((result) => result.candidate?.index)).toEqual([1, 2, 3]);
    // The point: measuring candidate n finds the n-1 before it already posted.
    expect(resultsPostedWhenBenchmarked).toEqual([0, 1, 2]);
  });
});
