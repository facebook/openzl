// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {spawn} = require('node:child_process');
const {assertReadableByZli, mayNotReachZliIntact, windowsAnsiCodePage} = require('./ansi-path.cjs');
const {z} = require('zod');

const TRAINING_THREADS = Math.min(4, Math.max(1, os.availableParallelism()));
const KILL_GRACE_MS = 2000;

const csvCount = z.string().trim().regex(/^\d+$/, 'expected a whole number').transform(Number);
const csvNumber = z.string().trim().min(1, 'expected a number').pipe(z.coerce.number().finite().min(0));
// The columns of a zli benchmark --output-csv row this client reads. The path
// column comes last and is not read, so a quoted path cannot shift the numbers.
const benchmarkRowSchema = z.object({
  srcSize: csvCount,
  compressedSize: csvCount,
  ctimeMs: csvNumber,
  dtimeMs: csvNumber,
  iters: csvCount.pipe(z.int().min(1)),
});

function parseOutput(schema, payload) {
  const parsed = schema.safeParse(payload);
  if (parsed.success) {
    return parsed.data;
  }
  const [issue] = parsed.error.issues;
  const field = issue.path.length > 0 ? `${issue.path.join('.')}: ` : '';
  throw new Error(`zli returned an unexpected result (${field}${issue.message})`);
}

// SIGTERM first, then SIGKILL if zli has not exited. Windows terminates on either.
function terminate(child) {
  child.kill('SIGTERM');
  const escalate = setTimeout(() => child.kill('SIGKILL'), KILL_GRACE_MS);
  // 'exit' rather than 'close', which waits for stdio and need not follow.
  child.once('exit', () => clearTimeout(escalate));
}

// At --verbose 3, zli prints a progress bar on stderr for each step of
// training, one line per redraw on a pipe:
// `[=====---------------------------------------------] Training ACE graph 1 / 2: ACE progress`.
const PROGRESS_LINE = /^\[([=-]+)\] /;
const GRAPH_COUNTER = /\bgraph (\d+) \/ (\d+)\b/;

/**
 * How far through training a zli progress line says it is, or undefined for
 * any other line. Each ACE graph gets a bar of its own that starts again from
 * empty, so a bar's fill is placed within its graph's share of the whole.
 */
function progressFraction(line) {
  const bar = PROGRESS_LINE.exec(line)?.[1];
  if (bar === undefined) {
    return undefined;
  }
  const fill = bar.replaceAll('-', '').length / bar.length;
  const [, graph, graphs] = GRAPH_COUNTER.exec(line)?.map(Number) ?? [];
  return graph >= 1 && graphs >= graph ? (graph - 1 + fill) / graphs : fill;
}

/**
 * What zli printed about why it failed. Its progress and other INFO lines come
 * first; a fatal error comes last, usually as a heading and its tab-indented
 * detail (`I/O Exception:` then `\tFailed to open input file ...`), otherwise
 * as one line. Empty when zli was killed by a signal (a crash, or out of
 * memory) and printed no heading: whatever INFO line came last says nothing
 * about that, so the signal is reported instead.
 */
function failureFromVerboseStderr(stderr, killed) {
  const lines = stderr
    .trim()
    .split(/\r?\n/)
    .filter((line) => progressFraction(line) === undefined);
  const heading = lines.findLastIndex((line, index) => !line.startsWith('\t') && lines[index + 1]?.startsWith('\t'));
  if (heading !== -1) {
    return lines.slice(heading).join('\n');
  }
  return killed ? '' : (lines.at(-1) ?? '');
}

/**
 * Runs zli, which reports failures on stderr. With `onProgress`, zli runs at
 * --verbose 3: each progress line is reported as a fraction, and only the
 * fatal error is kept as the failure message.
 */
