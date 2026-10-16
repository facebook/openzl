// Copyright (c) Meta Platforms, Inc. and affiliates.

// @vitest-environment node

import {chmodSync, mkdtempSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {afterEach, describe, expect, it} from 'vitest';
import {runOpenZLBenchmark} from '../electron/zli-client.cjs';

/**
 * Stands in for zli. Training writes candidates 0, 1, 2 and 10 beside a
 * benchmark.csv, as zli train --pareto-frontier does, and each candidate's
 * contents become its compressedSize. A benchmark writes zli's CSV header and
 * one row whose quoted path contains a comma, and fails if its --compressor
 * path does not exist.
 */
const FAKE_ZLI = `
const fs = require('node:fs');
const args = process.argv.slice(2);
// A test sets this to stand in for a zli that writes CRLF, as on Windows.
const eol = process.env.FAKE_ZLI_EOL ?? '\\n';
// Without the trailing path column, a stray CR would land on the last name read.
const pathColumn = process.env.FAKE_ZLI_PATH_COLUMN !== '0';
const flag = (name) => (args.includes(name) ? args[args.indexOf(name) + 1] : undefined);
if (args[0] === 'train') {
  const output = flag('--output');
  fs.mkdirSync(output, {recursive: true});
  for (const index of [10, 2, 0, 1]) {
    fs.writeFileSync(output + '/' + index + '.zc', String(index + 1));
  }
  fs.writeFileSync(output + '/benchmark.csv', '');
} else {
  const compressor = flag('--compressor');
  if (compressor !== undefined && !fs.existsSync(compressor)) {
    process.stderr.write('No such compressor: ' + compressor + '\\n');
    process.exit(1);
  }
  const compressedSize = compressor === undefined ? 2 : fs.readFileSync(compressor, 'utf8');
  fs.writeFileSync(
    flag('--output-csv'),
    'srcSize,compressedSize,compressionRatio,ctimeMs,dtimeMs,iters' + (pathColumn ? ',path' : '') + eol +
      '3,' + compressedSize + ',1.5,1,1,' + flag('--num-iters') + (pathColumn ? ',"/tmp/a,b"' : '') + eol,
  );
}
`;

let workDirectory: string | undefined;

afterEach(() => {
  if (workDirectory !== undefined) {
    rmSync(workDirectory, {recursive: true, force: true});
  }
});

function fakeZli(): string {
  workDirectory = mkdtempSync(join(tmpdir(), 'zli-client-test-'));
  const zliPath = join(workDirectory, 'zli');
  writeFileSync(zliPath, `#!${process.execPath}\n${FAKE_ZLI}`);
  chmodSync(zliPath, 0o755);
  return zliPath;
}

describe('runOpenZLBenchmark', () => {
  it('reads a CSV written with CRLF line endings and no path column', async () => {
    // On Windows zli writes CRLF, which would otherwise leave a CR on `iters`.
    process.env.FAKE_ZLI_EOL = '\r\n';
    process.env.FAKE_ZLI_PATH_COLUMN = '0';
    try {
      const results = await runOpenZLBenchmark(
        fakeZli(),
        {data: new Uint8Array([1, 2, 3])},
        {profile: 'serial', profileArg: '', level: 6, trainRequested: false, candidates: 10},
        4,
      );

      expect(results).toEqual([{iterations: 4, srcSize: 3, compressedSize: 2, compressMs: 1, decompressMs: 1}]);
    } finally {
      delete process.env.FAKE_ZLI_EOL;
      delete process.env.FAKE_ZLI_PATH_COLUMN;
    }
  });

  it('benchmarks every trained candidate in index order', async () => {
    const results = await runOpenZLBenchmark(
      fakeZli(),
      {data: new Uint8Array([1, 2, 3])},
      {profile: 'serial', profileArg: '', level: 6, trainRequested: true, candidates: 10},
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
});
