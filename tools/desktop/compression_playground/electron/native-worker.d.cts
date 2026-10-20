// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {ZliChildEvent} from './zli-client.cjs';

/** One compressor to measure at one level, as the page sends it. */
export type BenchmarkRow =
  | {
      id: number;
      compressor: 'OpenZL';
      level: number;
      profile: string;
      trainRequested: boolean;
      /** Read only when `trainRequested`. */
      candidates?: number;
      profileArg: string;
    }
  | {id: number; compressor: 'zstd' | 'gzip'; level: number};

export interface BenchmarkRequest {
  input: {path: string} | {data: Uint8Array};
  compressors: readonly BenchmarkRow[];
  iterations: number;
}

/** Which of a trained row's candidates a measurement is, from 1, best ratio first. */
export interface Candidate {
  index: number;
  total: number;
}

/** One measurement. `candidate` is null for every row that does not train. */
export interface BenchmarkResult {
  rowId: number;
  candidate: Candidate | null;
  iterations: number;
  srcSize: number;
  compressedSize: number;
  ratio: number;
  compressMs: number;
  decompressMs: number;
  compressMBps: number;
  decompressMBps: number;
}

/** A row that could not be measured. The run goes on to the next row. */
export interface BenchmarkFailure {
  rowId: number;
  candidate: Candidate | null;
  message: string;
}

/**
 * What a run streams, in the browser worker's `WorkerMessage` vocabulary, with
 * the row id where the browser has its job.
 */
export type BenchmarkEvent =
  | {type: 'step'; fraction: number}
  | {type: 'result'; result: BenchmarkResult}
  | {type: 'failure'; failure: BenchmarkFailure};

export function run(
  payload: {zliPath: string; request: BenchmarkRequest; tempRoot?: string},
  options?: {signal?: AbortSignal; onChild?: (event: ZliChildEvent) => void; onEvent?: (event: BenchmarkEvent) => void},
): Promise<BenchmarkResult[]>;
