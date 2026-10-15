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

export type ZliStage =
  | {stage: 'benchmark'}
  | {stage: 'train'}
  | {stage: 'candidates'; candidate: number; candidateCount: number};

export interface RunOptions {
  signal?: AbortSignal;
  onChild?: (event: ZliChildEvent) => void;
  onStage?: (stage: ZliStage) => void;
  tempRoot?: string;
}

export function runOpenZLBenchmark(
  zliPath: string,
  input: {path: string} | {data: Uint8Array},
  compressor: OpenZLCompressor,
  iterations: number,
  options?: RunOptions,
): Promise<ZliBenchmarkResult[]>;
