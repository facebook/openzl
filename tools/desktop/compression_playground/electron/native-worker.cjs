// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const fs = require('node:fs');
const {performance} = require('node:perf_hooks');
const zlib = require('node:zlib');
const {runOpenZLBenchmark} = require('./zli-client.cjs');

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

function readInput(input) {
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

function rowLabel(compressor) {
  return compressor.compressor === 'OpenZL'
    ? `OpenZL ${compressor.profile}`
    : `${compressor.compressor} ${compressor.level}`;
}

/**
 * Runs every row in order. `onEvent` receives {type: 'progress', progress} as
 * each row and stage starts, and {type: 'partial', results} as each row ends.
 */
async function run({zliPath, request, tempRoot}, {signal, onChild, onEvent} = {}) {
  const results = [];
  let input;

  for (const [rowIndex, compressor] of request.compressors.entries()) {
    signal?.throwIfAborted();
    const label = rowLabel(compressor);
    const row = {rowId: compressor.id, rowIndex, rowCount: request.compressors.length, label};
    const onStage = (stage) => onEvent?.({type: 'progress', progress: {...row, ...stage}});
    const rowResults = [];
    if (compressor.compressor === 'OpenZL') {
      const nativeResults = await runOpenZLBenchmark(zliPath, request.input, compressor, request.iterations, {
        signal,
        onChild,
        onStage,
        tempRoot,
      });
      for (let index = 0; index < nativeResults.length; index += 1) {
        const suffix = nativeResults.length > 1 ? ` candidate ${index + 1}` : '';
        rowResults.push({
          ...finishOpenZLResult(nativeResults[index]),
          rowId: compressor.id,
          candidate: index,
          label: `${label}${suffix}`,
        });
      }
    } else {
      onStage({stage: 'benchmark'});
      input ??= readInput(request.input);
      const result =
        compressor.compressor === 'zstd'
          ? await benchmarkZstd(input, compressor.level, request.iterations, signal)
          : await benchmarkGzip(input, compressor.level, request.iterations, signal);
      rowResults.push({...result, rowId: compressor.id, candidate: 0, label});
    }
    results.push(...rowResults);
    onEvent?.({type: 'partial', results: rowResults});
  }
  return results;
}

/**
 * Worker protocol. From the main process: {type: 'start', zliPath, request, tempRoot},
 * then optionally {type: 'cancel'}. To it: {type: 'child', pid, state} for
 * every zli process, run() events ({type: 'progress'} and {type: 'partial'}),
 * then exactly one of {type: 'results', results},
 * {type: 'error', message}, or {type: 'cancelled'}.
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
        (results) => postMessage({type: 'results', results}),
        (error) =>
          postMessage(
            // Only a rejection the abort caused: a run can fail on its own
            // just as a cancel arrives, and its error should still be seen.
            signal.aborted && error === signal.reason
              ? {type: 'cancelled'}
              : {type: 'error', message: error instanceof Error ? error.message : String(error)},
          ),
      );
    }
  });
}

if (process.parentPort !== undefined) {
  serve(process.parentPort);
}

module.exports = {run};
