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
const csvNumber = z.string().trim().min(1, 'expected a number').pipe(z.coerce.number().min(0));
// The columns of a zli benchmark --output-csv row this client reads. The path
// column comes last and is not read, so a quoted path cannot shift the numbers.
const benchmarkRowSchema = z.object({
  srcSize: csvCount,
  compressedSize: csvCount,
  ctimeMs: csvNumber,
  dtimeMs: csvNumber,
  iters: csvCount.pipe(z.int().min(1)),
});

function parseOutput(schema, payload, binary) {
  const parsed = schema.safeParse(payload);
  if (parsed.success) {
    return parsed.data;
  }
  const [issue] = parsed.error.issues;
  const field = issue.path.length > 0 ? `${issue.path.join('.')}: ` : '';
  throw new Error(`${binary} returned an unexpected result (${field}${issue.message})`);
}

function executableName(executable) {
  return path.basename(executable).replace(/\.exe$/i, '');
}

// SIGTERM first, then SIGKILL if the child has not exited. Windows terminates on either.
function terminate(child) {
  child.kill('SIGTERM');
  const escalate = setTimeout(() => child.kill('SIGKILL'), KILL_GRACE_MS);
  // 'exit' rather than 'close', which waits for stdio and need not follow.
  child.once('exit', () => clearTimeout(escalate));
}

/** Runs a native benchmark binary, which reports failures on stderr. */
function runNative(executable, args, {signal, onChild}) {
  return new Promise((resolve, reject) => {
    if (signal?.aborted) {
      reject(signal.reason);
      return;
    }
    const child = spawn(executable, args, {
      stdio: ['ignore', 'ignore', 'pipe'],
      windowsHide: true,
    });
    child.stderr.setEncoding('utf8');
    let stderr = '';
    // Why the child was stopped; reported once it has actually exited.
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

    child.stderr.on('data', (chunk) => {
      process.stderr.write(chunk);
      stderr += chunk;
    });
    child.once('error', (error) => {
      cleanup();
      fail(error);
    });
    child.once('close', (code, exitSignal) => {
      cleanup();
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
        const detail = stderr.trim();
        fail(
          new Error(
            detail !== ''
              ? detail
              : `${executableName(executable)} exited unexpectedly (${exitSignal === null ? `code ${code}` : `signal ${exitSignal}`})`,
          ),
        );
        return;
      }
      settled = true;
      resolve();
    });
  });
}

function runZli(zliPath, args, context) {
  // --verbose 1 limits stderr to errors, so a failure message can be shown as-is.
  return runNative(zliPath, [...args, '--verbose', '1'], context);
}

function profileArguments(compressor) {
  const args = ['--profile', compressor.profile];
  // An empty --profile-arg is not the same as none: csv rejects it as a separator.
  if (compressor.profileArg !== '') {
    args.push('--profile-arg', compressor.profileArg);
  }
  return args;
}

/**
 * Reads the single row zli benchmark --output-csv writes for one input file.
 * zstd-benchmark writes the same columns.
 */
async function readBenchmarkCsv(csvPath, binary) {
  const [header, row = ''] = (await fs.readFile(csvPath, 'utf8')).split(/\r?\n/);
  const values = row.split(',');
  const fields = Object.fromEntries(header.split(',').map((name, index) => [name.trim(), values[index]]));
  const field = parseOutput(benchmarkRowSchema, fields, binary);
  return {
    iterations: field.iters,
    srcSize: field.srcSize,
    compressedSize: field.compressedSize,
    compressMs: field.ctimeMs,
    decompressMs: field.dtimeMs,
  };
}

function benchmarkArguments(level, iterations, csvPath) {
  return ['--level', String(level), '--num-iters', String(iterations), '--output-csv', csvPath];
}

