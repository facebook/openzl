// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment node

import {existsSync, readdirSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {dirname, join} from 'node:path';
import {afterEach, describe, expect, it, vi} from 'vitest';
import {runOpenZLBenchmark, type ZliChildEvent, type ZliStage} from '../electron/zli-client.cjs';
import {fakeZli, HANGING_ZLI, removeFakeZlis, UNORDERED_CANDIDATES_ZLI, UNTRAINED} from './fakeZli.ts';

afterEach(removeFakeZlis);

describe('runOpenZLBenchmark', () => {
  it('reads a CSV written with CRLF line endings and no path column', async () => {
    // On Windows zli writes CRLF, which would otherwise leave a CR on `iters`.
    process.env.FAKE_ZLI_EOL = '\r\n';
    process.env.FAKE_ZLI_PATH_COLUMN = '0';
    try {
      const results = await runOpenZLBenchmark(fakeZli(), {data: new Uint8Array([1, 2, 3])}, UNTRAINED, 4);

      expect(results).toEqual([{iterations: 4, srcSize: 3, compressedSize: 2, compressMs: 1, decompressMs: 1}]);
    } finally {
      delete process.env.FAKE_ZLI_EOL;
      delete process.env.FAKE_ZLI_PATH_COLUMN;
    }
  });

  it('benchmarks every trained candidate in index order', async () => {
    const results = await runOpenZLBenchmark(
      fakeZli(UNORDERED_CANDIDATES_ZLI),
      {data: new Uint8Array([1, 2, 3])},
      {...UNTRAINED, trainRequested: true},
      4,
    );

    expect(results).toEqual(
      [1, 2, 3, 11].map((compressedSize) => ({
        iterations: 4,
        srcSize: 3,
        compressedSize,
        compressMs: 1,
        decompressMs: 1,
      })),
    );
  });

  it('reports each stage as it starts', async () => {
    const trained: ZliStage[] = [];
    await runOpenZLBenchmark(fakeZli(), {data: new Uint8Array([1])}, {...UNTRAINED, trainRequested: true}, 1, {
      onStage: (stage) => trained.push(stage),
    });
    expect(trained).toEqual([{stage: 'train'}, {stage: 'candidates', candidate: 1, candidateCount: 1}]);

    const untrained: ZliStage[] = [];
    await runOpenZLBenchmark(fakeZli(), {data: new Uint8Array([1])}, UNTRAINED, 1, {
      onStage: (stage) => untrained.push(stage),
    });
    expect(untrained).toEqual([{stage: 'benchmark'}]);
  });

  it('kills a zli that ignores SIGTERM when the run is aborted, and cleans up', async () => {
    const zliPath = fakeZli(HANGING_ZLI);
    const runDirectories = () => readdirSync(tmpdir()).filter((entry) => entry.startsWith('openzl-playground-'));
    const before = runDirectories();
    const controller = new AbortController();
    const events: ZliChildEvent[] = [];
    const run = runOpenZLBenchmark(zliPath, {data: new Uint8Array([1, 2, 3])}, UNTRAINED, 1, {
      signal: controller.signal,
      onChild: (event) => events.push(event),
    });
    await vi.waitFor(() => expect(events).toHaveLength(1));
    // Aborting before the stand-in ignores SIGTERM would kill it at once.
    await vi.waitFor(() => expect(existsSync(join(dirname(zliPath), 'ready'))).toBe(true));
    const [{pid}] = events;

    const abortedAt = Date.now();
    controller.abort(new Error('Benchmark cancelled'));
    await expect(run).rejects.toThrow('Benchmark cancelled');

    // SIGTERM was ignored, so only the SIGKILL after the grace period ended it.
    expect(Date.now() - abortedAt).toBeGreaterThanOrEqual(1900);
    expect(() => process.kill(pid, 0)).toThrow();
    expect(events).toEqual([
      {pid, state: 'spawned'},
      {pid, state: 'exited'},
    ]);
    expect(runDirectories()).toEqual(before);
  }, 10_000);

  it('does not start zli once the run is aborted', async () => {
    const controller = new AbortController();
    controller.abort(new Error('Benchmark cancelled'));
    const onChild = vi.fn();

    await expect(
      runOpenZLBenchmark(fakeZli(HANGING_ZLI), {data: new Uint8Array([1])}, UNTRAINED, 1, {
        signal: controller.signal,
        onChild,
      }),
    ).rejects.toThrow('Benchmark cancelled');
    expect(onChild).not.toHaveBeenCalled();
  });
});
