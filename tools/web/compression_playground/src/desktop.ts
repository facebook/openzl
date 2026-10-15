// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {
  BenchmarkEvent,
  BenchmarkRequest,
  BenchmarkResult,
} from '../../../desktop/compression_playground/electron/native-worker.cjs';
import type {RunInput} from './benchmarkTypes.ts';

export interface BenchmarkRun {
  results: Promise<BenchmarkResult[]>;
  /** Stops the run and its native processes; `results` then rejects. */
  cancel(): void;
}

/** What `electron/preload.cjs` exposes on `window`. */
export interface OpenZLDesktopBridge {
  startBenchmark(request: BenchmarkRequest, onEvent?: (event: BenchmarkEvent) => void): BenchmarkRun;
  getPathForFile(file: File): string;
}

declare global {
  interface Window {
    openzlDesktop?: OpenZLDesktopBridge;
  }
}

export function desktopBridge(): OpenZLDesktopBridge | undefined {
  return typeof window === 'undefined' ? undefined : window.openzlDesktop;
}

/**
 * Names the input for the backend, which reads it from disk. A file the user
 * picked or dropped always has a path in Electron; one made in the page does
 * not, and the backend has no way to read it.
 */
export function inputForFile(input: RunInput, bridge: OpenZLDesktopBridge): BenchmarkRequest['input'] {
  if ('kind' in input) {
    return {sample: true};
  }
  const path = bridge.getPathForFile(input);
  if (path === '') {
    throw new Error(`${input.name} is not a file on disk`);
  }
  return {path};
}
