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

export interface OpenZL {
  readonly maxBenchmarkIterations: number;
  getSerializedCompressor(profile: ProfileValue): Uint8Array;
  compress(data: Uint8Array, compressor: Uint8Array): Uint8Array;
  decompress(compressed: Uint8Array): Uint8Array;
  benchmark(
    data: Uint8Array,
    compressor: Uint8Array,
    iterations?: number,
  ): BenchmarkResult;
}

export interface OpenZLOptions {
  wasmUrl?: string | URL;
  locateFile?: (path: string, prefix?: string) => string | URL;
  [option: string]: unknown;
}

export declare function createOpenZL(options?: OpenZLOptions): Promise<OpenZL>;
