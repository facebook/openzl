// Copyright (c) Meta Platforms, Inc. and affiliates.

import {chmodSync, mkdtempSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

/**
 * Script for a zli stand-in. Training writes the given candidates beside a
 * benchmark.csv, as zli train --pareto-frontier does, and each candidate's
 * contents become its compressedSize. A benchmark writes zli's CSV header and
 * one row whose quoted path contains a comma, and fails if its --compressor
 * path does not exist.
 */
function fakeZliScript(candidateIndices: number[]): string {
  return `
const fs = require('node:fs');
const args = process.argv.slice(2);
// A test sets this to stand in for a zli that writes CRLF, as on Windows.
const eol = process.env.FAKE_ZLI_EOL ?? '\\n';
// Without the trailing path column, a stray CR would land on the last name read.
const pathColumn = process.env.FAKE_ZLI_PATH_COLUMN !== '0';
const flag = (name) => (args.includes(name) ? args[args.indexOf(name) + 1] : undefined);
if (args[0] === 'train') {
  // As zli does at --verbose 3 on a pipe: INFO lines and one line per redraw.
  if (flag('--verbose') === '3') {
    process.stderr.write('Using all provided training samples, total size 3\\n');
    for (const bar of ['[----]', '[==--]', '[====]']) {
      process.stderr.write(bar + ' Training ACE graph 1 / 1: ACE progress\\n');
    }
  }
  const output = flag('--output');
  fs.mkdirSync(output, {recursive: true});
  for (const index of ${JSON.stringify(candidateIndices)}) {
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
}

export const FAKE_ZLI = fakeZliScript([0]);

/** Like FAKE_ZLI, but training writes candidates 10, 2, 0 and 1, in that order. */
export const UNORDERED_CANDIDATES_ZLI = fakeZliScript([10, 2, 0, 1]);

/**
 * Stands in for a zli that ignores SIGTERM and never finishes. It writes a
 * `ready` file beside itself once SIGTERM is ignored, so a test can wait for
 * that before signalling it.
 */
export const HANGING_ZLI = `
process.on('SIGTERM', () => {});
require('node:fs').writeFileSync(require('node:path').join(__dirname, 'ready'), '');
setInterval(() => {}, 1000);
`;

/** Stands in for a zli whose benchmark CSV has a non-numeric srcSize. */
export const MALFORMED_ZLI = `
const args = process.argv.slice(2);
require('node:fs').writeFileSync(
  args[args.indexOf('--output-csv') + 1],
  'srcSize,compressedSize,compressionRatio,ctimeMs,dtimeMs,iters,path\\nthree,2,1.5,1,1,1,in\\n',
);
`;

/**
 * Stands in for zstd-benchmark. It fails unless called as
 * `<input> --level <n> --num-iters <n> --output-csv <path>`, then writes the
 * input's size as srcSize and the level as compressedSize, so a test can see
 * both arrived.
 */
export const FAKE_ZSTD_BENCHMARK = `
const fs = require('node:fs');
const [input, ...flags] = process.argv.slice(2);
const names = flags.filter((_, index) => index % 2 === 0);
if (JSON.stringify(names) !== JSON.stringify(['--level', '--num-iters', '--output-csv'])) {
  process.stderr.write('Unexpected arguments: ' + process.argv.slice(2).join(' ') + '\\n');
  process.exit(2);
}
const [level, iters, csv] = flags.filter((_, index) => index % 2 === 1);
fs.writeFileSync(
  csv,
  'srcSize,compressedSize,compressionRatio,ctimeMs,dtimeMs,iters\\n' +
    fs.statSync(input).size + ',' + level + ',1,2,1,' + iters + '\\n',
);
`;

export const UNTRAINED = {profile: 'serial', profileArg: '', level: 6, trainRequested: false, candidates: 10};

const fakeZliDirectories: string[] = [];

/** Removes every directory fakeZli created; call from afterEach. */
export function removeFakeZlis(): void {
  for (const directory of fakeZliDirectories.splice(0)) {
    rmSync(directory, {recursive: true, force: true});
  }
}

/** Writes `bytes` to a file for a run to read, and returns its path. Removed with the fakes. */
export function inputFile(bytes: Uint8Array): string {
  const directory = mkdtempSync(join(tmpdir(), 'zli-client-input-'));
  fakeZliDirectories.push(directory);
  const path = join(directory, 'input.bin');
  writeFileSync(path, bytes);
  return path;
}

/** Writes `script` as an executable stand-in called `name` and returns its path. */
export function fakeZli(script = FAKE_ZLI, name = 'zli'): string {
  const workDirectory = mkdtempSync(join(tmpdir(), 'zli-client-test-'));
  fakeZliDirectories.push(workDirectory);
  const zliPath = join(workDirectory, name);
  writeFileSync(zliPath, `#!${process.execPath}\n${script}`);
  chmodSync(zliPath, 0o755);
  return zliPath;
}
