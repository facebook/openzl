// Copyright (c) Meta Platforms, Inc. and affiliates.

import type {RunConfig, RunState} from './benchmarkTypes.ts';

/**
 * Replaces `runBenchmark.ts` in the desktop build (see vite.config.ts). The
 * desktop app runs `zli` and never loads the WASM module, so its bundle leaves
 * out the worker and builds without the Emscripten output.
 */
export function runBenchmark(_config: RunConfig, onState: (state: RunState) => void): void {
  onState({
    status: 'error',
    message: 'The desktop backend is unavailable, and this build has no browser backend',
    results: [],
    failures: [],
  });
}
