// Copyright (c) Meta Platforms, Inc. and affiliates.

import {compress as compressZstd, decompress as decompressZstd, init as initZstd} from '@bokuweb/zstd-wasm';
import {gzipSync, gunzipSync, type GzipOptions} from 'fflate';
import type {BenchmarkResult, OpenZL} from '../../../wasm/js/wasm_api.js';

export type {BenchmarkResult};

const MILLISECONDS_PER_SECOND = 1000;
const BYTES_PER_MEGABYTE = 1000 * 1000;

async function benchmarkCodec(
  data: Uint8Array,
  iterations: number,
  compress: (input: Uint8Array) => Uint8Array | Promise<Uint8Array>,
  decompress: (input: Uint8Array) => Uint8Array | Promise<Uint8Array>,
): Promise<BenchmarkResult> {
  if (!(data instanceof Uint8Array)) {
    throw new Error('benchmark expects Uint8Array');
  }
  if (!Number.isFinite(iterations)) {
    throw new Error(`benchmark iterations must be a finite number, got ${String(iterations)}`);
  }
  const runs = Math.max(1, Math.floor(iterations));
  // Untimed warmup, mirroring openzl_wasm_benchmarkCompress: absorbs
  // first-call costs and produces the frame the decompress loop times.
  let compressed = await compress(data);

  const compressStart = performance.now();
  for (let iteration = 0; iteration < runs; iteration += 1) {
    compressed = await compress(data);
  }
  const compressMs = performance.now() - compressStart;

  let decompressed: Uint8Array = new Uint8Array();
  const decompressStart = performance.now();
  for (let iteration = 0; iteration < runs; iteration += 1) {
    decompressed = await decompress(compressed);
  }
  const decompressMs = performance.now() - decompressStart;

  if (data.length !== decompressed.length || data.some((value, index) => value !== decompressed[index])) {
    throw new Error('benchmark decompression did not reproduce the input');
  }
  return {
    iterations: runs,
    srcSize: data.length,
    compressedSize: compressed.length,
    ratio: compressed.length > 0 ? data.length / compressed.length : 0,
    compressMs,
    decompressMs,
    compressMBps:
      compressMs > 0 ? (data.length * runs) / (compressMs / MILLISECONDS_PER_SECOND) / BYTES_PER_MEGABYTE : Infinity,
    decompressMBps:
      decompressMs > 0
        ? (data.length * runs) / (decompressMs / MILLISECONDS_PER_SECOND) / BYTES_PER_MEGABYTE
        : Infinity,
  };
}

export async function benchmarkOpenZL(
  openzl: Pick<OpenZL, 'compress' | 'decompress'>,
  data: Uint8Array,
  compressor: Uint8Array,
  iterations: number,
): Promise<BenchmarkResult> {
  return benchmarkCodec(
    data,
    iterations,
    (input) => openzl.compress(input, compressor),
    (compressed) => openzl.decompress(compressed),
  );
}

export async function benchmarkZstd(data: Uint8Array, level: number, iterations: number): Promise<BenchmarkResult> {
  if (!Number.isInteger(level) || level < 1 || level > 19) {
    throw new Error('zstd level must be an integer from 1 to 19');
  }
  // Setup stays outside benchmarkCodec's timed region, so its cost is not measured.
  await initZstd();
  return benchmarkCodec(
    data,
    iterations,
    (input) => compressZstd(input, level),
    (compressed) => decompressZstd(compressed),
  );
}

export async function benchmarkGzip(data: Uint8Array, level: number, iterations: number): Promise<BenchmarkResult> {
  if (!Number.isInteger(level) || level < 1 || level > 9) {
    throw new Error('gzip level must be an integer from 1 to 9');
  }
  const gzipLevel = level as GzipOptions['level'];
  return benchmarkCodec(
    data,
    iterations,
    (input) => gzipSync(input, {level: gzipLevel}),
    (compressed) => gunzipSync(compressed),
  );
}
