// Copyright (c) Meta Platforms, Inc. and affiliates.

export interface ZliBenchmarkResult {
  iterations: number;
  srcSize: number;
  compressedSize: number;
  compressMs: number;
  decompressMs: number;
}

export interface OpenZLCompressor {
  profile: string;
  profileArg: string;
  level: number;
  trainRequested: boolean;
  candidates: number;
}

export interface ZliChildEvent {
  pid: number;
  state: 'spawned' | 'exited';
}

export interface RunOptions {
  signal?: AbortSignal;
  onChild?: (event: ZliChildEvent) => void;
  tempRoot?: string;
}

export function runOpenZLBenchmark(
  zliPath: string,
  input: {path: string} | {data: Uint8Array},
  compressor: OpenZLCompressor,
  iterations: number,
  options?: RunOptions,
): Promise<ZliBenchmarkResult[]>;
