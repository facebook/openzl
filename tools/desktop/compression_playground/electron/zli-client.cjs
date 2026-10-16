// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {spawn} = require('node:child_process');

const TRAINING_THREADS = Math.min(4, Math.max(1, os.availableParallelism()));
// The leading columns of zli benchmark --output-csv. The path column after them
// is not read, so a quoted path cannot shift the numbers.
const CSV_COLUMNS = ['srcSize', 'compressedSize', 'compressionRatio', 'ctimeMs', 'dtimeMs', 'iters'];

function runZli(zliPath, args) {
  return new Promise((resolve, reject) => {
    // --verbose 1 limits stderr to errors, so a failure message can be shown as-is.
    const child = spawn(zliPath, [...args, '--verbose', '1'], {
      stdio: ['ignore', 'ignore', 'pipe'],
      windowsHide: true,
    });
    child.stderr.setEncoding('utf8');
    let stderr = '';
    let settled = false;
    const fail = (error) => {
      if (!settled) {
        settled = true;
        reject(error);
      }
    };
    child.stderr.on('data', (chunk) => {
      process.stderr.write(chunk);
      stderr += chunk;
    });
    child.once('error', (error) => {
      fail(error);
    });
    child.once('close', (code, signal) => {
      if (settled) {
        return;
      }
      if (code !== 0) {
        const detail = stderr.trim();
        fail(
          new Error(
            detail !== ''
              ? detail
              : `zli exited unexpectedly (${signal === null ? `code ${code}` : `signal ${signal}`})`,
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
  const names = header.split(',').slice(0, CSV_COLUMNS.length);
  const values = row.split(',').slice(0, CSV_COLUMNS.length);
  if (
    names.join() !== CSV_COLUMNS.join() ||
    values.length !== CSV_COLUMNS.length ||
    values.some((value) => value.trim() === '' || !(Number(value) >= 0))
  ) {
    throw new Error('zli returned an unexpected result');
  }
  const field = Object.fromEntries(CSV_COLUMNS.map((name, index) => [name, Number(values[index])]));
  return {
    iterations: field.iters,
    srcSize: field.srcSize,
    compressedSize: field.compressedSize,
    compressMs: field.ctimeMs,
    decompressMs: field.dtimeMs,
  };
}

async function benchmark(zliPath, inputPath, compressorArgs, level, iterations, csvPath) {
  await runZli(zliPath, [
    'benchmark',
    inputPath,
    ...compressorArgs,
    '--level',
    String(level),
    '--num-iters',
    String(iterations),
    '--output-csv',
    csvPath,
  ]);
  return readBenchmarkCsv(csvPath);
}

async function trainedCandidatePaths(zliPath, inputPath, compressor, outputDirectory) {
  await runZli(zliPath, [
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
  ]);
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
 */
async function runOpenZLBenchmark(zliPath, input, compressor, iterations) {
  const workDirectory = await fs.mkdtemp(path.join(os.tmpdir(), 'openzl-playground-'));
  try {
    let inputPath;
    if ('path' in input) {
      // Absolute paths never start with '-', so zli cannot parse them as flags.
      inputPath = path.resolve(input.path);
    } else {
      inputPath = path.join(workDirectory, 'input.bin');
      await fs.writeFile(inputPath, Buffer.from(input.data.buffer, input.data.byteOffset, input.data.byteLength));
    }

    if (!compressor.trainRequested) {
      return [
        await benchmark(
          zliPath,
          inputPath,
          profileArguments(compressor),
          compressor.level,
          iterations,
          path.join(workDirectory, 'benchmark.csv'),
        ),
      ];
    }
    const candidatePaths = await trainedCandidatePaths(
      zliPath,
      inputPath,
      compressor,
      path.join(workDirectory, 'candidates'),
    );
    const results = [];
    for (const [index, candidatePath] of candidatePaths.entries()) {
      results.push(
        await benchmark(
          zliPath,
          inputPath,
          ['--compressor', candidatePath],
          compressor.level,
          iterations,
          path.join(workDirectory, `candidate-${index}.csv`),
        ),
      );
    }
    return results;
  } finally {
    await fs.rm(workDirectory, {recursive: true, force: true});
  }
}

module.exports = {runOpenZLBenchmark};
