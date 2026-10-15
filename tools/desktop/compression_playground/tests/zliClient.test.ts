// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment node

import {existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {dirname, join} from 'node:path';
import {afterEach, describe, expect, it, vi} from 'vitest';
import {runOpenZLBenchmark, runZstdBenchmark, type ZliChildEvent, type ZliStage} from '../electron/zli-client.cjs';
import {
  FAKE_ZSTD_BENCHMARK,
  fakeZli,
  HANGING_ZLI,
  MALFORMED_ZLI,
  removeFakeZlis,
  UNORDERED_CANDIDATES_ZLI,
  UNTRAINED,
} from './fakeZli.ts';

afterEach(removeFakeZlis);

describe('runOpenZLBenchmark', () => {
  describe('on Windows, where zli reads paths in the ANSI code page', () => {
    // 99999 stands in for a code page that could not be checked: unknown, or
    // one this runtime cannot decode. Copying is safe, so it copies anyway.
    it.each([1252, 99999])('runs zli on a copy of an input path code page %i may not represent', async (codePage) => {
      const directory = mkdtempSync(join(tmpdir(), 'Rīgā-'));
      const input = join(directory, 'input.bin');
      writeFileSync(input, 'abc');
      const seen = join(directory, 'seen.json');
      const zli = fakeZli(`
const fs = require('node:fs');
const args = process.argv.slice(2);
fs.writeFileSync(${JSON.stringify(seen)}, JSON.stringify({path: args[1], contents: fs.readFileSync(args[1], 'utf8')}));
fs.writeFileSync(
  args[args.indexOf('--output-csv') + 1],
  'srcSize,compressedSize,compressionRatio,ctimeMs,dtimeMs,iters\\n3,2,1.5,1,1,1\\n',
);
`);
      try {
        const results = await runOpenZLBenchmark(zli, {path: input}, UNTRAINED, 1, {codePage});

        expect(results).toHaveLength(1);
        const read = JSON.parse(readFileSync(seen, 'utf8'));
        expect(read.path).not.toBe(input);
        expect(read.path).toMatch(/^[ -~]+$/);
        expect(read.contents).toBe('abc');
      } finally {
        rmSync(directory, {recursive: true, force: true});
      }
    });

    it('refuses a temp folder it cannot represent, which every file of the run lives under', async () => {
      await expect(
        runOpenZLBenchmark(fakeZli('process.exit(99);'), {path: '/data/input.bin'}, UNTRAINED, 4, {
          codePage: 1252,
          tempRoot: '/tmp/Ünïcödé-ā',
        }),
      ).rejects.toThrow('The temporary folder path contains');
    });

    it('lets through a path the code page can represent', async () => {
      const results = await runOpenZLBenchmark(fakeZli(), {path: '/data/Алексей/input.bin'}, UNTRAINED, 4, {
        codePage: 1251,
      });

      expect(results).toHaveLength(1);
    });

    it('does not check paths when it is not told a code page and is not on Windows', async () => {
      const results = await runOpenZLBenchmark(fakeZli(), {path: '/data/Rīgā/input.bin'}, UNTRAINED, 4);

      expect(results).toHaveLength(1);
    });
  });

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

  it('rejects a zli benchmark CSV that breaks its format, naming the field', async () => {
    await expect(runOpenZLBenchmark(fakeZli(MALFORMED_ZLI), {data: new Uint8Array([1])}, UNTRAINED, 1)).rejects.toThrow(
      'zli returned an unexpected result (srcSize:',
    );
  });

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

describe('runZstdBenchmark', () => {
  const runDirectories = () => readdirSync(tmpdir()).filter((entry) => entry.startsWith('openzl-playground-'));

  it('benchmarks bytes from a scratch file at the requested level, and cleans up', async () => {
    const before = runDirectories();
    const stages: ZliStage[] = [];

    const result = await runZstdBenchmark(
      fakeZli(FAKE_ZSTD_BENCHMARK, 'zstd-benchmark'),
      {data: new Uint8Array(5)},
      7,
      4,
      {onStage: (stage) => stages.push(stage)},
    );

    expect(result).toEqual({iterations: 4, srcSize: 5, compressedSize: 7, compressMs: 2, decompressMs: 1});
    expect(stages).toEqual([{stage: 'benchmark'}]);
    expect(runDirectories()).toEqual(before);
  });

  it('reports what zstd-benchmark wrote to stderr when it fails', async () => {
    const failing = fakeZli("process.stderr.write('Cannot open input file: x\\n'); process.exit(1);", 'zstd-benchmark');

    await expect(runZstdBenchmark(failing, {data: new Uint8Array(1)}, 3, 1)).rejects.toThrow(
      'Cannot open input file: x',
    );
  });

  it('names zstd-benchmark when its CSV breaks the format', async () => {
    await expect(
      runZstdBenchmark(fakeZli(MALFORMED_ZLI, 'zstd-benchmark'), {data: new Uint8Array(1)}, 3, 1),
    ).rejects.toThrow('zstd-benchmark returned an unexpected result (srcSize:');
  });

  it('runs on a copy of an input path the Windows code page cannot represent', async () => {
    const directory = mkdtempSync(join(tmpdir(), 'Rīgā-'));
    const input = join(directory, 'input.bin');
    writeFileSync(input, 'abc');
    // Fails unless the path it is given is one the code page can represent.
    const asciiOnly = fakeZli(
      `if (!/^[ -~]+$/.test(process.argv[2])) process.exit(3);\n${FAKE_ZSTD_BENCHMARK}`,
      'zstd-benchmark',
    );
    try {
      const result = await runZstdBenchmark(asciiOnly, {path: input}, 3, 1, {codePage: 1252});

      expect(result.srcSize).toBe(3);
    } finally {
      rmSync(directory, {recursive: true, force: true});
    }
  });
});
