// Copyright (c) Meta Platforms, Inc. and affiliates.

/* eslint-disable @typescript-eslint/no-require-imports */

'use strict';

const fs = require('node:fs');
const {performance} = require('node:perf_hooks');
const zlib = require('node:zlib');
const {runOpenZLBenchmark} = require('./zli-client.cjs');

function nativeCodecBenchmark(input, iterations, compress, decompress) {
  let compressed = compress(input);
  const compressStart = performance.now();
  for (let index = 0; index < iterations; index += 1) {
    compressed = compress(input);
  }
  const compressMs = performance.now() - compressStart;

  let decompressed = decompress(compressed);
  const decompressStart = performance.now();
  for (let index = 0; index < iterations; index += 1) {
    decompressed = decompress(compressed);
  }
  const decompressMs = performance.now() - decompressStart;
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

function benchmarkZstd(input, level, iterations) {
  if (typeof zlib.zstdCompressSync !== 'function' || typeof zlib.zstdDecompressSync !== 'function') {
    throw new Error('This Electron runtime does not include native zstd support');
  }
  const params = {[zlib.constants.ZSTD_c_compressionLevel]: level};
  return nativeCodecBenchmark(
    input,
    iterations,
    (value) => zlib.zstdCompressSync(value, {params}),
    (value) => zlib.zstdDecompressSync(value),
  );
}

function benchmarkGzip(input, level, iterations) {
  return nativeCodecBenchmark(
    input,
    iterations,
    (value) => zlib.gzipSync(value, {level}),
    (value) => zlib.gunzipSync(value),
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

async function run({zliPath, request}) {
  const results = [];
  let input;

  for (const compressor of request.compressors) {
    if (compressor.compressor === 'OpenZL') {
      const nativeResults = await runOpenZLBenchmark(zliPath, request.input, compressor, request.iterations);
      for (let index = 0; index < nativeResults.length; index += 1) {
        const suffix = nativeResults.length > 1 ? ` candidate ${index + 1}` : '';
        results.push({
          ...finishOpenZLResult(nativeResults[index]),
          rowId: compressor.id,
          candidate: index,
          label: `OpenZL ${compressor.profile}${suffix}`,
        });
      }
    } else {
      input ??= readInput(request.input);
      const result =
        compressor.compressor === 'zstd'
          ? benchmarkZstd(input, compressor.level, request.iterations)
          : benchmarkGzip(input, compressor.level, request.iterations);
      results.push({
        ...result,
        rowId: compressor.id,
        candidate: 0,
        label: `${compressor.compressor} ${compressor.level}`,
      });
    }
  }
  return results;
}

async function execute(payload, postMessage) {
  try {
    postMessage({ok: true, results: await run(payload)});
  } catch (error) {
    postMessage({ok: false, error: error instanceof Error ? error.message : String(error)});
  }
}

if (process.parentPort !== undefined) {
  process.parentPort.once('message', (event) => {
    void execute(event.data, (message) => process.parentPort.postMessage(message));
  });
}

module.exports = {run};
