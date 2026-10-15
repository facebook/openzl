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
  /** Read only when `trainRequested`. */
  candidates?: number;
}

export interface ZliChildEvent {
  pid: number;
  state: 'spawned' | 'exited';
}

export type ZliStage =
  | {stage: 'benchmark'}
  | {stage: 'train'; fraction?: number}
  | {stage: 'candidates'; candidate: number; candidateCount: number};

export interface RunOptions {
  signal?: AbortSignal;
  onChild?: (event: ZliChildEvent) => void;
  onStage?: (stage: ZliStage) => void;
  tempRoot?: string;
  /** The Windows ANSI code page, for tests. On win32 it is read from the registry; elsewhere paths are not checked. */
  codePage?: number;
}

export function runOpenZLBenchmark(
  zliPath: string,
  input: {path: string},
  compressor: OpenZLCompressor,
  iterations: number,
  options?: RunOptions,
): Promise<ZliBenchmarkResult[]>;

export function runZstdBenchmark(
  zstdBenchmarkPath: string,
  input: {path: string},
  level: number,
  iterations: number,
  options?: RunOptions,
): Promise<ZliBenchmarkResult>;
