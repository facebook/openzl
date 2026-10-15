// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {
  BenchmarkEvent,
  BenchmarkRequest,
  BenchmarkResult,
} from '../../../desktop/compression_playground/electron/native-worker.cjs';

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

export function inputForFile(file: File, bridge: OpenZLDesktopBridge): Promise<BenchmarkRequest['input']> {
  const path = bridge.getPathForFile(file);
  if (path !== '') {
    return Promise.resolve({path});
  }
  // A path is normally available in Electron. Keeping the byte fallback makes
  // programmatically-created Files and renderer tests useful without granting
  // the renderer general filesystem access.
  return file.arrayBuffer().then((contents) => ({data: new Uint8Array(contents)}));
}
