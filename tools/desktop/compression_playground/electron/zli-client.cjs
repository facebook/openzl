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

function runZli(zliPath, args, {signal, onChild}) {
  return new Promise((resolve, reject) => {
    if (signal?.aborted) {
      reject(signal.reason);
      return;
    }
    // --verbose 1 limits stderr to errors, so a failure message can be shown as-is.
    const child = spawn(zliPath, [...args, '--verbose', '1'], {
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
 * zli train benchmarks candidates at a fixed iteration count and has no
 * --level, so each candidate is re-benchmarked with the requested settings.
 * Aborting `signal` stops the running zli and rejects with `signal.reason`.
 * Scratch files go under `tempRoot`, which defaults to the OS temp directory.
 * `onStage` hears which phase is starting: benchmark, train, or candidates.
 */
async function runOpenZLBenchmark(
  zliPath,
  input,
  compressor,
  iterations,
  {signal, onChild, onStage, tempRoot = os.tmpdir(), codePage} = {},
) {
  const context = {signal, onChild};
  // Before anything is created or spawned: on Windows zli reads paths in the
  // ANSI code page, and every file of the run lives under the temp folder.
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
    let inputPath;
    if ('path' in input) {
      // Absolute paths never start with '-', so zli cannot parse them as flags.
      inputPath = path.resolve(input.path);
      // zli would get a path the code page cannot represent mangled, so it
      // reads a copy in the work directory, whose path passed the check above.
      // When the code page could not be checked, any path past ASCII is copied.
      if (onWindows && mayNotReachZliIntact(inputPath, ansiCodePage)) {
        const copy = path.join(workDirectory, 'input.bin');
        await fs.copyFile(inputPath, copy);
        inputPath = copy;
      }
    } else {
      inputPath = path.join(workDirectory, 'input.bin');
      await fs.writeFile(inputPath, Buffer.from(input.data.buffer, input.data.byteOffset, input.data.byteLength));
    }

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
  } finally {
    await fs.rm(workDirectory, {recursive: true, force: true});
  }
}

module.exports = {runOpenZLBenchmark};
