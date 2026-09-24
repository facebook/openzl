// Copyright (c) Meta Platforms, Inc. and affiliates.

import {compress as compressZstd, decompress as decompressZstd, init as initZstd} from '@bokuweb/zstd-wasm';
import {gzipSync, gunzipSync, type GzipOptions} from 'fflate';
import {getOpenZLMaxIterations} from '../../../wasm/js/wasm_api.js';
import {ITERATIONS_MAX} from './compressors.ts';
import type {BenchmarkResult, OpenZL} from '../../../wasm/js/wasm_api.js';

export type {BenchmarkResult};

const MILLISECONDS_PER_SECOND = 1000;
const BYTES_PER_MEGABYTE = 1000 * 1000;

// Live ceiling from the WASM module, loaded once on first benchmark and
// shared by every JS codec. Lazy so importing this module never starts a
// WASM load on its own.
let maxIterations: Promise<number> | undefined;
function getMaxIterations(): Promise<number> {
  maxIterations ??= getOpenZLMaxIterations().catch((error: unknown) => {
    maxIterations = undefined;
    throw error;
  });
  return maxIterations;
}

// `init()` is not idempotent: it re-enters the module's own initialiser while
// its wait promise, resolved once at import, returns immediately. The third of
// those leaves the module permanently unable to read a frame back, so it is
// called once and the promise shared. Same shape as `getMaxIterations` above.
let zstdInit: Promise<void> | undefined;
function zstdReady(): Promise<void> {
  zstdInit ??= initZstd().catch((error: unknown) => {
    zstdInit = undefined;
    throw error;
  });
  return zstdInit;
}

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
  // Same ceiling as the native path (clampIterations in wasm_api.js), read live
  // from the module so the two cannot drift apart. zstd and gzip need nothing
  // else from that module, so a load failure falls back to the slider's own
  // maximum rather than taking them down with it -- it is the smaller of the
  // two, so the ceiling it stands in for cannot be exceeded either way.
  const ceiling = await getMaxIterations().catch(() => ITERATIONS_MAX);
  const runs = Math.min(Math.max(1, Math.floor(iterations)), ceiling);

  // untimed compression warmup
  let compressed = await compress(data);

  const compressStart = performance.now();
  for (let iteration = 0; iteration < runs; iteration += 1) {
    compressed = await compress(data);
  }
  const compressMs = performance.now() - compressStart;

  // untimed decompression warmup
  let decompressed = await decompress(compressed);
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

/**
 * Runs the OpenZL WASM benchmark. Ratio matches native, speed reflect the
 * in-browser WASM build, not native performance.
 */
export async function benchmarkOpenZL(
  openzl: Pick<OpenZL, 'benchmark'>,
  data: Uint8Array,
  compressor: Uint8Array,
  iterations: number,
): Promise<BenchmarkResult> {
  return openzl.benchmark(data, compressor, iterations);
}

/**
 * Runs the zstd WASM benchmark. Ratio matches native, speed reflect the
 * in-browser WASM build, not native performance.
 */
export async function benchmarkZstd(data: Uint8Array, level: number, iterations: number): Promise<BenchmarkResult> {
  if (!Number.isInteger(level) || level < 1 || level > 19) {
    throw new Error('zstd level must be an integer from 1 to 19');
  }
  // Setup stays outside benchmarkCodec's timed region, so its cost is not measured.
  await zstdReady();
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