function runZli(zliPath, args, {signal, onChild, onProgress}) {
  return new Promise((resolve, reject) => {
    if (signal?.aborted) {
      reject(signal.reason);
      return;
    }
    // --verbose 1 limits stderr to errors, so a failure message can be shown
    // as-is; 3 adds the progress lines `onProgress` reads.
    const child = spawn(zliPath, [...args, '--verbose', onProgress === undefined ? '1' : '3'], {
      stdio: ['ignore', 'ignore', 'pipe'],
      windowsHide: true,
    });
    child.stderr.setEncoding('utf8');
    let stderr = '';
    // Why zli was stopped; reported once it has actually exited.
    let stopReason;
    let settled = false;
    const fail = (error) => {
      if (!settled) {
        settled = true;
        reject(error);
      }
    };
    const stop = (reason) => {
      if (stopReason === undefined) {
        stopReason = reason;
        terminate(child);
      }
    };
    const onAbort = () => stop(signal.reason);
    signal?.addEventListener('abort', onAbort, {once: true});
    const cleanup = () => {
      signal?.removeEventListener('abort', onAbort);
    };
    if (child.pid !== undefined) {
      onChild?.({pid: child.pid, state: 'spawned'});
    }

    // The end of the last chunk, until the rest of its line arrives.
    let partialLine = '';
    child.stderr.on('data', (chunk) => {
      stderr += chunk;
      if (onProgress === undefined) {
        process.stderr.write(chunk);
        return;
      }
      const lines = (partialLine + chunk).split(/\r?\n/);
      partialLine = lines.pop();
      for (const line of lines) {
        const fraction = progressFraction(line);
        if (fraction === undefined) {
          process.stderr.write(`${line}\n`);
        } else {
          onProgress(fraction);
        }
      }
    });
    child.once('error', (error) => {
      cleanup();
      fail(error);
    });
    child.once('close', (code, exitSignal) => {
      cleanup();
      // A last line with no newline after it, which the 'data' handler held back.
      if (partialLine !== '' && progressFraction(partialLine) === undefined) {
        process.stderr.write(`${partialLine}\n`);
      }
      if (child.pid !== undefined) {
        onChild?.({pid: child.pid, state: 'exited'});
      }
      if (settled) {
        return;
      }
      if (stopReason !== undefined) {
        fail(stopReason);
        return;
      }
      if (code !== 0) {
        const detail = onProgress === undefined ? stderr.trim() : failureFromVerboseStderr(stderr, exitSignal !== null);
        fail(
          new Error(
            detail !== ''
              ? detail
              : `zli exited unexpectedly (${exitSignal === null ? `code ${code}` : `signal ${exitSignal}`})`,
          ),
        );
        return;
      }
      settled = true;
      resolve();
    });
  });
}

function profileArguments(compressor) {
  const args = ['--profile', compressor.profile];
  // An empty --profile-arg is not the same as none: csv rejects it as a separator.
  if (compressor.profileArg !== '') {
    args.push('--profile-arg', compressor.profileArg);
  }
  return args;
}

/** Reads the single row zli benchmark --output-csv writes for one input file. */
async function readBenchmarkCsv(csvPath) {
  const [header, row = ''] = (await fs.readFile(csvPath, 'utf8')).split(/\r?\n/);
  const values = row.split(',');
  const fields = Object.fromEntries(header.split(',').map((name, index) => [name.trim(), values[index]]));
  const field = parseOutput(benchmarkRowSchema, fields);
  return {
    iterations: field.iters,
    srcSize: field.srcSize,
    compressedSize: field.compressedSize,
    compressMs: field.ctimeMs,
    decompressMs: field.dtimeMs,
  };
}

async function benchmark(zliPath, inputPath, compressorArgs, level, iterations, csvPath, context) {
  await runZli(
    zliPath,
    [
      'benchmark',
      inputPath,
      ...compressorArgs,
      '--level',
      String(level),
      '--num-iters',
      String(iterations),
      '--output-csv',
      csvPath,
    ],
    context,
  );
  return readBenchmarkCsv(csvPath);
}

async function trainedCandidatePaths(zliPath, inputPath, compressor, outputDirectory, context) {
  await runZli(
    zliPath,
    [
      'train',
      inputPath,
      ...profileArguments(compressor),
      '--output',
      outputDirectory,
      '--pareto-frontier',
      '--max-num-candidates',
      String(compressor.candidates),
      '--threads',
      String(TRAINING_THREADS),
      '--use-all-samples',
    ],
    context,
  );
  // zli train --pareto-frontier writes candidate i to <output>/<i>.zc.
  const indices = (await fs.readdir(outputDirectory))
    .flatMap((name) => /^(\d+)\.zc$/.exec(name)?.[1] ?? [])
    .map(Number)
    .sort((a, b) => a - b);
  if (indices.length === 0) {
    throw new Error('OpenZL training produced no candidates');
  }
  return indices.map((index) => path.join(outputDirectory, `${index}.zc`));
}

