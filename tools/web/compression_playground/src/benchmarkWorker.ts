// Copyright (c) Meta Platforms, Inc. and affiliates.

/// <reference lib="webworker" />

import {loadOpenZL, measureRun} from './measureRun.ts';
import type {WorkerRequest} from './benchmarkTypes.ts';

/**
 * Runs a benchmark off the page's thread.
 *
 * Training is the reason this exists: `train()` is synchronous and can run for
 * minutes, so on the main thread the page would simply stop. Plain benchmarks
 * block for less but still long enough to drop frames on a large input, and
 * cancelling either of them means discarding the thread it is on, which is
 * only possible because it has one.
 *
 * The measuring itself lives in `measureRun` so it can be exercised without a
 * worker; everything here is the message boundary.
 */
self.addEventListener('message', (event: MessageEvent<WorkerRequest>) => {
  if (event.data.type === 'profilesRequest') {
    void loadOpenZL()
      .then((openzl) => self.postMessage({type: 'profiles', profiles: openzl.profiles}))
      .catch((error: unknown) => {
        self.postMessage({type: 'profilesFailed', message: error instanceof Error ? error.message : String(error)});
      });
    return;
  } else if (event.data.type === 'benchmarkRequest') {
    void measureRun(event.data.config, (message) => {
      self.postMessage(message);
    }).catch((error: unknown) => {
      self.postMessage({type: 'failed', message: error instanceof Error ? error.message : String(error)});
    });
  } else {
    self.postMessage({type: 'failed', message: 'Received malformed worker message'});
  }
});