async function benchmark(zliPath, inputPath, compressorArgs, level, iterations, csvPath, context) {
  await runZli(
    zliPath,
    ['benchmark', inputPath, ...compressorArgs, ...benchmarkArguments(level, iterations, csvPath)],
    context,
  );
  return readBenchmarkCsv(csvPath, 'zli');
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
 * Calls `body(inputPath, workDirectory)` with a scratch directory under
 * `tempRoot` that is removed afterwards. The input is always a file already on
 * disk, read where it is. On Windows the binaries read paths in the ANSI code
 * page, so a temp folder they could not use is refused before anything is
 * created or spawned, and an input they could not open is copied.
 */
async function inWorkDirectory(input, {tempRoot = os.tmpdir(), codePage} = {}, body) {
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
  const workDirectory = await fs.mkdtemp(path.join(tempRoot, 'openzl-playground-'));
  try {
    // Absolute paths never start with '-', so they cannot be parsed as flags.
    let inputPath = path.resolve(input.path);
    // The binaries would get a path the code page cannot represent mangled, so
    // they read a copy in the work directory, whose path passed the check above.
    // When the code page could not be checked, any path past ASCII is copied.
    if (onWindows && mayNotReachZliIntact(inputPath, ansiCodePage)) {
      const copy = path.join(workDirectory, 'input.bin');
      await fs.copyFile(inputPath, copy);
      inputPath = copy;
    }
    return await body(inputPath, workDirectory);
  } finally {
    await fs.rm(workDirectory, {recursive: true, force: true});
  }
}

/**
 * zli train benchmarks candidates at a fixed iteration count and has no
 * --level, so each candidate is re-benchmarked with the requested settings.
 * Aborting `signal` stops the running zli and rejects with `signal.reason`.
 * Scratch files go under `tempRoot`, which defaults to the OS temp directory.
 * `onStage` hears which phase is starting: benchmark, train, or candidates.
 */
function runOpenZLBenchmark(
  zliPath,
  input,
  compressor,
  iterations,
  {signal, onChild, onStage, tempRoot, codePage} = {},
) {
  const context = {signal, onChild};
  return inWorkDirectory(input, {tempRoot, codePage}, async (inputPath, workDirectory) => {
    if (!compressor.trainRequested) {
      onStage?.({stage: 'benchmark'});
      return [
        await benchmark(
          zliPath,
          inputPath,
          profileArguments(compressor),
          compressor.level,
          iterations,
          path.join(workDirectory, 'benchmark.csv'),
          context,
        ),
      ];
    }
    onStage?.({stage: 'train'});
    const candidatePaths = await trainedCandidatePaths(
      zliPath,
      inputPath,
      compressor,
      path.join(workDirectory, 'candidates'),
      context,
    );
    const results = [];
    for (const [index, candidatePath] of candidatePaths.entries()) {
      onStage?.({stage: 'candidates', candidate: index + 1, candidateCount: candidatePaths.length});
      results.push(
        await benchmark(
          zliPath,
          inputPath,
          ['--compressor', candidatePath],
          compressor.level,
          iterations,
          path.join(workDirectory, `candidate-${index}.csv`),
          context,
        ),
      );
    }
    return results;
  });
}

/**
 * Measures zstd with the zstd-benchmark binary built beside zli, so zstd rows
 * are timed the same way as OpenZL rows. Takes the same options as
 * runOpenZLBenchmark.
 */
function runZstdBenchmark(
  zstdBenchmarkPath,
  input,
  level,
  iterations,
  {signal, onChild, onStage, tempRoot, codePage} = {},
) {
  return inWorkDirectory(input, {tempRoot, codePage}, async (inputPath, workDirectory) => {
    onStage?.({stage: 'benchmark'});
    const csvPath = path.join(workDirectory, 'zstd.csv');
    await runNative(zstdBenchmarkPath, [inputPath, ...benchmarkArguments(level, iterations, csvPath)], {
      signal,
      onChild,
    });
    return readBenchmarkCsv(csvPath, 'zstd-benchmark');
  });
}

module.exports = {runOpenZLBenchmark, runZstdBenchmark};
