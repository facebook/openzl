// Copyright (c) Meta Platforms, Inc. and affiliates.

import {beforeAll, describe, expect, it, vi} from 'vitest';
import {getOpenZLMaxIterations} from '../../../wasm/js/wasm_api.js';
import {benchmarkGzip, benchmarkOpenZL, benchmarkZstd} from '../src/benchmark.ts';
import {ITERATIONS_MAX} from '../src/compressors.ts';

const {mockGetOpenZLMaxIterations, mockInitZstd} = vi.hoisted(() => ({
  mockGetOpenZLMaxIterations: vi.fn(() => Promise.resolve(3)),
  mockInitZstd: vi.fn(),
}));

// fake module for testing
vi.mock('../../../wasm/js/wasm_api.js', () => ({
  getOpenZLMaxIterations: mockGetOpenZLMaxIterations,
}));

// Only `init` is stood in for, and by default it still runs the real one, so
// every other zstd case here round-trips for real.
vi.mock('@bokuweb/zstd-wasm', async (importOriginal) => {
  const actual = await importOriginal<typeof import('@bokuweb/zstd-wasm')>();
  mockInitZstd.mockImplementation(actual.init);
  return {...actual, init: mockInitZstd};
});

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

  it('caps itself without the limit when it will not load, and retries next time', async () => {
    // The ceiling is the only thing gzip wants from the OpenZL module, so a
    // module that will not load costs the live cap, not the measurement. The
    // memo is cleared on the way out, so the next call goes and asks again.
    vi.resetModules();
    mockGetOpenZLMaxIterations.mockReset();
    mockGetOpenZLMaxIterations.mockRejectedValueOnce(new Error('temporary load failure')).mockResolvedValue(3);
    const {benchmarkGzip: freshBenchmarkGzip} = await import('../src/benchmark.ts');
    const data = sampleData();

    await expect(freshBenchmarkGzip(data, 6, 20)).resolves.toMatchObject({iterations: ITERATIONS_MAX});
    await expect(freshBenchmarkGzip(data, 6, 20)).resolves.toMatchObject({iterations: 3});
    expect(mockGetOpenZLMaxIterations).toHaveBeenCalledTimes(2);
  });
});

describe('benchmarkZstd', () => {
  it('rejects levels outside 1..19 before loading wasm', async () => {
    const data = sampleData();
    mockInitZstd.mockClear();
    await expect(benchmarkZstd(data, 0, 1)).rejects.toThrow('1 to 19');
    await expect(benchmarkZstd(data, 20, 1)).rejects.toThrow('1 to 19');
    // The point of validating first: a rejected level must not spend one of
    // the few initialisations the module survives.
    expect(mockInitZstd).not.toHaveBeenCalled();
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

describe('zstd initialisation', () => {
  // The module is brought up here rather than by whichever case ran first:
  // both tests below read `init`'s call count, and the module survives only a
  // few real initialisations, so the one they share is stated.
  beforeAll(async () => {
    await benchmarkZstd(sampleData(), 3, 1);
  });

  it('retries after a failed load instead of caching the failure', async () => {
    // The memo would otherwise hand the rejected promise to every later call,
    // and only a reload would clear it. `getMaxIterations` above is guarded
    // the same way and has the same test.
    vi.resetModules();
    mockInitZstd.mockClear();
    mockInitZstd.mockRejectedValueOnce(new Error('temporary load failure'));
    // The retry resolves without running the real initialiser: the round-trip
    // below only needs the module the hook above brought up, and a second real
    // one would spend an initialisation this module has few of. The same
    // reason `getMaxIterations`'s test stubs its own retry.
    mockInitZstd.mockResolvedValueOnce(undefined);
    const {benchmarkZstd: freshBenchmarkZstd} = await import('../src/benchmark.ts');
    const data = sampleData();

    await expect(freshBenchmarkZstd(data, 3, 1)).rejects.toThrow('temporary load failure');
    await expect(freshBenchmarkZstd(data, 3, 1)).resolves.toMatchObject({iterations: 1});
    expect(mockInitZstd).toHaveBeenCalledTimes(2);
  });

  it('survives more benchmark runs than it takes to break the module', async () => {
    // `init()` re-enters the module's own initialiser every time it is called,
    // and the promise it awaits resolved once at import, so nothing waits for
    // the rebuild. The third one used to leave the module permanently unable
    // to read a frame back -- which a benchmark reaches by design, since a run
    // is a repeat of the last one.
    const data = sampleData();
    mockInitZstd.mockClear();
    for (let run = 0; run < 4; run += 1) {
      const result = await benchmarkZstd(data, 5, 1);
      expect(result.ratio).toBeGreaterThan(1);
    }
    // Said directly as well as survived: the hook above brought the module up,
    // and the memo is what keeps these four runs off `init()`.
    expect(mockInitZstd).not.toHaveBeenCalled();
  });
});
