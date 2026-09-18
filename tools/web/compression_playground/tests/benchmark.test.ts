// Copyright (c) Meta Platforms, Inc. and affiliates.

import {describe, expect, it, vi} from 'vitest';
import {getOpenZLMaxIterations} from '../../../wasm/js/wasm_api.js';
import {benchmarkGzip, benchmarkOpenZL, benchmarkZstd} from '../src/benchmark.ts';

const {mockGetOpenZLMaxIterations} = vi.hoisted(() => ({
  mockGetOpenZLMaxIterations: vi.fn(() => Promise.resolve(3)),
}));

// fake module for testing
vi.mock('../../../wasm/js/wasm_api.js', () => ({
  getOpenZLMaxIterations: mockGetOpenZLMaxIterations,
}));

function sampleData(): Uint8Array {
  return new TextEncoder().encode('hello world '.repeat(500));
}

describe('benchmarkGzip', () => {
  it('round-trips and reports consistent sizes', async () => {
    const data = sampleData();
    const result = await benchmarkGzip(data, 6, 2);
    expect(result.iterations).toBe(2);
    expect(result.srcSize).toBe(data.length);
    expect(result.compressedSize).toBeGreaterThan(0);
    expect(result.ratio).toBeCloseTo(data.length / result.compressedSize, 10);
    expect(result.compressMs).toBeGreaterThanOrEqual(0);
    expect(result.decompressMs).toBeGreaterThanOrEqual(0);
    expect(result.compressMBps).toBeGreaterThan(0);
    expect(result.decompressMBps).toBeGreaterThan(0);
  });

  it('runs at least one iteration', async () => {
    const data = sampleData();
    await expect(benchmarkGzip(data, 6, 0)).resolves.toMatchObject({iterations: 1});
    await expect(benchmarkGzip(data, 6, 2.7)).resolves.toMatchObject({iterations: 2});
  });

  it('rejects non-finite iterations and non-Uint8Array input', async () => {
    const data = sampleData();
    await expect(benchmarkGzip(data, 6, NaN)).rejects.toThrow('finite');
    await expect(benchmarkGzip(data, 6, Infinity)).rejects.toThrow('finite');
    await expect(benchmarkGzip('nope' as unknown as Uint8Array, 6, 1)).rejects.toThrow('Uint8Array');
  });

  it('rejects gzip levels outside 1..9', async () => {
    const data = sampleData();
    await expect(benchmarkGzip(data, 0, 1)).rejects.toThrow('1 to 9');
    await expect(benchmarkGzip(data, 10, 1)).rejects.toThrow('1 to 9');
    await expect(benchmarkGzip(data, 1.5, 1)).rejects.toThrow('1 to 9');
  });

  it('caps iterations at the live native max instead of hanging', async () => {
    const data = sampleData();
    const max = await getOpenZLMaxIterations();
    await expect(benchmarkGzip(data, 6, max + 100)).resolves.toMatchObject({iterations: max});
  });

  it('retries loading the iteration limit after a failure', async () => {
    vi.resetModules();
    mockGetOpenZLMaxIterations.mockReset();
    mockGetOpenZLMaxIterations.mockRejectedValueOnce(new Error('temporary load failure')).mockResolvedValue(3);
    const {benchmarkGzip: freshBenchmarkGzip} = await import('../src/benchmark.ts');
    const data = sampleData();

    await expect(freshBenchmarkGzip(data, 6, 1)).rejects.toThrow('temporary load failure');
    await expect(freshBenchmarkGzip(data, 6, 1)).resolves.toMatchObject({iterations: 1});
    expect(mockGetOpenZLMaxIterations).toHaveBeenCalledTimes(2);
  });
});

describe('benchmarkZstd', () => {
  it('rejects levels outside 1..19 before loading wasm', async () => {
    const data = sampleData();
    await expect(benchmarkZstd(data, 0, 1)).rejects.toThrow('1 to 19');
    await expect(benchmarkZstd(data, 20, 1)).rejects.toThrow('1 to 19');
  });

  it('round-trips small input', async () => {
    const data = sampleData();
    const result = await benchmarkZstd(data, 3, 1);
    expect(result.iterations).toBe(1);
    expect(result.srcSize).toBe(data.length);
    expect(result.compressedSize).toBeGreaterThan(0);
  });
});

describe('benchmarkOpenZL', () => {
  it('delegates to openzl.benchmark', async () => {
    const data = sampleData();
    // Mocked benchmark: verifies delegation only. Real timing behavior is
    // covered by the wasm_api binding tests.
    const compressor = new Uint8Array([9]);
    const expected = {
      iterations: 2,
      srcSize: data.length,
      compressedSize: 3,
      ratio: data.length / 3,
      compressMs: 1,
      decompressMs: 2,
      compressMBps: 3,
      decompressMBps: 4,
    };
    const openzl = {
      benchmark: vi.fn(() => expected),
    };
    const result = await benchmarkOpenZL(openzl, data, compressor, 2);
    expect(result).toBe(expected);
    expect(openzl.benchmark).toHaveBeenCalledWith(data, compressor, 2);
  });
});