/**
 * A path zli can read `input` at, made once so every row of a run reads the
 * same file rather than each writing or copying its own: bytes are written to
 * a file, and on Windows, where zli reads paths in the ANSI code page, a path
 * the code page may not represent is copied to one it can. When the code page
 * could not be checked, any path past ASCII is copied. Throws before writing
 * anything if the temp folder itself cannot be represented, since every file
 * of the run lives under it. `dispose` removes whatever it wrote.
 */
async function prepareInput(input, {tempRoot = os.tmpdir(), codePage} = {}) {
  const onWindows = codePage !== undefined || process.platform === 'win32';
  const ansiCodePage = codePage ?? (onWindows ? await windowsAnsiCodePage() : undefined);
  if (ansiCodePage !== undefined) {
    assertReadableByZli(
      tempRoot,
      'The temporary folder path',
      'Set the TEMP environment variable to a folder whose path uses only characters in that code page.',
      ansiCodePage,
    );
  }
  // Absolute paths never start with '-', so zli cannot parse them as flags.
  const inputPath = 'path' in input ? path.resolve(input.path) : undefined;
  if (inputPath !== undefined && !(onWindows && mayNotReachZliIntact(inputPath, ansiCodePage))) {
    return {path: inputPath, dispose: () => Promise.resolve()};
  }
  const directory = await fs.mkdtemp(path.join(tempRoot, 'openzl-input-'));
  const dispose = () => fs.rm(directory, {recursive: true, force: true});
  const preparedPath = path.join(directory, 'input.bin');
  try {
    if (inputPath === undefined) {
      await fs.writeFile(preparedPath, Buffer.from(input.data.buffer, input.data.byteOffset, input.data.byteLength));
    } else {
      await fs.copyFile(inputPath, preparedPath);
    }
  } catch (error) {
    await dispose();
    throw error;
  }
  return {path: preparedPath, dispose};
}

/**
 * zli train benchmarks candidates at a fixed iteration count and has no
 * --level, so each candidate is re-benchmarked with the requested settings.
 * Aborting `signal` stops the running zli and rejects with `signal.reason`.
 * Scratch files go under `tempRoot`, which defaults to the OS temp directory.
 * `onStage` hears which phase is starting: benchmark, train, or candidates;
 * then, while training, each fraction zli's progress bar shows.
 * `onMeasurement` hears each measurement as soon as it is made, with its index
 * and how many there will be, rather than when the last candidate is done.
 */
async function runOpenZLBenchmark(
  zliPath,
  input,
  compressor,
  iterations,
  {signal, onChild, onStage, onMeasurement, tempRoot = os.tmpdir(), codePage} = {},
) {
  const context = {signal, onChild};
  // Before anything is created or spawned. A path the worker already
  // prepared passes through as it is.
  const prepared = await prepareInput(input, {tempRoot, codePage});
  let workDirectory;
  try {
    workDirectory = await fs.mkdtemp(path.join(tempRoot, 'openzl-playground-'));
    const inputPath = prepared.path;

    if (!compressor.trainRequested) {
      onStage?.({stage: 'benchmark'});
      const result = await benchmark(
        zliPath,
        inputPath,
        profileArguments(compressor),
        compressor.level,
        iterations,
        path.join(workDirectory, 'benchmark.csv'),
        context,
      );
      onMeasurement?.(result, 0, 1);
      return [result];
    }
    onStage?.({stage: 'train'});
    const candidatePaths = await trainedCandidatePaths(
      zliPath,
      inputPath,
      compressor,
      path.join(workDirectory, 'candidates'),
      {...context, onProgress: (fraction) => onStage?.({stage: 'train', fraction})},
    );
    const results = [];
    for (const [index, candidatePath] of candidatePaths.entries()) {
      onStage?.({stage: 'candidates', candidate: index + 1, candidateCount: candidatePaths.length});
      const result = await benchmark(
        zliPath,
        inputPath,
        ['--compressor', candidatePath],
        compressor.level,
        iterations,
        path.join(workDirectory, `candidate-${index}.csv`),
        context,
      );
      onMeasurement?.(result, index, candidatePaths.length);
      results.push(result);
    }
    return results;
  } finally {
    if (workDirectory !== undefined) {
      await fs.rm(workDirectory, {recursive: true, force: true});
    }
    await prepared.dispose();
  }
}

module.exports = {prepareInput, runOpenZLBenchmark};
