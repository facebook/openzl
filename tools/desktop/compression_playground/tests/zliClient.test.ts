// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment node

import {existsSync, readdirSync, rmSync} from 'node:fs';
import fsPromises from 'node:fs/promises';
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
    it('refuses an input path the code page cannot represent, before zli starts', async () => {
      await expect(
        runOpenZLBenchmark(fakeZli('process.exit(99);'), {path: '/data/Rīgā/input.bin'}, UNTRAINED, 4, {
          codePage: 1252,
        }),
      ).rejects.toThrow(
        'The input file path contains "ī", "ā", which this PC\'s ANSI code page (1252) cannot represent',
      );
    });

    it('passes an input path through as it is when the code page cannot be checked', async () => {
      // 99999 stands in for a code page iconv-lite does not know.
      const results = await runOpenZLBenchmark(fakeZli(), {path: '/data/Rīgā/input.bin'}, UNTRAINED, 4, {
        codePage: 99999,
      });

      expect(results).toHaveLength(1);
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

  it('reports why zli failed even when its scratch directory cannot be removed', async () => {
    // As on Windows, where a zli that was just killed can still hold a file open.
    const rm = vi.spyOn(fsPromises, 'rm').mockRejectedValue(new Error('EBUSY: resource busy or locked'));
    try {
      await expect(
        runOpenZLBenchmark(
          join(tmpdir(), 'no-such-zli'),
          {data: new Uint8Array([1])},
          {profile: 'serial', profileArg: '', level: 6, trainRequested: false, candidates: 10},
          1,
        ),
      ).rejects.toThrow(/ENOENT/);
      expect(rm).toHaveBeenCalled();
    } finally {
      rm.mockRestore();
      for (const [directory] of rm.mock.calls) {
        rmSync(directory, {recursive: true, force: true});
      }
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
    // Training reports what zli's progress bar shows, read off its stderr.
    expect(trained).toEqual([
      {stage: 'train'},
      {stage: 'train', fraction: 0},
      {stage: 'train', fraction: 0.5},
      {stage: 'train', fraction: 1},
      {stage: 'candidates', candidate: 1, candidateCount: 1},
    ]);

    const untrained: ZliStage[] = [];
    await runOpenZLBenchmark(fakeZli(), {data: new Uint8Array([1])}, UNTRAINED, 1, {
      onStage: (stage) => untrained.push(stage),
    });
    expect(untrained).toEqual([{stage: 'benchmark'}]);
  });

  it('reads training progress across ACE graphs as one fraction', async () => {
    const twoGraphs = fakeZli(`
const fs = require('node:fs');
const args = process.argv.slice(2);
const flag = (name) => args[args.indexOf(name) + 1];
if (args[0] === 'train') {
  for (const line of ['[----] Training ACE graph 1 / 2: ACE progress', '[====] Training ACE graph 1 / 2: ACE progress',
                      '[----] Training ACE graph 2 / 2: ACE progress', '[==--] Training ACE graph 2 / 2: ACE progress']) {
    process.stderr.write(line + '\\n');
  }
  fs.mkdirSync(flag('--output'), {recursive: true});
  fs.writeFileSync(flag('--output') + '/0.zc', '1');
} else {
  fs.writeFileSync(flag('--output-csv'), 'srcSize,compressedSize,compressionRatio,ctimeMs,dtimeMs,iters\\n3,1,3,1,1,1\\n');
}
`);
    const fractions: number[] = [];
    await runOpenZLBenchmark(twoGraphs, {data: new Uint8Array([1])}, {...UNTRAINED, trainRequested: true}, 1, {
      onStage: (stage) => stage.stage === 'train' && stage.fraction !== undefined && fractions.push(stage.fraction),
    });
    // Each graph's bar starts again from empty; the whole only moves forward.
    expect(fractions).toEqual([0, 0.5, 0.5, 0.75]);
  });

  it('hears each candidate as soon as it is benchmarked, before the next starts', async () => {
    const heard: string[] = [];
    await runOpenZLBenchmark(
      fakeZli(UNORDERED_CANDIDATES_ZLI),
      {data: new Uint8Array([1, 2, 3])},
      {...UNTRAINED, trainRequested: true},
      1,
      {
        onStage: (stage) => stage.stage === 'candidates' && heard.push(`start ${stage.candidate}`),
        onMeasurement: (result, index, total) =>
          heard.push(`measured ${index + 1} of ${total}: ${result.compressedSize}`),
      },
    );
    expect(heard).toEqual([
      'start 1',
      'measured 1 of 4: 1',
      'start 2',
      'measured 2 of 4: 2',
      'start 3',
      'measured 3 of 4: 3',
      'start 4',
      'measured 4 of 4: 11',
    ]);
  });

  it('reports only the fatal error when training fails, not its progress', async () => {
    const failing = fakeZli(`
process.stderr.write('Using all provided training samples, total size 1\\n');
process.stderr.write('[==--] Training ACE graph 1 / 1: ACE progress\\n');
process.stderr.write('I/O Exception:\\n\\tFailed to open input file\\n');
process.exit(1);
`);

    await expect(
      runOpenZLBenchmark(failing, {data: new Uint8Array([1])}, {...UNTRAINED, trainRequested: true}, 1),
    ).rejects.toThrow(/^I\/O Exception:\n\tFailed to open input file$/);
  });

  it('reports the signal, not its last INFO line, when zli is killed mid-training', async () => {
    // A crash or an out-of-memory kill leaves no error heading.
    const killed = fakeZli(`
process.stderr.write('Using all provided training samples, total size 1\\n');
process.stderr.write('[==--] Training ACE graph 1 / 1: ACE progress\\n');
process.kill(process.pid, 'SIGKILL');
`);

    await expect(
      runOpenZLBenchmark(killed, {data: new Uint8Array([1])}, {...UNTRAINED, trainRequested: true}, 1),
    ).rejects.toThrow('zli exited unexpectedly (signal SIGKILL)');
  });

  it('reports a one-line error zli prints without detail', async () => {
    const failing = fakeZli(`
process.stderr.write('[==--] Training ACE graph 1 / 1: ACE progress\\n');
process.stderr.write('No command specified\\n');
process.exit(1);
`);

    await expect(
      runOpenZLBenchmark(failing, {data: new Uint8Array([1])}, {...UNTRAINED, trainRequested: true}, 1),
    ).rejects.toThrow(/^No command specified$/);
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

  it('rejects a CSV that repeats a column, rather than reading whichever came last', async () => {
    const repeated = fakeZli(`
const args = process.argv.slice(2);
require('node:fs').writeFileSync(
  args[args.indexOf('--output-csv') + 1],
  'srcSize,compressedSize,compressionRatio,ctimeMs,dtimeMs,iters,srcSize\\n3,2,1.5,1,1,1,999\\n',
);
`);
    await expect(runOpenZLBenchmark(repeated, {data: new Uint8Array([1])}, UNTRAINED, 1)).rejects.toThrow(
      'zli returned an unexpected result (column srcSize appears twice)',
    );
  });

  it('rejects a timing that is not finite', async () => {
    const infiniteTiming = fakeZli(`
const args = process.argv.slice(2);
require('node:fs').writeFileSync(
  args[args.indexOf('--output-csv') + 1],
  'srcSize,compressedSize,compressionRatio,ctimeMs,dtimeMs,iters,path\\n3,2,1.5,Infinity,1,1,in\\n',
);
`);
    await expect(runOpenZLBenchmark(infiniteTiming, {data: new Uint8Array([1])}, UNTRAINED, 1)).rejects.toThrow(
      'zli returned an unexpected result (ctimeMs:',
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

  it('refuses an input path the Windows code page cannot represent, before zstd-benchmark starts', async () => {
    await expect(
      runZstdBenchmark(fakeZli('process.exit(99);', 'zstd-benchmark'), {path: '/data/Rīgā/input.bin'}, 3, 1, {
        codePage: 1252,
      }),
    ).rejects.toThrow('The input file path contains "ī", "ā"');
  });
});
