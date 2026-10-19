// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {ZliChildEvent, ZliStage} from './zli-client.cjs';

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

export interface BenchmarkResult {
  rowId: number;
  label: string;
  candidate: number;
  iterations: number;
  srcSize: number;
  compressedSize: number;
  ratio: number;
  compressMs: number;
  decompressMs: number;
  compressMBps: number;
  decompressMBps: number;
}

/** Which row is running, and which stage of it has started. */
export type BenchmarkProgress = {rowId: number; rowIndex: number; rowCount: number; label: string} & ZliStage;

export type BenchmarkEvent =
  | {type: 'progress'; progress: BenchmarkProgress}
  /** Results for one row, sent as soon as that row finishes. */
  | {type: 'partial'; results: BenchmarkResult[]};

export function run(
  payload: {zliPath: string; request: BenchmarkRequest; tempRoot?: string},
  options?: {signal?: AbortSignal; onChild?: (event: ZliChildEvent) => void; onEvent?: (event: BenchmarkEvent) => void},
): Promise<BenchmarkResult[]>;
