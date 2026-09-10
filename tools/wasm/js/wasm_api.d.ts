// Copyright (c) Meta Platforms, Inc. and affiliates.

/** Defined when the module loads and validated when createOpenZL() resolves. */
export declare const Profile: {
  readonly SERIAL: 0;
  readonly U8: 1;
  readonly I8: 2;
  readonly U16: 3;
  readonly I16: 4;
  readonly U32: 5;
  readonly I32: 6;
  readonly U64: 7;
  readonly I64: 8;
};

export type ProfileValue = (typeof Profile)[keyof typeof Profile];

export interface BenchmarkResult {
  iterations: number;
  srcSize: number;
  compressedSize: number;
  ratio: number;
  compressMs: number;
  decompressMs: number;
  compressMBps: number;
  decompressMBps: number;
}

/**
 * Training options Every field is optional and falls back to the trainer's default when omitted.
 */
export interface TrainOptions {
  /** Thread pool size. Clamped to the build's maximum, which is the default. */
  threads?: number;
  /**
   * Wall-clock budget in seconds. The trainers spend whatever budget they are
   * given, so this sets how long the call takes rather than bounding it.
   */
  maxTimeSecs?: number;
  /**
   * Return the Pareto frontier, compressors trading compression ratio against
   * speed, rather than only the best-ratio one.
   */
  paretoFrontier?: boolean;
  /**
   * How many compressors to keep. Defaults to `trainParetoCandidates`, smaller
   * limits are raised to that value which is the minimum. Ignored unless
   * paretoFrontier is set. The maximum is 25 to bound WASM allocations.
   * Bounds the results, not the work: every candidate is searched and
   * benchmarked either way.
   */
  maxNumCandidates?: number;
  /** Narrow the ACE search by not exploring successor graphs. */
  noAceSuccessors?: boolean;
  /** Skip the clustering trainer. */
  noClustering?: boolean;
}

export interface OpenZL {
  readonly maxBenchmarkIterations: number;
  readonly trainParetoCandidates: number;

  getSerializedCompressor(profile: ProfileValue): Uint8Array;

  /**
   * Train runs synchronously and may run for several minutes. Browser callers
   * should invoke this API from their own Web Worker. Calling it from the main
   * thread blocks the UI until training completes.
   */
  train(data: Uint8Array, compressor: Uint8Array, options?: TrainOptions): Uint8Array[];
  compress(data: Uint8Array, compressor: Uint8Array): Uint8Array;
  decompress(compressed: Uint8Array): Uint8Array;
  benchmark(data: Uint8Array, compressor: Uint8Array, iterations?: number): BenchmarkResult;
}

export interface OpenZLOptions {
  wasmUrl?: string | URL;
  locateFile?: (path: string, prefix?: string) => string | URL;
  [option: string]: unknown;
}

export declare function createOpenZL(options?: OpenZLOptions): Promise<OpenZL>;
