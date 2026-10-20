// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const fs = require('node:fs');
const {performance} = require('node:perf_hooks');
const zlib = require('node:zlib');
const {prepareInput, runOpenZLBenchmark} = require('./zli-client.cjs');

// The codecs are synchronous, so the worker yields between timed iterations to
// receive a cancel message. Only the codec calls themselves are timed.
async function timedIterations(iterations, signal, operation) {
  let elapsedMs = 0;
  let output;
  for (let index = 0; index < iterations; index += 1) {
    await new Promise((resolve) => setImmediate(resolve));
    signal?.throwIfAborted();
    const start = performance.now();
    output = operation();
    elapsedMs += performance.now() - start;
  }
  return {elapsedMs, output};
}

async function nativeCodecBenchmark(input, iterations, compress, decompress, signal) {
  // The untimed warm-up calls cannot be interrupted either, so check first.
  signal?.throwIfAborted();
  compress(input);
  const {elapsedMs: compressMs, output: compressed} = await timedIterations(iterations, signal, () => compress(input));

  signal?.throwIfAborted();
  decompress(compressed);
  const {elapsedMs: decompressMs, output: decompressed} = await timedIterations(iterations, signal, () =>
    decompress(compressed),
  );
  if (!Buffer.from(decompressed).equals(input)) {
    throw new Error('Native codec did not reproduce the input');
  }
  const bytesMeasured = input.length * iterations;
  return {
    iterations,
    srcSize: input.length,
    compressedSize: compressed.length,
    ratio: compressed.length === 0 ? 0 : input.length / compressed.length,
    compressMs,
    decompressMs,
    compressMBps: compressMs === 0 ? Infinity : bytesMeasured / compressMs / 1000,
    decompressMBps: decompressMs === 0 ? Infinity : bytesMeasured / decompressMs / 1000,
  };
}

function benchmarkZstd(input, level, iterations, signal) {
  if (typeof zlib.zstdCompressSync !== 'function' || typeof zlib.zstdDecompressSync !== 'function') {
    throw new Error('This Electron runtime does not include native zstd support');
  }
  const params = {[zlib.constants.ZSTD_c_compressionLevel]: level};
  return nativeCodecBenchmark(
    input,
    iterations,
    (value) => zlib.zstdCompressSync(value, {params}),
    (value) => zlib.zstdDecompressSync(value),
    signal,
  );
}

function benchmarkGzip(input, level, iterations, signal) {
  return nativeCodecBenchmark(
    input,
    iterations,
    (value) => zlib.gzipSync(value, {level}),
    (value) => zlib.gunzipSync(value),
    signal,
  );
}

function readRequestInput(input) {
  if ('path' in input) {
    return fs.readFileSync(input.path);
  }
  return Buffer.from(input.data.buffer, input.data.byteOffset, input.data.byteLength);
}

function finishOpenZLResult(result) {
  const bytesMeasured = result.srcSize * result.iterations;
  return {
    ...result,
    ratio: result.compressedSize === 0 ? 0 : result.srcSize / result.compressedSize,
    compressMBps: result.compressMs === 0 ? Infinity : bytesMeasured / result.compressMs / 1000,
    decompressMBps: result.decompressMs === 0 ? Infinity : bytesMeasured / result.decompressMs / 1000,
  };
}

// A trained row's step covers training and then benchmarking its candidates,
// in one fraction that only grows. Training takes most of the time, so it gets
// most of the row.
const TRAINING_SHARE = 0.8;

function messageOf(error) {
  return error instanceof Error ? error.message : String(error);
}

/**
 * Measures one row, passing each measurement to `report` as soon as it is
 * made: a trained row's candidates one by one, not when the last is done.
 */
async function measure({zliPath, request, tempRoot}, compressor, readInput, options, report) {
  if (compressor.compressor === 'OpenZL') {
    await runOpenZLBenchmark(zliPath, request.input, compressor, request.iterations, {
      ...options,
      tempRoot,
      onMeasurement: (measurement, index, total) =>
        report({
          ...finishOpenZLResult(measurement),
          rowId: compressor.id,
          candidate: compressor.trainRequested ? {index: index + 1, total} : null,
        }),
    });
    return;
  }
  const measurement =
    compressor.compressor === 'zstd'
      ? await benchmarkZstd(readInput(), compressor.level, request.iterations, options.signal)
      : await benchmarkGzip(readInput(), compressor.level, request.iterations, options.signal);
  report({...measurement, rowId: compressor.id, candidate: null});
}

/**
 * Runs every row in order, reporting as the browser's worker does: `step` as
 * training progresses and as each trained candidate starts, then a
 * `result` per measurement as soon as it is made, or a `failure` for a row
 * that could not be measured. A failed row does not stop the run; only
 * cancelling does. Trained candidates are counted from 1, best ratio first;
 * `candidate` is null for every row that does not train.
 */
async function run(payload, {signal, onChild, onEvent} = {}) {
  // Once for the whole run, so a file that has to be written or copied for
  // zli is not written again for every row.
  const prepared = await prepareInput(payload.request.input, {tempRoot: payload.tempRoot});
  try {
    return await runRows(
      {...payload, request: {...payload.request, input: {path: prepared.path}}},
      {
        signal,
        onChild,
        onEvent,
      },
    );
  } finally {
    await prepared.dispose();
  }
}

async function runRows(payload, {signal, onChild, onEvent}) {
  const results = [];
  let input;
  const readInput = () => (input ??= readRequestInput(payload.request.input));
  const onStage = (stage) => {
    if (stage.stage === 'train' && stage.fraction !== undefined) {
      onEvent?.({type: 'step', fraction: stage.fraction * TRAINING_SHARE});
    } else if (stage.stage === 'candidates') {
      const benchmarked = (stage.candidate - 1) / stage.candidateCount;
      onEvent?.({type: 'step', fraction: TRAINING_SHARE + benchmarked * (1 - TRAINING_SHARE)});
    }
  };
  const report = (result) => {
    results.push(result);
    onEvent?.({type: 'result', result});
  };

  for (const compressor of payload.request.compressors) {
    signal?.throwIfAborted();
    try {
      await measure(payload, compressor, readInput, {signal, onChild, onStage}, report);
    } catch (error) {
      if (signal?.aborted) {
        throw error;
      }
      onEvent?.({type: 'failure', failure: {rowId: compressor.id, candidate: null, message: messageOf(error)}});
    }
  }
  return results;
}

/**
 * Worker protocol. From the main process: {type: 'start', zliPath, request, tempRoot},
 * then optionally {type: 'cancel'}. To it: {type: 'child', pid, state} for
 * every zli process, run()'s events, then exactly one of {type: 'finished'},
 * {type: 'failed', message}, or {type: 'cancelled'}.
 */
function serve(parentPort) {
  const controller = new AbortController();
  const postMessage = (message) => parentPort.postMessage(message);
  parentPort.on('message', ({data}) => {
    if (data?.type === 'cancel') {
      controller.abort(new Error('Benchmark cancelled'));
    } else if (data?.type === 'start') {
      const {signal} = controller;
      run(data, {signal, onChild: (event) => postMessage({type: 'child', ...event}), onEvent: postMessage}).then(
        () => postMessage({type: 'finished'}),
        (error) =>
          postMessage(
            // Only a rejection the abort caused: a run can fail on its own
            // just as a cancel arrives, and its error should still be seen.
            signal.aborted && error === signal.reason
              ? {type: 'cancelled'}
              : {type: 'failed', message: messageOf(error)},
          ),
      );
    }
  });
}

if (process.parentPort !== undefined) {
  serve(process.parentPort);
}

module.exports = {run};
